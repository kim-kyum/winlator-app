/*
 * Winlator S11 shell: a Windows 11 style taskbar and start menu for Wine's virtual desktop.
 *
 * Wine's own taskbar (explorer.exe) is a row of Windows 95 buttons that maximized windows
 * cover up. This replaces it (Winlator turns explorer's taskbar off for the "shell" desktop
 * and starts this program through winhandler.exe):
 *
 *  - centred app buttons with the window icon, a running/active indicator and tooltips;
 *  - notification area icons (the Shell_NotifyIcon protocol of Wine's shell32) and a clock;
 *  - a start menu with every installed program, search (also by Hangul initial consonants,
 *    e.g. "ㅋㅋㅇㅌ" finds 카카오톡) and Winlator's shut down / restart;
 *  - the Windows key opens the start menu; Win+D shows the desktop, Win+E the file manager,
 *    Win+I the settings;
 *  - maximized windows are kept above the taskbar, and the taskbar steps aside for
 *    fullscreen programs;
 *  - the icons sit in the middle or on the left (S11 Settings, Personalization).
 *
 * Built with clang against Wine's headers and import libraries (see build.sh).
 */
#include "s11ui.h"

static HINSTANCE instance;
static HWND taskbar, start_menu, search_edit, tooltip, hover_window, calendar;
static HFONT ui_font, small_font, menu_title_font, search_font, calendar_title_font;
static ULONG_PTR gdiplus_token;


#define WM_APP_TOGGLE_START (WM_APP + 1)
#define WM_APP_SHOW_DESKTOP (WM_APP + 2)
#define WM_APP_RUN (WM_APP + 3)
#define WM_APP_APPBAR (WM_APP + 4)
#define WM_APP_FILES (WM_APP + 5)
#define WM_APP_SETTINGS (WM_APP + 6)
#define WM_APP_TASK_MANAGER (WM_APP + 7)
#define WM_APP_SWITCH_TASK (WM_APP + 8)

static UINT settings_changed_msg;   /* "WinlatorS11SettingsChanged", posted by s11settings.exe */
static BOOL align_left;             /* taskbar icons on the left instead of in the middle */

static void read_settings(void)
{
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Winlator\\S11", L"TaskbarAlignment", RRF_RT_REG_DWORD, NULL,
                     &value, &size))
        value = 0;
    align_left = value == 1;
}

#define TIMER_POLL 1
#define TIMER_CLOCK 2
#define TIMER_TIP 3
#define TIMER_ANNOUNCE 4
#define TIMER_HOVER 5

/* ---------------------------------------------------------------------------------------- */
/* Windows on the taskbar                                                                   */
/* ---------------------------------------------------------------------------------------- */

#define MAX_TASKS 48

struct task {
    HWND hwnd;
    HICON icon;          /* belongs to the window or its class; not ours to destroy */
    WCHAR title[256];
    DWORD icon_tick;
    BOOL seen;
};

static struct task tasks[MAX_TASKS];
static int task_count;
static HWND active_task;
static HWND desktop_hidden[MAX_TASKS];
static int desktop_hidden_count;
static BOOL fullscreen_app;

static BOOL is_shell_window(HWND hwnd)
{
    WCHAR cls[64];
    if (hwnd == taskbar || hwnd == start_menu || hwnd == tooltip || hwnd == hover_window || hwnd == calendar) return TRUE;
    if (!GetClassNameW(hwnd, cls, ARRAY_SIZE(cls))) return TRUE;
    return !lstrcmpW(cls, L"Shell_TrayWnd") || !lstrcmpW(cls, L"Progman") || !lstrcmpW(cls, L"#32769") ||
           !lstrcmpW(cls, L"WorkerW") || !lstrcmpW(cls, L"tooltips_class32") || !lstrcmpW(cls, L"#32768");
}

/* The same rules as the Windows taskbar */
static BOOL is_task_window(HWND hwnd)
{
    LONG ex_style, style;
    HWND owner;
    RECT rc;

    if (!IsWindowVisible(hwnd) || is_shell_window(hwnd)) return FALSE;
    ex_style = GetWindowLongW(hwnd, GWL_EXSTYLE);
    style = GetWindowLongW(hwnd, GWL_STYLE);
    if (style & WS_CHILD) return FALSE;
    if (ex_style & WS_EX_APPWINDOW) return TRUE;
    if (ex_style & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return FALSE;
    owner = GetWindow(hwnd, GW_OWNER);
    if (owner && IsWindowVisible(owner)) return FALSE;
    GetWindowRect(hwnd, &rc);
    if ((rc.right - rc.left <= 1 || rc.bottom - rc.top <= 1) && !IsIconic(hwnd)) return FALSE;
    return TRUE;
}

/* Icons of program files, kept for the whole session (a handful of programs) */
#define MAX_EXE_ICONS 64
static struct { WCHAR path[MAX_PATH]; HICON icon; } exe_icons[MAX_EXE_ICONS];
static int exe_icon_count;

static HICON program_icon(HWND hwnd)
{
    DWORD pid = 0, size = MAX_PATH;
    WCHAR path[MAX_PATH];
    HANDLE process;
    HICON icon = NULL;
    int i;

    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !(process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))) return NULL;
    if (!QueryFullProcessImageNameW(process, 0, path, &size)) path[0] = 0;
    CloseHandle(process);
    if (!path[0]) return NULL;

    for (i = 0; i < exe_icon_count; i++) if (!lstrcmpiW(exe_icons[i].path, path)) return exe_icons[i].icon;
    PrivateExtractIconsW(path, 0, SC(32), SC(32), &icon, NULL, 1, 0);
    if (exe_icon_count < MAX_EXE_ICONS) {
        lstrcpyW(exe_icons[exe_icon_count].path, path);
        exe_icons[exe_icon_count++].icon = icon;
    }
    return icon;
}

static HICON window_icon(HWND hwnd)
{
    DWORD_PTR result = 0;
    HICON icon;
    /* Wine often supplies its generic legacy icon even when the executable has a newer one. */
    if ((icon = program_icon(hwnd))) return icon;
    if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 150, &result) && result)
        return (HICON)result;
    if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 150, &result) && result)
        return (HICON)result;
    if ((result = GetClassLongPtrW(hwnd, GCLP_HICON))) return (HICON)result;
    if ((result = GetClassLongPtrW(hwnd, GCLP_HICONSM))) return (HICON)result;
    return LoadIconW(NULL, (const WCHAR *)IDI_APPLICATION);
}

static struct task *find_task(HWND hwnd)
{
    int i;
    for (i = 0; i < task_count; i++) if (tasks[i].hwnd == hwnd) return &tasks[i];
    return NULL;
}

static BOOL CALLBACK collect_window(HWND hwnd, LPARAM param)
{
    BOOL *changed = (BOOL *)param;
    struct task *task;
    WCHAR title[256];

    if (!is_task_window(hwnd)) return TRUE;
    InternalGetWindowText(hwnd, title, ARRAY_SIZE(title));

    if (!(task = find_task(hwnd))) {
        if (task_count >= MAX_TASKS) return TRUE;
        task = &tasks[task_count++];
        ZeroMemory(task, sizeof(*task));
        task->hwnd = hwnd;
        *changed = TRUE;
    }
    task->seen = TRUE;
    if (lstrcmpW(task->title, title)) {
        lstrcpynW(task->title, title, ARRAY_SIZE(task->title));
        task->icon_tick = 0;
        *changed = TRUE;
    }
    if (!task->icon || GetTickCount() - task->icon_tick > 3000) {
        HICON icon = window_icon(hwnd);
        if (icon != task->icon) *changed = TRUE;
        task->icon = icon;
        task->icon_tick = GetTickCount();
    }
    return TRUE;
}

/* Returns TRUE when the set of buttons changed */
static BOOL scan_windows(void)
{
    BOOL changed = FALSE;
    HWND foreground;
    int i, j;

    for (i = 0; i < task_count; i++) tasks[i].seen = FALSE;
    EnumWindows(collect_window, (LPARAM)&changed);
    for (i = j = 0; i < task_count; i++) {
        if (tasks[i].seen) tasks[j++] = tasks[i];
        else changed = TRUE;
    }
    task_count = j;

    foreground = GetForegroundWindow();
    if (foreground == start_menu || foreground == taskbar || foreground == tooltip) foreground = active_task;
    else if (foreground) {
        HWND root = GetAncestor(foreground, GA_ROOTOWNER);
        if (find_task(root)) foreground = root;
    }
    if (active_task && !find_task(active_task) && foreground == active_task) foreground = NULL;
    if (foreground != active_task) {
        active_task = foreground;
        changed = TRUE;
    }
    return changed;
}

static void activate_task(HWND hwnd)
{
    if (IsIconic(hwnd)) {
        PostMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        SetForegroundWindow(hwnd);
        return;
    }
    if (hwnd == active_task && IsWindowEnabled(hwnd)) {
        PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        return;
    }
    if (!IsWindowEnabled(hwnd)) {
        /* a modal dialog owns the input: bring that up instead */
        HWND popup = GetLastActivePopup(hwnd);
        if (popup && popup != hwnd && IsWindowVisible(popup)) hwnd = popup;
    }
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
}

static void switch_task(int direction)
{
    int i, current = -1;
    if (task_count < 2) return;
    for (i = 0; i < task_count; i++) {
        if (tasks[i].hwnd == active_task) {
            current = i;
            break;
        }
    }
    if (current < 0) current = direction > 0 ? -1 : 0;
    i = (current + direction + task_count) % task_count;
    activate_task(tasks[i].hwnd);
}

static void toggle_show_desktop(void)
{
    int i;
    if (desktop_hidden_count) {
        for (i = 0; i < desktop_hidden_count; i++)
            if (IsWindow(desktop_hidden[i]) && IsIconic(desktop_hidden[i]))
                PostMessageW(desktop_hidden[i], WM_SYSCOMMAND, SC_RESTORE, 0);
        desktop_hidden_count = 0;
        return;
    }
    for (i = 0; i < task_count; i++) {
        if (IsIconic(tasks[i].hwnd)) continue;
        desktop_hidden[desktop_hidden_count++] = tasks[i].hwnd;
        PostMessageW(tasks[i].hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    }
}

static int screen_w, screen_h, bar_h;

/*
 * Wine's virtual desktop reports the whole screen as the work area, so maximized windows
 * would slide under the taskbar. Pull their bottom edge up instead.
 */
/* Windows we fitted lately: a program that maximizes itself again each time is left alone */
#define MAX_FITTED 16
static struct { HWND hwnd; DWORD first; int count; } fitted[MAX_FITTED];

static BOOL may_fit(HWND hwnd)
{
    DWORD now = GetTickCount();
    int i, slot = -1, oldest = 0;
    for (i = 0; i < MAX_FITTED; i++) {
        if (fitted[i].hwnd == hwnd) {
            slot = i;
            break;
        }
        if (!fitted[i].hwnd) {
            if (fitted[oldest].hwnd) oldest = i;      /* a free slot first */
        }
        else if (fitted[oldest].hwnd && now - fitted[i].first > now - fitted[oldest].first) oldest = i;
    }
    if (slot < 0) {
        slot = oldest;
        fitted[slot].hwnd = hwnd;
        fitted[slot].first = now;
        fitted[slot].count = 0;
    }
    else if (now - fitted[slot].first > 10000) {      /* a new round */
        fitted[slot].first = now;
        fitted[slot].count = 0;
    }
    return ++fitted[slot].count <= 3;
}

static void fit_maximized_windows(void)
{
    int i;
    if (fullscreen_app) return;
    for (i = 0; i < task_count; i++) {
        HWND hwnd = tasks[i].hwnd;
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        RECT rc;

        if (!(style & WS_MAXIMIZE) || (style & WS_CAPTION) != WS_CAPTION) continue;
        GetWindowRect(hwnd, &rc);
        if (rc.bottom <= screen_h - bar_h + SC(8)) continue;
        if (rc.bottom < screen_h - SC(2)) continue;  /* not a full-height maximized window */
        if (!may_fit(hwnd)) continue;
        SetWindowPos(hwnd, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top - bar_h,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }
}

/*
 * Winlator's Wine shows explorer's own taskbar on the "shell" desktop whatever the EnableShell
 * setting says. It sits under ours, but comes up over it when windows close; keep it hidden.
 */
static void hide_explorer_taskbar(void)
{
    HWND hwnd = NULL;
    while ((hwnd = FindWindowExW(NULL, hwnd, L"Shell_TrayWnd", NULL))) {
        if (hwnd != taskbar && IsWindowVisible(hwnd)) ShowWindowAsync(hwnd, SW_HIDE);
    }
}

/* A program covering the whole screen without a title bar (games, videos) gets it all */
static BOOL check_fullscreen(void)
{
    HWND fg = GetForegroundWindow();
    BOOL full = FALSE;

    if (fg && !is_shell_window(fg) && IsWindowVisible(fg)) {
        RECT rc;
        LONG style = GetWindowLongW(fg, GWL_STYLE);
        GetWindowRect(fg, &rc);
        if ((style & WS_CAPTION) != WS_CAPTION && rc.left <= 0 && rc.top <= 0 &&
            rc.right >= screen_w && rc.bottom >= screen_h)
            full = TRUE;
    }
    if (full == fullscreen_app) return FALSE;
    fullscreen_app = full;
    if (full) SetWindowPos(taskbar, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    else SetWindowPos(taskbar, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (full) ShowWindow(taskbar, SW_HIDE);
    else ShowWindow(taskbar, SW_SHOWNOACTIVATE);
    return TRUE;
}

/* ---------------------------------------------------------------------------------------- */
/* Notification area (Wine's Shell_NotifyIcon sends WM_COPYDATA to the first Shell_TrayWnd) */
/* ---------------------------------------------------------------------------------------- */

struct notify_data_icon {
    UINT width;
    UINT height;
    UINT planes;
    UINT bpp;
};

struct notify_data {  /* dlls/shell32/systray.c */
    LONG hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    struct notify_data_icon icon_info;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    union {
        UINT uTimeout;
        UINT uVersion;
    } u;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    struct notify_data_icon balloon_icon_info;
    BYTE icon_data[];
};

#define MAX_TRAY 32

struct tray_icon {
    HWND owner;
    UINT id;
    UINT callback;
    UINT version;
    HICON icon;
    WCHAR tip[128];
    BOOL hidden;
};

static struct tray_icon trays[MAX_TRAY];
static int tray_count;

static struct tray_icon *find_tray(HWND owner, UINT id)
{
    int i;
    for (i = 0; i < tray_count; i++) if (trays[i].owner == owner && trays[i].id == id) return &trays[i];
    return NULL;
}

static void delete_tray(struct tray_icon *icon)
{
    int index = (int)(icon - trays);
    if (icon->icon) DestroyIcon(icon->icon);
    memmove(&trays[index], &trays[index + 1], (tray_count - index - 1) * sizeof(trays[0]));
    tray_count--;
}

static BOOL handle_notify(COPYDATASTRUCT *cds)
{
    const struct notify_data *data = cds->lpData;
    struct tray_icon *icon;
    HWND owner;
    HICON image = NULL;

    if (cds->cbData < sizeof(*data)) return FALSE;
    owner = LongToHandle(data->hWnd);

    if (data->uFlags & NIF_ICON) {
        const struct notify_data_icon *info = &data->icon_info;
        LONG mask_bits = (info->width * info->height + 15) / 16 * 2;
        LONG color_bits = (info->planes * info->width * info->height * info->bpp + 15) / 16 * 2;
        if (info->width && cds->cbData >= sizeof(*data) + mask_bits + color_bits)
            image = CreateIcon(NULL, info->width, info->height, info->planes, info->bpp,
                               data->icon_data, data->icon_data + mask_bits);
    }

    icon = find_tray(owner, data->uID);
    switch (cds->dwData) {
    case NIM_ADD:
        if (icon || tray_count >= MAX_TRAY) break;
        icon = &trays[tray_count++];
        ZeroMemory(icon, sizeof(*icon));
        icon->owner = owner;
        icon->id = data->uID;
        /* fall through */
    case NIM_MODIFY:
        if (!icon) break;
        if (data->uFlags & NIF_MESSAGE) icon->callback = data->uCallbackMessage;
        if (data->uFlags & NIF_TIP) lstrcpynW(icon->tip, data->szTip, ARRAY_SIZE(icon->tip));
        if (data->uFlags & NIF_STATE)
            icon->hidden = (data->dwState & data->dwStateMask & NIS_HIDDEN) != 0 ||
                           (icon->hidden && !(data->dwStateMask & NIS_HIDDEN));
        if (image) {
            if (icon->icon) DestroyIcon(icon->icon);
            icon->icon = image;
            image = NULL;
        }
        break;
    case NIM_DELETE:
        if (icon) delete_tray(icon);
        break;
    case NIM_SETVERSION:
        if (icon) icon->version = data->u.uVersion;
        break;
    default:
        break;
    }
    if (image) DestroyIcon(image);
    return icon != NULL || cds->dwData == NIM_DELETE;
}

static BOOL prune_trays(void)
{
    BOOL changed = FALSE;
    int i;
    for (i = tray_count - 1; i >= 0; i--) {
        if (!IsWindow(trays[i].owner)) {
            delete_tray(&trays[i]);
            changed = TRUE;
        }
    }
    return changed;
}

/* Relays a mouse message to the program that owns the icon, as explorer.exe does */
static void notify_tray_owner(struct tray_icon *icon, UINT msg, POINT screen_pt)
{
    WPARAM wp = icon->id;
    LPARAM lp = msg;

    if (!icon->callback) return;
    if (icon->version >= NOTIFYICON_VERSION_4) {
        wp = MAKEWPARAM(screen_pt.x, screen_pt.y);
        lp = MAKELPARAM(msg, icon->id);
    }
    SendNotifyMessageW(icon->owner, icon->callback, wp, lp);
}

/* ---------------------------------------------------------------------------------------- */
/* Taskbar layout and painting                                                              */
/* ---------------------------------------------------------------------------------------- */

enum part_kind { PART_START, PART_SEARCH, PART_TASK, PART_TRAY, PART_CLOCK, PART_DESKTOP,
                 PART_LANGUAGE, PART_OVERFLOW, PART_NETWORK, PART_BLUETOOTH };

struct part {
    enum part_kind kind;
    int index;
    RECT rc;
};

#define MAX_PARTS (MAX_TASKS + MAX_TRAY + 12)
static struct part parts[MAX_PARTS];
static int part_count;
static int hot_part = -1, pressed_part = -1;
static WCHAR clock_time[64], clock_date[64];

static void add_part(enum part_kind kind, int index, int left, int top, int right, int bottom)
{
    if (part_count >= MAX_PARTS) return;
    parts[part_count].kind = kind;
    parts[part_count].index = index;
    SetRect(&parts[part_count].rc, left, top, right, bottom);
    part_count++;
}

static BOOL update_clock_text(void)
{
    WCHAR time_text[64], date_text[64];
    if (korean) {
        /* Wine's Korean locale data says AM/PM; Windows says 오전/오후 */
        SYSTEMTIME now;
        GetLocalTime(&now);
        wsprintfW(time_text, L"%s %d:%02d", now.wHour < 12 ? L"오전" : L"오후",
                  now.wHour % 12 ? now.wHour % 12 : 12, now.wMinute);
    }
    else GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, NULL, NULL, time_text, ARRAY_SIZE(time_text));
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, date_text, ARRAY_SIZE(date_text), NULL);
    if (!lstrcmpW(time_text, clock_time) && !lstrcmpW(date_text, clock_date)) return FALSE;
    lstrcpyW(clock_time, time_text);
    lstrcpyW(clock_date, date_text);
    return TRUE;
}

static int text_width(HFONT font, const WCHAR *text)
{
    HDC dc = GetDC(taskbar);
    HFONT old = SelectObject(dc, font);
    SIZE size = {0};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    SelectObject(dc, old);
    ReleaseDC(taskbar, dc);
    return size.cx;
}

static void layout_taskbar(void)
{
    int button = SC(44), gap = SC(4), height = SC(40), top = (bar_h - height) / 2;
    int x = screen_w, right_limit, count, total, left, i, clock_w;

    part_count = 0;

    add_part(PART_DESKTOP, 0, x - SC(10), 0, x, bar_h);
    x -= SC(10);
    clock_w = max(text_width(small_font, clock_time), text_width(small_font, clock_date)) + SC(20);
    add_part(PART_CLOCK, 0, x - clock_w, top, x, top + height);
    x -= clock_w + SC(2);
    add_part(PART_BLUETOOTH, 0, x - SC(28), top, x, top + height);
    x -= SC(28);
    add_part(PART_NETWORK, 0, x - SC(28), top, x, top + height);
    x -= SC(28);
    add_part(PART_OVERFLOW, 0, x - SC(24), top, x, top + height);
    x -= SC(24);
    add_part(PART_LANGUAGE, 0, x - SC(34), top, x, top + height);
    x -= SC(34);
    for (i = tray_count - 1; i >= 0; i--) {
        if (trays[i].hidden || !trays[i].icon) continue;
        add_part(PART_TRAY, i, x - SC(32), top, x, top + height);
        x -= SC(32);
    }
    right_limit = x - SC(12);

    count = 2 + task_count;
    total = count * button + (count - 1) * gap;
    left = align_left ? SC(8) : (screen_w - total) / 2;
    if (left + total > right_limit) left = max(SC(8), right_limit - total);

    add_part(PART_START, 0, left, top, left + button, top + height);
    left += button + gap;
    add_part(PART_SEARCH, 0, left, top, left + button, top + height);
    left += button + gap;
    for (i = 0; i < task_count && left + button <= right_limit; i++) {
        add_part(PART_TASK, i, left, top, left + button, top + height);
        left += button + gap;
    }
}

static void paint_taskbar(HDC hdc)
{
    struct canvas c;
    RECT client;
    int i;

    GetClientRect(taskbar, &client);
    if (!canvas_begin(&c, hdc, client.right, client.bottom)) return;

    fill_rect(&c, 0, 0, client.right, client.bottom, argb(pal.bar, 255));
    fill_rect(&c, 0, 0, client.right, 1, argb(pal.bar_border, 255));

    for (i = 0; i < part_count; i++) {
        struct part *p = &parts[i];
        RECT rc = p->rc;
        int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
        BOOL active = p->kind == PART_TASK && tasks[p->index].hwnd == active_task;

        if (p->kind != PART_DESKTOP) {
            if (i == pressed_part) fill_round_rect(&c, &rc, (float)SC(4), pal.pressed);
            else if (i == hot_part) fill_round_rect(&c, &rc, (float)SC(4), pal.hover);
            else if (active) fill_round_rect(&c, &rc, (float)SC(4), pal.active);
        }
        else if (i == hot_part) fill_rect(&c, rc.left + SC(4), rc.top + SC(8), 1, rc.bottom - rc.top - SC(16),
                                         argb(pal.text_dim, 255));

        switch (p->kind) {
        case PART_START:
            draw_logo(&c, (float)cx, (float)cy, (float)SC(24));
            break;
        case PART_SEARCH:
            draw_search_glyph(&c, (float)cx, (float)cy, (float)SC(22), pal.text);
            break;
        case PART_TASK: {
            struct task *t = &tasks[p->index];
            int size = SC(24);
            RECT bar;
            draw_icon(&c, t->icon, cx - size / 2, cy - size / 2 - SC(1), size);
            if (active) SetRect(&bar, cx - SC(8), rc.bottom - SC(3), cx + SC(8), rc.bottom);
            else SetRect(&bar, cx - SC(3), rc.bottom - SC(3), cx + SC(3), rc.bottom);
            fill_round_rect(&c, &bar, SC(3) / 2.0f, argb(active ? pal.accent : pal.indicator, 255));
            break;
        }
        case PART_TRAY: {
            int size = SC(16);
            draw_icon(&c, trays[p->index].icon, cx - size / 2, cy - size / 2, size);
            break;
        }
        case PART_CLOCK: {
            RECT top_rc = rc, bottom_rc = rc;
            top_rc.bottom = cy;
            bottom_rc.top = cy;
            draw_text(&c, clock_time, &top_rc, small_font, pal.text, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
            draw_text(&c, clock_date, &bottom_rc, small_font, pal.text, DT_CENTER | DT_TOP | DT_SINGLELINE);
            break;
        }
        case PART_LANGUAGE:
            draw_text(&c, korean ? L"한" : L"EN", &rc, small_font, pal.text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        case PART_OVERFLOW:
            draw_overflow_glyph(&c, (float)cx, (float)cy, (float)SC(14), pal.text);
            break;
        case PART_NETWORK:
            draw_wifi_glyph(&c, (float)cx, (float)cy, (float)SC(17), pal.text);
            break;
        case PART_BLUETOOTH:
            draw_bluetooth_glyph(&c, (float)cx, (float)cy, (float)SC(17), pal.text);
            break;
        case PART_DESKTOP:
            break;
        }
    }
    canvas_end(&c, hdc, 0, 0);
}

static int hit_part(int x, int y)
{
    POINT pt = {x, y};
    int i;
    for (i = 0; i < part_count; i++) if (PtInRect(&parts[i].rc, pt)) return i;
    return -1;
}

static void refresh_taskbar(void)
{
    layout_taskbar();
    if (hot_part >= part_count) hot_part = -1;
    if (pressed_part >= part_count) pressed_part = -1;
    InvalidateRect(taskbar, NULL, FALSE);
}

/* ---------------------------------------------------------------------------------------- */
/* Tooltip (a small popup above the taskbar)                                                 */
/* ---------------------------------------------------------------------------------------- */

static WCHAR tip_text[256];

static void hide_tip(void)
{
    KillTimer(taskbar, TIMER_TIP);
    if (IsWindowVisible(tooltip)) ShowWindow(tooltip, SW_HIDE);
}

static void show_tip(int part)
{
    struct part *p;
    RECT rc = {0, 0, SC(400), SC(100)};
    HDC dc;
    HFONT old;
    int w, h, x, y;

    if (part < 0 || part >= part_count) return;
    p = &parts[part];
    switch (p->kind) {
    case PART_START: lstrcpyW(tip_text, TR(L"시작", L"Start")); break;
    case PART_SEARCH: lstrcpyW(tip_text, TR(L"검색", L"Search")); break;
    case PART_TASK: lstrcpynW(tip_text, tasks[p->index].title, ARRAY_SIZE(tip_text)); break;
    case PART_TRAY: lstrcpynW(tip_text, trays[p->index].tip, ARRAY_SIZE(tip_text)); break;
    case PART_CLOCK: {
        WCHAR date[64];
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, NULL, NULL, date, ARRAY_SIZE(date), NULL);
        lstrcpyW(tip_text, date);
        break;
    }
    case PART_DESKTOP: lstrcpyW(tip_text, TR(L"바탕 화면 보기", L"Show desktop")); break;
    case PART_LANGUAGE: lstrcpyW(tip_text, TR(L"입력 언어", L"Input language")); break;
    case PART_OVERFLOW: lstrcpyW(tip_text, TR(L"백그라운드 앱", L"Background apps")); break;
    case PART_NETWORK: lstrcpyW(tip_text, TR(L"네트워크 설정", L"Network settings")); break;
    case PART_BLUETOOTH: lstrcpyW(tip_text, TR(L"Bluetooth 설정", L"Bluetooth settings")); break;
    }
    if (!tip_text[0]) return;

    dc = GetDC(tooltip);
    old = SelectObject(dc, small_font);
    DrawTextW(dc, tip_text, -1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(tooltip, dc);

    w = rc.right + SC(20);
    h = rc.bottom + SC(12);
    x = (p->rc.left + p->rc.right) / 2 - w / 2;
    x = max(SC(4), min(x, screen_w - w - SC(4)));
    y = screen_h - bar_h - h - SC(6);
    SetWindowPos(tooltip, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(tooltip, NULL, FALSE);
}

static LRESULT CALLBACK tooltip_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        struct canvas c;
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (canvas_begin(&c, hdc, rc.right, rc.bottom)) {
            RECT text = rc;
            fill_rect(&c, 0, 0, rc.right, rc.bottom, argb(pal.menu, 255));
            stroke_round_rect(&c, &rc, 0, argb(pal.menu_border, 255), 1);
            draw_text(&c, tip_text, &text, small_font, pal.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            canvas_end(&c, hdc, 0, 0);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------------------------- */
/* Title bar buttons: a highlight under the mouse, as in Windows 10 and 11                  */
/* ---------------------------------------------------------------------------------------- */

/*
 * Wine draws the minimize, maximize and close buttons as glyphs only and doesn't track the
 * mouse over them. A small window of ours covers the button under the mouse: gray (red for
 * close), with the same glyph. It is layered and transparent, so clicks go through to the
 * program's title bar.
 */
static HWND hover_target;
static int hover_code;              /* HTMINBUTTON, HTMAXBUTTON, HTCLOSE */
static BOOL hover_pressed, hover_restore;
static RECT hover_cell, hover_glyph; /* screen; the glyph's square as Wine sizes it */
static POINT hover_last = {-100000, -100000};
static BOOL hover_last_down;
static COLORREF hover_caption;      /* title bar colour behind */

static void hide_hover(void)
{
    hover_target = NULL;
    hover_code = 0;
    if (IsWindowVisible(hover_window)) ShowWindow(hover_window, SW_HIDE);
}

/* The frame thickness Wine takes off the window rectangle (win32u, get_inside_rect) */
static int frame_size(LONG style, LONG ex_style, BOOL vertical)
{
    BOOL thick = (style & WS_THICKFRAME) && (style & (WS_DLGFRAME | WS_BORDER)) != WS_DLGFRAME;
    BOOL dialog = (ex_style & WS_EX_DLGMODALFRAME) || ((style & WS_DLGFRAME) && !(style & WS_THICKFRAME));
    BOOL thin = (style & WS_BORDER) || !(style & (WS_CHILD | WS_POPUP));
    if (thick) return GetSystemMetrics(vertical ? SM_CYFRAME : SM_CXFRAME);
    if (dialog) return GetSystemMetrics(vertical ? SM_CYDLGFRAME : SM_CXDLGFRAME);
    if (thin) return GetSystemMetrics(vertical ? SM_CYBORDER : SM_CXBORDER);
    return 0;
}

/* The button's cell (full caption height) and the square Wine draws its glyph in */
static BOOL caption_button_rects(HWND hwnd, int code, RECT *cell, RECT *glyph)
{
    LONG style = GetWindowLongW(hwnd, GWL_STYLE), ex_style = GetWindowLongW(hwnd, GWL_EXSTYLE);
    int cx = GetSystemMetrics(SM_CXSIZE), cy = GetSystemMetrics(SM_CYSIZE);
    int right, top, n, side;
    RECT wr, r;

    if ((style & WS_CAPTION) != WS_CAPTION || !(style & WS_SYSMENU)) return FALSE;
    if (ex_style & (WS_EX_TOOLWINDOW | WS_EX_LAYOUTRTL)) return FALSE;
    if (code == HTMAXBUTTON && !(style & WS_MAXIMIZEBOX)) return FALSE;   /* grayed */
    if (code == HTMINBUTTON && !(style & WS_MINIMIZEBOX)) return FALSE;
    GetWindowRect(hwnd, &wr);
    right = wr.right - frame_size(style, ex_style, FALSE);
    top = wr.top + frame_size(style, ex_style, TRUE);
    n = code == HTCLOSE ? 1 : code == HTMAXBUTTON ? 2 : 3;
    SetRect(cell, right - n * cx, top, right - (n - 1) * cx, top + GetSystemMetrics(SM_CYCAPTION) - 1);

    /* draw_close_button / draw_max_button / draw_min_button, then make_square_rect */
    r.right = right - (n - 1) * cx;
    if (code == HTMINBUTTON) r.right += 2;
    r.left = r.right - cx;
    r.top = top + 2;
    r.bottom = top + cy - 2;
    r.right -= 2;
    side = min(r.right - r.left, r.bottom - r.top);
    if (r.right - r.left > side) r.left += (r.right - r.left - side) / 2;
    else r.top += (r.bottom - r.top - side) / 2;
    SetRect(glyph, r.left, r.top, r.left + side, r.top + side);
    return TRUE;
}

static void poll_hover(void)
{
    POINT pt;
    BOOL down = GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) < 0;
    DWORD_PTR hit = 0;
    HWND hwnd;
    RECT cell, glyph;
    BOOL pressed, restore;

    if (!GetCursorPos(&pt)) return;
    if (pt.x == hover_last.x && pt.y == hover_last.y && down == hover_last_down &&
        (!hover_target || IsWindow(hover_target)))
        return;
    hover_last = pt;
    hover_last_down = down;

    hwnd = WindowFromPoint(pt);   /* passes over the highlight: it is transparent */
    if (hwnd) hwnd = GetAncestor(hwnd, GA_ROOT);
    if (!hwnd || is_shell_window(hwnd) || GetDpiForWindow(hwnd) != dpi ||
        !SendMessageTimeoutW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y), SMTO_ABORTIFHUNG, 40, &hit) ||
        (hit != HTMINBUTTON && hit != HTMAXBUTTON && hit != HTCLOSE) ||
        !caption_button_rects(hwnd, (int)hit, &cell, &glyph)) {
        hide_hover();
        return;
    }

    /* pressed: the button went down on this very button */
    pressed = down && hover_target == hwnd && hover_code == (int)hit;
    restore = (GetWindowLongW(hwnd, GWL_STYLE) & WS_MAXIMIZE) != 0;
    if (hwnd == hover_target && hit == hover_code && pressed == hover_pressed && restore == hover_restore &&
        EqualRect(&cell, &hover_cell) && IsWindowVisible(hover_window))
        return;

    hover_target = hwnd;
    hover_code = (int)hit;
    hover_pressed = pressed;
    hover_restore = restore;
    hover_cell = cell;
    hover_glyph = glyph;
    hover_caption = GetSysColor(GetForegroundWindow() == hwnd ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
    SetWindowPos(hover_window, HWND_TOPMOST, cell.left, cell.top, cell.right - cell.left, cell.bottom - cell.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hover_window, NULL, FALSE);
}

static COLORREF mix(COLORREF a, COLORREF b, int alpha /* of b, 0-255 */)
{
    return RGB((GetRValue(a) * (255 - alpha) + GetRValue(b) * alpha) / 255,
               (GetGValue(a) * (255 - alpha) + GetGValue(b) * alpha) / 255,
               (GetBValue(a) * (255 - alpha) + GetBValue(b) * alpha) / 255);
}

/* The S11 Marlett glyphs (tools/s11/theme/build_marlett.py): 760/2048 of the em, 92/2048 strokes */
static void paint_hover(HDC hdc)
{
    struct canvas c;
    RECT rc;
    COLORREF back, ink;
    BOOL dark_caption = (GetRValue(hover_caption) * 299 + GetGValue(hover_caption) * 587 +
                         GetBValue(hover_caption) * 114) / 1000 < 128;
    float em, half, stroke, xc, yc;
    GpPen *pen;
    GpSolidFill *brush;

    GetClientRect(hover_window, &rc);
    if (!canvas_begin(&c, hdc, rc.right, rc.bottom)) return;
    if (hover_code == HTCLOSE) {
        back = hover_pressed ? RGB(148, 30, 20) : RGB(196, 43, 28);
        ink = RGB(255, 255, 255);
    }
    else {
        back = mix(hover_caption, dark_caption ? RGB(255, 255, 255) : RGB(0, 0, 0), hover_pressed ? 40 : 22);
        ink = GetSysColor(COLOR_BTNTEXT);
    }
    fill_rect(&c, 0, 0, rc.right, rc.bottom, argb(back, 255));

    /* Wine's font height is the square's side less 2; the glyph is centred in the square */
    em = (float)(hover_glyph.right - hover_glyph.left - 2);
    half = em * 380 / 2048;
    stroke = max(1.0f, em * 92 / 2048);
    xc = (float)((hover_glyph.left + hover_glyph.right) / 2 - hover_cell.left);
    yc = (float)((hover_glyph.top + hover_glyph.bottom) / 2 - hover_cell.top);
    GdipCreatePen1(argb(ink, 255), stroke, UnitPixel, &pen);
    GdipCreateSolidFill(argb(ink, 255), &brush);
    switch (hover_code) {
    case HTMINBUTTON:
        GdipFillRectangle(c.g, (GpBrush *)brush, xc - half, yc - stroke / 2, half * 2, stroke);
        break;
    case HTMAXBUTTON:
        if (!hover_restore)
            GdipDrawRectangle(c.g, pen, xc - half + stroke / 2, yc - half + stroke / 2, half * 2 - stroke, half * 2 - stroke);
        else {
            float off = em * 152 / 2048, side = half * 2 - off;
            /* the front window lower left, the back one's top and right edges upper right */
            GdipDrawRectangle(c.g, pen, xc - half + stroke / 2, yc - half + off + stroke / 2, side - stroke, side - stroke);
            GdipDrawLine(c.g, pen, xc - half + off + stroke / 2, yc - half + off, xc - half + off + stroke / 2, yc - half + stroke / 2);
            GdipDrawLine(c.g, pen, xc - half + off, yc - half + stroke / 2, xc + half - stroke / 2, yc - half + stroke / 2);
            GdipDrawLine(c.g, pen, xc + half - stroke / 2, yc - half, xc + half - stroke / 2, yc + half - off - stroke / 2);
            GdipDrawLine(c.g, pen, xc + half - off, yc + half - off - stroke / 2, xc + half - stroke / 2, yc + half - off - stroke / 2);
        }
        break;
    case HTCLOSE:
        GdipDeletePen(pen);
        GdipCreatePen1(argb(ink, 255), stroke * 1.05f, UnitPixel, &pen);
        GdipDrawLine(c.g, pen, xc - half, yc - half, xc + half, yc + half);
        GdipDrawLine(c.g, pen, xc - half, yc + half, xc + half, yc - half);
        break;
    }
    GdipDeletePen(pen);
    GdipDeleteBrush((GpBrush *)brush);
    canvas_end(&c, hdc, 0, 0);
}

static LRESULT CALLBACK hover_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_hover(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------------------------- */
/* Calendar (the clock opens it)                                                             */
/* ---------------------------------------------------------------------------------------- */

static int cal_year, cal_month;     /* the month shown */
static int cal_hot = -1;            /* CAL_PREV, CAL_NEXT or a day cell 0-41 */
static DWORD cal_hidden_tick;
#define CAL_PREV 100
#define CAL_NEXT 101

static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    BOOL leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[month - 1];
}

/* 0 = Sunday */
static int day_of_week(int year, int month, int day)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

static int first_day_of_week(void)   /* 0 = Sunday */
{
    WCHAR value[4] = L"6";
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK, value, ARRAY_SIZE(value));
    return (_wtoi(value) + 1) % 7;    /* the locale counts from Monday */
}

static int cal_w(void) { return SC(360); }
static int cal_h(void) { return SC(446); }
static int cal_cell(void) { return SC(44); }

static void cal_grid_origin(int *x, int *y)
{
    *x = (cal_w() - 7 * cal_cell()) / 2;
    *y = SC(150);
}

static void cal_arrow_rect(int which, RECT *rc)
{
    int right = cal_w() - SC(16) - (which == CAL_NEXT ? 0 : SC(44));
    SetRect(rc, right - SC(40), SC(84), right, SC(120));
}

static int cal_hit(int x, int y)
{
    POINT pt = {x, y};
    RECT rc;
    int gx, gy, col, row;
    cal_arrow_rect(CAL_PREV, &rc);
    if (PtInRect(&rc, pt)) return CAL_PREV;
    cal_arrow_rect(CAL_NEXT, &rc);
    if (PtInRect(&rc, pt)) return CAL_NEXT;
    cal_grid_origin(&gx, &gy);
    gy += SC(36);
    if (x < gx || y < gy) return -1;
    col = (x - gx) / cal_cell();
    row = (y - gy) / SC(40);
    if (col > 6 || row > 5) return -1;
    return row * 7 + col;
}

static void draw_chevron(struct canvas *c, float cx, float cy, float size, BOOL up, COLORREF color)
{
    GpPen *pen = make_pen(argb(color, 255), max(1.0f, size * 0.12f));
    float h = size * 0.30f, v = size * 0.16f * (up ? -1 : 1);
    GdipDrawLine(c->g, pen, cx - h, cy - v, cx, cy + v);
    GdipDrawLine(c->g, pen, cx, cy + v, cx + h, cy - v);
    GdipDeletePen(pen);
}

static void paint_calendar(HDC hdc)
{
    static const WCHAR *const ko_days[] = {L"일", L"월", L"화", L"수", L"목", L"금", L"토"};
    struct canvas c;
    RECT client, rc;
    SYSTEMTIME now;
    WCHAR text[96];
    int i, gx, gy, first, start, prev_days, days, fdw = first_day_of_week();

    GetLocalTime(&now);
    GetClientRect(calendar, &client);
    if (!canvas_begin(&c, hdc, client.right, client.bottom)) return;
    fill_rect(&c, 0, 0, client.right, client.bottom, argb(pal.menu, 255));
    stroke_round_rect(&c, &client, 0, argb(pal.menu_border, 255), 1);

    /* today */
    if (korean) wsprintfW(text, L"%d월 %d일 %s요일", now.wMonth, now.wDay, ko_days[now.wDayOfWeek]);
    else GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, L"dddd, MMMM d", text, ARRAY_SIZE(text), NULL);
    SetRect(&rc, SC(20), SC(16), client.right - SC(20), SC(56));
    draw_text(&c, text, &rc, calendar_title_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    fill_rect(&c, 0, SC(68), client.right, 1, argb(pal.menu_border, 255));

    /* month and the arrows */
    if (korean) wsprintfW(text, L"%d년 %d월", cal_year, cal_month);
    else {
        SYSTEMTIME first_day = {0};
        first_day.wYear = (WORD)cal_year;
        first_day.wMonth = (WORD)cal_month;
        first_day.wDay = 1;
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &first_day, L"MMMM yyyy", text, ARRAY_SIZE(text), NULL);
    }
    SetRect(&rc, SC(24), SC(84), client.right - SC(120), SC(120));
    draw_text(&c, text, &rc, menu_title_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (i = CAL_PREV; i <= CAL_NEXT; i++) {
        cal_arrow_rect(i, &rc);
        if (cal_hot == i) fill_round_rect(&c, &rc, (float)SC(4), pal.hover);
        draw_chevron(&c, (rc.left + rc.right) / 2.0f, (rc.top + rc.bottom) / 2.0f, (float)SC(22), i == CAL_PREV, pal.text);
    }

    /* the days of the week, then six weeks */
    cal_grid_origin(&gx, &gy);
    for (i = 0; i < 7; i++) {
        int day = (fdw + i) % 7;
        WCHAR name[16];
        if (korean) lstrcpyW(name, ko_days[day]);
        else GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTESTDAYNAME1 + (day + 6) % 7, name, ARRAY_SIZE(name));
        SetRect(&rc, gx + i * cal_cell(), gy, gx + (i + 1) * cal_cell(), gy + SC(32));
        draw_text(&c, name, &rc, small_font, pal.text_dim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    gy += SC(36);
    first = day_of_week(cal_year, cal_month, 1);
    start = (first - fdw + 7) % 7;               /* cells before the 1st */
    days = days_in_month(cal_year, cal_month);
    prev_days = days_in_month(cal_month == 1 ? cal_year - 1 : cal_year, cal_month == 1 ? 12 : cal_month - 1);
    for (i = 0; i < 42; i++) {
        int day = i - start + 1;
        BOOL inside = day >= 1 && day <= days;
        BOOL today = inside && cal_year == now.wYear && cal_month == now.wMonth && day == now.wDay;
        RECT cell = {gx + (i % 7) * cal_cell(), gy + (i / 7) * SC(40), gx + (i % 7 + 1) * cal_cell(), gy + (i / 7 + 1) * SC(40)};
        RECT dot = cell;
        int shown = inside ? day : day < 1 ? prev_days + day : day - days;
        InflateRect(&dot, -(cal_cell() - SC(36)) / 2, -SC(2));
        if (today) {
            GpSolidFill *accent;
            GdipCreateSolidFill(argb(pal.accent, 255), &accent);
            GdipFillEllipse(c.g, (GpBrush *)accent, (float)dot.left, (float)dot.top, (float)(dot.right - dot.left),
                            (float)(dot.bottom - dot.top));
            GdipDeleteBrush((GpBrush *)accent);
        }
        else if (cal_hot == i) {
            GpSolidFill *hover;
            GdipCreateSolidFill(pal.hover, &hover);
            GdipFillEllipse(c.g, (GpBrush *)hover, (float)dot.left, (float)dot.top, (float)(dot.right - dot.left),
                            (float)(dot.bottom - dot.top));
            GdipDeleteBrush((GpBrush *)hover);
        }
        wsprintfW(text, L"%d", shown);
        draw_text(&c, text, &cell, small_font, today ? RGB(255, 255, 255) : inside ? pal.text : pal.text_dim,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    canvas_end(&c, hdc, 0, 0);
}

static void hide_calendar(void)
{
    if (!IsWindowVisible(calendar)) return;
    ShowWindow(calendar, SW_HIDE);
    cal_hidden_tick = GetTickCount();
}

static void toggle_calendar(void)
{
    SYSTEMTIME now;
    if (IsWindowVisible(calendar)) {
        hide_calendar();
        return;
    }
    if (GetTickCount() - cal_hidden_tick < 250) return;   /* the click that closed it */
    hide_tip();
    choose_palette();
    GetLocalTime(&now);
    cal_year = now.wYear;
    cal_month = now.wMonth;
    cal_hot = -1;
    SetWindowPos(calendar, HWND_TOPMOST, screen_w - cal_w() - SC(12), screen_h - bar_h - cal_h() - SC(12), cal_w(), cal_h(),
                 SWP_SHOWWINDOW);
    SetForegroundWindow(calendar);
    InvalidateRect(calendar, NULL, FALSE);
}

static void calendar_step(int delta)
{
    cal_month += delta;
    if (cal_month < 1) { cal_month = 12; cal_year--; }
    if (cal_month > 12) { cal_month = 1; cal_year++; }
    InvalidateRect(calendar, NULL, FALSE);
}

static LRESULT CALLBACK calendar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_calendar(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) hide_calendar();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) hide_calendar();
        else if (wp == VK_UP || wp == VK_PRIOR) calendar_step(-1);
        else if (wp == VK_DOWN || wp == VK_NEXT) calendar_step(1);
        return 0;
    case WM_MOUSEWHEEL:
        calendar_step(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1);
        return 0;
    case WM_MOUSEMOVE: {
        int hot = cal_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hot != cal_hot) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            cal_hot = hot;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        cal_hot = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int hit = cal_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hit == CAL_PREV) calendar_step(-1);
        else if (hit == CAL_NEXT) calendar_step(1);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------------------------- */
/* Start menu: the programs                                                                  */
/* ---------------------------------------------------------------------------------------- */

struct app {
    WCHAR name[128];
    WCHAR path[MAX_PATH];      /* what is started: a shortcut or a program */
    WCHAR args[64];
    WCHAR target[MAX_PATH];    /* the program a shortcut starts (empty when unknown) */
    BOOL target_args;          /* ...with arguments of its own (an installer, a control panel) */
    WCHAR icon_path[MAX_PATH]; /* where the icon comes from */
    int icon_index;
    HICON icon;
    BOOL icon_loaded;
};

static struct app *apps;
static int app_count, app_capacity;
static int *shown;        /* indexes of the apps that match the search */
static int shown_count;
static BOOL run_item;     /* the search text is offered as a command */
static WCHAR query[128];

static struct app *new_app(void)
{
    if (app_count == app_capacity) {
        int capacity = app_capacity ? app_capacity * 2 : 64;
        HANDLE heap = GetProcessHeap();
        struct app *bigger = apps ? HeapReAlloc(heap, HEAP_ZERO_MEMORY, apps, capacity * sizeof(*apps))
                                  : HeapAlloc(heap, HEAP_ZERO_MEMORY, capacity * sizeof(*apps));
        int *bigger_shown;
        if (!bigger) return NULL;
        apps = bigger;
        bigger_shown = shown ? HeapReAlloc(heap, HEAP_ZERO_MEMORY, shown, capacity * sizeof(*shown))
                             : HeapAlloc(heap, HEAP_ZERO_MEMORY, capacity * sizeof(*shown));
        if (!bigger_shown) return NULL;
        shown = bigger_shown;
        app_capacity = capacity;
    }
    ZeroMemory(&apps[app_count], sizeof(apps[0]));
    return &apps[app_count++];
}

static BOOL has_app(const WCHAR *name)
{
    int i;
    for (i = 0; i < app_count; i++) if (!lstrcmpiW(apps[i].name, name)) return TRUE;
    return FALSE;
}

static BOOL skip_shortcut(const WCHAR *name)
{
    static const WCHAR *const skipped[] = {L"uninstall", L"제거", L"readme", L"license", L"help"};
    WCHAR lower[128];
    int i;
    lstrcpynW(lower, name, ARRAY_SIZE(lower));
    CharLowerW(lower);
    for (i = 0; i < ARRAY_SIZE(skipped); i++) {
        const WCHAR *s = skipped[i];
        const WCHAR *p;
        int n = lstrlenW(s);
        for (p = lower; *p; p++) if (!wcsncmp(p, s, n)) return TRUE;
    }
    return FALSE;
}

/* COM ids used here (instead of linking all of libuuid) */
static const CLSID clsid_shell_link = {0x00021401, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID iid_shell_link = {0x000214f9, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID iid_persist_file = {0x0000010b, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

/* Reads a shortcut's target and icon location. FALSE when the file is not a shortcut. */
static BOOL resolve_link(const WCHAR *lnk, WCHAR *target, WCHAR *icon_path, int *icon_index, BOOL *has_args)
{
    IShellLinkW *link = NULL;
    IPersistFile *file = NULL;
    WIN32_FIND_DATAW fd;
    WCHAR raw[MAX_PATH];
    BOOL ok = FALSE;

    target[0] = icon_path[0] = 0;
    *icon_index = 0;
    *has_args = FALSE;
    if (FAILED(CoCreateInstance(&clsid_shell_link, NULL, CLSCTX_INPROC_SERVER, &iid_shell_link, (void **)&link)))
        return FALSE;
    if (SUCCEEDED(IShellLinkW_QueryInterface(link, &iid_persist_file, (void **)&file)) &&
        SUCCEEDED(IPersistFile_Load(file, lnk, STGM_READ))) {
        ok = TRUE;
        raw[0] = 0;
        if (SUCCEEDED(IShellLinkW_GetPath(link, raw, MAX_PATH, &fd, 0)) && raw[0])
            ExpandEnvironmentStringsW(raw, target, MAX_PATH);
        raw[0] = 0;
        if (SUCCEEDED(IShellLinkW_GetArguments(link, raw, MAX_PATH)) && raw[0]) *has_args = TRUE;
        raw[0] = 0;
        if (SUCCEEDED(IShellLinkW_GetIconLocation(link, raw, MAX_PATH, icon_index)) && raw[0])
            ExpandEnvironmentStringsW(raw, icon_path, MAX_PATH);
        else {
            lstrcpyW(icon_path, target);
            *icon_index = 0;
        }
    }
    if (file) IPersistFile_Release(file);
    IShellLinkW_Release(link);
    return ok;
}

static const WCHAR *file_part(const WCHAR *path)
{
    const WCHAR *slash = wcsrchr(path, '\\'), *fwd = wcsrchr(path, '/');
    if (fwd > slash) slash = fwd;
    return slash ? slash + 1 : path;
}

/* Is a program already in the list (through a shortcut to the same file name)? */
static BOOL has_program(const WCHAR *file)
{
    int i;
    for (i = 0; i < app_count; i++) {
        if (apps[i].target[0] && !apps[i].target_args && !lstrcmpiW(file_part(apps[i].target), file_part(file)))
            return TRUE;
    }
    return FALSE;
}

static void scan_folder(const WCHAR *dir, int depth)
{
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;

    if (depth > 4 || lstrlenW(dir) + 3 >= MAX_PATH) return;
    wsprintfW(pattern, L"%s\\*", dir);
    if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) return;
    do {
        WCHAR path[MAX_PATH];
        int len;
        if (!lstrcmpW(fd.cFileName, L".") || !lstrcmpW(fd.cFileName, L"..")) continue;
        if (lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 >= MAX_PATH) continue;
        wsprintfW(path, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* programs that start with Windows are not in the menu */
            if (!lstrcmpiW(fd.cFileName, L"StartUp") || !lstrcmpiW(fd.cFileName, L"시작프로그램")) continue;
            scan_folder(path, depth + 1);
            continue;
        }
        len = lstrlenW(fd.cFileName);
        if (len > 4 && !lstrcmpiW(fd.cFileName + len - 4, L".lnk")) {
            WCHAR name[128], target[MAX_PATH], icon_path[MAX_PATH];
            int icon_index;
            BOOL has_args;
            struct app *app;
            lstrcpynW(name, fd.cFileName, min(len - 4 + 1, ARRAY_SIZE(name)));
            if (skip_shortcut(name) || has_app(name)) continue;
            if (!resolve_link(path, target, icon_path, &icon_index, &has_args)) continue;
            /* a shortcut to a program that is gone (an uninstalled app, Internet Explorer) */
            if (target[0] && target[1] == ':' && GetFileAttributesW(target) == INVALID_FILE_ATTRIBUTES) continue;
            /* Winlator's file manager: the S11 File Explorer takes its place */
            if (!lstrcmpiW(file_part(target), L"wfm.exe") && GetFileAttributesW(explorer_exe()) != INVALID_FILE_ATTRIBUTES)
                continue;
            if (!(app = new_app())) break;
            lstrcpyW(app->name, name);
            lstrcpyW(app->path, path);
            lstrcpyW(app->target, target);
            app->target_args = has_args;
            lstrcpyW(app->icon_path, icon_path);
            app->icon_index = icon_index;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

static void add_builtin(const WCHAR *name, const WCHAR *file, const WCHAR *args)
{
    WCHAR path[MAX_PATH];
    struct app *app;

    if (has_app(name) || has_program(file)) return;
    if (!SearchPathW(NULL, file, NULL, MAX_PATH, path, NULL)) return;
    if (!(app = new_app())) return;
    lstrcpynW(app->name, name, ARRAY_SIZE(app->name));
    lstrcpyW(app->path, path);
    lstrcpyW(app->target, path);
    lstrcpyW(app->icon_path, path);
    if (args) lstrcpynW(app->args, args, ARRAY_SIZE(app->args));
}

static int compare_apps(const void *a, const void *b)
{
    const struct app *x = a, *y = b;
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, x->name, -1, y->name, -1, NULL, NULL, 0) - 2;
}

static void free_apps(void)
{
    int i;
    for (i = 0; i < app_count; i++) if (apps[i].icon) DestroyIcon(apps[i].icon);
    app_count = 0;
}

static void scan_apps(void)
{
    WCHAR dir[MAX_PATH], windir[MAX_PATH], wfm[MAX_PATH];

    free_apps();
    /* the whole start menus: Winlator keeps its tools in "System Tools" next to "Programs" */
    if (SHGetSpecialFolderPathW(NULL, dir, CSIDL_STARTMENU, FALSE)) scan_folder(dir, 0);
    if (SHGetSpecialFolderPathW(NULL, dir, CSIDL_COMMON_STARTMENU, FALSE)) scan_folder(dir, 0);

    if (GetFileAttributesW(settings_exe()) != INVALID_FILE_ATTRIBUTES) add_builtin(TR(L"설정", L"Settings"), settings_exe(), NULL);

    GetWindowsDirectoryW(windir, MAX_PATH);
    wsprintfW(wfm, L"%s\\wfm.exe", windir);
    if (GetFileAttributesW(explorer_exe()) != INVALID_FILE_ATTRIBUTES)
        add_builtin(TR(L"파일 탐색기", L"File Explorer"), explorer_exe(), NULL);
    else if (GetFileAttributesW(wfm) != INVALID_FILE_ATTRIBUTES)
        add_builtin(TR(L"파일 탐색기", L"File Explorer"), wfm, NULL);

    add_builtin(TR(L"명령 프롬프트", L"Command Prompt"), L"cmd.exe", NULL);
    add_builtin(TR(L"메모장", L"Notepad"), L"notepad.exe", NULL);
    add_builtin(TR(L"작업 관리자", L"Task Manager"), L"taskmgr.exe", NULL);
    add_builtin(TR(L"제어판", L"Control Panel"), L"control.exe", NULL);
    add_builtin(TR(L"레지스트리 편집기", L"Registry Editor"), L"regedit.exe", NULL);
    add_builtin(TR(L"Wine 설정", L"Wine Configuration"), L"winecfg.exe", NULL);

    qsort(apps, app_count, sizeof(*apps), compare_apps);
}


static void load_app_icon(struct app *app)
{
    if (app->icon_loaded) return;
    app->icon_loaded = TRUE;
    /* the icon itself, not the shortcut overlay */
    if (app->icon_path[0])
        PrivateExtractIconsW(app->icon_path, app->icon_index, SC(32), SC(32), &app->icon, NULL, 1, 0);
    if (!app->icon && app->target[0]) PrivateExtractIconsW(app->target, 0, SC(32), SC(32), &app->icon, NULL, 1, 0);
    if (!app->icon) {
        SHFILEINFOW info = {0};
        if (SHGetFileInfoW(app->target[0] ? app->target : app->path, 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON))
            app->icon = info.hIcon;
    }
}

/* Hangul initial consonant of a syllable, as a compatibility jamo (ㄱ...ㅎ) */
static WCHAR hangul_initial(WCHAR ch)
{
    static const WCHAR initials[] = L"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
    if (ch < 0xac00 || ch > 0xd7a3) return 0;
    return initials[(ch - 0xac00) / 588];
}

static BOOL is_jamo_initial(WCHAR ch)
{
    return ch >= 0x3131 && ch <= 0x314e;
}

static BOOL matches(const WCHAR *name, const WCHAR *q)
{
    WCHAR lower_name[128], lower_q[128], initials[128];
    const WCHAR *p;
    int n, i;
    BOOL only_jamo = TRUE;

    if (!q[0]) return TRUE;
    lstrcpynW(lower_name, name, ARRAY_SIZE(lower_name));
    lstrcpynW(lower_q, q, ARRAY_SIZE(lower_q));
    CharLowerW(lower_name);
    CharLowerW(lower_q);
    n = lstrlenW(lower_q);
    for (p = lower_name; *p; p++) if (!wcsncmp(p, lower_q, n)) return TRUE;

    for (i = 0; lower_q[i]; i++) if (!is_jamo_initial(lower_q[i]) && lower_q[i] != ' ') only_jamo = FALSE;
    if (!only_jamo) return FALSE;
    for (i = 0; lower_name[i] && i < ARRAY_SIZE(initials) - 1; i++) {
        WCHAR initial = hangul_initial(lower_name[i]);
        initials[i] = initial ? initial : lower_name[i];
    }
    initials[i] = 0;
    for (p = initials; *p; p++) if (!wcsncmp(p, lower_q, n)) return TRUE;
    return FALSE;
}

static BOOL looks_runnable(const WCHAR *q)
{
    int len = lstrlenW(q);
    WCHAR path[MAX_PATH];
    if (!len) return FALSE;
    if (wcschr(q, '\\') || wcschr(q, ':')) return TRUE;
    if (len > 4 && !lstrcmpiW(q + len - 4, L".exe")) return TRUE;
    return SearchPathW(NULL, q, L".exe", MAX_PATH, path, NULL) != 0;
}

static void filter_apps(void)
{
    int i;
    shown_count = 0;
    for (i = 0; i < app_count; i++) if (matches(apps[i].name, query)) shown[shown_count++] = i;
    run_item = query[0] && looks_runnable(query);
}

/* ---------------------------------------------------------------------------------------- */
/* Start menu: window                                                                        */
/* ---------------------------------------------------------------------------------------- */

enum menu_part { MENU_NONE = -100, MENU_RUN = -1, MENU_FILES = -2, MENU_SETTINGS = -3, MENU_POWER = -4 };

static int menu_w, menu_h, menu_hot = MENU_NONE, menu_selected;
static int scroll_y, content_h;
static int columns = 6;
static DWORD menu_hidden_tick;
static HWND menu_previous;   /* the active window before the menu opened */
static BOOL dragging, drag_moved;
static int drag_start_y, drag_start_scroll;
static RECT search_rc, grid_rc, files_rc, settings_rc, power_rc;

static int tile_w(void) { return (grid_rc.right - grid_rc.left) / columns; }
static int tile_h(void) { return SC(96); }

static int item_count(void) { return shown_count + (run_item ? 1 : 0); }

static void item_rect(int item, RECT *rc)
{
    int col = item % columns, row = item / columns;
    rc->left = grid_rc.left + col * tile_w();
    rc->top = grid_rc.top + row * tile_h() - scroll_y;
    rc->right = rc->left + tile_w();
    rc->bottom = rc->top + tile_h();
}

static void layout_menu(void)
{
    int margin = SC(32), bottom_h = SC(64);
    SetRect(&search_rc, margin, SC(24), menu_w - margin, SC(24) + SC(40));
    SetRect(&grid_rc, margin - SC(8), search_rc.bottom + SC(48), menu_w - margin + SC(8), menu_h - bottom_h - SC(8));
    SetRect(&power_rc, menu_w - margin - SC(40), menu_h - bottom_h + SC(12), menu_w - margin, menu_h - SC(12));
    SetRect(&settings_rc, power_rc.left - SC(48), power_rc.top, power_rc.left - SC(8), power_rc.bottom);
    SetRect(&files_rc, settings_rc.left - SC(48), power_rc.top, settings_rc.left - SC(8), power_rc.bottom);
    content_h = ((item_count() + columns - 1) / columns) * tile_h();
}

static void clamp_scroll(void)
{
    int max_scroll = max(0, content_h - (grid_rc.bottom - grid_rc.top));
    scroll_y = max(0, min(scroll_y, max_scroll));
}

static void ensure_visible(int item)
{
    RECT rc;
    item_rect(item, &rc);
    if (rc.top < grid_rc.top) scroll_y -= grid_rc.top - rc.top;
    else if (rc.bottom > grid_rc.bottom) scroll_y += rc.bottom - grid_rc.bottom;
    clamp_scroll();
}

/*
 * A tile label on up to two lines, broken between words as Windows does for Korean too
 * ("레지스트리 / 편집기"); Wine's DT_WORDBREAK breaks Hangul between any two syllables.
 */
static void draw_label(struct canvas *c, const WCHAR *text, const RECT *rc, HFONT font, COLORREF color)
{
    HDC dc = GetDC(taskbar);
    HFONT old = SelectObject(dc, font);
    int width = rc->right - rc->left, len = lstrlenW(text), split = -1, i;
    SIZE size = {0};
    TEXTMETRICW tm;
    RECT line = *rc;

    GetTextMetricsW(dc, &tm);
    GetTextExtentPoint32W(dc, text, len, &size);
    if (size.cx > width) {
        for (i = len - 1; i > 0; i--) {
            if (text[i] != ' ') continue;
            GetTextExtentPoint32W(dc, text, i, &size);
            if (size.cx <= width) {
                split = i;
                break;
            }
        }
    }
    SelectObject(dc, old);
    ReleaseDC(taskbar, dc);

    if (size.cx <= width && split < 0) {
        draw_text(c, text, &line, font, color, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    if (split < 0) {
        draw_text(c, text, &line, font, color, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
        return;
    }
    {
        WCHAR first[128];
        lstrcpynW(first, text, min(split + 1, ARRAY_SIZE(first)));
        line.bottom = line.top + tm.tmHeight;
        draw_text(c, first, &line, font, color, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        line.top = line.bottom;
        line.bottom = line.top + tm.tmHeight;
        draw_text(c, text + split + 1, &line, font, color, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void paint_menu(HDC hdc)
{
    struct canvas c;
    RECT client, rc;
    HRGN clip;
    int i, n = item_count();
    WCHAR header[64];

    GetClientRect(start_menu, &client);
    if (!canvas_begin(&c, hdc, client.right, client.bottom)) return;

    fill_rect(&c, 0, 0, client.right, client.bottom, argb(pal.menu, 255));
    stroke_round_rect(&c, &client, 0, argb(pal.menu_border, 255), 1);

    /* search box */
    fill_round_rect(&c, &search_rc, (float)SC(6), argb(pal.field, 255));
    stroke_round_rect(&c, &search_rc, (float)SC(6), argb(pal.field_border, 255), 1);
    rc = search_rc;
    rc.top = rc.bottom - SC(2);
    rc.left += SC(6);
    rc.right -= SC(6);
    fill_round_rect(&c, &rc, (float)SC(1), argb(pal.accent, 255));
    draw_search_glyph(&c, (float)(search_rc.left + SC(20)), (float)((search_rc.top + search_rc.bottom) / 2),
                      (float)SC(18), pal.text_dim);

    /* section title */
    if (query[0]) wsprintfW(header, TR(L"검색 결과", L"Results"));
    else lstrcpyW(header, TR(L"모든 앱", L"All apps"));
    SetRect(&rc, grid_rc.left + SC(8), search_rc.bottom + SC(14), grid_rc.right, grid_rc.top - SC(6));
    draw_text(&c, header, &rc, menu_title_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* tiles */
    GdipFlush(c.g, FlushIntentionSync);
    clip = CreateRectRgnIndirect(&grid_rc);
    SelectClipRgn(c.dc, clip);
    GdipSetClipRectI(c.g, grid_rc.left, grid_rc.top, grid_rc.right - grid_rc.left, grid_rc.bottom - grid_rc.top,
                     CombineModeReplace);
    for (i = 0; i < n; i++) {
        RECT tile, label;
        int icon_size = SC(32);
        BOOL is_run = run_item && i == 0;
        int app_index = run_item ? i - 1 : i;

        item_rect(i, &tile);
        if (tile.bottom < grid_rc.top || tile.top > grid_rc.bottom) continue;
        InflateRect(&tile, -SC(4), -SC(4));
        if (menu_hot == i) fill_round_rect(&c, &tile, (float)SC(6), pal.hover);
        else if (menu_selected == i && query[0]) fill_round_rect(&c, &tile, (float)SC(6), pal.active);
        if (menu_selected == i && query[0]) stroke_round_rect(&c, &tile, (float)SC(6), argb(pal.accent, 160), 1);

        label = tile;
        label.top += SC(12) + icon_size + SC(6);
        label.left += SC(4);
        label.right -= SC(4);
        if (is_run) {
            WCHAR text[160];
            draw_icon(&c, LoadIconW(NULL, (const WCHAR *)IDI_APPLICATION), (tile.left + tile.right - icon_size) / 2,
                      tile.top + SC(12), icon_size);
            wsprintfW(text, TR(L"실행: %s", L"Run: %s"), query);
            draw_text(&c, text, &label, small_font, pal.text, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
        }
        else {
            struct app *app = &apps[shown[app_index]];
            load_app_icon(app);
            draw_icon(&c, app->icon ? app->icon : LoadIconW(NULL, (const WCHAR *)IDI_APPLICATION),
                      (tile.left + tile.right - icon_size) / 2, tile.top + SC(12), icon_size);
            draw_label(&c, app->name, &label, small_font, pal.text);
        }
    }
    if (!n) {
        rc = grid_rc;
        draw_text(&c, TR(L"찾는 앱이 없어요", L"No apps found"), &rc, ui_font, pal.text_dim,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    GdipFlush(c.g, FlushIntentionSync);
    SelectClipRgn(c.dc, NULL);
    DeleteObject(clip);
    GdipResetClip(c.g);

    /* bottom bar */
    fill_rect(&c, 0, menu_h - SC(64), client.right, 1, argb(pal.menu_border, 255));
    fill_rect(&c, 1, menu_h - SC(64) + 1, client.right - 2, SC(64) - 2, argb(pal.bar, 255));
    draw_logo(&c, (float)(SC(32) + SC(14)), (float)(menu_h - SC(32)), (float)SC(28));
    SetRect(&rc, SC(32) + SC(38), menu_h - SC(64), files_rc.left - SC(8), menu_h);
    draw_text(&c, L"S11 Workspace", &rc, ui_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (menu_hot == MENU_FILES) fill_round_rect(&c, &files_rc, (float)SC(4), pal.hover);
    if (menu_hot == MENU_SETTINGS) fill_round_rect(&c, &settings_rc, (float)SC(4), pal.hover);
    if (menu_hot == MENU_POWER) fill_round_rect(&c, &power_rc, (float)SC(4), pal.hover);
    draw_folder_glyph(&c, (files_rc.left + files_rc.right) / 2.0f, (files_rc.top + files_rc.bottom) / 2.0f, (float)SC(20), pal.text);
    draw_gear_glyph(&c, (settings_rc.left + settings_rc.right) / 2.0f, (settings_rc.top + settings_rc.bottom) / 2.0f, (float)SC(20), pal.text);
    draw_power_glyph(&c, (power_rc.left + power_rc.right) / 2.0f, (power_rc.top + power_rc.bottom) / 2.0f, (float)SC(20), pal.text);

    canvas_end(&c, hdc, 0, 0);
}

static int menu_hit(int x, int y)
{
    POINT pt = {x, y};
    int i, n = item_count();
    if (PtInRect(&files_rc, pt)) return MENU_FILES;
    if (PtInRect(&settings_rc, pt)) return MENU_SETTINGS;
    if (PtInRect(&power_rc, pt)) return MENU_POWER;
    if (!PtInRect(&grid_rc, pt)) return MENU_NONE;
    for (i = 0; i < n; i++) {
        RECT rc;
        item_rect(i, &rc);
        if (PtInRect(&rc, pt)) return i;
    }
    return MENU_NONE;
}

static void hide_start_menu(void)
{
    if (!IsWindowVisible(start_menu)) return;
    ShowWindow(start_menu, SW_HIDE);
    menu_hidden_tick = GetTickCount();
    dragging = FALSE;
}

/* Closing the menu on purpose gives the keyboard back to the window that had it */
static void dismiss_start_menu(void)
{
    BOOL was_active = GetForegroundWindow() == start_menu;
    hide_start_menu();
    if (was_active && menu_previous && IsWindow(menu_previous) && IsWindowVisible(menu_previous))
        SetForegroundWindow(menu_previous);
}

static void launch_item(int item)
{
    if (item < 0 || item >= item_count()) return;
    hide_start_menu();
    if (run_item && item == 0) {
        run(query, NULL);
        return;
    }
    {
        struct app *app = &apps[shown[run_item ? item - 1 : item]];
        const WCHAR *exe = wcsrchr(app->path, L'\\');
        exe = exe ? exe + 1 : app->path;
        if (!lstrcmpiW(exe, L"cmd.exe")) run_in_directory(app->path, NULL, L"C:\\");
        else run(app->path, app->args[0] ? app->args : NULL);
    }
}

static void show_power_options(void)
{
    HMENU menu = CreatePopupMenu();
    POINT pt = {power_rc.left, power_rc.top};
    int cmd;

    AppendMenuW(menu, MF_STRING, 2, TR(L"다시 시작", L"Restart"));
    AppendMenuW(menu, MF_STRING, 1, TR(L"종료 (컨테이너 목록으로)", L"Shut down (back to Winlator)"));
    ClientToScreen(start_menu, &pt);
    cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                         pt.x, pt.y, 0, start_menu, NULL);
    DestroyMenu(menu);
    if (cmd) {
        hide_start_menu();
        send_winlator_request(cmd == 2 ? WINLATOR_RESTART : WINLATOR_SHUTDOWN);
    }
}

static void menu_command(int part)
{
    switch (part) {
    case MENU_FILES:
        hide_start_menu();
        open_files();
        break;
    case MENU_SETTINGS:
        hide_start_menu();
        open_settings(NULL);
        break;
    case MENU_POWER:
        show_power_options();
        break;
    default:
        if (part >= 0) launch_item(part);
        break;
    }
}

static void show_start_menu(void)
{
    RECT work;
    int x, y;

    hide_tip();
    menu_previous = GetForegroundWindow();
    if (menu_previous == start_menu || menu_previous == taskbar) menu_previous = NULL;
    choose_palette();
    scan_apps();
    query[0] = 0;
    SetWindowTextW(search_edit, L"");
    filter_apps();
    menu_selected = 0;
    menu_hot = MENU_NONE;
    scroll_y = 0;

    menu_w = SC(660);
    menu_h = min(SC(640), screen_h - bar_h - SC(24));
    SetRect(&work, 0, 0, screen_w, screen_h - bar_h);
    x = align_left ? SC(12) : (screen_w - menu_w) / 2;
    y = work.bottom - menu_h - SC(12);
    layout_menu();
    SetWindowPos(start_menu, HWND_TOPMOST, x, y, menu_w, menu_h, SWP_SHOWWINDOW);
    SetWindowPos(search_edit, NULL, search_rc.left + SC(40), search_rc.top + SC(9), search_rc.right - search_rc.left - SC(52),
                 search_rc.bottom - search_rc.top - SC(16), SWP_NOZORDER);
    SetForegroundWindow(start_menu);
    SetFocus(search_edit);
    InvalidateRect(start_menu, NULL, FALSE);
}

static void toggle_start_menu(void)
{
    if (IsWindowVisible(start_menu)) dismiss_start_menu();
    else if (GetTickCount() - menu_hidden_tick > 250) show_start_menu();
}

static void move_selection(int delta)
{
    int n = item_count();
    if (!n) return;
    menu_selected = max(0, min(n - 1, menu_selected + delta));
    ensure_visible(menu_selected);
    InvalidateRect(start_menu, NULL, FALSE);
}

static LRESULT CALLBACK search_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    if (msg == WM_KEYDOWN) {
        switch (wp) {
        case VK_ESCAPE: dismiss_start_menu(); return 0;
        case VK_RETURN: launch_item(menu_selected); return 0;
        case VK_DOWN: move_selection(columns); return 0;
        case VK_UP: move_selection(-columns); return 0;
        case VK_TAB: move_selection(GetKeyState(VK_SHIFT) < 0 ? -1 : 1); return 0;
        case VK_LEFT: if (!query[0]) { move_selection(-1); return 0; } break;
        case VK_RIGHT: if (!query[0]) { move_selection(1); return 0; } break;
        }
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == 27 || wp == '\t')) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK menu_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_menu(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT: {
        static HBRUSH brush;
        static COLORREF brush_color;
        HDC dc = (HDC)wp;
        if (!brush || brush_color != pal.field) {
            if (brush) DeleteObject(brush);
            brush = CreateSolidBrush(pal.field);
            brush_color = pal.field;
        }
        SetTextColor(dc, pal.text);
        SetBkColor(dc, pal.field);
        return (LRESULT)brush;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 1 && HIWORD(wp) == EN_CHANGE) {
            GetWindowTextW(search_edit, query, ARRAY_SIZE(query));
            filter_apps();
            menu_selected = 0;
            scroll_y = 0;
            layout_menu();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) hide_start_menu();
        return 0;
    case WM_MOUSEWHEEL:
        scroll_y -= GET_WHEEL_DELTA_WPARAM(wp) * tile_h() / WHEEL_DELTA;
        clamp_scroll();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp), hot;
        if (dragging) {
            if (abs(y - drag_start_y) > SC(8)) drag_moved = TRUE;
            if (drag_moved) {
                scroll_y = drag_start_scroll - (y - drag_start_y);
                clamp_scroll();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        hot = menu_hit(x, y);
        if (hot != menu_hot) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            menu_hot = hot;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (menu_hot != MENU_NONE) {
            menu_hot = MENU_NONE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (PtInRect(&grid_rc, pt)) {
            dragging = TRUE;
            drag_moved = FALSE;
            drag_start_y = pt.y;
            drag_start_scroll = scroll_y;
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        BOOL was_drag = dragging && drag_moved;
        if (dragging) {
            dragging = FALSE;
            ReleaseCapture();
        }
        if (!was_drag) menu_command(menu_hit(x, y));
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) dismiss_start_menu();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------------------------- */
/* Taskbar window                                                                            */
/* ---------------------------------------------------------------------------------------- */

static void register_appbar(BOOL first)
{
    APPBARDATA abd = {sizeof(abd)};
    abd.hWnd = taskbar;
    abd.uCallbackMessage = WM_APP_APPBAR;
    if (first) SHAppBarMessage(ABM_NEW, &abd);
    abd.uEdge = ABE_BOTTOM;
    SetRect(&abd.rc, 0, screen_h - bar_h, screen_w, screen_h);
    SHAppBarMessage(ABM_SETPOS, &abd);
}

static void update_geometry(BOOL first)
{
    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    bar_h = SC(48);
    SetWindowPos(taskbar, HWND_TOPMOST, 0, screen_h - bar_h, screen_w, bar_h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    register_appbar(first);
    refresh_taskbar();
}

static void task_menu(int index, POINT screen_pt)
{
    HWND hwnd = tasks[index].hwnd;
    HMENU menu = CreatePopupMenu();
    int cmd;

    if (IsIconic(hwnd) || IsZoomed(hwnd)) AppendMenuW(menu, MF_STRING, SC_RESTORE, TR(L"이전 크기로", L"Restore"));
    if (!IsIconic(hwnd)) AppendMenuW(menu, MF_STRING, SC_MINIMIZE, TR(L"최소화", L"Minimize"));
    if (!IsZoomed(hwnd)) AppendMenuW(menu, MF_STRING, SC_MAXIMIZE, TR(L"최대화", L"Maximize"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, SC_CLOSE, TR(L"창 닫기", L"Close window"));
    SetForegroundWindow(taskbar);
    cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_CENTERALIGN,
                         screen_pt.x, screen_h - bar_h, 0, taskbar, NULL);
    PostMessageW(taskbar, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd == SC_RESTORE || cmd == SC_MAXIMIZE || (!cmd && hwnd == active_task)) SetForegroundWindow(hwnd);
    else if (active_task && IsWindow(active_task)) SetForegroundWindow(active_task);
    if (cmd) PostMessageW(hwnd, WM_SYSCOMMAND, cmd, 0);
}

static void taskbar_menu(POINT screen_pt)
{
    HMENU menu = CreatePopupMenu();
    int cmd;
    AppendMenuW(menu, MF_STRING, 1, TR(L"작업 관리자", L"Task Manager"));
    AppendMenuW(menu, MF_STRING, 2, TR(L"바탕 화면 보기", L"Show desktop"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, 3, TR(L"작업 표시줄 설정", L"Taskbar settings"));
    SetForegroundWindow(taskbar);
    cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_CENTERALIGN,
                         screen_pt.x, screen_h - bar_h, 0, taskbar, NULL);
    PostMessageW(taskbar, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (active_task && IsWindow(active_task)) SetForegroundWindow(active_task);
    if (cmd == 1) run(L"taskmgr.exe", NULL);
    else if (cmd == 2) toggle_show_desktop();
    else if (cmd == 3) open_settings(L"personalization");
}

static void show_hidden_trays(POINT screen_pt)
{
    HMENU menu = CreatePopupMenu();
    int i, selected;
    if (!menu) return;
    for (i = 0; i < tray_count; i++) {
        if (!trays[i].hidden || !trays[i].icon) continue;
        AppendMenuW(menu, MF_STRING, (UINT_PTR)(i + 1),
                    trays[i].tip[0] ? trays[i].tip : TR(L"백그라운드 앱", L"Background app"));
    }
    if (GetMenuItemCount(menu) == 0)
        AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, TR(L"숨겨진 앱이 없어요", L"No hidden apps"));
    SetForegroundWindow(taskbar);
    selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
                              screen_pt.x, screen_pt.y, 0, taskbar, NULL);
    PostMessageW(taskbar, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (selected > 0 && selected <= tray_count) {
        struct tray_icon *icon = &trays[selected - 1];
        notify_tray_owner(icon, NIN_SELECT, screen_pt);
    }
}

static void click_part(int index)
{
    struct part *p = &parts[index];
    switch (p->kind) {
    case PART_START:
        toggle_start_menu();
        break;
    case PART_SEARCH:
        toggle_start_menu();
        break;
    case PART_TASK:
        activate_task(tasks[p->index].hwnd);
        break;
    case PART_DESKTOP:
        toggle_show_desktop();
        break;
    case PART_CLOCK:
        toggle_calendar();
        break;
    case PART_LANGUAGE:
        run(L"control.exe", L"intl.cpl");
        break;
    case PART_OVERFLOW: {
        POINT pt = {parts[index].rc.right, parts[index].rc.top};
        ClientToScreen(taskbar, &pt);
        show_hidden_trays(pt);
        break;
    }
    case PART_NETWORK:
        send_winlator_request(WINLATOR_WIFI_PANEL);
        break;
    case PART_BLUETOOTH:
        send_winlator_request(WINLATOR_BLUETOOTH_PANEL);
        break;
    default:
        break;
    }
}

static BOOL is_mouse_message(UINT msg)
{
    return msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK ||
           msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_RBUTTONDBLCLK ||
           msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP;
}

static LRESULT CALLBACK taskbar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static UINT taskbar_created;

    if (is_mouse_message(msg)) {
        int part = hit_part(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (part >= 0 && parts[part].kind == PART_TRAY) {
            struct tray_icon *icon = &trays[parts[part].index];
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd, &pt);
            notify_tray_owner(icon, msg, pt);
            if (icon->version > 0 && msg == WM_LBUTTONUP) notify_tray_owner(icon, NIN_SELECT, pt);
            if (icon->version > 0 && msg == WM_RBUTTONUP) notify_tray_owner(icon, WM_CONTEXTMENU, pt);
        }
    }

    switch (msg) {
    case WM_CREATE:
        taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        return 0;
    case WM_APP_SWITCH_TASK:
        scan_windows();
        switch_task((int)(INT_PTR)wp);
        return 0;
    default:
        if (msg == settings_changed_msg && msg) {
            read_settings();
            refresh_taskbar();
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_taskbar(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_COPYDATA:
        if (handle_notify((COPYDATASTRUCT *)lp)) {
            refresh_taskbar();
            return TRUE;
        }
        return FALSE;
    case WM_TIMER:
        switch (wp) {
        case TIMER_POLL: {
            BOOL changed;
            hide_explorer_taskbar();
            changed = scan_windows();
            changed |= prune_trays();
            changed |= check_fullscreen();
            fit_maximized_windows();
            if (changed) refresh_taskbar();
            break;
        }
        case TIMER_CLOCK:
            if (update_clock_text()) refresh_taskbar();
            break;
        case TIMER_TIP:
            KillTimer(hwnd, TIMER_TIP);
            if (hot_part >= 0 && pressed_part < 0) show_tip(hot_part);
            break;
        case TIMER_HOVER:
            poll_hover();
            break;
        case TIMER_ANNOUNCE:
            KillTimer(hwnd, TIMER_ANNOUNCE);
            /* programs started before us put their icons in explorer's tray: ask again */
            PostMessageW(HWND_BROADCAST, taskbar_created, 0, 0);
            break;
        }
        return 0;
    case WM_MOUSEMOVE: {
        int part = hit_part(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (part != hot_part) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            hot_part = part;
            TrackMouseEvent(&tme);
            hide_tip();
            if (part >= 0) SetTimer(hwnd, TIMER_TIP, 600, NULL);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        hot_part = -1;
        hide_tip();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        hide_tip();
        pressed_part = hit_part(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (pressed_part >= 0) SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int part = hit_part(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        int pressed = pressed_part;
        pressed_part = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (part >= 0 && part == pressed) click_part(part);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_RBUTTONUP: {
        int part = hit_part(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd, &pt);
        hide_tip();
        if (part >= 0 && parts[part].kind == PART_TASK) task_menu(parts[part].index, pt);
        else if (part < 0 || parts[part].kind != PART_TRAY) taskbar_menu(pt);
        return 0;
    }
    case WM_APP_TOGGLE_START:
        toggle_start_menu();
        return 0;
    case WM_APP_SHOW_DESKTOP:
        toggle_show_desktop();
        return 0;
    case WM_APP_FILES:
        open_files();
        return 0;
    case WM_APP_SETTINGS:
        open_settings(NULL);
        return 0;
    case WM_APP_TASK_MANAGER:
        run(L"taskmgr.exe", NULL);
        return 0;
    case WM_APP_RUN:
        run(L"rundll32.exe", L"shell32.dll,Control_RunDLL");
        return 0;
    case WM_APP_APPBAR:
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        update_geometry(FALSE);
        return 0;
    case WM_SYSCOLORCHANGE:
        choose_palette();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) ExitProcess(0);
        return 0;
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------------------------- */
/* Windows key                                                                               */
/* ---------------------------------------------------------------------------------------- */

static BOOL win_down, win_combo;

static LRESULT CALLBACK keyboard_hook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT *key = (const KBDLLHOOKSTRUCT *)lp;
        BOOL down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;

        if (key->vkCode == VK_LWIN || key->vkCode == VK_RWIN) {
            if (down) {
                if (!win_down) win_combo = FALSE;
                win_down = TRUE;
            }
            else {
                if (win_down && !win_combo) PostMessageW(taskbar, WM_APP_TOGGLE_START, 0, 0);
                win_down = FALSE;
            }
        }
        else if (down && key->vkCode == VK_ESCAPE && GetAsyncKeyState(VK_CONTROL) < 0 && GetAsyncKeyState(VK_MENU) >= 0) {
            /* Ctrl+Shift+Esc: Task Manager; Ctrl+Esc: Start (when the Windows key can't be used) */
            if (GetAsyncKeyState(VK_SHIFT) < 0) PostMessageW(taskbar, WM_APP_TASK_MANAGER, 0, 0);
            else PostMessageW(taskbar, WM_APP_TOGGLE_START, 0, 0);
            return 1;
        }
        else if (key->vkCode == VK_TAB && GetAsyncKeyState(VK_MENU) < 0) {
            if (down) PostMessageW(taskbar, WM_APP_SWITCH_TASK,
                                   GetAsyncKeyState(VK_SHIFT) < 0 ? (WPARAM)-1 : (WPARAM)1, 0);
            return 1;
        }
        else if (down && win_down) {
            win_combo = TRUE;
            switch (key->vkCode) {
            case 'D': PostMessageW(taskbar, WM_APP_SHOW_DESKTOP, 0, 0); return 1;
            case 'E': PostMessageW(taskbar, WM_APP_FILES, 0, 0); return 1;
            case 'I': PostMessageW(taskbar, WM_APP_SETTINGS, 0, 0); return 1;
            case 'S': case 'Q': PostMessageW(taskbar, WM_APP_TOGGLE_START, 0, 0); return 1;
            case 'M': PostMessageW(taskbar, WM_APP_SHOW_DESKTOP, 0, 0); return 1;
            }
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

/* ---------------------------------------------------------------------------------------- */
/* Startup                                                                                   */
/* ---------------------------------------------------------------------------------------- */

/*
 * Folders that programs open (ShellExecute "open" or "explore", folder icons on the desktop)
 * come to the S11 File Explorer instead of Wine's. Wine's HKEY_CLASSES_ROOT is the machine
 * key alone, and its Folder class asks the "Folders" DDE server first, so that goes too.
 */
static void register_folder_handler(void)
{
    static const WCHAR *verbs[] = {L"open", L"explore"};
    WCHAR command[MAX_PATH + 16], key[96], value[MAX_PATH + 16];
    DWORD size;
    int i;

    if (GetFileAttributesW(explorer_exe()) == INVALID_FILE_ATTRIBUTES) return;
    wsprintfW(command, L"\"%s\" \"%%1\"", explorer_exe());
    for (i = 0; i < ARRAY_SIZE(verbs); i++) {
        wsprintfW(key, L"Software\\Classes\\Folder\\shell\\%s\\command", verbs[i]);
        size = sizeof(value);
        if (!RegGetValueW(HKEY_LOCAL_MACHINE, key, NULL, RRF_RT_REG_SZ, NULL, value, &size) && !lstrcmpiW(value, command))
            continue;
        RegSetKeyValueW(HKEY_LOCAL_MACHINE, key, NULL, REG_SZ, command, (lstrlenW(command) + 1) * sizeof(WCHAR));
        wsprintfW(key, L"Software\\Classes\\Folder\\shell\\%s\\ddeexec", verbs[i]);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, key);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
    }
}

static HANDLE make_system_process(void)
{
    /* Like explorer and services: don't keep the session alive, and learn when it ends */
    HANDLE event = NULL;
    NtSetInformationProcess(GetCurrentProcess(), (PROCESSINFOCLASS)1000 /* ProcessWineMakeProcessSystem */,
                            &event, sizeof(HANDLE *));
    return event;
}

static void create_windows(void)
{
    WNDCLASSEXW wc = {sizeof(wc)};
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};

    InitCommonControlsEx(&icc);

    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);

    wc.lpfnWndProc = taskbar_proc;
    wc.lpszClassName = L"Shell_TrayWnd";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = menu_proc;
    wc.lpszClassName = L"S11StartMenu";
    wc.style = CS_DROPSHADOW;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = tooltip_proc;
    wc.lpszClassName = L"S11Tooltip";
    wc.style = 0;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = hover_proc;
    wc.lpszClassName = L"S11CaptionHover";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = calendar_proc;
    wc.lpszClassName = L"S11Calendar";
    wc.style = CS_DROPSHADOW;
    RegisterClassExW(&wc);

    taskbar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"Shell_TrayWnd", L"",
                              WS_POPUP | WS_CLIPCHILDREN, 0, 0, 100, 10, NULL, NULL, instance, NULL);
    start_menu = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"S11StartMenu", TR(L"시작", L"Start"),
                                 WS_POPUP | WS_CLIPCHILDREN, 0, 0, 100, 100, NULL, NULL, instance, NULL);
    tooltip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                              L"S11Tooltip", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, instance, NULL);
    /* layered + transparent: Wine passes clicks to the window below */
    hover_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                                   L"S11CaptionHover", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, instance, NULL);
    SetLayeredWindowAttributes(hover_window, 0, 255, LWA_ALPHA);
    calendar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"S11Calendar", TR(L"달력", L"Calendar"),
                               WS_POPUP | WS_CLIPCHILDREN, 0, 0, 100, 100, NULL, NULL, instance, NULL);
    search_edit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  0, 0, 100, 20, start_menu, (HMENU)1, instance, NULL);
    SendMessageW(search_edit, WM_SETFONT, (WPARAM)search_font, FALSE);
    SendMessageW(search_edit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)TR(L"앱, 설정, 명령 검색", L"Search apps or type a command"));
    SetWindowSubclass(search_edit, search_proc, 1, 0);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, WCHAR *cmdline, int show)
{
    struct GdiplusStartupInput gdiplus_input = {1};
    HANDLE session_end, mutex;
    MSG msg;

    instance = inst;
    mutex = CreateMutexW(NULL, TRUE, L"Local\\WinlatorS11Shell");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDPIAware();
    dpi = GetDpiForSystem();
    korean = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN ||
             PRIMARYLANGID(LANGIDFROMLCID(GetUserDefaultLCID())) == LANG_KOREAN;
    choose_palette();
    GdiplusStartup(&gdiplus_token, &gdiplus_input, NULL);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    ui_font = make_font(100, FW_NORMAL);
    small_font = make_font(90, FW_NORMAL);
    menu_title_font = make_font(105, FW_SEMIBOLD);
    search_font = make_font(105, FW_NORMAL);
    calendar_title_font = make_font(120, FW_SEMIBOLD);

    settings_changed_msg = RegisterWindowMessageW(L"WinlatorS11SettingsChanged");
    read_settings();
    register_folder_handler();
    create_windows();
    update_clock_text();
    scan_windows();
    update_geometry(TRUE);

    SetTimer(taskbar, TIMER_POLL, 250, NULL);
    SetTimer(taskbar, TIMER_CLOCK, 1000, NULL);
    SetTimer(taskbar, TIMER_ANNOUNCE, 400, NULL);
    SetTimer(taskbar, TIMER_HOVER, 40, NULL);
    SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, inst, 0);

    session_end = make_system_process();
    for (;;) {
        DWORD wait = MsgWaitForMultipleObjectsEx(session_end ? 1 : 0, &session_end, INFINITE, QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE);
        if (session_end && wait == WAIT_OBJECT_0) break;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:
    GdiplusShutdown(gdiplus_token);
    if (mutex) CloseHandle(mutex);
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
