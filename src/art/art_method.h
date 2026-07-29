// art_method.h
#pragma once
#include <jni.h>
#include <cstdint>

namespace sandhook {
    namespace art {
        void init(JNIEnv* env);
        void* pac_strip(void* addr);
        void* getQuickEntryPoint(jmethodID methodId);
        void setQuickEntryPoint(jmethodID methodId, void* entry);
        void* get_quick_stub();
        
        // Nuevas APIs de ART (Hidden APIs)
        void suspend_vm();
        void resume_vm();
        void disable_compilable(jmethodID methodId);
    }
}