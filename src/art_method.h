// art_method.h
// Interface for interacting with Android Runtime (ART) ArtMethod struct
// Allows extracting and modifying native entry points of Java methods.

#pragma once
#include <jni.h>
#include <cstdint>

namespace sandhook {
    namespace art {
        // Initializes internal ART offsets based on the current Android SDK level.
        // Must be called once before using get/set QuickEntryPoint.
        void init(JNIEnv* env);

        // Retrieves the native quick code entry point from a Java method.
        // This is the memory address that the CPU executes.
        void* getQuickEntryPoint(jmethodID methodId);

        // Sets the native quick code entry point for a Java method.
        // Used to redirect Java method execution to a trampoline.
        void setQuickEntryPoint(jmethodID methodId, void* entry);
    }
}