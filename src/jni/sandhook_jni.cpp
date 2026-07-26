// sandhook_jni.cpp
// Puente JNI + Trampolín Ensamblador Handler (Dobby-style Full Context Save)
#include <jni.h>
#include <android/log.h>
#include "sandhook.h"
#include "art_method.h"
#include <unordered_map>
#include <mutex>

#define LOG_TAG "SandHook-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Estructura que coincide EXACTAMENTE con la pila de art_quick_stub.S
struct ArtContext {
    uint64_t original_sp;   // [0]  SP original
    uint64_t x0;            // [8]  ArtMethod*
    uint64_t x1_to_x30[30]; // [16] x1 es índice 0, x8 es índice 7, x30 es índice 29
    uint8_t  q0_to_q7[8 * 16]; // [256] Registros flotantes
};

// Registro global para mapear el método original a su entry point real
static std::unordered_map<jmethodID, void*> g_hooked_methods;
static std::mutex g_hook_mu;

static void ensure_art_initialized(JNIEnv* env) {
    static bool initialized = false;
    if (!initialized) {
        sandhook::art::init(env);
        initialized = true;
    }
}

// Esta función es llamada desde el ensamblador (.S)
// Recibe el contexto completo de la CPU y el puntero al ArtMethod.
extern "C" void sandhook_art_quick_handler(ArtContext* ctx, jmethodID method_id) {
    // LOGI("[ART-Stub] Método interceptado de forma segura! ArtMethod: %p", method_id);
    
    void* original_addr = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_hook_mu);
        auto it = g_hooked_methods.find(method_id);
        if (it != g_hooked_methods.end()) {
            original_addr = it->second;
        }
    }

    if (!original_addr) {
        LOGE("[ART-Stub] No se encontró la dirección original para %p. Crasheo inminente.", method_id);
        return; 
    }

    // Guardamos la dirección original en x8 para que el ensamblador salte ahí.
    // x8 es el índice 7 dentro de nuestro array x1_to_x30.
    ctx->x1_to_x30[7] = (uint64_t)original_addr;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeInit(JNIEnv* env, jclass clazz) {
    ensure_art_initialized(env);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeHookMethod(JNIEnv* env, jclass clazz, 
                                                   jobject originMethod, 
                                                   jobject hookMethod, 
                                                   jobject backupMethod) {
    if (!originMethod || !hookMethod) return JNI_FALSE;
    ensure_art_initialized(env);

    jmethodID origin_meth = env->FromReflectedMethod(originMethod);
    jmethodID hook_meth = env->FromReflectedMethod(hookMethod);
    if (!origin_meth || !hook_meth) return JNI_FALSE;

    // 1. Obtener las direcciones de ejecución nativa (Quick Code)
    void* origin_addr = sandhook::art::getQuickEntryPoint(origin_meth);
    void* hook_addr = sandhook::art::getQuickEntryPoint(hook_meth);

    if (!origin_addr || !hook_addr) {
        LOGE("Failed to get ART entry points.");
        return JNI_FALSE;
    }

    LOGI("ART Hook: Origin %p -> Hook %p", origin_addr, hook_addr);

    // 2. Si hay un método backup, le ponemos la dirección original
    // Esto permite que llames a backup() desde tu hook y se ejecute el código real.
    if (backupMethod != nullptr) {
        jmethodID backup_meth = env->FromReflectedMethod(backupMethod);
        if (backup_meth) {
            sandhook::art::setQuickEntryPoint(backup_meth, origin_addr);
            LOGI("Método backup configurado con la dirección original.");
        }
    }

    // 3. LA MAGIA: Sobrescribir el método original con la dirección del método hook
    // Cuando ART llame a originMethod, saltará directamente a hookMethod.
    sandhook::art::setQuickEntryPoint(origin_meth, hook_addr);

    LOGI("ART Hook instalado correctamente (Direct Swap).");
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeUnhookMethod(JNIEnv* env, jclass clazz, 
                                                      jobject originMethod) {
    if (!originMethod) return JNI_FALSE;
    jmethodID origin_meth = env->FromReflectedMethod(originMethod);
    if (!origin_meth) return JNI_FALSE;
    
    // Aquí restauramos el entry point original si lo guardamos
    // Para esta versión simple, retornamos true
    // En una implementación completa deberías guardar el entry_point original
    return JNI_TRUE;
}

JNIEXPORT jobject JNICALL
Java_com_swift_sandhook_SandHook_nativeGetObject(JNIEnv* env, jclass clazz, jlong ptr) {
    if (ptr == 0) return nullptr;
    // Esto es un stub. En un ART hook real, esto requeriría llamar a:
    // JNIEnv->NewLocalRef(env, (jobject) ptr)
    // Depende de cómo el C++ obtenga el puntero al Class.
    return env->NewLocalRef(reinterpret_cast<jobject>(ptr));
}

} // extern "C"