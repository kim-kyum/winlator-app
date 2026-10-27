package com.winlator.core;

import android.content.Context;
import android.content.SharedPreferences;
import android.opengl.EGL14;

import androidx.collection.ArrayMap;
import androidx.preference.PreferenceManager;

import java.util.Locale;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.opengles.GL10;

import dalvik.annotation.optimization.CriticalNative;

public abstract class GPUHelper {
    public enum VkPresentMode {
        IMMEDIATE, MAILBOX, FIFO, FIFO_RELAXED;

        public String value() {
            return name().toLowerCase(Locale.ENGLISH);
        }
    }
    public static int VK_API_VERSION_1_3 = GPUHelper.vkMakeVersion(1, 3, 0);

    static {
        System.loadLibrary("winlator");
    }

    private static ArrayMap<String, String> loadGPUInformation(Context context) {
        final ArrayMap<String, String> gpuInfo = new ArrayMap<>();
        gpuInfo.put("renderer", "");
        gpuInfo.put("vendor", "");
        gpuInfo.put("version", "");

        // Winlator S11: asked on a thread of its own (a current GL context must not stay on the
        // caller's), joined rather than wait/notify (the notify could come before the wait), and
        // only through a context that exists: without one glGetString crashes the app
        Thread thread = new Thread(() -> {
            int[] attribList = new int[] {
                EGL10.EGL_SURFACE_TYPE, EGL10.EGL_PBUFFER_BIT,
                EGL10.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                EGL10.EGL_RED_SIZE, 8,
                EGL10.EGL_GREEN_SIZE, 8,
                EGL10.EGL_BLUE_SIZE, 8,
                EGL10.EGL_ALPHA_SIZE, 0,
                EGL10.EGL_NONE
            };
            EGLConfig[] configs = new EGLConfig[1];
            int[] configCounts = new int[1];

            EGL10 egl = (EGL10)EGLContext.getEGL();
            EGLDisplay eglDisplay = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
            if (eglDisplay == EGL10.EGL_NO_DISPLAY) return;

            int[] version = new int[2];
            if (!egl.eglInitialize(eglDisplay, version)) return;
            if (!egl.eglChooseConfig(eglDisplay, attribList, configs, 1, configCounts) || configCounts[0] < 1) return;

            attribList = new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL10.EGL_NONE};
            EGLContext eglContext = egl.eglCreateContext(eglDisplay, configs[0], EGL10.EGL_NO_CONTEXT, attribList);
            if (eglContext == null || eglContext == EGL10.EGL_NO_CONTEXT) return;

            // a context without a surface (EGL_KHR_surfaceless_context); else a 1x1 pbuffer
            EGLSurface surface = EGL10.EGL_NO_SURFACE;
            boolean current = egl.eglMakeCurrent(eglDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, eglContext);
            if (!current) {
                surface = egl.eglCreatePbufferSurface(eglDisplay, configs[0], new int[]{EGL10.EGL_WIDTH, 1, EGL10.EGL_HEIGHT, 1, EGL10.EGL_NONE});
                current = surface != null && surface != EGL10.EGL_NO_SURFACE && egl.eglMakeCurrent(eglDisplay, surface, surface, eglContext);
            }
            if (!current) {
                egl.eglDestroyContext(eglDisplay, eglContext);
                return;
            }

            // the context is an OpenGL ES 2 one: ask through GLES 2 (GL10's ES 1 entry points
            // crashed in the emulator)
            String gpuRenderer = Objects.toString(android.opengl.GLES20.glGetString(android.opengl.GLES20.GL_RENDERER), "");
            String gpuVendor = Objects.toString(android.opengl.GLES20.glGetString(android.opengl.GLES20.GL_VENDOR), "");
            String gpuVersion = Objects.toString(android.opengl.GLES20.glGetString(android.opengl.GLES20.GL_VERSION), "");

            egl.eglMakeCurrent(eglDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
            if (surface != EGL10.EGL_NO_SURFACE) egl.eglDestroySurface(eglDisplay, surface);
            egl.eglDestroyContext(eglDisplay, eglContext);

            synchronized (gpuInfo) {
                gpuInfo.put("renderer", gpuRenderer);
                gpuInfo.put("vendor", gpuVendor);
                gpuInfo.put("version", gpuVersion);
            }

            final SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
            preferences.edit()
                .putString("gpu_renderer", gpuRenderer)
                .putString("gpu_vendor", gpuVendor)
                .putString("gpu_version", gpuVersion)
                .apply();
        });
        thread.start();
        try {
            thread.join(5000);
        }
        catch (InterruptedException e) {}
        synchronized (gpuInfo) {
            return new ArrayMap<>(gpuInfo);
        }
    }

    public static String glGetRenderer(Context context) {
        final SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        String value = preferences.getString("gpu_renderer", "");
        if (!value.isEmpty()) return value;

        ArrayMap<String, String> gpuInfo = loadGPUInformation(context);
        return gpuInfo.get("renderer");
    }

    public static String glGetVendor(Context context) {
        final SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        String value = preferences.getString("gpu_vendor", "");
        if (!value.isEmpty()) return value;

        ArrayMap<String, String> gpuInfo = loadGPUInformation(context);
        return gpuInfo.get("vendor");
    }

    public static String glGetVersion(Context context) {
        final SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        String value = preferences.getString("gpu_version", "");
        if (!value.isEmpty()) return value;

        ArrayMap<String, String> gpuInfo = loadGPUInformation(context);
        return gpuInfo.get("version");
    }

    public static short getAdrenoModelId(Context context) {
        Matcher matcher = Pattern.compile("adreno[^678]*([678][0-9]{2})", Pattern.CASE_INSENSITIVE).matcher(glGetRenderer(context));
        return (short)(matcher.find() ? Integer.parseInt(matcher.group(1)) : 0);
    }

    public static int vkMakeVersion(String value) {
        final Pattern pattern = Pattern.compile("([0-9]+)\\.([0-9]+)\\.?([0-9]+)?");
        Matcher matcher = pattern.matcher(value);
        if (matcher.find()) {
            try {
                int major = matcher.group(1) != null ? Integer.parseInt(matcher.group(1)) : 0;
                int minor = matcher.group(2) != null ? Integer.parseInt(matcher.group(2)) : 0;
                int patch = matcher.group(3) != null ? Integer.parseInt(matcher.group(3)) : 0;
                if (matcher.group(1) == null && patch == 0) patch = minor;
                return vkMakeVersion(major, minor, patch);
            }
            catch (NumberFormatException e) {
                return 0;
            }
        }
        else return 0;
    }

    public static int vkMakeVersion(int major, int minor, int patch) {
        return  ((major) << 22) | ((minor) << 12) | (patch);
    }

    public static int vkVersionMajor(int version) {
        return (version) >> 22;
    }

    public static int vkVersionMinor(int version) {
        return ((version) >> 12) & 0x3FF;
    }

    public static int vkVersionPatch(int version) {
        return (version) & 0xFFF;
    }

    public static native String[] vkGetDeviceExtensions();

    @CriticalNative
    public static native int vkGetApiVersion();

    public static native void setGlobalEGLContext();

    public static native long createOffscreenEGLContext(boolean sharedContext);

    public static native void destroyOffscreenEGLContext(long contextPtr);
}
