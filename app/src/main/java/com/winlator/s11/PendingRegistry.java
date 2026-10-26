package com.winlator.s11;

import com.winlator.container.Container;
import com.winlator.core.WineRegistryEditor;
import com.winlator.core.WineUtils;
import com.winlator.win32.WinVersions;

import java.io.File;

/**
 * Registry settings changed in the container settings while the container is running.
 *
 * The system font, the dpi, mouse warp and the Windows version live in the container's
 * registry files (user.reg, system.reg). While Wine runs, wineserver keeps the registry in
 * memory and writes it over those files every 30 seconds and when it exits, so a value
 * written into the files from Android would be lost. The container settings opened from a
 * running session (S11ContainerSettingsActivity) keep these values in the container data
 * instead, and they are written before Wine starts the next time
 * (XServerDisplayActivity.setupWineSystemFiles), like the other container settings.
 * The settings page shows them in the meantime.
 */
public final class PendingRegistry {
    public static final String SYSTEM_FONT = "s11PendingSystemFont";
    public static final String LOG_PIXELS = "s11PendingLogPixels";
    public static final String MOUSE_WARP_OVERRIDE = "s11PendingMouseWarpOverride";
    /** The version id from WinVersions ("win10"), not its position in the list */
    public static final String WIN_VERSION = "s11PendingWinVersion";
    private static final String[] KEYS = {SYSTEM_FONT, LOG_PIXELS, MOUSE_WARP_OVERRIDE, WIN_VERSION};

    private PendingRegistry() {}

    /** The value waiting for the next start, or the fallback when there is none. */
    public static String get(Container container, String key, String fallback) {
        String value = container != null ? container.getExtra(key) : "";
        return value.isEmpty() ? fallback : value;
    }

    public static Integer getLogPixels(Container container, Integer fallback) {
        String value = get(container, LOG_PIXELS, null);
        return value != null && value.matches("[0-9]{2,3}") ? Integer.valueOf(value) : fallback;
    }

    /** Position of the waiting Windows version in WinVersions.getWinVersions(), or -1. */
    public static int getWinVersionIndex(Container container) {
        String version = get(container, WIN_VERSION, null);
        if (version == null) return -1;
        WinVersions.WinVersion[] winVersions = WinVersions.getWinVersions();
        for (int i = 0; i < winVersions.length; i++) {
            if (winVersions[i].version.equals(version)) return i;
        }
        return -1;
    }

    /**
     * Writes the waiting values into the registry files and forgets them. Only while Wine is
     * not running. Returns true when the container data changed (the caller saves it).
     */
    public static boolean applyIfNeeded(Container container) {
        String systemFont = get(container, SYSTEM_FONT, null);
        Integer logPixels = getLogPixels(container, null);
        String mouseWarpOverride = get(container, MOUSE_WARP_OVERRIDE, null);
        int winVersionIndex = getWinVersionIndex(container);

        boolean pending = false;
        for (String key : KEYS) {
            if (!container.getExtra(key).isEmpty()) pending = true;
        }
        if (!pending) return false;

        File userRegFile = new File(container.getRootDir(), ".wine/user.reg");
        if ((systemFont != null || logPixels != null || mouseWarpOverride != null) && userRegFile.isFile()) {
            try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
                if (systemFont != null) WineUtils.setSystemFont(registryEditor, systemFont);
                if (logPixels != null) registryEditor.setDwordValue("Control Panel\\Desktop", "LogPixels", logPixels);
                if (mouseWarpOverride != null) registryEditor.setStringValue("Software\\Wine\\DirectInput", "MouseWarpOverride", mouseWarpOverride);
                // written with every save from the container list as well
                registryEditor.setStringValue("Software\\Wine\\Direct3D", "shader_backend", "glsl");
                registryEditor.setStringValue("Software\\Wine\\Direct3D", "UseGLSL", "enabled");
            }
        }

        File systemRegFile = new File(container.getRootDir(), ".wine/system.reg");
        if (winVersionIndex != -1 && systemRegFile.isFile()) WineUtils.setWinVersion(container, winVersionIndex);

        for (String key : KEYS) container.putExtra(key, null);
        return true;
    }
}
