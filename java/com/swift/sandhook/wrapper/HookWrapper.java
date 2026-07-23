package com.swift.sandhook.wrapper;

import java.lang.reflect.Method;

public class HookWrapper {
    
    public static class HookEntity {
        public Method target;
        public Method hook;
        public Method backup;
        public boolean initClass;

        public HookEntity(Method target, Method hook, Method backup) {
            this.target = target;
            this.hook = hook;
            this.backup = backup;
        }
    }
}