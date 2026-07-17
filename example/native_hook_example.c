// native_hook_example.c
// Example: Hooking a native C function using SandHook
// Build: clang -shared -fPIC -target aarch64-linux-android21 native_hook_example.c -o libnative_example.so -llog -ldl

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>

#define LOG_TAG "NativeExample"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// SandHook API signatures
typedef int (*sandhook_install_ex_t)(void*, void*, void**);
typedef const char* (*sandhook_error_string_t)(int);

static sandhook_install_ex_t sandhook_install_ex = NULL;
static sandhook_error_string_t sandhook_error_string = NULL;

// Original function pointer (Trampoline)
static size_t (*original_strlen)(const char*) = NULL;

// Hook replacement
size_t hooked_strlen(const char* str) {
    LOGI("strlen intercepted: \"%s\"", str);
    // Call original via trampoline
    return original_strlen(str);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("Loading SandHook...");
    void* lib = dlopen("libsandhook.so", RTLD_NOW);
    if (!lib) {
        LOGE("Failed to load libsandhook.so: %s", dlerror());
        return -1;
    }

    sandhook_install_ex = (sandhook_install_ex_t)dlsym(lib, "sandhook_install_ex");
    sandhook_error_string = (sandhook_error_string_t)dlsym(lib, "sandhook_error_string");
    
    if (!sandhook_install_ex || !sandhook_error_string) {
        LOGE("Failed to find SandHook symbols");
        return -1;
    }

    LOGI("Installing hook on strlen...");
    int err = sandhook_install_ex((void*)strlen, (void*)hooked_strlen, (void**)&original_strlen);
    
    if (err == 0) {
        LOGI("Hook installed successfully!");
    } else {
        LOGE("Hook failed: %s", sandhook_error_string(err));
        return -1;
    }

    return JNI_VERSION_1_6;
}