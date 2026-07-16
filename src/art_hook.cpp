// art_hook.cpp
// Implementation of ArtMethod manipulation for Android 7.0 - 12.0+
// ARM64 architecture only.

#include "art_method.h"
#include <android/log.h>
#include <cstring>

#define LOG_TAG "SandHook-ART"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sandhook {
    namespace art {
        static int sdk_int = 0;
        static size_t entry_point_offset = 0;

        void init(JNIEnv* env) {
            if (sdk_int != 0) return; // Already initialized

            // 1. Obtener el SDK_INT desde Java
            jclass versionClass = env->FindClass("android/os/Build$VERSION");
            if (!versionClass) {
                LOGE("Failed to find Build.VERSION class");
                return;
            }
            jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
            if (!sdkIntField) {
                LOGE("Failed to find SDK_INT field");
                env->DeleteLocalRef(versionClass);
                return;
            }
            sdk_int = env->GetStaticIntField(versionClass, sdkIntField);
            env->DeleteLocalRef(versionClass);

            // 2. Calcular el offset de 'entry_point_from_quick_compiled_code'
            // En ARM64 (64 bits), el tamaño de un puntero es 8 bytes.
            // Los offsets de ArtMethod han cambiado a lo largo de las versiones de Android.
            
            if (sdk_int >= 26) { 
                // Android 8.0 (Oreo) y superiores (incluyendo 11 y 12)
                // El tamaño de ArtMethod se redujo. El entry point suele estar en el offset 24.
                entry_point_offset = 24;
            } else if (sdk_int >= 24) { 
                // Android 7.0 - 7.1 (Nougat)
                entry_point_offset = 32;
            } else if (sdk_int >= 23) { 
                // Android 6.0 (Marshmallow)
                entry_point_offset = 32;
            } else {
                // Fallback para versiones antiguas (no guarantees on ARM64)
                entry_point_offset = 32;
            }

            LOGI("ART initialized. SDK_INT: %d, EntryPoint Offset: %zu", sdk_int, entry_point_offset);
        }

        void* getQuickEntryPoint(jmethodID methodId) {
            if (!methodId || entry_point_offset == 0) return nullptr;
            
            // jmethodID es, bajo el capó, un puntero directo al struct ArtMethod en C++.
            // (Nota: En Android 11+ ART introdujo indirection para jmethodID en algunos casos,
            // pero para hooks de bajo nivel, el casteo directo sigue siendo el estándar en la mayoría de dispositivos).
            uintptr_t art_method_ptr = reinterpret_cast<uintptr_t>(methodId);
            
            // Leer el puntero en la dirección de memoria + offset
            void* entry = *reinterpret_cast<void**>(art_method_ptr + entry_point_offset);
            return entry;
        }

        void setQuickEntryPoint(jmethodID methodId, void* entry) {
            if (!methodId || entry_point_offset == 0) return;
            
            uintptr_t art_method_ptr = reinterpret_cast<uintptr_t>(methodId);
            
            // Escribir el nuevo puntero en la memoria
            *reinterpret_cast<void**>(art_method_ptr + entry_point_offset) = entry;
        }
    }
}