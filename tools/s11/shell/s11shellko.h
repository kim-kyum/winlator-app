/*
 * Korean for the parts of Wine's shell that stay English in a Korean session.
 *
 * Wine's Korean resources cover most of its file views, but some text is not in them:
 *  - shell32 builds the "Type" column itself: "Folder", "File" and "<ext> file" are fixed
 *    strings (pidl.c, shell32_main.c), and the file type names come from the registry, where
 *    wine.inf put English ones ("Application", "Text Document");
 *  - Wine 10.10's Korean translation lacks "Ne&w" and "Run as &Administrator", and verbs
 *    without a description show their key name ("Open", "Repair", "install");
 *  - the date column gets "AM"/"PM" from Wine's Korean locale data.
 *
 * A WH_CALLWNDPROCRET hook on the UI thread fixes these where they are shown: the shell
 * view's list (LVN_GETDISPINFOW, after the view filled it in), menus (WM_INITMENUPOPUP, after
 * their owner filled them in) and dialogs such as the file properties (WM_INITDIALOG).
 * Winlator writes Korean type names into the registry too (KoreanShell.java), which covers
 * other programs' file dialogs; this also handles types that were added later.
 *
 * Call shellko_install() once on the thread that shows the shell view (or file dialogs).
 */
#ifndef S11SHELLKO_H
#define S11SHELLKO_H

#include <shobjidl.h>

static const IID shellko_iid_shell_folder = {0x000214e6, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID shellko_iid_shell_folder2 = {0x93f2f68c, 0x1d1b, 0x11d3, {0xa3, 0x0e, 0x00, 0xc0, 0x4f, 0x79, 0xab, 0xd1}};
static const IID shellko_iid_folder_view = {0xcde725b0, 0xccc9, 0x4519, {0x91, 0x7e, 0x32, 0x5d, 0x72, 0xfa, 0xb4, 0xce}};
static const GUID shellko_fmtid_storage = {0xb725f130, 0x47ef, 0x101a, {0xa5, 0xf1, 0x02, 0x60, 0x8c, 0x9e, 0xeb, 0xac}};
#define SHELLKO_PID_STORAGETYPE 4
#define SHELLKO_PID_WRITETIME 14
#ifndef CWM_GETISHELLBROWSER
#define CWM_GETISHELLBROWSER (WM_USER + 7)   /* answered by Explorer browsers and file dialogs */
#endif

struct shellko_pair { const WCHAR *en, *ko; };

/* Wine's own file type names (wine.inf, and classes Winlator adds without a name) */
static const struct shellko_pair shellko_type_names[] = {
    {L"Application", L"응용 프로그램"},
    {L"Application Extension", L"응용 프로그램 확장"},
    {L"Text Document", L"텍스트 문서"},
    {L"Configuration Settings", L"구성 설정"},
    {L"Shortcut", L"바로 가기"},
    {L"Internet Shortcut", L"인터넷 바로 가기"},
    {L"Compiled HTML Help File", L"컴파일된 HTML 도움말 파일"},
    {L"Control Panel Item", L"제어판 항목"},
    {L"Help File", L"도움말 파일"},
    {L"HTML Document", L"HTML 문서"},
    {L"MHTML Document", L"MHTML 문서"},
    {L"XHTML Document", L"XHTML 문서"},
    {L"XML Document", L"XML 문서"},
    {L"SVG Document", L"SVG 문서"},
    {L"PDF Document", L"PDF 문서"},
    {L"Rich Text Document", L"서식 있는 텍스트 문서"},
    {L"Write Document", L"Write 문서"},
    {L"XSL Stylesheet", L"XSL 스타일시트"},
    {L"Setup Information", L"설치 정보"},
    {L"GIF Image", L"GIF 이미지"},
    {L"JPEG Image", L"JPEG 이미지"},
    {L"PNG Image", L"PNG 이미지"},
    {L"XBM Image", L"XBM 이미지"},
    {L"JScript Script File", L"JScript 스크립트 파일"},
    {L"VBScript Script File", L"VBScript 스크립트 파일"},
    {L"Windows Installer Package", L"Windows Installer 패키지"},
    {L"Windows Installer Patch", L"Windows Installer 패치"},
    {L"Internet Document Set", L"인터넷 문서 세트"},
    {L"Registration Entries", L"등록 항목"},
    {L"Windows Registry File", L"등록 항목"},
    {L"Security Certificate", L"보안 인증서"},
    {L"Windows Batch File", L"Windows 배치 파일"},
    {L"Windows Command Script", L"Windows 명령 스크립트"},
    {L"MS-DOS Application", L"MS-DOS 응용 프로그램"},
};

/* Types Windows names but a Wine prefix does not register ("bat file" in Wine) */
static const struct shellko_pair shellko_extension_names[] = {
    {L"exe", L"응용 프로그램"},
    {L"dll", L"응용 프로그램 확장"},
    {L"txt", L"텍스트 문서"},
    {L"log", L"텍스트 문서"},
    {L"ini", L"구성 설정"},
    {L"lnk", L"바로 가기"},
    {L"bat", L"Windows 배치 파일"},
    {L"cmd", L"Windows 명령 스크립트"},
    {L"com", L"MS-DOS 응용 프로그램"},
    {L"reg", L"등록 항목"},
    {L"sys", L"시스템 파일"},
    {L"ocx", L"ActiveX 컨트롤"},
    {L"msi", L"Windows Installer 패키지"},
    {L"zip", L"압축(ZIP) 파일"},
    {L"ttf", L"TrueType 글꼴 파일"},
    {L"otf", L"OpenType 글꼴 파일"},
    {L"ttc", L"TrueType 글꼴 모음 파일"},
    {L"fon", L"글꼴 파일"},
    {L"ico", L"아이콘"},
    {L"bmp", L"비트맵 이미지"},
    {L"jpg", L"JPEG 이미지"},
    {L"jpeg", L"JPEG 이미지"},
    {L"png", L"PNG 이미지"},
    {L"gif", L"GIF 이미지"},
    {L"tif", L"TIFF 이미지"},
    {L"tiff", L"TIFF 이미지"},
    {L"webp", L"WEBP 이미지"},
    {L"mp3", L"MP3 형식 사운드"},
    {L"wav", L"웨이브 사운드"},
    {L"mid", L"MIDI 시퀀스"},
    {L"midi", L"MIDI 시퀀스"},
    {L"mp4", L"MP4 비디오"},
    {L"avi", L"AVI 비디오"},
    {L"mkv", L"MKV 비디오"},
    {L"htm", L"HTML 문서"},
    {L"html", L"HTML 문서"},
    {L"xml", L"XML 문서"},
    {L"pdf", L"PDF 문서"},
    {L"rtf", L"서식 있는 텍스트 문서"},
    {L"hwp", L"한글 문서"},
    {L"hwpx", L"한글 문서"},
    {L"iso", L"디스크 이미지 파일"},
};

/* Menu texts: what Wine 10.10 leaves English, verbs shown by key name, and Wine's Korean
 * where Windows says it differently ("제거" is "remove"; Windows deletes with "삭제") */
static const struct shellko_pair shellko_menu_texts[] = {
    {L"Ne&w", L"새로 만들기(&W)"},
    {L"&New", L"새로 만들기(&W)"},
    {L"New", L"새로 만들기"},
    {L"새 작업", L"새로 만들기"},
    {L"Run as &Administrator", L"관리자 권한으로 실행(&A)"},
    {L"Run as &administrator", L"관리자 권한으로 실행(&A)"},
    {L"runas", L"관리자 권한으로 실행"},
    {L"Open with Control Panel", L"제어판에서 열기"},
    {L"open", L"열기"},
    {L"Open", L"열기"},
    {L"&Open", L"열기(&O)"},
    {L"edit", L"편집"},
    {L"Edit", L"편집"},
    {L"print", L"인쇄"},
    {L"&Print", L"인쇄(&P)"},
    {L"printto", L"프린터로 인쇄"},
    {L"explore", L"탐색"},
    {L"E&xplore", L"탐색(&X)"},
    {L"install", L"설치"},
    {L"Install", L"설치"},
    {L"Repair", L"복구"},
    {L"Uninstall", L"제거"},
    {L"play", L"재생"},
    {L"Play", L"재생"},
    {L"find", L"찾기"},
    {L"preview", L"미리 보기"},
    {L"Folder", L"폴더"},
    {L"&Folder", L"폴더(&F)"},
    {L"Briefcase", L"서류 가방"},
    {L"제거(&D)", L"삭제(&D)"},
    /* the rest of the shell's menus, should Wine run with an English UI */
    {L"C&ut", L"잘라내기(&T)"},
    {L"&Copy", L"복사(&C)"},
    {L"&Paste", L"붙여넣기(&P)"},
    {L"Paste", L"붙여넣기"},
    {L"Paste as Link", L"바로 가기 붙여넣기"},
    {L"Create &Link", L"바로 가기 만들기(&S)"},
    {L"&Delete", L"삭제(&D)"},
    {L"&Rename", L"이름 바꾸기(&M)"},
    {L"&Properties", L"속성(&R)"},
    {L"Properties", L"속성"},
    {L"&View", L"보기(&V)"},
    {L"View", L"보기"},
    {L"Lar&ge Icons", L"큰 아이콘(&G)"},
    {L"S&mall Icons", L"작은 아이콘(&M)"},
    {L"&List", L"목록(&L)"},
    {L"&Details", L"자세히(&D)"},
    {L"Arrange &Icons", L"정렬 기준(&O)"},
    {L"By &Name", L"이름(&N)"},
    {L"By &Type", L"유형(&T)"},
    {L"By &Size", L"크기(&S)"},
    {L"By &Date", L"수정한 날짜(&D)"},
    {L"&Auto Arrange", L"자동 정렬(&A)"},
    {L"Line up Icons", L"아이콘 줄 맞춤"},
    {L"Refresh", L"새로 고침"},
    {L"New &Folder", L"폴더(&F)"},
    {L"New &Link", L"바로 가기(&S)"},
    {L"&Select", L"선택(&S)"},
    {L"&Restore", L"복원(&R)"},
    {L"&Erase", L"삭제(&D)"},
    /* Wine's Korean, in Windows' words */
    {L"아이콘 정렬(&I)", L"정렬 기준(&O)"},
    {L"이름별(&N)", L"이름(&N)"},
    {L"종류별(&T)", L"유형(&T)"},
    {L"크기순(&S)", L"크기(&S)"},
    {L"날짜순(&D)", L"수정한 날짜(&D)"},
    {L"링크로 붙여넣기", L"바로 가기 붙여넣기"},
    {L"링크 만들기(&L)", L"바로 가기 만들기(&S)"},
    {L"새 링크(&L)", L"바로 가기(&S)"},
    {L"새 폴더(&F)", L"폴더(&F)"},
};

/* Texts set in dialogs (the file properties) */
static const struct shellko_pair shellko_dialog_texts[] = {
    {L"(unknown)", L"(알 수 없음)"},
};

static const WCHAR *shellko_lookup(const struct shellko_pair *pairs, int count, const WCHAR *text, BOOL nocase)
{
    int i;
    for (i = 0; i < count; i++)
        if (nocase ? !lstrcmpiW(pairs[i].en, text) : !lstrcmpW(pairs[i].en, text)) return pairs[i].ko;
    return NULL;
}

/* "Folder" for a drive: what Windows calls the kind of drive */
static const WCHAR *shellko_drive_type(const WCHAR *root)
{
    switch (GetDriveTypeW(root)) {
    case DRIVE_REMOVABLE: return L"이동식 디스크";
    case DRIVE_CDROM: return L"CD 드라이브";
    case DRIVE_REMOTE: return L"네트워크 드라이브";
    case DRIVE_RAMDISK: return L"RAM 디스크";
    default: return L"로컬 디스크";
    }
}

static void shellko_strret(STRRET *sr, PCUITEMID_CHILD child, WCHAR *out, int size)
{
    out[0] = 0;
    switch (sr->uType) {
    case STRRET_WSTR:
        if (sr->pOleStr) {
            lstrcpynW(out, sr->pOleStr, size);
            CoTaskMemFree(sr->pOleStr);
        }
        break;
    case STRRET_CSTR:
        MultiByteToWideChar(CP_ACP, 0, sr->cStr, -1, out, size);
        break;
    case STRRET_OFFSET:
        MultiByteToWideChar(CP_ACP, 0, (const char *)child + sr->uOffset, -1, out, size);
        break;
    }
}

/* Wine's "<ext> file": one word (an extension, or a class without a name) and " file" */
static BOOL shellko_is_ext_file(const WCHAR *text)
{
    int len = lstrlenW(text), i;
    if (len <= 5 || len > 5 + 24 || lstrcmpW(text + len - 5, L" file")) return FALSE;
    for (i = 0; i < len - 5; i++) if (text[i] == ' ') return FALSE;
    return TRUE;
}

/*
 * The Korean for a type text Wine gave: its name for one of its own types, or a fixed
 * "Folder" / "File" / "<ext> file". name is the item's parsing name ("C:\\x\\a.txt", "C:\\",
 * "::{...}" for a virtual folder), empty when unknown. FALSE when the text stays.
 */
static BOOL shellko_type_text(const WCHAR *text, const WCHAR *name, WCHAR *out, int size)
{
    const WCHAR *ko = shellko_lookup(shellko_type_names, ARRAY_SIZE(shellko_type_names), text, FALSE), *ext = NULL;
    int len = lstrlenW(text);

    if (ko) {
        lstrcpynW(out, ko, size);
        return TRUE;
    }
    /* a Korean name read through a code page without Hangul ("PDF ??"): Wine reads type
     * names as ANSI text. The extension says what it was */
    if (wcschr(text, '?') && name[0]) {
        const WCHAR *dot = wcsrchr(name, '.'), *slash = wcsrchr(name, '\\');
        if (dot && (!slash || dot > slash) && dot[1]) {
            WCHAR upper[64];
            int i;
            if ((ko = shellko_lookup(shellko_extension_names, ARRAY_SIZE(shellko_extension_names), dot + 1, TRUE))) {
                lstrcpynW(out, ko, size);
                return TRUE;
            }
            for (i = 0; dot[i + 1] && i < (int)ARRAY_SIZE(upper) - 1; i++) upper[i] = towupper(dot[i + 1]);
            upper[i] = 0;
            wsprintfW(out, L"%.40s 파일", upper);
            return TRUE;
        }
    }
    if (!lstrcmpW(text, L"Folder")) {
        if (name[0] && name[1] == ':' && name[2] == '\\' && !name[3]) lstrcpynW(out, shellko_drive_type(name), size);
        else if (name[0] == ':' && name[1] == ':') lstrcpynW(out, L"시스템 폴더", size);
        else lstrcpynW(out, L"파일 폴더", size);
        return TRUE;
    }
    if (!lstrcmpW(text, L"File")) {
        lstrcpynW(out, L"파일", size);
        return TRUE;
    }
    if (shellko_is_ext_file(text)) {
        WCHAR upper[64];
        int i;
        /* the item's own extension: Wine writes the class name when the class has no name */
        if (name[0]) {
            const WCHAR *dot = wcsrchr(name, '.'), *slash = wcsrchr(name, '\\');
            if (dot && (!slash || dot > slash) && dot[1]) ext = dot + 1;
        }
        if (!ext) {
            lstrcpynW(upper, text, min(len - 4, (int)ARRAY_SIZE(upper)));
            ext = upper;
        }
        if ((ko = shellko_lookup(shellko_extension_names, ARRAY_SIZE(shellko_extension_names), ext, TRUE))) {
            lstrcpynW(out, ko, size);
            return TRUE;
        }
        for (i = 0; ext[i] && i < (int)ARRAY_SIZE(upper) - 1; i++) upper[i] = towupper(ext[i]);
        upper[i] = 0;
        wsprintfW(out, L"%.40s 파일", upper);
        return TRUE;
    }
    return FALSE;
}

/* "2026. 10. 27. AM 3:07": Windows says 오전 / 오후 */
static BOOL shellko_date_text(WCHAR *text, int size)
{
    WCHAR out[128], *p;
    BOOL changed = FALSE;
    lstrcpynW(out, text, ARRAY_SIZE(out));
    for (p = out; *p; p++) {
        if ((p[0] == 'A' || p[0] == 'P') && p[1] == 'M' && (p == out || p[-1] == ' ') && (!p[2] || p[2] == ' ')) {
            const WCHAR *ko = p[0] == 'A' ? L"오전" : L"오후";
            p[0] = ko[0];
            p[1] = ko[1];
            changed = TRUE;
        }
    }
    if (changed) lstrcpynW(text, out, size);
    return changed;
}

/* ---------------------------------------------------------------------------------------- */
/* The shell view                                                                            */
/* ---------------------------------------------------------------------------------------- */

static struct {
    HWND view;              /* the SHELLDLL_DefView these are for */
    IShellFolder *folder;
    int type_column, date_column;
} shellko_view;

static void shellko_forget_view(void)
{
    if (shellko_view.folder) IShellFolder_Release(shellko_view.folder);
    ZeroMemory(&shellko_view, sizeof(shellko_view));
    shellko_view.type_column = shellko_view.date_column = -1;
}

/* The folder and columns of a shell view, from the browser that hosts it (an Explorer
 * browser or a file dialog: both answer CWM_GETISHELLBROWSER) */
static BOOL shellko_find_view(HWND defview)
{
    IShellBrowser *browser;
    IShellView *view = NULL;
    IFolderView *folder_view;
    IShellFolder2 *folder2;
    HWND hwnd = NULL;
    UINT i;

    if (shellko_view.view == defview && shellko_view.folder) return TRUE;
    shellko_forget_view();
    browser = (IShellBrowser *)SendMessageW(GetParent(defview), CWM_GETISHELLBROWSER, 0, 0);
    if (!browser || FAILED(IShellBrowser_QueryActiveShellView(browser, &view)) || !view) return FALSE;
    /* while a new folder opens its view may not be the active one yet: try again later */
    if (SUCCEEDED(IShellView_GetWindow(view, &hwnd)) && hwnd == defview &&
        SUCCEEDED(IShellView_QueryInterface(view, &shellko_iid_folder_view, (void **)&folder_view))) {
        if (SUCCEEDED(IFolderView_GetFolder(folder_view, &shellko_iid_shell_folder, (void **)&shellko_view.folder)) &&
            shellko_view.folder) {
            shellko_view.view = defview;
            if (SUCCEEDED(IShellFolder_QueryInterface(shellko_view.folder, &shellko_iid_shell_folder2, (void **)&folder2))) {
                for (i = 0; i < 16; i++) {
                    SHCOLUMNID scid;
                    if (FAILED(IShellFolder2_MapColumnToSCID(folder2, i, &scid))) {
                        if (i > 0) break;
                        continue;
                    }
                    if (!IsEqualGUID(&scid.fmtid, &shellko_fmtid_storage)) continue;
                    if (scid.pid == SHELLKO_PID_STORAGETYPE && shellko_view.type_column < 0) shellko_view.type_column = i;
                    if (scid.pid == SHELLKO_PID_WRITETIME && shellko_view.date_column < 0) shellko_view.date_column = i;
                }
                IShellFolder2_Release(folder2);
            }
        }
        IFolderView_Release(folder_view);
    }
    IShellView_Release(view);
    return shellko_view.folder != NULL;
}

/* Optional: the program's own name for an item, from its parsing name (the File Explorer names
 * drives as its navigation pane does: "로컬 디스크 (C:)" where Wine shows "(C:)") */
static BOOL (*shellko_item_name)(const WCHAR *parsing, WCHAR *text, int size);

static void shellko_dispinfo(HWND defview, NMLVDISPINFOW *info)
{
    WCHAR cls[32], name[MAX_PATH] = L"", out[128];
    LVITEMW *item = &info->item;
    PCUITEMID_CHILD child = (PCUITEMID_CHILD)item->lParam;
    int len;

    if (!(item->mask & LVIF_TEXT) || !item->pszText || item->cchTextMax < 2 || item->iSubItem < 0) return;
    if (!GetClassNameW(defview, cls, ARRAY_SIZE(cls)) || lstrcmpW(cls, L"SHELLDLL_DefView")) return;
    if (!shellko_find_view(defview)) return;

    if (item->iSubItem == 0) {
        /* only drive names ("(C:)", "Label (D:)") are asked about */
        STRRET sr = {0};
        len = lstrlenW(item->pszText);
        if (!shellko_item_name || !child || len < 4 || item->pszText[len - 1] != ')' || item->pszText[len - 2] != ':' ||
            item->pszText[len - 4] != '(')
            return;
        if (SUCCEEDED(IShellFolder_GetDisplayNameOf(shellko_view.folder, child, SHGDN_FORPARSING, &sr)))
            shellko_strret(&sr, child, name, ARRAY_SIZE(name));
        if (name[0] && shellko_item_name(name, out, ARRAY_SIZE(out))) lstrcpynW(item->pszText, out, item->cchTextMax);
        return;
    }

    if (item->iSubItem == shellko_view.date_column) {
        shellko_date_text(item->pszText, item->cchTextMax);
        return;
    }
    if (item->iSubItem != shellko_view.type_column) return;
    /* Wine's names and fixed texts only (and names a code page lost); a program's own type
     * names stay as they are */
    if (!shellko_lookup(shellko_type_names, ARRAY_SIZE(shellko_type_names), item->pszText, FALSE) &&
        lstrcmpW(item->pszText, L"Folder") && lstrcmpW(item->pszText, L"File") && !wcschr(item->pszText, '?') &&
        !shellko_is_ext_file(item->pszText))
        return;
    if (child) {
        STRRET sr = {0};
        if (SUCCEEDED(IShellFolder_GetDisplayNameOf(shellko_view.folder, child, SHGDN_FORPARSING, &sr)))
            shellko_strret(&sr, child, name, ARRAY_SIZE(name));
    }
    if (shellko_type_text(item->pszText, name, out, ARRAY_SIZE(out))) lstrcpynW(item->pszText, out, item->cchTextMax);
}

/* ---------------------------------------------------------------------------------------- */
/* Menus and dialogs                                                                         */
/* ---------------------------------------------------------------------------------------- */

static void shellko_menu(HMENU menu, int depth)
{
    int i, count = GetMenuItemCount(menu);
    for (i = 0; i < count; i++) {
        WCHAR text[160] = L"";
        MENUITEMINFOW info = {sizeof(info), MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU};
        const WCHAR *ko;
        info.dwTypeData = text;
        info.cch = ARRAY_SIZE(text);
        if (!GetMenuItemInfoW(menu, i, TRUE, &info)) continue;
        if (!(info.fType & (MFT_SEPARATOR | MFT_BITMAP | MFT_OWNERDRAW)) && text[0] &&
            (ko = shellko_lookup(shellko_menu_texts, ARRAY_SIZE(shellko_menu_texts), text, FALSE))) {
            MENUITEMINFOW set = {sizeof(set), MIIM_STRING};
            set.dwTypeData = (WCHAR *)ko;
            SetMenuItemInfoW(menu, i, TRUE, &set);
        }
        if (info.hSubMenu && depth < 4) shellko_menu(info.hSubMenu, depth + 1);
    }
}

static BOOL CALLBACK shellko_dialog_child(HWND child, LPARAM unused)
{
    WCHAR cls[16], text[160], out[160];
    const WCHAR *ko;
    if (!GetClassNameW(child, cls, ARRAY_SIZE(cls))) return TRUE;
    /* labels, and the read-only fields of the properties pages (never a box the user types in) */
    if (lstrcmpiW(cls, L"Static") && (lstrcmpiW(cls, L"Edit") || !(GetWindowLongW(child, GWL_STYLE) & ES_READONLY)))
        return TRUE;
    if (!GetWindowTextW(child, text, ARRAY_SIZE(text)) || !text[0]) return TRUE;
    if ((ko = shellko_lookup(shellko_dialog_texts, ARRAY_SIZE(shellko_dialog_texts), text, FALSE)))
        SetWindowTextW(child, ko);
    else if (shellko_type_text(text, L"", out, ARRAY_SIZE(out)))
        SetWindowTextW(child, out);
    else if (text[0] >= '0' && text[0] <= '9' && shellko_date_text(text, ARRAY_SIZE(text)))   /* "2026년 10월 27일 화요일 AM 3:07" */
        SetWindowTextW(child, text);
    return TRUE;
}

static HHOOK shellko_hook;

static LRESULT CALLBACK shellko_proc(int code, WPARAM wp, LPARAM lp)
{
    const CWPRETSTRUCT *msg = (const CWPRETSTRUCT *)lp;
    if (code == HC_ACTION && msg) {
        switch (msg->message) {
        case WM_NOTIFY: {
            NMHDR *hdr = (NMHDR *)msg->lParam;
            if (hdr && hdr->code == LVN_GETDISPINFOW) shellko_dispinfo(msg->hwnd, (NMLVDISPINFOW *)hdr);
            break;
        }
        case WM_INITMENUPOPUP:
            if (!HIWORD(msg->lParam)) shellko_menu((HMENU)msg->wParam, 0);
            break;
        case WM_INITDIALOG:
            EnumChildWindows(msg->hwnd, shellko_dialog_child, 0);
            break;
        case WM_DESTROY:
            if (msg->hwnd == shellko_view.view) shellko_forget_view();
            break;
        }
    }
    return CallNextHookEx(shellko_hook, code, wp, lp);
}

static void shellko_install(void)
{
    shellko_forget_view();
    if (!shellko_hook) shellko_hook = SetWindowsHookExW(WH_CALLWNDPROCRET, shellko_proc, NULL, GetCurrentThreadId());
}

/* Column titles as Windows has them; Wine's are English or its own Korean */
static const WCHAR *shellko_column_title(const WCHAR *text, int index, BOOL drives)
{
    static const struct shellko_pair titles[] = {
        {L"File", L"이름"}, {L"파일", L"이름"}, {L"Name", L"이름"},
        {L"Type", L"유형"}, {L"종류", L"유형"},
        {L"Modified", L"수정한 날짜"}, {L"Date Modified", L"수정한 날짜"}, {L"수정됨", L"수정한 날짜"},
        {L"Attributes", L"특성"}, {L"속성", L"특성"},
        {L"Size available", L"사용 가능한 공간"}, {L"사용할 수 있는 크기", L"사용 가능한 공간"},
        {L"Comments", L"설명"}, {L"주석", L"설명"},
        {L"Original location", L"원래 위치"},
        {L"Date deleted", L"삭제한 날짜"}, {L"삭제된 날짜", L"삭제한 날짜"},
        {L"Total size", L"전체 크기"},
    };
    if (index == 0) return L"이름";
    if (!lstrcmpiW(text, L"Size") || !lstrcmpW(text, L"크기")) return drives ? L"전체 크기" : L"크기";
    return shellko_lookup(titles, ARRAY_SIZE(titles), text, TRUE);
}

#endif
