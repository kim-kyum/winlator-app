package com.winlator.s11;

import com.winlator.container.Container;
import com.winlator.core.FileUtils;
import com.winlator.core.WineRegistryEditor;
import com.winlator.core.WineUtils;
import com.winlator.win32.MSLogFont;
import com.winlator.xenvironment.RootFS;

import java.io.File;

/**
 * The Winlator S11 desktop look for a container.
 *
 * - The "Winlator S11" visual style: Wine 11's Aero theme (flat buttons, thin borders,
 *   crisp bitmaps for every DPI) with our colours, built by tools/s11/theme and shipped in
 *   the rootfs overlay. Wine 10's uxtheme loads it fine. It is used with the light desktop
 *   theme; the dark one stays classic because the visual style is light only.
 * - Windows 10 proportions for title bars (tall caption, wide caption buttons) and thin
 *   window frames. The caption buttons themselves are flat glyphs, see CaptionButtons.
 * - Segoe UI as the UI font, served by S11 Sans (our build of Microsoft's Selawik, which
 *   has Segoe UI's metrics), with dialogs ("MS Shell Dlg") following it.
 * - 1920x1200 at 144 dpi instead of 1280x800 at 96 dpi for containers that still have
 *   the old defaults: on the 2560x1600 tablet panel that is sharper and the same size.
 */
public final class ModernLook {
    private static final String VERSION = "1";
    public static final String EXTRA_KEY = "s11ModernLook";
    private static final String DISPLAY_EXTRA_KEY = "s11Display";

    public static final String DEFAULT_SCREEN_SIZE = "1920x1200";
    public static final int DEFAULT_DPI = 144;
    private static final String OLD_SCREEN_SIZE = "1280x800";
    private static final int OLD_DPI = 96;

    public static final String UI_FONT = "Segoe UI";
    private static final String OLD_UI_FONT = "Tahoma";

    private static final String THEME_IN_ROOTFS = "usr/share/s11/themes/s11.msstyles";
    private static final String THEME_IN_PREFIX = "drive_c/windows/resources/themes/s11/s11.msstyles";
    private static final String THEME_DLL = "C:\\windows\\resources\\themes\\s11\\s11.msstyles";

    private static final String THEME_KEY = "Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager";
    private static final String METRICS_KEY = "Control Panel\\Desktop\\WindowMetrics";
    private static final String DESKTOP_KEY = "Control Panel\\Desktop";
    private static final String REPLACEMENTS_KEY = "Software\\Wine\\Fonts\\Replacements";
    private static final String SUBSTITUTES_KEY = "Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes";

    // Segoe UI families that programs ask for by name -> the S11 Sans face that stands in
    private static final String[][] FONT_REPLACEMENTS = {
        {"Segoe UI", "S11 Sans"},
        {"Segoe UI Semibold", "S11 Sans"},
        {"Segoe UI Light", "S11 Sans Light"},
        {"Segoe UI Semilight", "S11 Sans Light"}
    };

    private ModernLook() {}

    /**
     * Visual style and window metrics, written together with the colours whenever the
     * desktop theme is applied (WineThemeManager.apply).
     */
    public static void applyTheme(File rootDir, WineRegistryEditor userRegistry, boolean light) {
        boolean themed = light && installTheme(rootDir);
        if (themed) {
            userRegistry.setStringValue(THEME_KEY, "ThemeActive", "1");
            userRegistry.setStringValue(THEME_KEY, "DllName", THEME_DLL);
            userRegistry.setStringValue(THEME_KEY, "ColorName", "Blue");
            userRegistry.setStringValue(THEME_KEY, "SizeName", "NormalSize");
            // uxtheme applies a theme's own colours the first time it loads it; this marks
            // that as done, so it keeps the colours WineThemeManager writes.
            userRegistry.setStringValue(THEME_KEY, "LoadedBefore", "1");
        }
        else userRegistry.setStringValue(THEME_KEY, "ThemeActive", "0");

        // Sizes at 96 dpi; Wine scales them with LogPixels
        setPixels(userRegistry, "CaptionHeight", 28);
        setPixels(userRegistry, "CaptionWidth", 46);
        setPixels(userRegistry, "SmCaptionHeight", 22);
        setPixels(userRegistry, "SmCaptionWidth", 22);
        setPixels(userRegistry, "MenuHeight", 19);
        setPixels(userRegistry, "MenuWidth", 19);
        setPixels(userRegistry, "BorderWidth", 1);
        setPixels(userRegistry, "PaddedBorderWidth", 2);
        setPixels(userRegistry, "ScrollWidth", 17);
        setPixels(userRegistry, "ScrollHeight", 17);
    }

    /** Fonts, once per container. Returns true when the container data changed. */
    public static boolean applyIfNeeded(Container container, File rootDir) {
        if (VERSION.equals(container.getExtra(EXTRA_KEY))) return false;

        File userRegFile = new File(rootDir, RootFS.WINEPREFIX + "/user.reg");
        File systemRegFile = new File(rootDir, RootFS.WINEPREFIX + "/system.reg");
        if (!userRegFile.isFile() || !systemRegFile.isFile()) return false;

        try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
            for (String[] replacement : FONT_REPLACEMENTS) {
                registryEditor.setStringValue(REPLACEMENTS_KEY, replacement[0], replacement[1]);
            }

            // Move containers that still use the old default UI font to Segoe UI; keep a
            // font the user picked.
            byte[] captionFont = registryEditor.getHexValues(METRICS_KEY, "CaptionFont");
            String face = captionFont != null ? new MSLogFont().fromByteArray(captionFont).getFaceName() : null;
            if (face == null || face.isEmpty() || face.equalsIgnoreCase(OLD_UI_FONT)) {
                WineUtils.setSystemFont(registryEditor, UI_FONT);
            }
        }

        try (WineRegistryEditor registryEditor = new WineRegistryEditor(systemRegFile)) {
            // Dialog templates ask for "MS Shell Dlg"; Wine maps it to Tahoma
            registryEditor.setStringValue(SUBSTITUTES_KEY, "MS Shell Dlg", UI_FONT);
            registryEditor.setStringValue(SUBSTITUTES_KEY, "MS Shell Dlg 2", UI_FONT);
        }

        container.putExtra(EXTRA_KEY, VERSION);
        return true;
    }

    /**
     * Containers made with the old defaults (1280x800, 96 dpi) move to 1920x1200 at 144 dpi,
     * once. Call before the X server is created from the container's screen size.
     */
    public static void migrateDisplay(Container container) {
        if ("1".equals(container.getExtra(DISPLAY_EXTRA_KEY))) return;

        if (OLD_SCREEN_SIZE.equals(container.getScreenSize())) {
            File userRegFile = new File(container.getRootDir(), ".wine/user.reg");
            if (userRegFile.isFile()) {
                try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
                    Integer dpi = registryEditor.getDwordValue(DESKTOP_KEY, "LogPixels", OLD_DPI);
                    if (dpi != null && dpi == OLD_DPI) {
                        registryEditor.setDwordValue(DESKTOP_KEY, "LogPixels", DEFAULT_DPI);
                        container.setScreenSize(DEFAULT_SCREEN_SIZE);
                    }
                }
            }
        }

        container.putExtra(DISPLAY_EXTRA_KEY, "1");
        container.saveData();
    }

    /** Links the visual style from the rootfs into the prefix. False when it is missing. */
    private static boolean installTheme(File rootDir) {
        File source = new File(rootDir, THEME_IN_ROOTFS);
        if (!source.isFile()) return false;

        File target = new File(rootDir, RootFS.WINEPREFIX + "/" + THEME_IN_PREFIX);
        if (FileUtils.isSymlink(target)) {
            if (FileUtils.readSymlink(target).equals(source.getAbsolutePath()) && target.isFile()) return true;
        }
        else if (target.isFile() && target.length() == source.length()) return true;

        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory()) parent.mkdirs();
        target.delete();
        FileUtils.symlink(source, target);
        if (!target.isFile()) FileUtils.copy(source, target);
        return target.isFile();
    }

    private static void setPixels(WineRegistryEditor registryEditor, String name, int pixels) {
        // Negative values are twips (1/1440 inch), which Wine converts with the current dpi
        registryEditor.setStringValue(METRICS_KEY, name, String.valueOf(-15 * pixels));
    }
}
