package com.winlator.s11;

import android.content.SharedPreferences;

import com.winlator.container.Container;
import com.winlator.core.WineRegistryEditor;
import com.winlator.xenvironment.RootFS;

import java.io.File;

/**
 * The Winlator S11 taskbar and start menu (tools/s11/shell, s11shell.exe in the rootfs
 * overlay). It replaces explorer.exe's Windows 95 taskbar on the desktop session: Wine's
 * taskbar and its stand-alone notification area are switched off for the "shell" desktop,
 * and winhandler.exe starts s11shell.exe once it is up. Shortcuts that start a program
 * straight away use the "nogui" desktop and get neither.
 */
public final class S11Shell {
    public static final String PREF_KEY = "s11_shell";
    private static final String EXTRA_KEY = "s11Shell";
    private static final String EXE_IN_ROOTFS = "usr/share/s11/shell/s11shell.exe";
    public static final String EXE_DOS_PATH = "Z:\\usr\\share\\s11\\shell\\s11shell.exe";
    private static final String DESKTOP_KEY = "Software\\Wine\\Explorer\\Desktops\\shell";

    private S11Shell() {}

    public static boolean isEnabled(SharedPreferences preferences, File rootDir) {
        return preferences.getBoolean(PREF_KEY, true) && new File(rootDir, EXE_IN_ROOTFS).isFile();
    }

    /** Turns explorer's taskbar off while ours is used. Returns true when the container data changed. */
    public static boolean configure(Container container, File rootDir, boolean enabled) {
        String wanted = enabled ? "1" : "0";
        if (wanted.equals(container.getExtra(EXTRA_KEY))) return false;

        File userRegFile = new File(rootDir, RootFS.WINEPREFIX + "/user.reg");
        if (!userRegFile.isFile()) return false;
        try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
            registryEditor.setDwordValue(DESKTOP_KEY, "EnableShell", enabled ? 0 : 1);
            registryEditor.setDwordValue(DESKTOP_KEY, "ShowSystray", enabled ? 0 : 1);
        }
        container.putExtra(EXTRA_KEY, wanted);
        return true;
    }
}
