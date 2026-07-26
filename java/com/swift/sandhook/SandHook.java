package com.swift.sandhook;

import java.lang.reflect.Method;

public class SandHook {

    static {
        System.loadLibrary("sandhook");
    }

    public static native boolean nativeInit();
    public static native boolean nativeHookMethod(Method origin, Method hook, Method backup);
    public static native boolean nativeUnhookMethod(Method origin);
    public static native Object nativeGetObject(long ptr);

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
     * Instala un hook nativo usando Direct Swap.
     */
    public static boolean hook(Method origin, Method hook, Method backup) {
        if (origin == null || hook == null) {
            throw new NullPointerException("Origin and Hook methods cannot be null");
        }
        try {
            // 1. FORZAR COMPILACIÓN JIT (Truco de LSPosed/YAHFA)
            // Esto asegura que el método original tenga un "entry_point" nativo válido.
            warmUpMethod(origin);
            warmUpMethod(hook);
            
            // 2. Instalar el hook
            boolean success = nativeHookMethod(origin, hook, backup);
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
            return nativeUnhookMethod(origin);
        } catch (Throwable e) {
            e.printStackTrace();
            return false;
        }
    }

    // --- Truco para forzar la compilación JIT en Android 9+ ---
    private static void warmUpMethod(Method method) {
        try {
            method.setAccessible(true);
            // Intentamos invocar el método con argumentos nulos.
            // Si el método es estático, el primer argumento es null.
            // Si falla (porque requiere argumentos específicos), capturamos la excepción
            // pero el simple intento a menudo basta para que el JIT lo compile.
            method.invoke(null, new Object[method.getParameterTypes().length]);
        } catch (Throwable e) {
            // Ignoramos el error. La excepción (NullPointerException, etc.)
            // ya le dijo al compilador JIT que evalúe el método.
        }
    }

    // --- Métodos auxiliares ---
    public static boolean initForPendingHook() {
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