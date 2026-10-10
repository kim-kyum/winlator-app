package com.winlator.s11;

import com.winlator.container.Container;
import com.winlator.core.FileUtils;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;

/**
 * Removes Wine's Internet Explorer stub from a container.
 *
 * The APK no longer ships it (tools/s11/patch_assets.py strips iexplore.exe from the rootfs
 * and the IE folders from the container pattern). This cleans what is left: folders in
 * containers made by earlier builds, and the registry entries Wine writes for IE, which a
 * wineboot update would write back. Commands that start iexplore.exe go, icons that pointed
 * at it switch to Wine's globe icon, and web links keep opening in the Android browser
 * through winebrowser and winhandler.exe.
 */
public final class LegacyBrowserCleanup {
    private static final String VERSION = "1";
    public static final String EXTRA_KEY = "s11LegacyBrowserCleanup";

    private static final String[] FOLDERS = {
        ".wine/drive_c/Program Files/Internet Explorer",
        ".wine/drive_c/Program Files (x86)/Internet Explorer"
    };

    // Each key is removed together with everything below it.
    private static final String[] REMOVED_KEYS = {
        "Software\\Classes\\Applications\\iexplore.exe",
        "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\iexplore.exe",
        "Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\App Paths\\iexplore.exe",
        "Software\\Classes\\InternetExplorer.Application",
        "Software\\Classes\\InternetExplorer.Application.1",
        "Software\\Classes\\CLSID\\{0002DF01-0000-0000-C000-000000000046}",
        "Software\\Classes\\Wow6432Node\\CLSID\\{0002DF01-0000-0000-C000-000000000046}",
        "Software\\Classes\\CLSID\\{DF4FCC34-067A-4E0A-8352-4A1A5095346E}",
        "Software\\Classes\\Wow6432Node\\CLSID\\{DF4FCC34-067A-4E0A-8352-4A1A5095346E}",
        "Software\\Classes\\CLSID\\{871C5380-42A0-1069-A2EA-08002B30309D}\\Shell\\OpenHomePage",
        "Software\\Classes\\Wow6432Node\\CLSID\\{871C5380-42A0-1069-A2EA-08002B30309D}\\Shell\\OpenHomePage",
        // Wine opens these image types with "iexplore.exe -nohome"
        "Software\\Classes\\giffile\\shell\\open",
        "Software\\Classes\\jpegfile\\shell\\open",
        "Software\\Classes\\pjpegfile\\shell\\open",
        "Software\\Classes\\pngfile\\shell\\open",
        "Software\\Classes\\xbmfile\\shell\\open"
    };

    // How icon values referring to iexplore.exe look in a .reg file ("...\\iexplore.exe,1").
    private static final String IE_ICON = "internet explorer\\\\iexplore.exe,";
    // Wine's globe icon (IDI_SHELL_ENTIRE_NETWORK, resources/internet.ico in shell32).
    private static final String GLOBE_ICON = "\"C:\\\\windows\\\\system32\\\\shell32.dll,-14\"";

    private LegacyBrowserCleanup() {}

    /**
     * Runs once per container, and again when the IE folders reappear or the prefix was
     * updated. Call before Wine starts. Returns true when the container data needs saving.
     */
    public static boolean applyIfNeeded(Container container, boolean prefixUpdated) {
        File rootDir = container.getRootDir();
        boolean foldersPresent = false;
        for (String folder : FOLDERS) {
            if (new File(rootDir, folder).exists()) foldersPresent = true;
        }
        if (!prefixUpdated && !foldersPresent && VERSION.equals(container.getExtra(EXTRA_KEY))) return false;

        for (String folder : FOLDERS) FileUtils.delete(new File(rootDir, folder));

        File systemRegFile = new File(rootDir, ".wine/system.reg");
        if (systemRegFile.isFile() && !cleanRegistry(systemRegFile)) return false;

        container.putExtra(EXTRA_KEY, VERSION);
        return true;
    }

    /**
     * One pass over a Wine registry file: drops the sections under REMOVED_KEYS and points
     * IE icon values at the globe icon. Bytes are copied as ISO-8859-1, so text the pass does
     * not touch is written back exactly as it was read.
     */
    static boolean cleanRegistry(File regFile) {
        String[] prefixes = new String[REMOVED_KEYS.length];
        for (int i = 0; i < REMOVED_KEYS.length; i++) {
            // Key names are stored with doubled backslashes.
            prefixes[i] = REMOVED_KEYS[i].replace("\\", "\\\\");
        }

        File tempFile = new File(regFile.getPath() + ".s11tmp");
        boolean changed = false;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(new FileInputStream(regFile), StandardCharsets.ISO_8859_1));
             BufferedWriter writer = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(tempFile), StandardCharsets.ISO_8859_1))) {
            boolean skipping = false;
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.startsWith("[")) {
                    int end = line.lastIndexOf(']');
                    skipping = end > 1 && isRemoved(line.substring(1, end), prefixes);
                    if (skipping) changed = true;
                }
                if (skipping) continue;

                if ((line.startsWith("@=") || line.startsWith("\"")) && line.toLowerCase().contains(IE_ICON)) {
                    int eq = line.indexOf('=');
                    String value = line.substring(eq + 1);
                    line = line.substring(0, eq + 1) + (value.startsWith("str(2):") ? "str(2):" : "") + GLOBE_ICON;
                    changed = true;
                }
                writer.write(line);
                writer.write('\n');
            }
        }
        catch (IOException e) {
            tempFile.delete();
            return false;
        }

        if (!changed) {
            tempFile.delete();
            return true;
        }
        return tempFile.renameTo(regFile);
    }

    private static boolean isRemoved(String key, String[] prefixes) {
        for (String prefix : prefixes) {
            if (key.equalsIgnoreCase(prefix)) return true;
            if (key.length() > prefix.length() + 1
                && key.regionMatches(true, 0, prefix, 0, prefix.length())
                && key.startsWith("\\\\", prefix.length())) return true;
        }
        return false;
    }
}
