package com.swift.sandhook;

import com.swift.sandhook.utils.ClassStatusUtils;
import com.swift.sandhook.wrapper.HookErrorException;
import com.swift.sandhook.wrapper.HookWrapper;
import java.util.Map;
import java.util.Vector;
import java.util.concurrent.ConcurrentHashMap;

public class PendingHookHandler {
    private static Map<Class, Vector<HookWrapper.HookEntity>> pendingHooks = new ConcurrentHashMap<>();
    private static boolean canUsePendingHook;

    static {
        if (SandHookConfig.delayHook) {
            canUsePendingHook = SandHook.initForPendingHook();
        }
    }

    public static boolean canWork() {
        return canUsePendingHook;
    }

    public static synchronized void addPendingHook(HookWrapper.HookEntity hookEntity) {
        Vector<HookWrapper.HookEntity> entities = pendingHooks.get(hookEntity.target.getDeclaringClass());
        if (entities == null) {
            entities = new Vector<>();
            pendingHooks.put(hookEntity.target.getDeclaringClass(), entities);
        }
        entities.add(hookEntity);
    }

    public static void onClassInit(long clazz_ptr) {
        if (clazz_ptr == 0) return;
        Class clazz = (Class) SandHook.getObject(clazz_ptr);
        if (clazz == null) return;
        
        Vector<HookWrapper.HookEntity> entities = pendingHooks.get(clazz);
        if (entities == null) return;
        
        for (HookWrapper.HookEntity entity : entities) {
            try {
                entity.initClass = false;
                SandHook.hook(entity.target, entity.hook, entity.backup);
            } catch (Throwable e) {
                HookLog.e("Pending Hook Error!", e);
            }
        }
        pendingHooks.remove(clazz);
    }
}