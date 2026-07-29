// art_hook.cpp
#include "art_method.h"
#include <android/log.h>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <string>
#include <sstream>
#include <fstream>
#include "../xdl/xdl.h"

#define LOG_TAG "SandHook-ART"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

extern "C" void sandhook_art_quick_stub();

namespace sandhook {
    namespace art {
        static int sdk_int = 0;
        static size_t entry_point_offset = 0;
        
        // Hidden API: Punteros a funciones de suspensión de ART
        static void (*art_suspend_vm)() = nullptr;
        static void (*art_resume_vm)() = nullptr;
        
        void init(JNIEnv* env) {
            if (sdk_int != 0) return;

            jclass versionClass = env->FindClass("android/os/Build$VERSION");
            if (!versionClass) return;
            jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
            sdk_int = env->GetStaticIntField(versionClass, sdkIntField);
            env->DeleteLocalRef(versionClass);

            void* libart = xdl_open("libart.so", XDL_DEFAULT);
            if (!libart) {
                LOGE("Failed to load libart.so via xdl");
                return;
            }

            // 1. Resolver Suspensión de VM (StopTheWorld real)
            // FIX: xdl_dsym requiere 3 argumentos (handle, symbol, symbol_size)
            art_suspend_vm = (void(*)())xdl_dsym(libart, "_ZN3art3Dbg9SuspendVMEv", nullptr);
            art_resume_vm = (void(*)())xdl_dsym(libart, "_ZN3art3Dbg8ResumeVMEv", nullptr);
            if (art_suspend_vm && art_resume_vm) {
                LOGI("ART SuspendVM/ResumeVM resolved successfully!");
            } else {
                LOGW("Could not resolve SuspendVM. Thread safety fallback to mutex.");
            }

            // 2. Resolver Entry Point
            void* interpreter_bridge = xdl_sym(libart, "art_quick_to_interpreter_bridge", nullptr);
            if (!interpreter_bridge) {
                LOGW("Symbol not in .dynsym. Trying hidden .symtab via xdl_dsym...");
                interpreter_bridge = xdl_dsym(libart, "art_quick_to_interpreter_bridge", nullptr);
            }

            if (!interpreter_bridge) {
                LOGW("Could not find art_quick_to_interpreter_bridge. Using fallback offset 24.");
                entry_point_offset = 24; 
                xdl_close(libart);
                return;
            }

            LOGI("Successfully found art_quick_to_interpreter_bridge via xDL!");

            jclass objClass = env->FindClass("java/lang/Object");
            jmethodID dummy_method = env->GetMethodID(objClass, "hashCode", "()I");
            env->DeleteLocalRef(objClass);

            if (!dummy_method) {
                LOGE("Failed to get dummy method for offset calculation.");
                entry_point_offset = 24;
                xdl_close(libart);
                return;
            }

            uintptr_t art_method_ptr = reinterpret_cast<uintptr_t>(dummy_method);
            entry_point_offset = 0;
            
            for (size_t i = 0; i < 64; i += sizeof(void*)) {
                void* val = *reinterpret_cast<void**>(art_method_ptr + i);
                if (val == interpreter_bridge) {
                    entry_point_offset = i;
                    break;
                }
            }

            if (entry_point_offset == 0) {
                LOGW("Dynamic offset scan failed. Using fallback 24.");
                entry_point_offset = 24;
            } else {
                LOGI("Dynamic ArtMethod offset found successfully: %zu", entry_point_offset);
            }
            
            xdl_close(libart);
        }

        void suspend_vm() {
            if (art_suspend_vm) art_suspend_vm();
        }

        void resume_vm() {
            if (art_resume_vm) art_resume_vm();
        }

        void disable_compilable(jmethodID methodId) {
            if (!methodId || sdk_int < 24) return; // Solo Android 7+
            uintptr_t art_method_ptr = reinterpret_cast<uintptr_t>(methodId);
            
            // En Android 9-14, access_flags está en el offset 4
            uint32_t* access_flags = reinterpret_cast<uint32_t*>(art_method_ptr + 4);
            
            // kAccCompileDontBother (0x02000000) en Android 8.1+
            // kAccPreCompiled (0x00200000) en Android 7-8
            *access_flags |= 0x02000000 | 0x00200000;
        }

        void* pac_strip(void* addr) {
            if (!addr) return nullptr;
#if defined(__aarch64__)
            uintptr_t val = reinterpret_cast<uintptr_t>(addr);
            val &= 0x0000FFFFFFFFFFFFULL;
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

        void* get_quick_stub() {
            return (void*)sandhook_art_quick_stub;
        }
    }
}