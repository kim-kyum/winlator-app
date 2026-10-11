package com.winlator.s11;

import android.app.Activity;

import java.lang.reflect.Method;

/**
 * Lets the Windows (Meta) key of a hardware keyboard reach the container on Samsung devices.
 *
 * One UI keeps the Meta key for itself (search, app drawer, its own shortcuts), so the
 * activity never sees it. Samsung's SemWindowManager.requestMetaKeyEvent hands it to an
 * activity that asks; remote desktop apps use it the same way. Elsewhere this does nothing.
 */
public final class MetaKeyCapture {
    private static Object manager;
    private static Method requestMetaKeyEvent;
    private static boolean looked;

    private MetaKeyCapture() {}

    /** Call when the activity gains (true) or loses (false) the window focus. */
    public static void request(Activity activity, boolean capture) {
        if (!looked) {
            looked = true;
            try {
                Class<?> cls = Class.forName("com.samsung.android.view.SemWindowManager");
                manager = cls.getMethod("getInstance").invoke(null);
                requestMetaKeyEvent = cls.getDeclaredMethod("requestMetaKeyEvent", android.content.ComponentName.class, boolean.class);
            }
            catch (Throwable e) {
                manager = null;
                requestMetaKeyEvent = null;
            }
        }
        if (manager == null || requestMetaKeyEvent == null) return;
        try {
            requestMetaKeyEvent.invoke(manager, activity.getComponentName(), capture);
        }
        catch (Throwable e) {}
    }
}
