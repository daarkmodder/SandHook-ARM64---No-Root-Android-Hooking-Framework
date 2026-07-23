package com.swift.sandhook;

import android.util.Log;

public class HookLog {
    private static final String TAG = "SandHook-Java";

    public static void e(String msg, Throwable t) {
        Log.e(TAG, msg, t);
    }

    public static void d(String msg) {
        Log.d(TAG, msg);
    }
}