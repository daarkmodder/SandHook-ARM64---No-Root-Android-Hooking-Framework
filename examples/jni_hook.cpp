// jni_hook.cpp – JNI library that hooks a native function on load
// Build: g++ -std=c++17 -shared -fPIC -o libjni_hook.so jni_hook.cpp -llog -ldl -static-libgcc

#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>

#define LOG_TAG "JNIHook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

typedef int (*hook_fn)(void*, void*, void**);

static int (*original_printf)(const char*, ...) = NULL;

int hooked_printf(const char* fmt, ...) {
    LOGI("hooked_printf called with fmt: %s", fmt);
    return original_printf(fmt);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    void* lib = dlopen("libsandhook.so", RTLD_NOW);
    if (!lib) { LOGI("libsandhook not found"); return JNI_VERSION_1_6; }
    hook_fn install = (hook_fn)dlsym(lib, "sandhook_install_ex");
    if (install) {
        install((void*)printf, (void*)hooked_printf, (void**)&original_printf);
        LOGI("printf hooked");
    }
    return JNI_VERSION_1_6;
}