package com.swift.sandhook;

import java.lang.reflect.Method;

public class SandHook {

    static {
        System.loadLibrary("sandhook");
    }

    public static native boolean nativeInit();
    public static native boolean nativeHookMethod(Method origin, Method hook, Method backup, String shorty, boolean isStatic, Class<?> declaringClass);
    public static native boolean nativeUnhookMethod(Method origin);
    public static native Object nativeGetObject(long ptr);

    public static void init() {
        try {
            nativeInit();
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    public static boolean hook(Method origin, Method hook, Method backup) {
        if (origin == null || hook == null) {
            throw new NullPointerException("Origin and Hook methods cannot be null");
        }
        try {
            warmUpMethod(origin);
            warmUpMethod(hook);
            
            String shorty = getShorty(origin);
            boolean isStatic = java.lang.reflect.Modifier.isStatic(origin.getModifiers());
            Class<?> declaringClass = origin.getDeclaringClass();
            
            return nativeHookMethod(origin, hook, backup, shorty, isStatic, declaringClass);
        } catch (Throwable e) {
            e.printStackTrace();
            return false;
        }
    }

    public static boolean unhook(Method origin) {
        if (origin == null) return false;
        try {
            return nativeUnhookMethod(origin);
        } catch (Throwable e) {
            e.printStackTrace();
            return false;
        }
    }

    private static void warmUpMethod(Method method) {
        try {
            method.setAccessible(true);
            method.invoke(null, new Object[method.getParameterTypes().length]);
        } catch (Throwable e) { }
    }

    private static String getShorty(Method method) {
        StringBuilder sb = new StringBuilder();
        appendShorty(sb, method.getReturnType());
        for (Class<?> p : method.getParameterTypes()) {
            appendShorty(sb, p);
        }
        return sb.toString();
    }

    // FIX: Arrays ahora usan '[' en vez de 'L'
    private static void appendShorty(StringBuilder sb, Class<?> c) {
        if (c == void.class)          sb.append('V');
        else if (c == boolean.class)  sb.append('Z');
        else if (c == byte.class)     sb.append('B');
        else if (c == char.class)     sb.append('C');
        else if (c == short.class)    sb.append('S');
        else if (c == int.class)      sb.append('I');
        else if (c == long.class)     sb.append('J');
        else if (c == float.class)    sb.append('F');
        else if (c == double.class)   sb.append('D');
        else if (c.isArray())         sb.append('[');
        else                          sb.append('L');
    }

    public static boolean initForPendingHook() { return true; }
    public static Object getObject(long ptr) {
        try { return nativeGetObject(ptr); } catch (Throwable e) { return null; }
    }
}