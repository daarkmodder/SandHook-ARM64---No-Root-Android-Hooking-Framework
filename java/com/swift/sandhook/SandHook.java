package com.swift.sandhook;

import java.lang.reflect.Method;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class SandHook {

    static {
        System.loadLibrary("sandhook");
    }

    // --- Métodos Nativos (Ahora coinciden con sandhook_jni.cpp) ---
    public static native boolean nativeInit();
    public static native boolean nativeHookMethod(Method origin, Method hook, Method backup);
    public static native boolean nativeUnhookMethod(Method origin);
    public static native void nativePendingHook(); // Reservado para futuro
    public static native Object nativeGetObject(long ptr);

    // --- Cache de métodos hookeados ---
    private static final Map<Method, Method> hookedMethods = new ConcurrentHashMap<>();

    /**
     * Inicializa el motor de hooking (calcula offsets de ART).
     */
    public static void init() {
        try {
            nativeInit();
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    /**
     * Instala un hook estándar.
     */
    public static boolean hook(Method origin, Method hook, Method backup) {
        if (origin == null || hook == null) {
            throw new NullPointerException("Origin and Hook methods cannot be null");
        }
        try {
            boolean success = nativeHookMethod(origin, hook, backup);
            if (success) {
                hookedMethods.put(origin, hook);
            }
            return success;
        } catch (Throwable e) {
            e.printStackTrace();
            return false;
        }
    }

    /**
     * Desinstala un hook.
     */
    public static boolean unhook(Method origin) {
        if (origin == null) return false;
        try {
            boolean success = nativeUnhookMethod(origin);
            if (success) {
                hookedMethods.remove(origin);
            }
            return success;
        } catch (Throwable e) {
            e.printStackTrace();
            return false;
        }
    }

    // --- Métodos auxiliares para PendingHookHandler ---
    public static boolean initForPendingHook() {
        // En una implementación futura esto llamaría a un inicializador de Pending en C++
        return true;
    }

    public static Object getObject(long ptr) {
        try {
            return nativeGetObject(ptr);
        } catch (Throwable e) {
            return null;
        }
    }
}