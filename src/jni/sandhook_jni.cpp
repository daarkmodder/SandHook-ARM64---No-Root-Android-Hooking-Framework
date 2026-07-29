#include <jni.h>
#include <android/log.h>
#include "../public/sandhook.h"
#include "../art/art_method.h"
#include <unordered_map>
#include <mutex>
#include <cstring>

#define LOG_TAG "SandHook-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct ArtContext {
    uint64_t original_sp;
    uint64_t x0;               // ArtMethod*
    uint64_t x1_to_x30[30];    // x1 = índice 0 (this), x2 = índice 1, etc.
    uint8_t  q0_to_q7[8 * 16];
};

struct HookEntity {
    jmethodID   hook_method;
    jclass      target_class;
    void*       backup_entry;
    char        shorty[32];
    bool        is_static;
    char        return_type;
};

static std::unordered_map<void*, HookEntity> g_entities;
static std::mutex g_entities_mu;
static JavaVM* g_jvm = nullptr;

// FIX ABI: x0 = ArtMethod*, x1 = this (instancia), x2+ = args (instancia) o x1+ = args (static)
static int pack_args(ArtContext* ctx, const char* shorty, bool is_static, jvalue* out) {
    int greg = is_static ? 0 : 1; // Si es instancia, nos saltamos x1 (this) y empezamos en x2 (índice 1)
    int freg = 0;
    int arg_count = 0;

    for (const char* p = shorty + 1; *p; ++p) {
        switch (*p) {
            case 'Z': case 'B': case 'C': case 'S': case 'I':
                out[arg_count++].i = static_cast<jint>(ctx->x1_to_x30[greg++]);
                break;
            case 'J':
                out[arg_count++].j = static_cast<jlong>(ctx->x1_to_x30[greg++]);
                break;
            case 'F': {
                uint32_t bits;
                std::memcpy(&bits, &ctx->q0_to_q7[freg * 16], 4);
                out[arg_count++].f = *reinterpret_cast<float*>(&bits);
                freg++;
                break;
            }
            case 'D': {
                uint64_t bits;
                std::memcpy(&bits, &ctx->q0_to_q7[freg * 16], 8);
                out[arg_count++].d = *reinterpret_cast<double*>(&bits);
                freg++;
                break;
            }
            case 'L': case '[':
                out[arg_count++].l = reinterpret_cast<jobject>(ctx->x1_to_x30[greg++]);
                break;
            default:
                return -1;
        }
    }
    return arg_count;
}

extern "C" void sandhook_art_quick_handler(ArtContext* ctx, void* art_method) {
    HookEntity entity;
    {
        std::lock_guard<std::mutex> lk(g_entities_mu);
        auto it = g_entities.find(art_method);
        if (it == g_entities.end()) return;
        entity = it->second;
    }

    JNIEnv* env = nullptr;
    if (g_jvm) {
        if (g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            g_jvm->AttachCurrentThread(&env, nullptr);
        }
    }
    if (!env) return;

    jvalue args[16] = {};
    int argc = pack_args(ctx, entity.shorty, entity.is_static, args);
    if (argc < 0) return;

    // FIX: this está en x1 (índice 0 de x1_to_x30)
    jobject receiver = entity.is_static ? nullptr 
                                        : reinterpret_cast<jobject>(ctx->x1_to_x30[0]);

    // LOG DE DIAGNÓSTICO (lo pide el reviewer)
    LOGI("HANDLER: art_method=%p this=%p arg0=%p is_static=%d shorty=%s",
         art_method, receiver, (void*)args[0].l, entity.is_static ? 1 : 0, entity.shorty);

    switch (entity.return_type) {
        case 'V':
            if (entity.is_static) env->CallStaticVoidMethodA(entity.target_class, entity.hook_method, args);
            else env->CallVoidMethodA(receiver, entity.hook_method, args);
            break;
        case 'Z': case 'B': case 'C': case 'S': case 'I': {
            jint res = entity.is_static
                ? env->CallStaticIntMethodA(entity.target_class, entity.hook_method, args)
                : env->CallIntMethodA(receiver, entity.hook_method, args);
            ctx->x0 = static_cast<uint64_t>(res);
            break;
        }
        case 'J': {
            jlong res = entity.is_static
                ? env->CallStaticLongMethodA(entity.target_class, entity.hook_method, args)
                : env->CallLongMethodA(receiver, entity.hook_method, args);
            ctx->x0 = static_cast<uint64_t>(res);
            break;
        }
        case 'F': {
            float res = entity.is_static
                ? env->CallStaticFloatMethodA(entity.target_class, entity.hook_method, args)
                : env->CallFloatMethodA(receiver, entity.hook_method, args);
            std::memcpy(&ctx->q0_to_q7[0], &res, 4);
            break;
        }
        case 'D': {
            double res = entity.is_static
                ? env->CallStaticDoubleMethodA(entity.target_class, entity.hook_method, args)
                : env->CallDoubleMethodA(receiver, entity.hook_method, args);
            std::memcpy(&ctx->q0_to_q7[0], &res, 8);
            break;
        }
        case 'L': case '[': {
            jobject res = entity.is_static
                ? env->CallStaticObjectMethodA(entity.target_class, entity.hook_method, args)
                : env->CallObjectMethodA(receiver, entity.hook_method, args);
            ctx->x0 = reinterpret_cast<uint64_t>(res);
            break;
        }
    }

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeInit(JNIEnv* env, jclass clazz) {
    static bool initialized = false;
    if (!initialized) {
        sandhook::art::init(env);
        initialized = true;
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeHookMethod(JNIEnv* env, jclass clazz,
                                                   jobject originMethod,
                                                   jobject hookMethod,
                                                   jobject backupMethod,
                                                   jstring shortyStr,
                                                   jboolean isStatic,
                                                   jclass declaringClass) {
    if (!originMethod || !hookMethod || !declaringClass) return JNI_FALSE;

    jmethodID origin_meth = env->FromReflectedMethod(originMethod);
    jmethodID hook_meth = env->FromReflectedMethod(hookMethod);
    if (!origin_meth || !hook_meth) return JNI_FALSE;

    void* origin_addr = sandhook::art::getQuickEntryPoint(origin_meth);
    void* stub_addr = sandhook::art::get_quick_stub();
    if (!origin_addr || !stub_addr) return JNI_FALSE;

    HookEntity entity;
    entity.hook_method = hook_meth;
    entity.target_class = (jclass)env->NewGlobalRef(declaringClass);
    entity.backup_entry = origin_addr;
    entity.is_static = isStatic;
    
    const char* shorty_c = env->GetStringUTFChars(shortyStr, nullptr);
    std::strncpy(entity.shorty, shorty_c, sizeof(entity.shorty) - 1);
    entity.shorty[sizeof(entity.shorty) - 1] = '\0';
    entity.return_type = entity.shorty[0];
    env->ReleaseStringUTFChars(shortyStr, shorty_c);

    // ====================================================================
    // FIX SANDHOOK: Suspender la VM de forma real (StopTheWorld)
    // ====================================================================
    sandhook::art::suspend_vm();

    {
        std::lock_guard<std::mutex> lk(g_entities_mu);
        g_entities[origin_meth] = entity;
    }

    // Configurar el backup
    if (backupMethod != nullptr) {
        jmethodID backup_meth = env->FromReflectedMethod(backupMethod);
        if (backup_meth) {
            sandhook::art::setQuickEntryPoint(backup_meth, origin_addr);
        }
    }

    // ====================================================================
    // FIX SANDHOOK: Desactivar JIT para que no recompile y borre el hook
    // ====================================================================
    sandhook::art::disable_compilable(origin_meth);
    sandhook::art::disable_compilable(hook_meth);

    // Redirigir el método original a nuestro stub ensamblador
    sandhook::art::setQuickEntryPoint(origin_meth, stub_addr);

    // Reanudar la VM
    sandhook::art::resume_vm();

    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_swift_sandhook_SandHook_nativeUnhookMethod(JNIEnv* env, jclass clazz, jobject originMethod) {
    if (!originMethod) return JNI_FALSE;
    jmethodID origin_meth = env->FromReflectedMethod(originMethod);
    if (!origin_meth) return JNI_FALSE;

    std::lock_guard<std::mutex> lk(g_entities_mu);
    auto it = g_entities.find(origin_meth);
    if (it != g_entities.end()) {
        sandhook::art::setQuickEntryPoint(origin_meth, it->second.backup_entry);
        if (it->second.target_class) {
            env->DeleteGlobalRef(it->second.target_class);
        }
        g_entities.erase(it);
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_com_swift_sandhook_SandHook_nativeGetObject(JNIEnv* env, jclass clazz, jlong ptr) {
    if (ptr == 0) return nullptr;
    return env->NewLocalRef(reinterpret_cast<jobject>(ptr));
}

} // extern "C"