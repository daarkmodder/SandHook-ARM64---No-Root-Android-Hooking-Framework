// art_method.h
#pragma once
#include <jni.h>
#include <cstdint>

namespace sandhook {
    namespace art {
        // Initializes internal ART offsets based on the current Android SDK level.
        void init(JNIEnv* env);

        // Strips Pointer Authentication Codes (PAC) from ARMv8.3+ pointers.
        // Crucial for Android 11+ where entry points are signed.
        void* pac_strip(void* addr);

        // Retrieves the native quick code entry point from a Java method.
        void* getQuickEntryPoint(jmethodID methodId);

        // Sets the native quick code entry point for a Java method.
        void setQuickEntryPoint(jmethodID methodId, void* entry);
    }
}