// art_method.h
#pragma once
#include <jni.h>
#include <cstdint>

namespace sandhook {
    namespace art {
        // Initializes internal ART offsets based on the current Android SDK level.
        void init(JNIEnv* env);

        // Suspends the Android Runtime VM (Real StopTheWorld).
        void suspendVM_Runtime();

        // Resumes the Android Runtime VM.
        void resumeVM_Runtime();

        // Strips Pointer Authentication Codes (PAC) from ARMv8.3+ pointers.
        void* pac_strip(void* addr);

        // Retrieves the native quick code entry point from a Java method.
        void* getQuickEntryPoint(jmethodID methodId);

        // Sets the native quick code entry point for a Java method.
        void setQuickEntryPoint(jmethodID methodId, void* entry);
    }
}