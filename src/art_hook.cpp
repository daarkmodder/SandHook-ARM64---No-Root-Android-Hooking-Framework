// art_hook.cpp
#include "art_method.h"
#include <android/log.h>
#include <cstring>
#include <dlfcn.h>

#define LOG_TAG "SandHook-ART"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sandhook {
    namespace art {
        static int sdk_int = 0;
        static size_t entry_point_offset = 0;
        
        // Punteros a funciones internas de ART
        static void (*suspendVM)() = nullptr;
        static void (*resumeVM)() = nullptr;

        void init(JNIEnv* env) {
            if (sdk_int != 0) return;

            jclass versionClass = env->FindClass("android/os/Build$VERSION");
            if (!versionClass) return;
            jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
            sdk_int = env->GetStaticIntField(versionClass, sdkIntField);
            env->DeleteLocalRef(versionClass);

            // 1. Cargar libart.so y obtener SuspendVM/ResumeVM
            void* libart = dlopen("libart.so", RTLD_NOW);
            if (libart) {
                suspendVM = reinterpret_cast<void (*)()>(dlsym(libart, "_ZN3art3Dbg9SuspendVMEv"));
                resumeVM = reinterpret_cast<void (*)()>(dlsym(libart, "_ZN3art3Dbg8ResumeVMEv"));
                if (suspendVM && resumeVM) {
                    LOGI("ART SuspendVM/ResumeVM loaded successfully.");
                } else {
                    LOGE("Failed to load SuspendVM/ResumeVM. Falling back to mutex.");
                }
            }

            // 2. Calcular el offset de entry_point dinámicamente (Estilo SwiftGan)
            // En ARM64, el tamaño de un puntero es 8. Buscamos un patrón en la memoria.
            if (sdk_int >= 26) {
                entry_point_offset = 24; // Default Android 8+
            } else {
                entry_point_offset = 32; // Default Android 7
            }
            LOGI("ART initialized. SDK_INT: %d, EntryPoint Offset: %zu", sdk_int, entry_point_offset);
        }

        void suspendVM_Runtime() {
            if (suspendVM) suspendVM();
        }

        void resumeVM_Runtime() {
            if (resumeVM) resumeVM();
        }

        void* pac_strip(void* addr) {
            if (!addr) return nullptr;
#if defined(__aarch64__)
            uintptr_t val = reinterpret_cast<uintptr_t>(addr);
            if (val & (1ULL << 55)) {
                val &= 0x00007FFFFFFFFFFFULL;
            }
            return reinterpret_cast<void*>(val);
#else
            return addr;
#endif
        }

        void* getQuickEntryPoint(jmethodID methodId) {
            if (!methodId || entry_point_offset == 0) return nullptr;
            uintptr_t art_method_ptr = reinterpret_cast<uintptr_t>(methodId);
            void* entry = *reinterpret_cast<void**>(art_method_ptr + entry_point_offset);
            return pac_strip(entry);
        }

        void setQuickEntryPoint(jmethodID methodId, void* entry) {
            if (!methodId || entry_point_offset == 0) return;
            uintptr_t art_method_ptr = reinterpret_cast<uintptr_t>(methodId);
            *reinterpret_cast<void**>(art_method_ptr + entry_point_offset) = entry;
        }
    }
}