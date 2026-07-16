// sandhook_jni.cpp
// JNI Bridge between Java/Kotlin and the SandHook Native Engine
// Receives Java Method objects, extracts their native memory addresses,
// and orchestrates the hook installation.

#include <jni.h>
#include <android/log.h>
#include "sandhook.h"
#include "art_method.h"

#define LOG_TAG "SandHook-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Helper para inicializar ART solo una vez de forma segura
static void ensure_art_initialized(JNIEnv* env) {
    static bool initialized = false;
    if (!initialized) {
        sandhook::art::init(env);
        initialized = true;
    }
}

extern "C" {

// Método nativo que corresponde a SandHook.java
JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeInit(JNIEnv* env, jclass clazz) {
    ensure_art_initialized(env);
    return JNI_TRUE;
}

// Método nativo principal para instalar un hook
JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeHookMethod(JNIEnv* env, jclass clazz, 
                                                   jobject originMethod, 
                                                   jobject hookMethod, 
                                                   jobject backupMethod) {
    if (!originMethod || !hookMethod) {
        LOGE("originMethod or hookMethod is null");
        return JNI_FALSE;
    }

    // 1. Asegurarnos de que la capa ART esté inicializada
    ensure_art_initialized(env);

    // 2. Convertir los objetos Method de Java a jmethodID
    // (jmethodID es el puntero a la estructura ArtMethod en C++)
    jmethodID origin_meth = env->FromReflectedMethod(originMethod);
    jmethodID hook_meth = env->FromReflectedMethod(hookMethod);

    if (!origin_meth || !hook_meth) {
        LOGE("Failed to get jmethodID");
        return JNI_FALSE;
    }

    // 3. Extraer las direcciones de memoria nativas (Quick Code Entry Points)
    void* origin_addr = sandhook::art::getQuickEntryPoint(origin_meth);
    void* hook_addr = sandhook::art::getQuickEntryPoint(hook_meth);

    if (!origin_addr || !hook_addr) {
        LOGE("Failed to get native entry points");
        return JNI_FALSE;
    }

    LOGI("Hooking Origin: %p -> Replacement: %p", origin_addr, hook_addr);

    // 4. Llamar a TU MOTOR para instalar el hook
    // Si se proporciona un backupMethod, pedimos un trampolín
    void* trampoline = nullptr;
    int result = sandhook_install_ex(origin_addr, hook_addr, 
                                     (backupMethod != nullptr) ? &trampoline : nullptr);

    // 5. Si todo salió bien y tenemos un backup, redirigir el backup al trampolín
    if (result == HOOK_OK) {
        if (backupMethod != nullptr && trampoline != nullptr) {
            jmethodID backup_meth = env->FromReflectedMethod(backupMethod);
            if (backup_meth) {
                // Hacemos que el método de backup ejecute el trampolín (código original)
                sandhook::art::setQuickEntryPoint(backup_meth, trampoline);
                LOGI("Backup method redirected to trampoline at %p", trampoline);
            }
        }
        return JNI_TRUE;
    }

    LOGE("Hook installation failed: %s", sandhook_error_string(result));
    return JNI_FALSE;
}

// Método nativo para desinstalar un hook
JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeUnhookMethod(JNIEnv* env, jclass clazz, jobject originMethod) {
    if (!originMethod) return JNI_FALSE;

    jmethodID origin_meth = env->FromReflectedMethod(originMethod);
    if (!origin_meth) return JNI_FALSE;

    void* origin_addr = sandhook::art::getQuickEntryPoint(origin_meth);
    if (!origin_addr) return JNI_FALSE;

    int result = sandhook_remove(origin_addr);
    return (result == HOOK_OK) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"