// sandhook_jni_test.c – JNI test for SandHook
// Build: gcc -shared -fPIC -o libsandhooktest.so sandhook_jni_test.c -llog -ldl -static

#include <jni.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "SandHookTest"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

typedef int (*sandhook_install_ex_t)(void*, void*, void**);
typedef int (*sandhook_remove_t)(void*);
typedef const char* (*sandhook_version_t)(void);
typedef const char* (*sandhook_error_string_t)(int);

static sandhook_install_ex_t sandhook_install_ex = NULL;
static sandhook_remove_t sandhook_remove = NULL;
static sandhook_version_t sandhook_version = NULL;
static sandhook_error_string_t sandhook_error_string = NULL;

static int stub_remove(void* t) { return 0; }
static const char* stub_version() { return "unknown"; }
static const char* stub_error_string(int e) {
    switch (e) {
        case 0: return "OK";
        case 1: return "NULL_ARGS";
        case 2: return "ALREADY_HOOKED";
        case 3: return "RELOCATION_FAILED";
        case 4: return "MPROTECT_FAILED";
        case 5: return "ALLOC_FAILED";
        case 6: return "INVALID_TARGET";
        case 7: return "BOUNDS_EXCEEDED";
        default: return "UNKNOWN";
    }
}

int load_sandhook() {
    void* h = dlopen("libsandhook.so", RTLD_NOW);
    if (!h) { LOGE("dlopen failed: %s", dlerror()); return 0; }
    sandhook_install_ex = (sandhook_install_ex_t)dlsym(h, "sandhook_install_ex");
    if (!sandhook_install_ex) { LOGE("sandhook_install_ex missing"); return 0; }
    sandhook_remove = (sandhook_remove_t)dlsym(h, "sandhook_remove");
    if (!sandhook_remove) sandhook_remove = stub_remove;
    sandhook_version = (sandhook_version_t)dlsym(h, "sandhook_version");
    if (!sandhook_version) sandhook_version = stub_version;
    sandhook_error_string = (sandhook_error_string_t)dlsym(h, "sandhook_error_string");
    if (!sandhook_error_string) sandhook_error_string = stub_error_string;
    LOGI("SandHook loaded: %s", sandhook_version());
    return 1;
}

// Test function
int test_add(int a, int b) { return a + b; }

int hooked_add(int a, int b) {
    LOGI("hooked_add(%d, %d)", a, b);
    return a + b + 100;
}

static int (*original_add)(int, int) = NULL;

void run_tests() {
    LOGI("[TEST] Hooking test_add...");
    int err = sandhook_install_ex((void*)test_add, (void*)hooked_add, (void**)&original_add);
    if (err != 0) {
        LOGE("install failed: %s", sandhook_error_string(err));
        return;
    }
    LOGI("Hook installed, original_add=%p", original_add);
    int res = test_add(5, 3);
    LOGI("test_add(5,3) = %d", res);
    sandhook_remove((void*)test_add);
    LOGI("[TEST] Done");
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad started");
    if (load_sandhook()) {
        run_tests();
    }
    return JNI_VERSION_1_6;
}