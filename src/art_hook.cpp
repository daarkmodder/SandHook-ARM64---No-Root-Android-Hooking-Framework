// art_hook.cpp
// Dynamic ArtMethod Offset Finder + Entry Point Replacement (Android 9-14+)
#include "art_method.h"
#include <android/log.h>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <string>
#include <sstream>
#include <fstream>

#define LOG_TAG "SandHook-ART"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// Declaración de la función ensambladora externa
extern "C" void sandhook_art_quick_stub();

namespace sandhook {
    namespace art {
        static int sdk_int = 0;
        static size_t entry_point_offset = 0;
        
        void init(JNIEnv* env) {
            if (sdk_int != 0) return;

            jclass versionClass = env->FindClass("android/os/Build$VERSION");
            if (!versionClass) return;
            jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
            sdk_int = env->GetStaticIntField(versionClass, sdkIntField);
            env->DeleteLocalRef(versionClass);

            // 1. Cargar libart.so y obtener el puente del intérprete
            void* libart = dlopen("libart.so", RTLD_NOW);
            if (!libart) {
                LOGE("Failed to load libart.so");
                return;
            }

            void* interpreter_bridge = dlsym(libart, "art_quick_to_interpreter_bridge");
            if (!interpreter_bridge) {
                LOGW("Could not find art_quick_to_interpreter_bridge. Using fallback offset 24.");
                entry_point_offset = 24; // Fallback Android 9+
                return;
            }

            // 2. Obtener un ArtMethod conocido (Object.hashCode)
            jclass objClass = env->FindClass("java/lang/Object");
            jmethodID dummy_method = env->GetMethodID(objClass, "hashCode", "()I");
            env->DeleteLocalRef(objClass);

            if (!dummy_method) {
                LOGE("Failed to get dummy method for offset calculation.");
                entry_point_offset = 24;
                return;
            }

            // 3. Calcular offset dinámicamente escaneando la estructura ArtMethod
            uintptr_t art_method_ptr = reinterpret_cast<uintptr_t>(dummy_method);
            entry_point_offset = 0;
            
            // El ArtMethod suele medir 32 o 40 bytes. Escaneamos los primeros 64 bytes.
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
        }

        void* pac_strip(void* addr) {
            if (!addr) return nullptr;
#if defined(__aarch64__)
            // En Android 13+ (ARM64 v8.3+), los punteros pueden tener firmas PAC.
            // Limpiamos los bits superiores para obtener la dirección real.
            uintptr_t val = reinterpret_cast<uintptr_t>(addr);
            val &= 0x0000FFFFFFFFFFFFULL; // Máscara de 48 bits para espacio de usuario
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