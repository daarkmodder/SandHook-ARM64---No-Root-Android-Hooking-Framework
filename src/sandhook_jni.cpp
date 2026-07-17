// sandhook_jni.cpp
#include <jni.h>
#include <android/log.h>
#include "sandhook.h"
#include "art_method.h"

#define LOG_TAG "SandHook-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void ensure_art_initialized(JNIEnv* env) {
    static bool initialized = false;
    if (!initialized) {
        sandhook::art::init(env);
        initialized = true;
    }
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

    void* origin_addr = sandhook::art::getQuickEntryPoint(origin_meth);
    void* hook_addr = sandhook::art::getQuickEntryPoint(hook_meth);
    if (!origin_addr || !hook_addr) return JNI_FALSE;

    LOGI("Hooking Origin: %p -> Replacement: %p", origin_addr, hook_addr);

    // --- STOP THE WORLD REAL ---
    // Suspender la VM de Android para que el Garbage Collector no mueva la memoria
    sandhook::art::suspendVM_Runtime();

    void* trampoline = nullptr;
    int result = sandhook_install_ex(origin_addr, hook_addr, 
                                     (backupMethod != nullptr) ? &trampoline : nullptr);

    if (result == HOOK_OK) {
        if (backupMethod != nullptr && trampoline != nullptr) {
            jmethodID backup_meth = env->FromReflectedMethod(backupMethod);
            if (backup_meth) {
                sandhook::art::setQuickEntryPoint(backup_meth, trampoline);
            }
        }
    }

    // Reanudar la VM de Android
    sandhook::art::resumeVM_Runtime();

    if (result == HOOK_OK) return JNI_TRUE;
    LOGE("Hook installation failed: %s", sandhook_error_string(result));
    return JNI_FALSE;
}

} // extern "C"