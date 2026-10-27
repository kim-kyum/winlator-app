package com.winlator.s11;

import com.winlator.container.Container;
import com.winlator.core.EnvVars;
import com.winlator.core.WineRegistryEditor;
import com.winlator.xenvironment.RootFS;

import java.io.File;
import java.util.Locale;

/**
 * Korean names for the file types and menu commands Wine registers in English, and the
 * Windows notation of dates (2026-10-27 오전 3:07), written before Wine starts.
 *
 * Wine's Korean resources cover its own dialogs, but the type names come from wine.inf
 * ("Application", "Text Document") and some classes have no name at all (dllfile, the
 * REGfile Winlator adds), which Wine shows as "dllfile file". Verbs without a description
 * show their key name ("Repair", "install"), and Wine 10.10 has no Korean "Run as
 * Administrator". Wine's Korean locale data says "AM"/"PM" and "2026. 10. 27.".
 *
 * Only display names change: no file type gains or loses a command. When the app language is
 * not Korean the English names come back. Dates are written only once Wine has set the user
 * locale to ko-KR itself (it rewrites the whole key when the locale changes), and only once,
 * so changes made in Wine's regional settings stay.
 *
 * The S11 programs translate the same texts where they show them (s11shellko.h), which also
 * covers types registered later; this makes other programs' file dialogs Korean too.
 */
public final class KoreanShell {
    private static final String VERSION = "1";
    private static final String EXTRA_KEY = "s11KoreanShell";
    private static final String DATES_EXTRA_KEY = "s11KoreanDates";
    private static final String CLASSES = "Software\\Classes\\";
    private static final String INTERNATIONAL_KEY = "Control Panel\\International";

    /* class, Korean name, English name (Wine's, or Windows' for the classes Wine leaves unnamed) */
    private static final String[][] TYPE_NAMES = {
        {"exefile", "응용 프로그램", "Application"},
        {"dllfile", "응용 프로그램 확장", "Application Extension"},
        {"txtfile", "텍스트 문서", "Text Document"},
        {"inifile", "구성 설정", "Configuration Settings"},
        {"lnkfile", "바로 가기", "Shortcut"},
        {"InternetShortcut", "인터넷 바로 가기", "Internet Shortcut"},
        {"chm.file", "컴파일된 HTML 도움말 파일", "Compiled HTML Help File"},
        {"cplfile", "제어판 항목", "Control Panel Item"},
        {"hlpfile", "도움말 파일", "Help File"},
        {"htmlfile", "HTML 문서", "HTML Document"},
        {"mhtmlfile", "MHTML 문서", "MHTML Document"},
        {"xhtmlfile", "XHTML 문서", "XHTML Document"},
        {"xmlfile", "XML 문서", "XML Document"},
        {"svgfile", "SVG 문서", "SVG Document"},
        {"pdffile", "PDF 문서", "PDF Document"},
        {"rtffile", "서식 있는 텍스트 문서", "Rich Text Document"},
        {"wrifile", "Write 문서", "Write Document"},
        {"xslfile", "XSL 스타일시트", "XSL Stylesheet"},
        {"inffile", "설치 정보", "Setup Information"},
        {"giffile", "GIF 이미지", "GIF Image"},
        {"jpegfile", "JPEG 이미지", "JPEG Image"},
        {"pjpegfile", "JPEG 이미지", "JPEG Image"},
        {"pngfile", "PNG 이미지", "PNG Image"},
        {"xbmfile", "XBM 이미지", "XBM Image"},
        {"JSFile", "JScript 스크립트 파일", "JScript Script File"},
        {"VBSFile", "VBScript 스크립트 파일", "VBScript Script File"},
        {"Msi.Package", "Windows Installer 패키지", "Windows Installer Package"},
        {"Msi.Patch", "Windows Installer 패치", "Windows Installer Patch"},
        {"ITS FILE", "인터넷 문서 세트", "Internet Document Set"},
        {"REGfile", "등록 항목", "Registration Entries"},
    };

    /* verb key, Korean, English (null: Wine has no description, so none is kept) */
    private static final String[][] VERB_NAMES = {
        {"cplfile\\shell\\cplopen", "제어판에서 열기(&O)", "Open with Control Panel"},
        {"exefile\\shell\\runas", "관리자 권한으로 실행(&A)", null},
        {"Msi.Package\\shell\\Open", "열기(&O)", null},
        {"Msi.Package\\shell\\Repair", "복구(&R)", null},
        {"Msi.Package\\shell\\Uninstall", "제거(&U)", null},
        {"Msi.Patch\\shell\\Open", "열기(&O)", null},
        {"inffile\\shell\\install", "설치(&I)", null},
    };

    private KoreanShell() {}

    /* Korean as LocaleHelper sets it (LC_ALL from the app language), unless the container's
     * own LC_ALL says otherwise: Wine reads type names through the ANSI code page, and one
     * without Hangul shows "??" */
    private static boolean isKorean(Container container) {
        String lcAll = new EnvVars(container.getEnvVars()).get("LC_ALL");
        if (!lcAll.isEmpty()) return lcAll.startsWith("ko");
        return Locale.getDefault().getLanguage().equals("ko");
    }

    /** Returns true when the container data changed and needs saving. */
    public static boolean applyIfNeeded(Container container, File rootDir) {
        boolean korean = isKorean(container);
        boolean changed = false;
        File systemRegFile = new File(rootDir, RootFS.WINEPREFIX + "/system.reg");
        File userRegFile = new File(rootDir, RootFS.WINEPREFIX + "/user.reg");
        if (!systemRegFile.isFile() || !userRegFile.isFile()) return false;

        String wanted = VERSION + (korean ? ":ko" : ":en");
        if (!wanted.equals(container.getExtra(EXTRA_KEY))) {
            try (WineRegistryEditor registryEditor = new WineRegistryEditor(systemRegFile)) {
                for (String[] type : TYPE_NAMES) {
                    String name = korean ? type[1] : type[2];
                    if (!name.equals(registryEditor.getStringValue(CLASSES + type[0], null)))
                        registryEditor.setStringValue(CLASSES + type[0], null, name);
                }
                for (String[] verb : VERB_NAMES) {
                    String key = CLASSES + verb[0];
                    // only verbs that exist: a name alone would add a command that does nothing
                    if (registryEditor.getStringValue(key + "\\command", null) == null) continue;
                    String name = korean ? verb[1] : verb[2];
                    if (name != null) registryEditor.setStringValue(key, null, name);
                    else registryEditor.removeValue(key, null);
                }
            }
            container.putExtra(EXTRA_KEY, wanted);
            changed = true;
        }

        if (!korean) {
            // Wine sets the dates of another language itself; Korean ones get written again later
            if (container.getExtra(DATES_EXTRA_KEY, null) != null) {
                container.putExtra(DATES_EXTRA_KEY, null);
                changed = true;
            }
        }
        else if (!VERSION.equals(container.getExtra(DATES_EXTRA_KEY))) {
            try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
                registryEditor.setCreateKeyIfNotExist(false);
                // Wine writes the Korean locale data at its first start in Korean; after that
                // it keeps what is there as long as the locale stays the same
                if ("ko-KR".equals(registryEditor.getStringValue(INTERNATIONAL_KEY, "LocaleName"))) {
                    registryEditor.setStringValues(INTERNATIONAL_KEY,
                        new String[]{"s1159", "오전"},
                        new String[]{"s2359", "오후"},
                        new String[]{"sShortDate", "yyyy-MM-dd"},
                        new String[]{"sDate", "-"},
                        new String[]{"iDate", "2"});
                    container.putExtra(DATES_EXTRA_KEY, VERSION);
                    changed = true;
                }
            }
        }
        return changed;
    }
}
