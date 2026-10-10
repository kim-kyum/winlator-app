package com.winlator.s11;

import com.winlator.container.Container;
import com.winlator.core.WineRegistryEditor;
import com.winlator.xenvironment.RootFS;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;

/**
 * Points the Korean Windows font names at the bundled NanumGothic, and links NanumGothic
 * behind the common Latin fonts so Hangul in their text shows up.
 *
 * Korean programs ask for Gulim, Dotum, Batang or Malgun Gothic by name. None of them exist
 * in a fresh prefix, so Hangul showed up as boxes. Wine's [HKCU\Software\Wine\Fonts\Replacements]
 * maps a missing family to an installed one and is skipped when the real font exists,
 * so fonts the user installs later still win.
 *
 * Text in a Latin font (the UI font, Arial, ...) gets Hangul through font linking:
 * [HKLM\...\FontLink\SystemLink] lists fallback fonts by file name, and Wine's defaults
 * name Windows files (GULIM.TTC, MALGUN.TTF) that are not there. NanumGothic goes first.
 */
public final class KoreanFonts {
    private static final String VERSION = "2";
    private static final String EXTRA_KEY = "s11KoreanFonts";
    private static final String FAMILY = "NanumGothic";
    private static final String LINK = "NanumGothic-Regular.ttf,NanumGothic";
    private static final String REPLACEMENTS_KEY = "Software\\Wine\\Fonts\\Replacements";
    private static final String SYSTEM_LINK_KEY = "Software\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink";
    private static final String[] ALIASES = {
        "Gulim", "GulimChe", "Dotum", "DotumChe", "Batang", "BatangChe", "Gungsuh", "GungsuhChe",
        "Malgun Gothic", "굴림", "굴림체", "돋움", "돋움체", "바탕", "바탕체", "궁서", "궁서체", "맑은 고딕"
    };
    // Fonts whose text should fall back to NanumGothic for Hangul
    private static final String[] LINKED_FONTS = {
        "Segoe UI", "S11 Sans", "Tahoma", "Microsoft Sans Serif", "Lucida Sans Unicode", "Arial", "Verdana",
        "Times New Roman", "Courier New", "Trebuchet MS", "Georgia", "Comic Sans MS", "Calibri"
    };

    private KoreanFonts() {}

    /** Returns true when the container data changed and needs saving. */
    public static boolean applyIfNeeded(Container container, File rootDir) {
        if (VERSION.equals(container.getExtra(EXTRA_KEY))) return false;

        File userRegFile = new File(rootDir, RootFS.WINEPREFIX + "/user.reg");
        File systemRegFile = new File(rootDir, RootFS.WINEPREFIX + "/system.reg");
        if (!userRegFile.isFile() || !systemRegFile.isFile()) return false;

        try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
            for (String alias : ALIASES) registryEditor.setStringValue(REPLACEMENTS_KEY, alias, FAMILY);
        }

        try (WineRegistryEditor registryEditor = new WineRegistryEditor(systemRegFile)) {
            for (String font : LINKED_FONTS) {
                String[] current = registryEditor.getMultiStringValue(SYSTEM_LINK_KEY, font);
                ArrayList<String> links = new ArrayList<>();
                links.add(LINK);
                if (current != null) {
                    for (String link : current) {
                        if (!link.equalsIgnoreCase(LINK)) links.add(link);
                    }
                }
                if (current != null && Arrays.equals(current, links.toArray(new String[0]))) continue;
                registryEditor.setMultiStringValue(SYSTEM_LINK_KEY, font, links.toArray(new String[0]));
            }
        }

        container.putExtra(EXTRA_KEY, VERSION);
        return true;
    }
}
