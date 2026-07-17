// art_hook.cpp
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
            if (sdk_int != 0) return;

            jclass versionClass = env->FindClass("android/os/Build$VERSION");
            if (!versionClass) {
                env->ExceptionClear();
                LOGE("Failed to find Build.VERSION class");
                return;
            }
            jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
            sdk_int = env->GetStaticIntField(versionClass, sdkIntField);
            env->DeleteLocalRef(versionClass);

            // Cálculo de offsets (Mejorado)
            if (sdk_int >= 26) { 
                entry_point_offset = 24; // Android 8.0+ (Oreo)
            } else if (sdk_int >= 24) { 
                entry_point_offset = 32; // Android 7.0 (Nougat)
            } else {
                entry_point_offset = 32; // Fallback
            }
            LOGI("ART initialized. SDK_INT: %d, EntryPoint Offset: %zu", sdk_int, entry_point_offset);
        }

        // Técnica de Dobby/ShadowHook: Strip PAC en ARMv8.3+
        // Usamos xpaclri para limpiar el PAC del registro LR, lo que nos da
        // una dirección base limpia. Luego aplicamos la máscara.
        void* pac_strip(void* addr) {
            if (!addr) return nullptr;
            
#if defined(__aarch64__)
            // Si el procesador soporta PAC, los bits superiores están firmados.
            // Hacemos un clear de los bits 63:55 (o 63:39 en dispositivos extremadamente estrictos)
            // Pero la forma segura en C++ es enmascarar.
            // En Android, generalmente los bits de PAC están en 55:39 o superiores.
            // Para no romper la adresación, enmascaramos solo si el bit 55 está activo.
            uintptr_t val = reinterpret_cast<uintptr_t>(addr);
            if (val & (1ULL << 55)) {
                // Limpiamos los bits de PAC (bits 55 a 63)
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
            
            // Limpiamos PAC antes de devolver el puntero
            return pac_strip(entry);
        }

        void setQuickEntryPoint(jmethodID methodId, void* entry) {
            if (!methodId || entry_point_offset == 0) return;
            
            uintptr_t art_method_ptr = reinterpret_cast<uintptr_t>(methodId);
            *reinterpret_cast<void**>(art_method_ptr + entry_point_offset) = entry;
        }
    }
}