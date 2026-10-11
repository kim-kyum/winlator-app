package com.winlator.s11;

import android.content.SharedPreferences;

import com.winlator.container.Container;
import com.winlator.core.WineRegistryEditor;
import com.winlator.xenvironment.RootFS;

import java.io.File;

/**
 * The Winlator S11 taskbar and start menu (tools/s11/shell, s11shell.exe in the rootfs
 * overlay). It replaces explorer.exe's Windows 95 taskbar on the desktop session, and
 * winhandler.exe starts s11shell.exe once it is up. Shortcuts that start a program straight
 * away use the "nogui" desktop and get neither.
 *
 * The desktop is called "s11" rather than Winlator's "shell": Winlator's Wine turns
 * explorer's taskbar on for a desktop named "shell" whatever the EnableShell setting says.
 * On "s11" explorer keeps it off, and the stand-alone notification area off too, so tray
 * icons come to our taskbar.
 */
public final class S11Shell {
    public static final String PREF_KEY = "s11_shell";
    public static final String DESKTOP_NAME = "s11";
    private static final String EXTRA_KEY = "s11Shell";
    // what configure() wrote; a new value when the registry setup changes
    private static final String ENABLED_VERSION = "2";
    private static final String EXE_IN_ROOTFS = "usr/share/s11/shell/s11shell.exe";
    public static final String EXE_DOS_PATH = "Z:\\usr\\share\\s11\\shell\\s11shell.exe";
    private static final String DESKTOPS_KEY = "Software\\Wine\\Explorer\\Desktops\\";

    private S11Shell() {}

    public static boolean isEnabled(SharedPreferences preferences, File rootDir) {
        return preferences.getBoolean(PREF_KEY, true) && new File(rootDir, EXE_IN_ROOTFS).isFile();
    }

    /** Explorer's taskbar and tray off while ours is used. Returns true when the container data changed. */
    public static boolean configure(Container container, File rootDir, boolean enabled) {
        String wanted = enabled ? ENABLED_VERSION : "0";
        if (wanted.equals(container.getExtra(EXTRA_KEY))) return false;

        File userRegFile = new File(rootDir, RootFS.WINEPREFIX + "/user.reg");
        if (!userRegFile.isFile()) return false;
        try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
            registryEditor.setDwordValue(DESKTOPS_KEY + DESKTOP_NAME, "EnableShell", 0);
            registryEditor.setDwordValue(DESKTOPS_KEY + DESKTOP_NAME, "ShowSystray", 0);
            // Wine's own taskbar again on "shell" when ours is switched off
            registryEditor.setDwordValue(DESKTOPS_KEY + "shell", "EnableShell", enabled ? 0 : 1);
            registryEditor.setDwordValue(DESKTOPS_KEY + "shell", "ShowSystray", enabled ? 0 : 1);
        }
        container.putExtra(EXTRA_KEY, wanted);
        return true;
    }
}
