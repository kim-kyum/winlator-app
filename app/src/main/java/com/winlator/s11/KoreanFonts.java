package com.winlator.s11;

import com.winlator.container.Container;
import com.winlator.core.WineRegistryEditor;
import com.winlator.xenvironment.RootFS;

import java.io.File;

/**
 * Points the Korean Windows font names at the bundled NanumGothic.
 *
 * Wine falls back from its UI font (Tahoma) to "Gulim" for Hangul, and Korean programs
 * ask for Gulim, Dotum, Batang or Malgun Gothic by name. None of them exist in a fresh
 * prefix, so Hangul showed up as boxes. Wine's [HKCU\Software\Wine\Fonts\Replacements]
 * maps a missing family to an installed one and is skipped when the real font exists,
 * so fonts the user installs later still win.
 */
public final class KoreanFonts {
    private static final String VERSION = "1";
    private static final String EXTRA_KEY = "s11KoreanFonts";
    private static final String FAMILY = "NanumGothic";
    private static final String REPLACEMENTS_KEY = "Software\\Wine\\Fonts\\Replacements";
    private static final String[] ALIASES = {
        "Gulim", "GulimChe", "Dotum", "DotumChe", "Batang", "BatangChe", "Gungsuh", "GungsuhChe",
        "Malgun Gothic", "굴림", "굴림체", "돋움", "돋움체", "바탕", "바탕체", "궁서", "궁서체", "맑은 고딕"
    };

    private KoreanFonts() {}

    /** Returns true when the container data changed and needs saving. */
    public static boolean applyIfNeeded(Container container, File rootDir) {
        if (VERSION.equals(container.getExtra(EXTRA_KEY))) return false;

        File userRegFile = new File(rootDir, RootFS.WINEPREFIX + "/user.reg");
        if (!userRegFile.isFile()) return false;
        try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
            for (String alias : ALIASES) registryEditor.setStringValue(REPLACEMENTS_KEY, alias, FAMILY);
        }
        container.putExtra(EXTRA_KEY, VERSION);
        return true;
    }
}
