// libc_hook.c – Hook strcmp using GOT (works without SELinux issues)
// Build: gcc -O2 libc_hook.c -o libc_hook -ldl -llog -static

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>

#define LOG_TAG "LibcHook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

typedef int (*hook_fn)(void*, void*, void**);

int hooked_strcmp(const char* s1, const char* s2) {
    LOGI("strcmp(\"%s\", \"%s\") intercepted", s1, s2);
    return 0; // always equal
}

int main() {
    void* lib = dlopen("libsandhook.so", RTLD_NOW);
    if (!lib) return 1;
    hook_fn install = (hook_fn)dlsym(lib, "sandhook_install_ex");
    if (!install) return 1;

    int (*orig)(const char*, const char*) = NULL;
    int err = install((void*)strcmp, (void*)hooked_strcmp, (void**)&orig);
    if (err) { printf("install error %d\n", err); return 1; }

    int res = strcmp("hello", "world");
    printf("strcmp(\"hello\",\"world\") = %d (expected 0)\n", res);
    return 0;
}