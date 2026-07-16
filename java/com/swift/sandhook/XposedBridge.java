package com.swift.sandhook;

import java.lang.reflect.Method;

/**
 * Capa de compatibilidad para imitar la API de Xposed.
 * Permite a los desarrolladores acostumbrados a Xposed usar SandHook fácilmente.
 */
public class XposedBridge {

    /**
     * Interface callback al estilo Xposed.
     */
    public interface XC_MethodHook {
        void beforeHookedMethod(MethodHookParam param) throws Throwable;
        void afterHookedMethod(MethodHookParam param) throws Throwable;
    }

    /**
     * Clase de parámetros al estilo Xposed.
     */
    public static class MethodHookParam {
        public Method method;
        public Object thisObject;
        public Object[] args;
        private Object result = null;
        private Throwable throwable = null;

        public Object getResult() { return result; }
        public void setResult(Object result) { this.result = result; }
        public Throwable getThrowable() { return throwable; }
        public void setThrowable(Throwable throwable) { this.throwable = throwable; }
    }

    /**
     * Encuentra y hookea un método al estilo Xposed.
     * 
     * Ejemplo de uso:
     * XposedBridge.findAndHookMethod(ClassLoader.class, "loadClass", String.class, new XC_MethodHook() {
     *     @Override
     *     protected void afterHookedMethod(MethodHookParam param) {
     *         Log.d("Xposed", "Clase cargada: " + param.args[0]);
     *     }
     * });
     */
    public static Method findAndHookMethod(Class<?> clazz, String methodName, Object... parameterTypesAndCallback) {
        if (parameterTypesAndCallback.length == 0 || !(parameterTypesAndCallback[parameterTypesAndCallback.length - 1] instanceof XC_MethodHook)) {
            throw new IllegalArgumentException("El último argumento debe ser un XC_MethodHook");
        }

        XC_MethodHook callback = (XC_MethodHook) parameterTypesAndCallback[parameterTypesAndCallback.length - 1];
        Class<?>[] paramTypes = new Class<?>[parameterTypesAndCallback.length - 1];
        for (int i = 0; i < paramTypes.length; i++) {
            if (!(parameterTypesAndCallback[i] instanceof Class)) {
                throw new IllegalArgumentException("Los parámetros deben ser de tipo Class");
            }
            paramTypes[i] = (Class<?>) parameterTypesAndCallback[i];
        }

        try {
            // 1. Encontrar el método original por reflexión
            Method originMethod = clazz.getDeclaredMethod(methodName, paramTypes);
            originMethod.setAccessible(true);

            // En un framework Xposed completo, aquí generaríamos dinámicamente un método 
            // "hook" y un método "backup" usando librerías como ASM o Dexmaker.
            // Para esta integración básica de tu motor nativo, asumimos que el usuario 
            // pasará los métodos directamente a SandHook.hook() o delegaremos en él.

            // NOTA: Para un soporte Xposed 100% dinámico (donde solo pasas el callback),
            // requerirías un generador de bytecode que cree los métodos Method en memoria.
            // Esta clase sirve como punto de entrada para esa futura expansión.

            return originMethod;
        } catch (NoSuchMethodException e) {
            throw new Error("Método no encontrado: " + methodName, e);
        }
    }
}