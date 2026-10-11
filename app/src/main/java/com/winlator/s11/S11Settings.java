package com.winlator.s11;

import android.content.Context;
import android.content.res.Configuration;

import com.winlator.container.Container;
import com.winlator.core.WineThemeManager;

import java.io.File;

/**
 * The Winlator S11 Settings app (tools/s11/shell/s11settings.c, s11settings.exe in the rootfs
 * overlay). Settings that Wine keeps in the registry it writes itself; the ones Winlator keeps
 * for the container (screen size, light or dark mode, background) it sends to winhandler's
 * UDP port as [code][ASCII value][NUL], and they are stored here. Winlator applies them the
 * next time the container starts, as if they had been changed in the container settings.
 */
public final class S11Settings {
    // The same codes as com.winlator.winhandler.RequestCodes
    public static final byte SET_SCREEN = 102;
    public static final byte SET_THEME = 103;
    public static final byte SET_WALLPAPER = 104;
    private static final String THEME_MODE_EXTRA = "s11ThemeMode";

    private S11Settings() {}

    /** Returns true when the container's data changed. */
    public static boolean apply(Context context, Container container, byte code, String value) {
        switch (code) {
            case SET_SCREEN:
                if (!value.matches("[0-9]{3,4}x[0-9]{3,4}")) return false;
                String[] size = value.split("x");
                int width = Integer.parseInt(size[0]), height = Integer.parseInt(size[1]);
                if (width < 640 || height < 480 || width % 2 != 0 || height % 2 != 0) return false;
                if (value.equals(container.getScreenSize())) return false;
                container.setScreenSize(value);
                return true;
            case SET_THEME:
                if (!value.equals("LIGHT") && !value.equals("DARK") && !value.equals("SYSTEM")) return false;
                String oldMode = container.getExtra(THEME_MODE_EXTRA);
                container.putExtra(THEME_MODE_EXTRA, value);
                String resolved = value.equals("SYSTEM") ? systemTheme(context) : value;
                return setDesktopTheme(container, resolved, null) || !value.equals(oldMode);
            case SET_WALLPAPER: {
                String wallpaper;
                if (value.equals(WineThemeManager.DEFAULT_WALLPAPER_ID)) wallpaper = "0";
                else if (value.matches("wallpaper-[a-z0-9-]+")) wallpaper = value;
                else if (value.equals("user")) {
                    // the app saved the picture where the container settings keep yours
                    File file = WineThemeManager.getUserWallpaperFile(context);
                    if (!file.isFile()) return false;
                    wallpaper = String.valueOf(file.lastModified());
                }
                else return false;
                return setDesktopTheme(container, null, wallpaper);
            }
            default:
                return false;
        }
    }

    /** Re-resolve follow-system appearance whenever this container starts. */
    public static boolean applySystemThemeIfNeeded(Context context, Container container) {
        if (!"SYSTEM".equals(container.getExtra(THEME_MODE_EXTRA))) return false;
        return setDesktopTheme(container, systemTheme(context), null);
    }

    private static String systemTheme(Context context) {
        int mode = context.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK;
        return mode == Configuration.UI_MODE_NIGHT_YES ? "DARK" : "LIGHT";
    }

    /** desktopTheme is "THEME,TYPE,#COLOR[,WALLPAPER]" (see ContainerDetailFragment.getDesktopTheme) */
    private static boolean setDesktopTheme(Container container, String theme, String wallpaper) {
        String current = container.getDesktopTheme();
        String[] values = current.split(",");
        String oldTheme = values[0];
        String type = values.length >= 3 ? values[1] : WineThemeManager.BackgroundType.IMAGE.name();
        String color = values.length >= 3 ? values[2] : (values.length == 2 ? values[1] : "#0277bd");
        String oldWallpaper = values.length >= 4 ? values[3] : "0";

        if (wallpaper != null) type = WineThemeManager.BackgroundType.IMAGE.name();
        String result = (theme != null ? theme : oldTheme)+","+type+","+color;
        if (type.equals(WineThemeManager.BackgroundType.IMAGE.name())) result += ","+(wallpaper != null ? wallpaper : oldWallpaper);
        if (result.equals(current)) return false;
        container.setDesktopTheme(result);
        return true;
    }
}
