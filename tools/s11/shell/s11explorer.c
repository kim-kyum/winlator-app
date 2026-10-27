/*
 * Winlator S11 File Explorer: a Windows 11 style file manager.
 *
 * The folder view is Wine's own shell view, hosted through IExplorerBrowser (the same one
 * Wine's explorer.exe uses): icons, details, sorting, renaming, drag and drop, right-click
 * menus and opening files all come from Wine. Around it this draws what Windows 11 has:
 *
 *  - back, forward, up and refresh, and an address bar of clickable folder names that turns
 *    into a text box for typing a path;
 *  - a command bar: new folder, cut, copy, paste, rename, delete, sort and view;
 *  - a navigation pane with the user folders and the drives (Winlator's Android folders
 *    show up as drives), and a status bar with the number of items.
 *
 * It closes without asking. Ctrl+C/X/V/A, Ctrl+L, Alt+Left/Right/Up, F2, F5 and Delete work.
 */
#include "s11ui.h"
#include <shobjidl.h>
#include <uxtheme.h>
#include "s11shellko.h"

#define WM_APP_NAVIGATED (WM_APP + 30)

static const CLSID clsid_explorer_browser = {0x71f96385, 0xddd6, 0x48d3, {0xa0, 0xc1, 0xae, 0x06, 0xe8, 0xb0, 0x55, 0xfb}};
static const IID iid_explorer_browser = {0xdfd3b6b5, 0xc10c, 0x4be9, {0x85, 0xf6, 0xa6, 0x69, 0x69, 0xf4, 0x02, 0xf6}};
static const IID iid_explorer_browser_events = {0x361bbdc7, 0xe6ee, 0x4e13, {0xbe, 0x58, 0x58, 0xe2, 0x24, 0x0c, 0x81, 0x0f}};
static const IID iid_shell_view = {0x000214e3, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID iid_context_menu = {0x000214e4, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID iid_unknown = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

static HINSTANCE instance;
static HWND main_window, address_edit;
static HFONT ui_font, small_font, nav_font, nav_header_font;
static ULONG_PTR gdiplus_token;
static IExplorerBrowser *browser;
static DWORD advise_cookie;
static PIDLIST_ABSOLUTE current;    /* the folder shown */
static WCHAR current_path[MAX_PATH];/* its file system path, if it has one */
static PIDLIST_ABSOLUTE this_pc;    /* My Computer */

/* sizes at 96 dpi */
#define TOP_H 52
#define CMD_H 46
#define NAV_W 232
#define STATUS_H 30

/* ---------------------------------------------------------------------------------------- */
/* History (Wine's browser keeps its own; this one knows whether back/forward are possible)  */
/* ---------------------------------------------------------------------------------------- */

#define MAX_HISTORY 64
static PIDLIST_ABSOLUTE history[MAX_HISTORY];
static int history_count, history_pos = -1;
static enum { NAV_NEW, NAV_BACK, NAV_FORWARD } nav_mode;

static void history_record(PCIDLIST_ABSOLUTE pidl)
{
    int i;
    if (nav_mode == NAV_BACK && history_pos > 0) history_pos--;
    else if (nav_mode == NAV_FORWARD && history_pos < history_count - 1) history_pos++;
    else {
        if (history_pos >= 0 && ILIsEqual(history[history_pos], pidl)) goto done;
        for (i = history_pos + 1; i < history_count; i++) ILFree(history[i]);
        history_count = history_pos + 1;
        if (history_count == MAX_HISTORY) {
            ILFree(history[0]);
            memmove(history, history + 1, (MAX_HISTORY - 1) * sizeof(history[0]));
            history_count--;
        }
        history[history_count++] = ILClone(pidl);
        history_pos = history_count - 1;
    }
done:
    nav_mode = NAV_NEW;
}

/* ---------------------------------------------------------------------------------------- */
/* Browser events                                                                            */
/* ---------------------------------------------------------------------------------------- */

static HRESULT WINAPI events_QueryInterface(IExplorerBrowserEvents *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &iid_unknown) || IsEqualIID(riid, &iid_explorer_browser_events)) {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI events_AddRef(IExplorerBrowserEvents *iface) { return 2; }
static ULONG WINAPI events_Release(IExplorerBrowserEvents *iface) { return 1; }
static HRESULT WINAPI events_OnNavigationPending(IExplorerBrowserEvents *iface, PCIDLIST_ABSOLUTE pidl) { return S_OK; }
static HRESULT WINAPI events_OnViewCreated(IExplorerBrowserEvents *iface, IShellView *view) { return S_OK; }

static HRESULT WINAPI events_OnNavigationComplete(IExplorerBrowserEvents *iface, PCIDLIST_ABSOLUTE pidl)
{
    if (current) ILFree(current);
    current = ILClone(pidl);
    history_record(pidl);
    PostMessageW(main_window, WM_APP_NAVIGATED, 0, 0);
    return S_OK;
}

static HRESULT WINAPI events_OnNavigationFailed(IExplorerBrowserEvents *iface, PCIDLIST_ABSOLUTE pidl)
{
    nav_mode = NAV_NEW;
    return S_OK;
}

static IExplorerBrowserEventsVtbl events_vtbl = {
    events_QueryInterface, events_AddRef, events_Release, events_OnNavigationPending,
    events_OnViewCreated, events_OnNavigationComplete, events_OnNavigationFailed
};
static IExplorerBrowserEvents events = {&events_vtbl};

/* ---------------------------------------------------------------------------------------- */
/* The folder view                                                                           */
/* ---------------------------------------------------------------------------------------- */

static IShellView *current_view(void)
{
    IShellView *view = NULL;
    if (browser) IExplorerBrowser_GetCurrentView(browser, &iid_shell_view, (void **)&view);
    return view;
}

static HWND view_window(void)
{
    IShellView *view = current_view();
    HWND hwnd = NULL;
    if (view) {
        IShellView_GetWindow(view, &hwnd);
        IShellView_Release(view);
    }
    return hwnd;
}

static HWND view_list(void)
{
    HWND hwnd = view_window();
    return hwnd ? FindWindowExW(hwnd, NULL, WC_LISTVIEWW, NULL) : NULL;
}

/* Keep Wine's working shell view, but give its inner list/header the same S11 treatment. */
static void style_shell_view(void)
{
    HWND view = view_window(), list = view_list(), header;
    if (view) SetWindowTheme(view, L"Explorer", NULL);
    if (!list) return;
    SetWindowTheme(list, L"Explorer", NULL);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
    SetWindowLongW(list, GWL_EXSTYLE, GetWindowLongW(list, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
    SendMessageW(list, WM_SETFONT, (WPARAM)ui_font, TRUE);
    SendMessageW(list, LVM_SETBKCOLOR, 0, pal.field);
    SendMessageW(list, LVM_SETTEXTBKCOLOR, 0, pal.field);
    SendMessageW(list, LVM_SETTEXTCOLOR, 0, pal.text);
    header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
    if (header) {
        SetWindowTheme(header, L"Explorer", NULL);
        SendMessageW(header, WM_SETFONT, (WPARAM)small_font, TRUE);
    }
    SetWindowPos(list, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

static void item_counts(UINT *all, UINT *selected)
{
    HWND list = view_list();
    *all = list ? (UINT)SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) : 0;
    *selected = list ? (UINT)SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0) : 0;
}

/* Runs a verb of the selection ("cut", "copy", "delete") or of the folder ("paste", "NewFolder") */
static void invoke(BOOL background, const char *verb)
{
    IShellView *view = current_view();
    IContextMenu *menu = NULL;
    HWND hwnd = NULL;
    if (!view) return;
    IShellView_GetWindow(view, &hwnd);
    if (SUCCEEDED(IShellView_GetItemObject(view, background ? SVGIO_BACKGROUND : SVGIO_SELECTION, &iid_context_menu,
                                           (void **)&menu)) && menu) {
        CMINVOKECOMMANDINFO info = {sizeof(info)};
        /* the browser's window answers CWM_GETISHELLBROWSER, which the folder menu asks */
        info.hwnd = hwnd ? GetParent(hwnd) : main_window;
        info.lpVerb = verb;
        info.nShow = SW_SHOWNORMAL;
        IContextMenu_InvokeCommand(menu, &info);
        IContextMenu_Release(menu);
    }
    IShellView_Release(view);
}

static void rename_selected(void)
{
    HWND list = view_list();
    int index;
    if (!list) return;
    index = (int)SendMessageW(list, LVM_GETNEXTITEM, -1, LVNI_FOCUSED | LVNI_SELECTED);
    if (index < 0) index = (int)SendMessageW(list, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    if (index < 0) return;
    SetFocus(list);
    SendMessageW(list, LVM_EDITLABELW, index, 0);
}

static void select_all(void)
{
    HWND list = view_list();
    LVITEMW item = {0};
    if (!list) return;
    item.stateMask = item.state = LVIS_SELECTED;
    SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&item);
    SetFocus(list);
}

/*
 * Wine sizes the detail columns for 96 dpi and a narrow window ("Modified" gets cut off).
 * Fit them to their titles and contents instead, give them Windows' titles ("이름", "유형",
 * "수정한 날짜" rather than Wine's "파일", "종류", "수정됨"), and align them as Windows does:
 * sizes on the right, everything else on the left (Wine puts most of them on the right).
 */
static void fit_columns(BOOL grow_only)
{
    HWND list = view_list(), header;
    int i, count;
    BOOL drives = current && this_pc && ILIsEqual(current, this_pc);

    if (!list || (GetWindowLongW(list, GWL_STYLE) & LVS_TYPEMASK) != LVS_REPORT) return;
    header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
    count = header ? (int)SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) : 0;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    for (i = 0; i < count; i++) {
        WCHAR text[128] = L"";
        LVCOLUMNW column = {LVCF_TEXT | LVCF_FMT};
        int title, content, width, old_width = (int)SendMessageW(list, LVM_GETCOLUMNWIDTH, i, 0);

        column.pszText = text;
        column.cchTextMax = ARRAY_SIZE(text);
        SendMessageW(list, LVM_GETCOLUMNW, i, (LPARAM)&column);
        if (korean) {
            const WCHAR *ko = shellko_column_title(text, i, drives);
            if (ko) lstrcpynW(text, ko, ARRAY_SIZE(text));
            column.mask = LVCF_TEXT | LVCF_FMT;
            column.pszText = text;
            column.fmt = (column.fmt & ~LVCFMT_JUSTIFYMASK) |
                         (i > 0 && (!lstrcmpW(text, L"크기") || !lstrcmpW(text, L"전체 크기") ||
                                    !lstrcmpW(text, L"사용 가능한 공간")) ? LVCFMT_RIGHT : LVCFMT_LEFT);
            SendMessageW(list, LVM_SETCOLUMNW, i, (LPARAM)&column);
        }
        title = (int)SendMessageW(list, LVM_GETSTRINGWIDTHW, 0, (LPARAM)text) + SC(28);
        SendMessageW(list, LVM_SETCOLUMNWIDTH, i, LVSCW_AUTOSIZE);
        content = (int)SendMessageW(list, LVM_GETCOLUMNWIDTH, i, 0) + SC(16);
        width = max(title, content);
        if (i == 0) width = max(width, SC(260));
        width = min(width, SC(560));
        /* new items: wider if needed, but keep what the user dragged wider */
        if (grow_only) width = max(width, old_width);
        SendMessageW(list, LVM_SETCOLUMNWIDTH, i, width);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, NULL, TRUE);
}

static void view_command(UINT id)
{
    HWND hwnd = view_window();
    if (hwnd) SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
    if (id == FCIDM_SHVIEW_REPORTVIEW) fit_columns(FALSE);
}

static UINT current_view_command(void)
{
    HWND list = view_list();
    if (!list) return 0;
    switch (GetWindowLongW(list, GWL_STYLE) & LVS_TYPEMASK) {
    case LVS_ICON: return FCIDM_SHVIEW_BIGICON;
    case LVS_SMALLICON: return FCIDM_SHVIEW_SMALLICON;
    case LVS_LIST: return FCIDM_SHVIEW_LISTVIEW;
    default: return FCIDM_SHVIEW_REPORTVIEW;
    }
}

static void navigate_pidl(PCIDLIST_ABSOLUTE pidl)
{
    if (browser && pidl) IExplorerBrowser_BrowseToIDList(browser, pidl, SBSP_ABSOLUTE);
}

static BOOL navigate_path(const WCHAR *path)
{
    PIDLIST_ABSOLUTE pidl = NULL;
    WCHAR expanded[MAX_PATH * 2];
    ExpandEnvironmentStringsW(path, expanded, ARRAY_SIZE(expanded));
    if (FAILED(SHParseDisplayName(expanded, NULL, &pidl, 0, NULL)) || !pidl) return FALSE;
    navigate_pidl(pidl);
    ILFree(pidl);
    return TRUE;
}

static void go_back(void)
{
    if (history_pos <= 0) return;
    nav_mode = NAV_BACK;
    IExplorerBrowser_BrowseToObject(browser, NULL, SBSP_NAVIGATEBACK);
}

static void go_forward(void)
{
    if (history_pos >= history_count - 1) return;
    nav_mode = NAV_FORWARD;
    IExplorerBrowser_BrowseToObject(browser, NULL, SBSP_NAVIGATEFORWARD);
}

static BOOL can_go_up(void) { return current && current->mkid.cb; }

static void go_up(void)
{
    if (can_go_up()) IExplorerBrowser_BrowseToObject(browser, NULL, SBSP_PARENT);
}

static void refresh(void)
{
    IShellView *view = current_view();
    if (view) {
        IShellView_Refresh(view);
        IShellView_Release(view);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Navigation pane                                                                           */
/* ---------------------------------------------------------------------------------------- */

enum place_kind { PLACE_ITEM, PLACE_HEADER, PLACE_GAP };

struct place {
    WCHAR name[96];
    WCHAR path[MAX_PATH];   /* the file system folder, so a path typed or opened matches too */
    PIDLIST_ABSOLUTE pidl;
    HICON icon;
    enum place_kind kind;
    int indent;
    RECT rc;
};

#define MAX_PLACES 40
static struct place places[MAX_PLACES];
static int place_count, hot_place = -1;

/* A folder's file system path without the trailing backslash Wine gives special folders */
static BOOL folder_path(PCIDLIST_ABSOLUTE pidl, WCHAR *path)
{
    int len;
    if (!pidl || !SHGetPathFromIDListW(pidl, path)) {
        path[0] = 0;
        return FALSE;
    }
    len = lstrlenW(path);
    while (len > 3 && path[len - 1] == '\\') path[--len] = 0;
    return path[0] != 0;
}

static void add_place_pidl(const WCHAR *name, PIDLIST_ABSOLUTE pidl)
{
    struct place *p;
    SHFILEINFOW info = {0};
    if (!pidl || place_count >= MAX_PLACES) return;
    p = &places[place_count++];
    ZeroMemory(p, sizeof(*p));
    p->pidl = pidl;
    if (SHGetFileInfoW((const WCHAR *)pidl, 0, &info, sizeof(info),
                       SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON | (name ? 0 : SHGFI_DISPLAYNAME)))
        p->icon = info.hIcon;
    lstrcpynW(p->name, name ? name : info.szDisplayName, ARRAY_SIZE(p->name));
    folder_path(pidl, p->path);
}

static BOOL is_current_place(const struct place *p)
{
    if (p->kind != PLACE_ITEM || !current) return FALSE;
    if (ILIsEqual(p->pidl, current)) return TRUE;
    return p->path[0] && current_path[0] && !lstrcmpiW(p->path, current_path);
}

static void add_place_csidl(const WCHAR *name, int csidl)
{
    PIDLIST_ABSOLUTE pidl = NULL;
    /* the user folders may not exist yet in a new container */
    if (SUCCEEDED(SHGetFolderLocation(NULL, csidl | CSIDL_FLAG_CREATE, NULL, 0, &pidl))) add_place_pidl(name, pidl);
}

static void add_place_path(const WCHAR *name, const WCHAR *path)
{
    PIDLIST_ABSOLUTE pidl = NULL;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
    if (SUCCEEDED(SHParseDisplayName(path, NULL, &pidl, 0, NULL))) add_place_pidl(name, pidl);
}

static void add_header(const WCHAR *name, enum place_kind kind)
{
    struct place *p;
    if (place_count >= MAX_PLACES) return;
    p = &places[place_count++];
    ZeroMemory(p, sizeof(*p));
    lstrcpynW(p->name, name, ARRAY_SIZE(p->name));
    p->kind = kind;
}

/* "Local Disk (C:)", or for a folder Winlator maps as a drive its Android name ("Download (D:)") */
static void drive_label(int index, WCHAR *name, int size)
{
    WCHAR root[4] = {'A' + index, ':', '\\', 0}, label[MAX_PATH] = L"", text[MAX_PATH + 16];
    char *(CDECL *unix_name)(const WCHAR *) =
        (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name");

    if (index == 2) {
        lstrcpynW(name, TR(L"로컬 디스크 (C:)", L"Local Disk (C:)"), size);
        return;
    }
    if (index == 'Z' - 'A') {
        lstrcpynW(name, TR(L"Linux 시스템 (Z:)", L"Linux (Z:)"), size);
        return;
    }
    {
        char *path = unix_name ? unix_name(root) : NULL;
        if (path) {
            char *end = path + strlen(path), *start;
            while (end > path && end[-1] == '/') *--end = 0;
            start = strrchr(path, '/');
            MultiByteToWideChar(CP_UTF8, 0, start ? start + 1 : path, -1, label, MAX_PATH);
            HeapFree(GetProcessHeap(), 0, path);
        }
    }
    if (!label[0]) GetVolumeInformationW(root, label, MAX_PATH, NULL, NULL, NULL, NULL, 0);
    if (label[0]) wsprintfW(text, L"%.60s (%c:)", label, 'A' + index);
    else wsprintfW(text, L"%s (%c:)", TR(L"드라이브", L"Drive"), 'A' + index);
    lstrcpynW(name, text, size);
}

/* The shell view's names for drives, as in the navigation pane (s11shellko.h asks) */
static BOOL drive_item_name(const WCHAR *parsing, WCHAR *text, int size)
{
    if (!parsing[0] || parsing[1] != ':' || parsing[2] != '\\' || parsing[3]) return FALSE;
    drive_label(towupper(parsing[0]) - 'A', text, size);
    return TRUE;
}

static void add_drive(int index)
{
    WCHAR root[4] = {'A' + index, ':', '\\', 0}, name[96];
    PIDLIST_ABSOLUTE pidl = NULL;

    if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) return;
    drive_label(index, name, ARRAY_SIZE(name));
    if (SUCCEEDED(SHParseDisplayName(root, NULL, &pidl, 0, NULL))) {
        add_place_pidl(name, pidl);
        places[place_count - 1].indent = 1;
    }
}

/* The names Windows 11 uses: "This PC", "Local Disk (C:)"; Wine's otherwise */
static void display_name(PCIDLIST_ABSOLUTE pidl, WCHAR *out, int size)
{
    WCHAR path[MAX_PATH], *name = NULL;
    int i;
    out[0] = 0;
    if (this_pc && ILIsEqual(pidl, this_pc)) {
        lstrcpynW(out, TR(L"내 PC", L"This PC"), size);
        return;
    }
    if (folder_path(pidl, path)) {
        if (path[1] == ':' && path[2] == '\\' && !path[3]) {
            drive_label(towupper(path[0]) - 'A', out, size);
            return;
        }
        /* the user folders by the names in the navigation pane ("문서", not "Documents") */
        for (i = 0; i < place_count; i++) {
            if (places[i].kind == PLACE_ITEM && !lstrcmpiW(places[i].path, path)) {
                lstrcpynW(out, places[i].name, size);
                return;
            }
        }
        if (path[1] == ':' && !lstrcmpiW(path + 2, L"\\users")) {
            lstrcpynW(out, TR(L"사용자", L"Users"), size);
            return;
        }
    }
    if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name)) && name) {
        lstrcpynW(out, name, size);
        CoTaskMemFree(name);
    }
}

static void build_places(void)
{
    WCHAR profile[MAX_PATH], path[MAX_PATH];
    DWORD drives = GetLogicalDrives();
    int i;

    add_header(TR(L"즐겨찾기", L"Quick access"), PLACE_HEADER);
    add_place_csidl(TR(L"바탕 화면", L"Desktop"), CSIDL_DESKTOPDIRECTORY);
    if (SHGetSpecialFolderPathW(NULL, profile, CSIDL_PROFILE, FALSE)) {
        wsprintfW(path, L"%s\\Downloads", profile);
        CreateDirectoryW(path, NULL);
        add_place_path(TR(L"다운로드", L"Downloads"), path);
    }
    add_place_csidl(TR(L"문서", L"Documents"), CSIDL_PERSONAL);
    add_place_csidl(TR(L"사진", L"Pictures"), CSIDL_MYPICTURES);
    add_place_csidl(TR(L"음악", L"Music"), CSIDL_MYMUSIC);
    add_place_csidl(TR(L"동영상", L"Videos"), CSIDL_MYVIDEO);

    add_header(L"", PLACE_GAP);
    if (this_pc) add_place_pidl(TR(L"내 PC", L"This PC"), ILClone(this_pc));
    for (i = 2; i < 26; i++)        /* C: and up; Z: is the Linux side of Winlator */
        if ((drives & (1u << i)) && i != 'Z' - 'A') add_drive(i);
}

static void nav_layout(void)
{
    int i, y = SC(TOP_H + CMD_H) + SC(8);
    for (i = 0; i < place_count; i++) {
        int h = places[i].kind == PLACE_HEADER ? SC(34) : places[i].kind == PLACE_GAP ? SC(14) : SC(36);
        SetRect(&places[i].rc, SC(8), y, SC(NAV_W) - SC(8), y + h);
        y += h;
    }
}

static int nav_hit(POINT pt)
{
    int i;
    for (i = 0; i < place_count; i++)
        if (places[i].kind == PLACE_ITEM && PtInRect(&places[i].rc, pt)) return i;
    return -1;
}

/* ---------------------------------------------------------------------------------------- */
/* Address bar: the folder's path as clickable names                                         */
/* ---------------------------------------------------------------------------------------- */

#define MAX_CRUMBS 12
static struct { WCHAR name[96]; PIDLIST_ABSOLUTE pidl; RECT rc; } crumbs[MAX_CRUMBS];
static int crumb_count, hot_crumb = -1;
static RECT address_rc;

static void free_crumbs(void)
{
    int i;
    for (i = 0; i < crumb_count; i++) ILFree(crumbs[i].pidl);
    crumb_count = 0;
}

static void build_crumbs(void)
{
    PIDLIST_ABSOLUTE chain[MAX_CRUMBS * 2], pidl;
    int n = 0, i;

    free_crumbs();
    if (!current) return;
    pidl = ILClone(current);
    while (pidl && pidl->mkid.cb && n < ARRAY_SIZE(chain)) {
        chain[n++] = ILClone(pidl);
        ILRemoveLastID(pidl);
    }
    ILFree(pidl);
    /* the last MAX_CRUMBS levels, outermost first */
    for (i = min(n, MAX_CRUMBS) - 1; i >= 0; i--) {
        crumbs[crumb_count].pidl = chain[i];
        chain[i] = NULL;
        display_name(crumbs[crumb_count].pidl, crumbs[crumb_count].name, ARRAY_SIZE(crumbs[crumb_count].name));
        crumb_count++;
    }
    for (i = 0; i < n; i++) if (chain[i]) ILFree(chain[i]);
}

static int text_width(HDC dc, HFONT font, const WCHAR *text)
{
    SIZE size = {0};
    HFONT old = SelectObject(dc, font);
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    SelectObject(dc, old);
    return size.cx;
}

static void layout_crumbs(HDC dc)
{
    int i, x = address_rc.left + SC(36), first = 0, total = 0, room = address_rc.right - SC(12) - x;
    int widths[MAX_CRUMBS];
    for (i = 0; i < crumb_count; i++) {
        widths[i] = text_width(dc, ui_font, crumbs[i].name) + SC(14);
        total += widths[i] + SC(18);
    }
    /* drop the outer levels that don't fit */
    while (first < crumb_count - 1 && total > room) total -= widths[first++] + SC(18);
    for (i = 0; i < crumb_count; i++) SetRectEmpty(&crumbs[i].rc);
    for (i = first; i < crumb_count; i++) {
        SetRect(&crumbs[i].rc, x, address_rc.top + SC(5), x + widths[i], address_rc.bottom - SC(5));
        x += widths[i] + SC(18);
    }
}

static void show_address_edit(void)
{
    WCHAR *text = NULL;
    if (current && SUCCEEDED(SHGetNameFromIDList(current, SIGDN_DESKTOPABSOLUTEEDITING, &text)) && text) {
        SetWindowTextW(address_edit, text);
        CoTaskMemFree(text);
    }
    SetWindowPos(address_edit, HWND_TOP, address_rc.left + SC(12), address_rc.top + SC(9),
                 address_rc.right - address_rc.left - SC(24), address_rc.bottom - address_rc.top - SC(18), SWP_SHOWWINDOW);
    SetFocus(address_edit);
    SendMessageW(address_edit, EM_SETSEL, 0, -1);
}

static void hide_address_edit(void)
{
    if (!IsWindowVisible(address_edit)) return;
    ShowWindow(address_edit, SW_HIDE);
    InvalidateRect(main_window, &address_rc, FALSE);
}

static LRESULT CALLBACK address_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        WCHAR path[MAX_PATH * 2];
        GetWindowTextW(hwnd, path, ARRAY_SIZE(path));
        if (navigate_path(path)) {
            hide_address_edit();
            SetFocus(view_window());
        }
        else MessageBeep(MB_ICONWARNING);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        hide_address_edit();
        SetFocus(view_window());
        return 0;
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == 27)) return 0;
    if (msg == WM_KILLFOCUS) hide_address_edit();
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------------------------- */
/* Buttons: navigation and command bar                                                       */
/* ---------------------------------------------------------------------------------------- */

enum button_id {
    BTN_BACK, BTN_FORWARD, BTN_UP, BTN_REFRESH,
    BTN_NEW, BTN_CUT, BTN_COPY, BTN_PASTE, BTN_RENAME, BTN_DELETE, BTN_SORT, BTN_VIEW, BTN_COUNT
};

static RECT buttons[BTN_COUNT];
static int hot_button = -1, pressed_button = -1;
static UINT count_all, count_selected;

static const WCHAR *button_label(int id)
{
    switch (id) {
    case BTN_NEW: return TR(L"새 폴더", L"New folder");
    case BTN_CUT: return TR(L"잘라내기", L"Cut");
    case BTN_COPY: return TR(L"복사", L"Copy");
    case BTN_PASTE: return TR(L"붙여넣기", L"Paste");
    case BTN_RENAME: return TR(L"이름 바꾸기", L"Rename");
    case BTN_DELETE: return TR(L"삭제", L"Delete");
    case BTN_SORT: return TR(L"정렬", L"Sort");
    case BTN_VIEW: return TR(L"보기", L"View");
    case BTN_BACK: return TR(L"뒤로", L"Back");
    case BTN_FORWARD: return TR(L"앞으로", L"Forward");
    case BTN_UP: return TR(L"위로", L"Up");
    case BTN_REFRESH: return TR(L"새로 고침", L"Refresh");
    }
    return L"";
}

static BOOL button_enabled(int id)
{
    switch (id) {
    case BTN_BACK: return history_pos > 0;
    case BTN_FORWARD: return history_pos < history_count - 1;
    case BTN_UP: return can_go_up();
    case BTN_CUT: case BTN_COPY: case BTN_DELETE: return count_selected > 0;
    case BTN_RENAME: return count_selected == 1;
    case BTN_PASTE: return IsClipboardFormatAvailable(CF_HDROP);
    default: return TRUE;
    }
}

static void layout(void)
{
    RECT client;
    HDC dc;
    int x, i;

    GetClientRect(main_window, &client);
    for (i = BTN_BACK; i <= BTN_REFRESH; i++) {
        x = SC(8) + i * SC(40);
        SetRect(&buttons[i], x, SC(8), x + SC(36), SC(TOP_H) - SC(8));
    }
    SetRect(&address_rc, SC(8) + 4 * SC(40) + SC(8), SC(8), client.right - SC(16), SC(TOP_H) - SC(8));

    dc = GetDC(main_window);
    x = SC(12);
    for (i = BTN_NEW; i < BTN_COUNT; i++) {
        int w = SC(28) + text_width(dc, small_font, button_label(i)) + SC(16) + (i >= BTN_SORT ? SC(12) : 0);
        if (i == BTN_CUT || i == BTN_SORT) x += SC(12);      /* separators */
        SetRect(&buttons[i], x, SC(TOP_H) + SC(6), x + w, SC(TOP_H + CMD_H) - SC(6));
        x += w + SC(4);
    }
    layout_crumbs(dc);
    ReleaseDC(main_window, dc);
    nav_layout();

    if (browser) {
        RECT rc = {SC(NAV_W), SC(TOP_H + CMD_H), client.right, client.bottom - SC(STATUS_H)};
        IExplorerBrowser_SetRect(browser, NULL, rc);
    }
    if (IsWindowVisible(address_edit))
        SetWindowPos(address_edit, NULL, address_rc.left + SC(12), address_rc.top + SC(9),
                     address_rc.right - address_rc.left - SC(24), address_rc.bottom - address_rc.top - SC(18), SWP_NOZORDER);
}

/* Line icons in the style of Windows 11's command bar */
static void draw_button_glyph(struct canvas *c, int id, float cx, float cy, float s, COLORREF color)
{
    GpPen *pen = make_pen(argb(color, 255), max(1.0f, s * 0.085f));
    switch (id) {
    case BTN_BACK: case BTN_FORWARD: {
        float d = id == BTN_BACK ? -1.0f : 1.0f;
        GdipDrawLine(c->g, pen, cx - d * s * 0.36f, cy, cx + d * s * 0.36f, cy);
        GdipDrawLine(c->g, pen, cx + d * s * 0.36f, cy, cx + d * s * 0.04f, cy - s * 0.32f);
        GdipDrawLine(c->g, pen, cx + d * s * 0.36f, cy, cx + d * s * 0.04f, cy + s * 0.32f);
        break;
    }
    case BTN_UP:
        GdipDrawLine(c->g, pen, cx, cy + s * 0.36f, cx, cy - s * 0.36f);
        GdipDrawLine(c->g, pen, cx, cy - s * 0.36f, cx - s * 0.32f, cy - s * 0.04f);
        GdipDrawLine(c->g, pen, cx, cy - s * 0.36f, cx + s * 0.32f, cy - s * 0.04f);
        break;
    case BTN_REFRESH:
        GdipDrawArc(c->g, pen, cx - s * 0.32f, cy - s * 0.32f, s * 0.64f, s * 0.64f, -60, 300);
        GdipDrawLine(c->g, pen, cx + s * 0.16f, cy - s * 0.28f, cx + s * 0.18f, cy - s * 0.46f);
        GdipDrawLine(c->g, pen, cx + s * 0.16f, cy - s * 0.28f, cx + s * 0.34f, cy - s * 0.22f);
        break;
    case BTN_NEW: {
        GpPointF folder[] = {{cx - s * 0.40f, cy + s * 0.30f}, {cx - s * 0.40f, cy - s * 0.30f}, {cx - s * 0.12f, cy - s * 0.30f},
                             {cx - s * 0.04f, cy - s * 0.20f}, {cx + s * 0.40f, cy - s * 0.20f}, {cx + s * 0.40f, cy + s * 0.30f}};
        GdipDrawPolygon(c->g, pen, folder, ARRAY_SIZE(folder));
        GdipDrawLine(c->g, pen, cx, cy - s * 0.06f, cx, cy + s * 0.22f);
        GdipDrawLine(c->g, pen, cx - s * 0.14f, cy + s * 0.08f, cx + s * 0.14f, cy + s * 0.08f);
        break;
    }
    case BTN_CUT:
        GdipDrawEllipse(c->g, pen, cx - s * 0.40f, cy + s * 0.10f, s * 0.28f, s * 0.28f);
        GdipDrawEllipse(c->g, pen, cx + s * 0.12f, cy + s * 0.10f, s * 0.28f, s * 0.28f);
        GdipDrawLine(c->g, pen, cx - s * 0.16f, cy + s * 0.12f, cx + s * 0.22f, cy - s * 0.40f);
        GdipDrawLine(c->g, pen, cx + s * 0.16f, cy + s * 0.12f, cx - s * 0.22f, cy - s * 0.40f);
        break;
    case BTN_COPY:
        GdipDrawRectangle(c->g, pen, cx - s * 0.20f, cy - s * 0.38f, s * 0.56f, s * 0.62f);
        GdipDrawLine(c->g, pen, cx - s * 0.36f, cy - s * 0.18f, cx - s * 0.36f, cy + s * 0.40f);
        GdipDrawLine(c->g, pen, cx - s * 0.36f, cy + s * 0.40f, cx + s * 0.14f, cy + s * 0.40f);
        break;
    case BTN_PASTE:
        GdipDrawRectangle(c->g, pen, cx - s * 0.32f, cy - s * 0.30f, s * 0.64f, s * 0.70f);
        GdipDrawRectangle(c->g, pen, cx - s * 0.14f, cy - s * 0.42f, s * 0.28f, s * 0.16f);
        break;
    case BTN_RENAME:
        GdipDrawRectangle(c->g, pen, cx - s * 0.42f, cy - s * 0.18f, s * 0.62f, s * 0.36f);
        GdipDrawLine(c->g, pen, cx + s * 0.32f, cy - s * 0.36f, cx + s * 0.32f, cy + s * 0.36f);
        GdipDrawLine(c->g, pen, cx + s * 0.22f, cy - s * 0.36f, cx + s * 0.42f, cy - s * 0.36f);
        GdipDrawLine(c->g, pen, cx + s * 0.22f, cy + s * 0.36f, cx + s * 0.42f, cy + s * 0.36f);
        break;
    case BTN_DELETE:
        GdipDrawLine(c->g, pen, cx - s * 0.38f, cy - s * 0.26f, cx + s * 0.38f, cy - s * 0.26f);
        GdipDrawLine(c->g, pen, cx - s * 0.12f, cy - s * 0.38f, cx + s * 0.12f, cy - s * 0.38f);
        GdipDrawLine(c->g, pen, cx - s * 0.28f, cy - s * 0.26f, cx - s * 0.22f, cy + s * 0.40f);
        GdipDrawLine(c->g, pen, cx - s * 0.22f, cy + s * 0.40f, cx + s * 0.22f, cy + s * 0.40f);
        GdipDrawLine(c->g, pen, cx + s * 0.22f, cy + s * 0.40f, cx + s * 0.28f, cy - s * 0.26f);
        break;
    case BTN_SORT:
        GdipDrawLine(c->g, pen, cx - s * 0.20f, cy - s * 0.38f, cx - s * 0.20f, cy + s * 0.38f);
        GdipDrawLine(c->g, pen, cx - s * 0.20f, cy + s * 0.38f, cx - s * 0.36f, cy + s * 0.20f);
        GdipDrawLine(c->g, pen, cx + s * 0.20f, cy + s * 0.38f, cx + s * 0.20f, cy - s * 0.38f);
        GdipDrawLine(c->g, pen, cx + s * 0.20f, cy - s * 0.38f, cx + s * 0.36f, cy - s * 0.20f);
        break;
    case BTN_VIEW: {
        int i;
        for (i = 0; i < 3; i++) {
            float y = cy - s * 0.30f + i * s * 0.30f;
            GdipDrawLine(c->g, pen, cx - s * 0.38f, y, cx - s * 0.26f, y);
            GdipDrawLine(c->g, pen, cx - s * 0.12f, y, cx + s * 0.38f, y);
        }
        break;
    }
    }
    GdipDeletePen(pen);
}

/* ---------------------------------------------------------------------------------------- */
/* Painting                                                                                  */
/* ---------------------------------------------------------------------------------------- */

static void paint(HDC hdc)
{
    struct canvas c;
    RECT client, rc;
    int i;
    WCHAR status[128];

    GetClientRect(main_window, &client);
    if (!canvas_begin(&c, hdc, client.right, client.bottom)) return;
    fill_rect(&c, 0, 0, client.right, client.bottom, argb(pal.menu, 255));
    fill_rect(&c, 0, 0, client.right, SC(TOP_H + CMD_H), argb(pal.bar, 255));
    fill_rect(&c, 0, SC(TOP_H + CMD_H) - 1, client.right, 1, argb(pal.bar_border, 255));

    /* navigation buttons */
    for (i = BTN_BACK; i <= BTN_REFRESH; i++) {
        BOOL enabled = button_enabled(i);
        if (enabled && i == pressed_button) fill_round_rect(&c, &buttons[i], (float)SC(4), pal.pressed);
        else if (enabled && i == hot_button) fill_round_rect(&c, &buttons[i], (float)SC(4), pal.hover);
        draw_button_glyph(&c, i, (buttons[i].left + buttons[i].right) / 2.0f, (buttons[i].top + buttons[i].bottom) / 2.0f,
                          (float)SC(18), enabled ? pal.text : pal.indicator);
    }

    /* address bar */
    fill_round_rect(&c, &address_rc, (float)SC(4), argb(pal.field, 255));
    stroke_round_rect(&c, &address_rc, (float)SC(4), argb(pal.field_border, 255), 1);
    if (!IsWindowVisible(address_edit)) {
        if (crumb_count) {
            /* the folder's own icon, then the names */
            SHFILEINFOW info = {0};
            if (SHGetFileInfoW((const WCHAR *)current, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON)) {
                draw_icon(&c, info.hIcon, address_rc.left + SC(10), (address_rc.top + address_rc.bottom) / 2 - SC(8), SC(16));
                DestroyIcon(info.hIcon);
            }
        }
        for (i = 0; i < crumb_count; i++) {
            RECT chevron;
            if (IsRectEmpty(&crumbs[i].rc)) continue;
            if (i == hot_crumb) fill_round_rect(&c, &crumbs[i].rc, (float)SC(4), pal.hover);
            rc = crumbs[i].rc;
            draw_text(&c, crumbs[i].name, &rc, ui_font, pal.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (i == crumb_count - 1) break;
            SetRect(&chevron, rc.right, rc.top, rc.right + SC(18), rc.bottom);
            draw_text(&c, L"›", &chevron, ui_font, pal.text_dim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    /* command bar */
    for (i = BTN_NEW; i < BTN_COUNT; i++) {
        BOOL enabled = button_enabled(i);
        COLORREF color = enabled ? pal.text : pal.indicator;
        rc = buttons[i];
        if (enabled && i == pressed_button) fill_round_rect(&c, &rc, (float)SC(4), pal.pressed);
        else if (enabled && i == hot_button) fill_round_rect(&c, &rc, (float)SC(4), pal.hover);
        draw_button_glyph(&c, i, rc.left + SC(18), (rc.top + rc.bottom) / 2.0f, (float)SC(18), color);
        rc.left += SC(34);
        draw_text(&c, button_label(i), &rc, small_font, color, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (i >= BTN_SORT) {   /* a drop-down chevron */
            GpPen *pen = make_pen(argb(color, 255), max(1.0f, SC(1) * 1.2f));
            float x = (float)(buttons[i].right - SC(12)), y = (buttons[i].top + buttons[i].bottom) / 2.0f;
            GdipDrawLine(c.g, pen, x - SC(4), y - SC(2), x, y + SC(2));
            GdipDrawLine(c.g, pen, x, y + SC(2), x + SC(4), y - SC(2));
            GdipDeletePen(pen);
        }
        if (i == BTN_NEW || i == BTN_DELETE)
            fill_rect(&c, buttons[i].right + SC(8), buttons[i].top + SC(6), 1, buttons[i].bottom - buttons[i].top - SC(12),
                      argb(pal.bar_border, 255));
    }

    /* navigation pane */
    for (i = 0; i < place_count; i++) {
        struct place *p = &places[i];
        rc = p->rc;
        if (p->kind == PLACE_GAP) {
            fill_rect(&c, rc.left + SC(12), (rc.top + rc.bottom) / 2, rc.right - rc.left - SC(24), 1, argb(pal.bar_border, 255));
            continue;
        }
        if (p->kind == PLACE_HEADER) {
            rc.left += SC(12);
            rc.top += SC(8);
            draw_text(&c, p->name, &rc, nav_header_font, pal.text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            continue;
        }
        if (is_current_place(p)) {
            RECT bar = {rc.left, rc.top + SC(9), rc.left + SC(3), rc.bottom - SC(9)};
            fill_round_rect(&c, &rc, (float)SC(4), pal.active);
            fill_round_rect(&c, &bar, SC(3) / 2.0f, argb(pal.accent, 255));
        }
        else if (i == hot_place) fill_round_rect(&c, &rc, (float)SC(4), pal.hover);
        rc.left += p->indent * SC(18);
        draw_icon(&c, p->icon, rc.left + SC(14), (rc.top + rc.bottom) / 2 - SC(8), SC(16));
        rc.left += SC(40);
        rc.right -= SC(6);
        draw_text(&c, p->name, &rc, nav_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    fill_rect(&c, SC(NAV_W) - 1, SC(TOP_H + CMD_H), 1, client.bottom - SC(TOP_H + CMD_H), argb(pal.bar_border, 255));

    /* status bar */
    if (count_selected)
        wsprintfW(status, TR(L"항목 %u개    %u개 선택함", count_all == 1 ? L"%u item    %u selected" : L"%u items    %u selected"),
                  count_all, count_selected);
    else wsprintfW(status, TR(L"항목 %u개", count_all == 1 ? L"%u item" : L"%u items"), count_all);
    SetRect(&rc, SC(NAV_W) + SC(12), client.bottom - SC(STATUS_H), client.right, client.bottom);
    draw_text(&c, status, &rc, small_font, pal.text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    canvas_end(&c, hdc, 0, 0);
}

/* ---------------------------------------------------------------------------------------- */
/* Window                                                                                    */
/* ---------------------------------------------------------------------------------------- */

static void update_title(void)
{
    WCHAR name[128] = L"", title[200];
    if (current) display_name(current, name, ARRAY_SIZE(name));
    if (name[0]) wsprintfW(title, L"%s - %s", name, TR(L"파일 탐색기", L"File Explorer"));
    else lstrcpyW(title, TR(L"파일 탐색기", L"File Explorer"));
    SetWindowTextW(main_window, title);
}

static void show_menu(int id)
{
    HMENU menu = CreatePopupMenu();
    POINT pt = {buttons[id].left, buttons[id].bottom};
    int cmd;
    if (id == BTN_VIEW) {
        AppendMenuW(menu, MF_STRING, FCIDM_SHVIEW_BIGICON, TR(L"큰 아이콘", L"Large icons"));
        AppendMenuW(menu, MF_STRING, FCIDM_SHVIEW_SMALLICON, TR(L"작은 아이콘", L"Small icons"));
        AppendMenuW(menu, MF_STRING, FCIDM_SHVIEW_LISTVIEW, TR(L"목록", L"List"));
        AppendMenuW(menu, MF_STRING, FCIDM_SHVIEW_REPORTVIEW, TR(L"자세히", L"Details"));
        if (current_view_command())
            CheckMenuRadioItem(menu, FCIDM_SHVIEW_BIGICON, FCIDM_SHVIEW_REPORTVIEW, current_view_command(), MF_BYCOMMAND);
    }
    else {
        /* the shell view's sort commands are 0x30 to 0x33 (name, size, type, date) */
        AppendMenuW(menu, MF_STRING, 0x30, TR(L"이름", L"Name"));
        AppendMenuW(menu, MF_STRING, 0x31, TR(L"크기", L"Size"));
        AppendMenuW(menu, MF_STRING, 0x32, TR(L"유형", L"Type"));
        AppendMenuW(menu, MF_STRING, 0x33, TR(L"수정한 날짜", L"Date modified"));
    }
    ClientToScreen(main_window, &pt);
    cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, main_window, NULL);
    DestroyMenu(menu);
    if (cmd) view_command(cmd);
}

static void press_button(int id)
{
    if (!button_enabled(id)) return;
    switch (id) {
    case BTN_BACK: go_back(); break;
    case BTN_FORWARD: go_forward(); break;
    case BTN_UP: go_up(); break;
    case BTN_REFRESH: refresh(); break;
    case BTN_NEW: invoke(TRUE, CMDSTR_NEWFOLDERA); break;
    case BTN_CUT: invoke(FALSE, "cut"); break;
    case BTN_COPY: invoke(FALSE, "copy"); break;
    case BTN_PASTE: invoke(TRUE, "paste"); break;
    case BTN_RENAME: rename_selected(); break;
    case BTN_DELETE: invoke(FALSE, "delete"); break;
    case BTN_SORT: case BTN_VIEW: show_menu(id); break;
    }
    InvalidateRect(main_window, NULL, FALSE);
}

static int hit_button(POINT pt)
{
    int i;
    for (i = 0; i < BTN_COUNT; i++) if (PtInRect(&buttons[i], pt)) return i;
    return -1;
}

static int hit_crumb(POINT pt)
{
    int i;
    if (IsWindowVisible(address_edit)) return -1;
    for (i = 0; i < crumb_count; i++) if (PtInRect(&crumbs[i].rc, pt)) return i;
    return -1;
}

static void update_counts(void)
{
    UINT all, selected;
    item_counts(&all, &selected);
    if (all != count_all || selected != count_selected) {
        if (all != count_all && count_all != (UINT)-1) fit_columns(TRUE);
        count_all = all;
        count_selected = selected;
        InvalidateRect(main_window, NULL, FALSE);
    }
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        FOLDERSETTINGS settings = {FVM_DETAILS, FWF_NOWEBVIEW};
        RECT rc = {0, 0, 100, 100};
        main_window = hwnd;
        address_edit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd, (HMENU)1, instance, NULL);
        SendMessageW(address_edit, WM_SETFONT, (WPARAM)ui_font, FALSE);
        SetWindowSubclass(address_edit, address_proc, 1, 0);
        build_places();
        if (FAILED(CoCreateInstance(&clsid_explorer_browser, NULL, CLSCTX_INPROC_SERVER, &iid_explorer_browser,
                                    (void **)&browser)))
            return -1;
        IExplorerBrowser_Initialize(browser, hwnd, &rc, &settings);
        IExplorerBrowser_SetOptions(browser, EBO_NOBORDER);
        IExplorerBrowser_Advise(browser, &events, &advise_cookie);
        SetTimer(hwnd, 1, 400, NULL);
        return 0;
    }
    case WM_SIZE:
        layout();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = SC(640);
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = SC(400);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT: {
        static HBRUSH brush;
        static COLORREF brush_color;
        if (!brush || brush_color != pal.field) {
            if (brush) DeleteObject(brush);
            brush = CreateSolidBrush(pal.field);
            brush_color = pal.field;
        }
        SetTextColor((HDC)wp, pal.text);
        SetBkColor((HDC)wp, pal.field);
        return (LRESULT)brush;
    }
    case WM_APP_NAVIGATED: {
        HDC dc = GetDC(hwnd);
        HWND focus = GetFocus();
        folder_path(current, current_path);
        build_crumbs();
        style_shell_view();
        layout_crumbs(dc);
        ReleaseDC(hwnd, dc);
        hide_address_edit();
        update_title();
        fit_columns(FALSE);
        count_all = count_selected = (UINT)-1;
        update_counts();
        /* the old view had the keyboard and is gone: the new one gets it, as in Windows */
        if (GetActiveWindow() == hwnd && (!focus || focus == hwnd || !IsChild(hwnd, focus))) SetFocus(view_window());
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_SETFOCUS: {
        HWND view = view_window();
        if (view) SetFocus(view);
        return 0;
    }
    case WM_TIMER:
        update_counts();
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int b = hit_button(pt), p = nav_hit(pt), cr = hit_crumb(pt);
        if (b != hot_button || p != hot_place || cr != hot_crumb) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            hot_button = b;
            hot_place = p;
            hot_crumb = cr;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        hot_button = hot_place = hot_crumb = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        pressed_button = hit_button(pt);
        if (pressed_button >= 0) {
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int b = hit_button(pt), pressed = pressed_button, p, cr;
        pressed_button = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (pressed >= 0) {
            if (b == pressed) press_button(b);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if ((p = nav_hit(pt)) >= 0) navigate_pidl(places[p].pidl);
        else if ((cr = hit_crumb(pt)) >= 0) {
            if (cr == crumb_count - 1) show_address_edit();
            else navigate_pidl(crumbs[cr].pidl);
        }
        else if (PtInRect(&address_rc, pt)) show_address_edit();
        return 0;
    }
    case WM_XBUTTONUP:
        if (GET_XBUTTON_WPARAM(wp) == XBUTTON1) go_back();
        else go_forward();
        return TRUE;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        choose_palette();
        style_shell_view();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_DESTROY:
        if (browser) {
            IExplorerBrowser_Unadvise(browser, advise_cookie);
            IExplorerBrowser_Destroy(browser);
            IExplorerBrowser_Release(browser);
            browser = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Keys the shell view leaves alone; FALSE when the message should go on as usual */
static BOOL handle_key(const MSG *msg)
{
    WCHAR cls[32] = L"";
    BOOL ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    HWND focus = GetFocus();

    /* WM_SYSKEYDOWN without Alt: keys pressed while no window has the focus */
    if (msg->message == WM_SYSKEYDOWN && (msg->lParam & (1 << 29))) {
        if (msg->wParam == VK_LEFT) { go_back(); return TRUE; }
        if (msg->wParam == VK_RIGHT) { go_forward(); return TRUE; }
        if (msg->wParam == VK_UP) { go_up(); return TRUE; }
        if (msg->wParam == 'D') { show_address_edit(); return TRUE; }
        return FALSE;
    }
    if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN) return FALSE;
    if (msg->wParam == VK_F5) { refresh(); return TRUE; }
    if (msg->wParam == VK_BROWSER_BACK) { go_back(); return TRUE; }
    if (msg->wParam == VK_BROWSER_FORWARD) { go_forward(); return TRUE; }
    if (!ctrl) return FALSE;
    if (focus) GetClassNameW(focus, cls, ARRAY_SIZE(cls));
    if (!lstrcmpiW(cls, WC_EDITW)) return FALSE;   /* typing a name or a path */
    switch (msg->wParam) {
    case 'C': if (button_enabled(BTN_COPY)) invoke(FALSE, "copy"); return TRUE;
    case 'X': if (button_enabled(BTN_CUT)) invoke(FALSE, "cut"); return TRUE;
    case 'V': if (button_enabled(BTN_PASTE)) invoke(TRUE, "paste"); return TRUE;
    case 'A': select_all(); return TRUE;
    case 'L': show_address_edit(); return TRUE;
    case 'W': DestroyWindow(main_window); return TRUE;
    case 'N': if (shift) { invoke(TRUE, CMDSTR_NEWFOLDERA); return TRUE; } break;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, WCHAR *cmdline, int show)
{
    struct GdiplusStartupInput gdiplus_input = {1};
    WNDCLASSEXW wc = {sizeof(wc)};
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES};
    MSG msg;
    RECT work;
    int w, h;
    WCHAR path[MAX_PATH * 2];

    instance = inst;
    SetProcessDPIAware();
    dpi = GetDpiForSystem();
    korean = TRUE; /* S11 is a Korean-first workspace even if Wine's UI locale remains en-US. */
    /* what Wine's shell view and menus leave English (file types, "New", some verbs) */
    if (korean) {
        shellko_item_name = drive_item_name;
        shellko_install();
    }
    choose_palette();
    GdiplusStartup(&gdiplus_token, &gdiplus_input, NULL);
    OleInitialize(NULL);
    InitCommonControlsEx(&icc);

    ui_font = make_font(100, FW_NORMAL);
    small_font = make_font(90, FW_NORMAL);
    nav_font = make_font(95, FW_NORMAL);
    nav_header_font = make_font(90, FW_SEMIBOLD);

    SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &this_pc);

    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"S11Explorer";
    RegisterClassExW(&wc);

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    w = min(SC(1040), work.right - work.left);
    h = min(SC(680), work.bottom - work.top - SC(72));
    {
        /* a few windows open side by side, not on top of each other */
        int step = (int)(GetTickCount() / 7 % 5) * SC(24);
        int x = min((work.right - w) / 2 + step, work.right - w);
        int y = min(max(0, (work.bottom - SC(72) - h) / 2) + step, max(0, work.bottom - SC(72) - h));
        if (!CreateWindowExW(0, L"S11Explorer", TR(L"파일 탐색기", L"File Explorer"), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             x, y, w, h, NULL, NULL, inst, NULL))
            return 1;
    }
    ShowWindow(main_window, SW_SHOWNORMAL);
    UpdateWindow(main_window);

    /* a folder from the command line ("C:\\...", "%USERPROFILE%", quoted or not), else This PC */
    lstrcpynW(path, cmdline, ARRAY_SIZE(path));
    if (path[0] == '"') {
        WCHAR *end = wcschr(path + 1, '"');
        memmove(path, path + 1, (lstrlenW(path) + 1) * sizeof(WCHAR));
        if (end) *(end - 1) = 0;
    }
    if (!path[0] || !navigate_path(path)) navigate_pidl(this_pc);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (handle_key(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    OleUninitialize();
    GdiplusShutdown(gdiplus_token);
    return 0;
}

void __cdecl entry(void)
{
    WCHAR *cmd = GetCommandLineW();
    BOOL quoted = FALSE;
    while (*cmd && (quoted || (*cmd != ' ' && *cmd != '\t'))) {
        if (*cmd == '"') quoted = !quoted;
        cmd++;
    }
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    ExitProcess(wWinMain(GetModuleHandleW(NULL), NULL, cmd, SW_SHOWNORMAL));
}
