// JavaHookExample.java
// Example: Hooking a Java method using SandHook from Java

package com.swift.sandhook.examples;

import android.util.Log;
import com.swift.sandhook.SandHook;
import java.lang.reflect.Method;

public class JavaHookExample {

    public static void initHooks() {
        // 1. Initialize the SandHook native engine
        SandHook.init();

        try {
            // 2. Find the target method to hook (e.g., Log.println)
            Method originMethod = Log.class.getMethod("println", int.class, String.class, String.class);
            
            // 3. Define the hook replacement method (must have same signature)
            Method hookMethod = JavaHookExample.class.getMethod("hooked_println", int.class, String.class, String.class);
            
            // 4. Define the backup method (to call original)
            Method backupMethod = JavaHookExample.class.getMethod("backup_println", int.class, String.class, String.class);

            // 5. Install the hook
            boolean success = SandHook.hook(originMethod, hookMethod, backupMethod);
            
            if (success) {
                Log.i("SandHook", "Log.println hooked successfully!");
            } else {
                Log.e("SandHook", "Failed to hook Log.println");
            }

        } catch (NoSuchMethodException e) {
            Log.e("SandHook", "Method not found", e);
        }
    }

    // --- Hook Methods ---
    
    public static int hooked_println(int priority, String tag, String msg) {
        // We can modify the message before calling the original
        String newMsg = "[HOOKED] " + msg;
        return backup_println(priority, tag, newMsg);
    }

    public static int backup_println(int priority, String tag, String msg) {
        // This method's body will be replaced by the trampoline call
        return 0;
    }
}