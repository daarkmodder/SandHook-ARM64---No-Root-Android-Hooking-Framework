// ClassStatusUtils.java
package com.swift.sandhook.utils;

import com.swift.sandhook.SandHookConfig;
import java.lang.reflect.Field;

public class ClassStatusUtils {
    static Field fieldStatusOfClass;
    static {
        try {
            fieldStatusOfClass = Class.class.getDeclaredField("status");
            fieldStatusOfClass.setAccessible(true);
        } catch (NoSuchFieldException e) {}
    }

    public static boolean isInitialized(Class clazz) {
        if (fieldStatusOfClass == null) return true;
        try {
            int status = fieldStatusOfClass.getInt(clazz);
            if (SandHookConfig.SDK_INT >= 30) return status >= 14;
            if (SandHookConfig.SDK_INT >= 28) return status == 14;
            if (SandHookConfig.SDK_INT == 27) return status == 11;
            return status == 10;
        } catch (Throwable e) { return true; }
    }
}