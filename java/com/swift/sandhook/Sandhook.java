package com.swift.sandhook;

import java.lang.reflect.Method;

public class SandHook {

    // Cargar la librería nativa compilada con tus 4 pasos anteriores
    static {
        System.loadLibrary("sandhook");
    }

    // Métodos nativos definidos en sandhook_jni.cpp
    public static native boolean initNative();
    public static native boolean hookMethod(Method origin, Method hook, Method backup);
    public static native boolean unhookMethod(Method origin);

    /**
     * Inicializa el motor de hooking (calcula offsets de ART).
     * Debe llamarse al inicio de la app.
     */
    public static void init() {
        try {
            initNative();
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    /**
     * Instala un hook nativo.
     *
     * @param origin  El método original de la app que quieres interceptar.
     * @param hook    Tu método de reemplazo (debe tener la misma firma).
     * @param backup  Un método vacío donde se guardará el trampolín para llamar al original (puede ser null).
     * @return true si tuvo éxito.
     */
    public static boolean hook(Method origin, Method hook, Method backup) {
        if (origin == null || hook == null) {
            throw new NullPointerException("Origin and Hook methods cannot be null");
        }
        try {
            return hookMethod(origin, hook, backup);
        } catch (Throwable e) {
            e.printStackTrace();
            return false;
        }
    }
}