// art_method.h
#pragma once
#include <jni.h>
#include <cstdint>

namespace sandhook {
    namespace art {
        void init(JNIEnv* env);
        void suspendVM_Runtime();
        void resumeVM_Runtime();
        void* pac_strip(void* addr);
        void* getQuickEntryPoint(jmethodID methodId);
        void setQuickEntryPoint(jmethodID methodId, void* entry);
        
        // NUEVA FUNCIÓN
        void* get_quick_stub();
    }
}