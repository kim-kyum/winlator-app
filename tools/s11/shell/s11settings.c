/*
 * Winlator S11 Settings: a Windows 11 style settings app inside the container.
 *
 *  - System: display scale (LogPixels), screen resolution (stored by Winlator) and the
 *    Windows version programs see.
 *  - Personalization: background (the S11 pictures or your own), light or dark mode
 *    (stored by Winlator), taskbar alignment.
 *  - Apps: installed programs from the Uninstall keys; uninstall them, install from a file.
 *  - Fonts: installed families with a preview; install fonts from files.
 *  - About: Winlator S11, Wine, Windows, processor, memory and display.
 *
 * Settings Wine keeps (registry) are written directly. Settings that belong to the
 * container in Winlator (resolution, mode, background) go to the app over winhandler's
 * UDP port and take effect the next time the container starts, or right away where Wine
 * can apply them itself (the background).
 */
#include "s11ui.h"
#include <commdlg.h>
#include <uxtheme.h>

#define WINLATOR_SET_SCREEN 102
#define WINLATOR_SET_THEME 103
#define WINLATOR_SET_WALLPAPER 104

#define S11_KEY L"Software\\Winlator\\S11"
#define WALLPAPER_DIR L"Z:\\usr\\share\\s11\\wallpapers"
#define WALLPAPER_BMP L"Z:\\home\\xuser\\.cache\\wallpaper.bmp"
#define USER_WALLPAPER L"Z:\\home\\xuser\\.config\\user-wallpaper.png"

#define WM_APP_REFRESH_APPS (WM_APP + 20)
#define WM_APP_SELECT_PAGE (WM_APP + 21)   /* another start asked for a page */

enum page { PAGE_SYSTEM, PAGE_PERSONALIZE, PAGE_APPS, PAGE_FONTS, PAGE_ABOUT, PAGE_COUNT };

enum control_id {
    ID_SCALE = 100, ID_RESOLUTION, ID_WINVER, ID_RESTART, ID_MODE, ID_ALIGN,
    ID_APP_LIST, ID_UNINSTALL, ID_INSTALL, ID_FONT_LIST, ID_FONT_INSTALL, ID_BROWSE
};

static HINSTANCE instance;
static HWND main_window;
static HFONT ui_font, small_font, title_font, nav_font, big_font;
static ULONG_PTR gdiplus_token;
static enum page current_page;
static int hot_nav = -1;
static BOOL restart_needed;
static int scroll_y, content_bottom;

/* controls of each page */
static HWND scale_box, resolution_box, winver_box, restart_button, mode_box, align_box, browse_button;
static HWND app_list, uninstall_button, install_button, font_list, font_install_button;
static HIMAGELIST app_images, font_images;

/* ---------------------------------------------------------------------------------------- */
/* Pages as cards                                                                            */
/* ---------------------------------------------------------------------------------------- */

struct row {
    enum page page;
    const WCHAR *title;
    WCHAR detail[200];
    HWND *control;      /* on the right of the card */
    HWND *control2;     /* a second button next to it */
    int height;         /* at 96 dpi */
    int list_height;    /* a list under the title instead (apps, fonts) */
    BOOL wallpapers;    /* the background pictures */
    RECT rc;
};

#define MAX_ROWS 24
static struct row rows[MAX_ROWS];
static int row_count;

static struct row *add_row(enum page page, const WCHAR *title, HWND *control)
{
    struct row *r = &rows[row_count++];
    ZeroMemory(r, sizeof(*r));
    r->page = page;
    r->title = title;
    r->control = control;
    r->height = 68;
    return r;
}

static const WCHAR *page_title(enum page page)
{
    switch (page) {
    case PAGE_SYSTEM: return TR(L"시스템", L"System");
    case PAGE_PERSONALIZE: return TR(L"개인 설정", L"Personalization");
    case PAGE_APPS: return TR(L"앱", L"Apps");
    case PAGE_FONTS: return TR(L"글꼴", L"Fonts");
    case PAGE_ABOUT: return TR(L"정보", L"About");
    default: return L"";
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Registry helpers                                                                          */
/* ---------------------------------------------------------------------------------------- */

static DWORD reg_get_dword(HKEY root, const WCHAR *key, const WCHAR *name, DWORD fallback)
{
    DWORD value, size = sizeof(value);
    if (RegGetValueW(root, key, name, RRF_RT_REG_DWORD, NULL, &value, &size)) return fallback;
    return value;
}

static BOOL reg_get_string(HKEY root, const WCHAR *key, const WCHAR *name, WCHAR *out, DWORD chars)
{
    DWORD size = chars * sizeof(WCHAR), type = 0;
    out[0] = 0;
    /* RegGetValue refuses RRF_RT_REG_EXPAND_SZ without RRF_NOEXPAND */
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type, out, &size)) {
        out[0] = 0;
        return FALSE;
    }
    if (type == REG_EXPAND_SZ && wcschr(out, '%')) {
        WCHAR *expanded = HeapAlloc(GetProcessHeap(), 0, chars * sizeof(WCHAR));
        if (expanded) {
            DWORD n = ExpandEnvironmentStringsW(out, expanded, chars);
            if (n && n <= chars) lstrcpynW(out, expanded, chars);
            HeapFree(GetProcessHeap(), 0, expanded);
        }
    }
    return TRUE;
}

static void reg_set_dword(HKEY root, const WCHAR *key, const WCHAR *name, DWORD value)
{
    RegSetKeyValueW(root, key, name, REG_DWORD, &value, sizeof(value));
}

static void reg_set_string(HKEY root, const WCHAR *key, const WCHAR *name, const WCHAR *value)
{
    RegSetKeyValueW(root, key, name, REG_SZ, value, (lstrlenW(value) + 1) * sizeof(WCHAR));
}

/* A message to Winlator with a short ASCII payload (winhandler's buffer is 64 bytes) */
static void send_winlator_value(BYTE code, const char *value)
{
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in addr = {0};
    char packet[64];
    int len = lstrlenA(value);

    if (len > (int)sizeof(packet) - 2) return;
    packet[0] = (char)code;
    memcpy(packet + 1, value, len + 1);
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        addr.sin_family = AF_INET;
        addr.sin_port = htons(WINLATOR_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sendto(s, packet, len + 2, 0, (struct sockaddr *)&addr, sizeof(addr));
        closesocket(s);
    }
    WSACleanup();
}

/* Taller rows for list views: a small image list sets the row height */
static void set_row_height(HWND list, int height, HIMAGELIST *images, int icon)
{
    if (*images) ImageList_Destroy(*images);
    *images = ImageList_Create(icon ? icon : 1, height, ILC_COLOR32 | ILC_MASK, 8, 8);
    SendMessageW(list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)*images);
}

/* ---------------------------------------------------------------------------------------- */
/* System                                                                                    */
/* ---------------------------------------------------------------------------------------- */

static const int scales[] = {96, 120, 144, 168, 192};
static const SIZE resolutions[] = {{1280, 800}, {1680, 1050}, {1920, 1200}, {2560, 1600}};
static int resolution_count;
static SIZE resolution_items[8];
static SIZE pending_screen;      /* chosen here, used from the next start */
static int pending_mode = -1;    /* 0 light, 1 dark */

/* The scale that keeps things the size they are at 1920x1200 and 150% */
static int recommended_dpi(int width)
{
    int i, best = 2, target = 144 * width / 1920;
    for (i = 0; i < ARRAY_SIZE(scales); i++)
        if (abs(scales[i] - target) < abs(scales[best] - target)) best = i;
    return scales[best];
}

static void fill_scales(int width)
{
    WCHAR text[64];
    int i, selected = -1, dpi_now = (int)reg_get_dword(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"LogPixels", dpi);
    int recommended = recommended_dpi(width);

    SendMessageW(scale_box, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < ARRAY_SIZE(scales); i++) {
        wsprintfW(text, L"%d%%%s", scales[i] * 100 / 96, scales[i] == recommended ? TR(L" (권장)", L" (recommended)") : L"");
        SendMessageW(scale_box, CB_ADDSTRING, 0, (LPARAM)text);
        if (scales[i] == dpi_now) selected = i;
    }
    SendMessageW(scale_box, CB_SETCURSEL, selected >= 0 ? selected : 2, 0);
}

struct win_version {
    const WCHAR *name;
    const WCHAR *current_version;
    DWORD major, minor, build;
    const WCHAR *csd;
};

/* the same values Winlator's container settings write (WinVersions.java) */
static const struct win_version win_versions[] = {
    {L"Windows 11", L"6.3", 10, 0, 22000, L""},
    {L"Windows 10", L"6.3", 10, 0, 19043, L""},
    {L"Windows 8.1", L"6.3", 6, 3, 9600, L""},
    {L"Windows 7", L"6.1", 6, 1, 7601, L"Service Pack 1"},
    {L"Windows XP", L"5.1", 5, 1, 2600, L"Service Pack 3"},
};

static void fill_system(void)
{
    WCHAR text[64], build[32];
    int i, width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN), selected = -1;
    DWORD current_build;

    if (pending_screen.cx) {
        width = pending_screen.cx;
        height = pending_screen.cy;
    }
    fill_scales(width);

    SendMessageW(resolution_box, CB_RESETCONTENT, 0, 0);
    resolution_count = 0;
    selected = -1;
    for (i = 0; i < ARRAY_SIZE(resolutions); i++) {
        resolution_items[resolution_count++] = resolutions[i];
        if (resolutions[i].cx == width && resolutions[i].cy == height) selected = i;
    }
    if (selected < 0) {
        resolution_items[resolution_count].cx = width;
        resolution_items[resolution_count].cy = height;
        selected = resolution_count++;
    }
    for (i = 0; i < resolution_count; i++) {
        const WCHAR *note = L"";
        if (resolution_items[i].cx == 1920 && resolution_items[i].cy == 1200) note = TR(L" (권장)", L" (recommended)");
        else if (resolution_items[i].cx == 2560 && resolution_items[i].cy == 1600) note = TR(L" (태블릿 화면 그대로)", L" (native)");
        wsprintfW(text, L"%d × %d%s", resolution_items[i].cx, resolution_items[i].cy, note);
        SendMessageW(resolution_box, CB_ADDSTRING, 0, (LPARAM)text);
    }
    SendMessageW(resolution_box, CB_SETCURSEL, selected, 0);

    SendMessageW(winver_box, CB_RESETCONTENT, 0, 0);
    reg_get_string(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber",
                   build, ARRAY_SIZE(build));
    current_build = wcstoul(build, NULL, 10);
    selected = 1;
    for (i = 0; i < ARRAY_SIZE(win_versions); i++) {
        SendMessageW(winver_box, CB_ADDSTRING, 0, (LPARAM)win_versions[i].name);
        if (win_versions[i].build == current_build) selected = i;
    }
    SendMessageW(winver_box, CB_SETCURSEL, selected, 0);
}

static void set_win_version(int index)
{
    static const WCHAR *const keys[] = {
        L"Software\\Microsoft\\Windows NT\\CurrentVersion",
        L"Software\\Wow6432Node\\Microsoft\\Windows NT\\CurrentVersion"
    };
    const struct win_version *v = &win_versions[index];
    WCHAR build[16], product[64];
    int i;

    wsprintfW(build, L"%u", v->build);
    wsprintfW(product, L"Microsoft %s", v->name);
    for (i = 0; i < ARRAY_SIZE(keys); i++) {
        reg_set_string(HKEY_LOCAL_MACHINE, keys[i], L"CurrentVersion", v->current_version);
        reg_set_dword(HKEY_LOCAL_MACHINE, keys[i], L"CurrentMajorVersionNumber", v->major);
        reg_set_dword(HKEY_LOCAL_MACHINE, keys[i], L"CurrentMinorVersionNumber", v->minor);
        reg_set_string(HKEY_LOCAL_MACHINE, keys[i], L"CSDVersion", v->csd);
        reg_set_string(HKEY_LOCAL_MACHINE, keys[i], L"CurrentBuild", build);
        reg_set_string(HKEY_LOCAL_MACHINE, keys[i], L"CurrentBuildNumber", build);
        reg_set_string(HKEY_LOCAL_MACHINE, keys[i], L"ProductName", product);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Personalization                                                                           */
/* ---------------------------------------------------------------------------------------- */

struct wallpaper {
    const WCHAR *id;          /* what Winlator stores */
    const WCHAR *file;
    GpBitmap *thumb;          /* NULL when the picture is missing (no picture of your own yet) */
    RECT rc;
};

static struct wallpaper wallpapers[] = {
    {L"wallpaper-s11", WALLPAPER_DIR L"\\s11-dark.jpg"},
    {L"wallpaper-s11-light", WALLPAPER_DIR L"\\s11-light.jpg"},
    {L"user", USER_WALLPAPER},
};
static WCHAR wallpaper_choice[64];
static int hot_wallpaper = -1;

#define THUMB_W 176
#define THUMB_H 110
#define TIMER_REPAINT 1

/* Draws an image so it covers the rectangle, cropping the overflow evenly */
static void draw_cover(GpGraphics *g, GpImage *image, float x, float y, float w, float h)
{
    UINT iw = 0, ih = 0;
    float scale, sw, sh;
    GdipGetImageWidth(image, &iw);
    GdipGetImageHeight(image, &ih);
    if (!iw || !ih) return;
    scale = max(w / iw, h / ih);
    sw = w / scale;
    sh = h / scale;
    GdipDrawImageRectRect(g, image, x, y, w, h, (iw - sw) / 2, (ih - sh) / 2, sw, sh, UnitPixel, NULL, NULL, NULL);
}

/* A small copy of a picture: drawing the full pictures on every paint would be slow */
static GpBitmap *make_thumbnail(const WCHAR *file)
{
    GpImage *image = NULL;
    GpBitmap *thumb = NULL;
    GpGraphics *g = NULL;
    int w = SC(THUMB_W), h = SC(THUMB_H);

    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) return NULL;
    if (GdipLoadImageFromFile(file, &image) != Ok || !image) return NULL;
    if (GdipCreateBitmapFromScan0(w, h, 0, PixelFormat32bppPARGB, NULL, &thumb) == Ok &&
        GdipGetImageGraphicsContext((GpImage *)thumb, &g) == Ok) {
        GdipSetInterpolationMode(g, InterpolationModeHighQualityBicubic);
        GdipSetPixelOffsetMode(g, PixelOffsetModeHalf);
        draw_cover(g, image, 0, 0, (float)w, (float)h);
        GdipDeleteGraphics(g);
    }
    GdipDisposeImage(image);
    return thumb;
}

static void load_thumbnails(BOOL reload_user)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(wallpapers); i++) {
        BOOL user = !lstrcmpW(wallpapers[i].id, L"user");
        if (wallpapers[i].thumb && !(user && reload_user)) continue;
        if (wallpapers[i].thumb) GdipDisposeImage((GpImage *)wallpapers[i].thumb);
        wallpapers[i].thumb = make_thumbnail(wallpapers[i].file);
    }
    /* Winlator writes it with the container's background (empty for a plain colour) */
    if (!reg_get_string(HKEY_CURRENT_USER, S11_KEY, L"Wallpaper", wallpaper_choice, ARRAY_SIZE(wallpaper_choice)))
        lstrcpyW(wallpaper_choice, L"wallpaper-s11");
}

static BOOL CALLBACK redraw_window(HWND hwnd, LPARAM param)
{
    if (IsWindowVisible(hwnd)) RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

/*
 * Wine's desktop paints a new background over the windows on it without asking them to
 * paint again (title bars and the taskbar would stay covered), so ask them a little later.
 */
static void repaint_windows_soon(void)
{
    SetTimer(main_window, TIMER_REPAINT, 300, NULL);
}

static BOOL write_bmp(const WCHAR *path, const BYTE *bits, int width, int height)
{
    BITMAPFILEHEADER file = {0};
    BITMAPINFOHEADER info = {sizeof(info), width, height, 1, 24, BI_RGB};
    int stride = (width * 3 + 3) & ~3, y, x;
    BYTE *row = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, stride);
    HANDLE f;
    DWORD written;
    BOOL ok = TRUE;

    if (!row) return FALSE;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, row);
        return FALSE;
    }
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + stride * height;
    info.biSizeImage = stride * height;
    WriteFile(f, &file, sizeof(file), &written, NULL);
    WriteFile(f, &info, sizeof(info), &written, NULL);
    for (y = height - 1; y >= 0 && ok; y--) {   /* bottom-up rows */
        const BYTE *src = bits + (size_t)y * width * 4;
        for (x = 0; x < width; x++) {
            row[x * 3] = src[x * 4];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        ok = WriteFile(f, row, stride, &written, NULL);
    }
    CloseHandle(f);
    HeapFree(GetProcessHeap(), 0, row);
    return ok;
}

/* PNG encoder of GDI+ */
static const CLSID png_encoder = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};

/* Renders a picture at the screen size into Winlator's wallpaper bitmap and shows it */
static BOOL apply_wallpaper(const WCHAR *file, const WCHAR *id, BOOL keep_copy)
{
    int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    BITMAPINFO bmi = {{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB}};
    GpImage *image = NULL;
    GpGraphics *g = NULL;
    HDC dc;
    HBITMAP bitmap;
    void *bits;
    BOOL ok;
    HCURSOR old_cursor = SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_WAIT));
    char value[64];

    if (GdipLoadImageFromFile(file, &image) != Ok || !image) {
        SetCursor(old_cursor);
        return FALSE;
    }
    CreateDirectoryW(L"Z:\\home\\xuser\\.cache", NULL);
    if (keep_copy) CreateDirectoryW(L"Z:\\home\\xuser\\.config", NULL);
    dc = CreateCompatibleDC(NULL);
    bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(dc, bitmap);
    GdipCreateFromHDC(dc, &g);
    GdipSetInterpolationMode(g, InterpolationModeHighQualityBicubic);
    draw_cover(g, image, 0, 0, (float)width, (float)height);
    GdipFlush(g, FlushIntentionSync);
    GdipDeleteGraphics(g);

    if (keep_copy) {
        /* Winlator renders the background again from this file when the container starts */
        UINT iw = 0, ih = 0;
        GdipGetImageWidth(image, &iw);
        GdipGetImageHeight(image, &ih);
        if (iw > 2560 || ih > 2560) {
            float scale = 2560.0f / max(iw, ih);
            GpBitmap *reduced = NULL;
            GpGraphics *sg = NULL;
            GdipCreateBitmapFromScan0((INT)(iw * scale), (INT)(ih * scale), 0, PixelFormat32bppARGB, NULL, &reduced);
            GdipGetImageGraphicsContext((GpImage *)reduced, &sg);
            GdipSetInterpolationMode(sg, InterpolationModeHighQualityBicubic);
            GdipDrawImageRectI(sg, image, 0, 0, (INT)(iw * scale), (INT)(ih * scale));
            GdipDeleteGraphics(sg);
            GdipSaveImageToFile((GpImage *)reduced, USER_WALLPAPER, &png_encoder, NULL);
            GdipDisposeImage((GpImage *)reduced);
        }
        else GdipSaveImageToFile(image, USER_WALLPAPER, &png_encoder, NULL);
    }
    GdipDisposeImage(image);

    ok = write_bmp(WALLPAPER_BMP, bits, width, height);
    DeleteDC(dc);
    DeleteObject(bitmap);
    if (ok) {
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void *)WALLPAPER_BMP, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        repaint_windows_soon();
        if (keep_copy) load_thumbnails(TRUE);
        lstrcpynW(wallpaper_choice, id, ARRAY_SIZE(wallpaper_choice));
        reg_set_string(HKEY_CURRENT_USER, S11_KEY, L"Wallpaper", id);
        WideCharToMultiByte(CP_UTF8, 0, id, -1, value, sizeof(value), NULL, NULL);
        send_winlator_value(WINLATOR_SET_WALLPAPER, value);
    }
    SetCursor(old_cursor);
    return ok;
}

static void browse_wallpaper(void)
{
    WCHAR file[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = main_window;
    ofn.lpstrFilter = korean ? L"사진 (*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0모든 파일\0*.*\0"
                             : L"Pictures (*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;
    if (!apply_wallpaper(file, L"user", TRUE))
        MessageBoxW(main_window, TR(L"이 사진을 열 수 없어요.", L"This picture can't be opened."), L"Winlator S11", MB_ICONWARNING);
    InvalidateRect(main_window, NULL, FALSE);
}

static void fill_personalize(void)
{
    COLORREF face = GetSysColor(COLOR_BTNFACE);
    BOOL dark = (GetRValue(face) * 299 + GetGValue(face) * 587 + GetBValue(face) * 114) / 1000 < 128;
    SendMessageW(mode_box, CB_RESETCONTENT, 0, 0);
    SendMessageW(mode_box, CB_ADDSTRING, 0, (LPARAM)TR(L"밝게", L"Light"));
    SendMessageW(mode_box, CB_ADDSTRING, 0, (LPARAM)TR(L"어둡게", L"Dark"));
    SendMessageW(mode_box, CB_SETCURSEL, pending_mode >= 0 ? pending_mode : dark ? 1 : 0, 0);

    SendMessageW(align_box, CB_RESETCONTENT, 0, 0);
    SendMessageW(align_box, CB_ADDSTRING, 0, (LPARAM)TR(L"가운데", L"Center"));
    SendMessageW(align_box, CB_ADDSTRING, 0, (LPARAM)TR(L"왼쪽", L"Left"));
    SendMessageW(align_box, CB_SETCURSEL, reg_get_dword(HKEY_CURRENT_USER, S11_KEY, L"TaskbarAlignment", 0) ? 1 : 0, 0);
    load_thumbnails(FALSE);
}

/* ---------------------------------------------------------------------------------------- */
/* Apps                                                                                      */
/* ---------------------------------------------------------------------------------------- */

struct installed_app {
    WCHAR name[128];
    WCHAR publisher[96];
    WCHAR version[48];
    WCHAR uninstall[MAX_PATH * 2];
    WCHAR icon[MAX_PATH];      /* "file,index" or "file" */
    DWORD size_kb;
};

static struct installed_app *installed;
static int installed_count, installed_capacity;

static void add_installed(HKEY root, const WCHAR *path, REGSAM view)
{
    HKEY key;
    DWORD i;
    WCHAR sub[256];

    if (RegOpenKeyExW(root, path, 0, KEY_READ | view, &key)) return;
    for (i = 0; ; i++) {
        DWORD len = ARRAY_SIZE(sub);
        HKEY app_key;
        struct installed_app app = {0};
        int j;
        BOOL duplicate = FALSE;

        if (RegEnumKeyExW(key, i, sub, &len, NULL, NULL, NULL, NULL)) break;
        if (RegOpenKeyExW(key, sub, 0, KEY_READ | view, &app_key)) continue;
        reg_get_string(app_key, NULL, L"DisplayName", app.name, ARRAY_SIZE(app.name));
        reg_get_string(app_key, NULL, L"Publisher", app.publisher, ARRAY_SIZE(app.publisher));
        reg_get_string(app_key, NULL, L"DisplayVersion", app.version, ARRAY_SIZE(app.version));
        reg_get_string(app_key, NULL, L"UninstallString", app.uninstall, ARRAY_SIZE(app.uninstall));
        reg_get_string(app_key, NULL, L"DisplayIcon", app.icon, ARRAY_SIZE(app.icon));
        app.size_kb = reg_get_dword(app_key, NULL, L"EstimatedSize", 0);
        if (reg_get_dword(app_key, NULL, L"SystemComponent", 0) == 1) app.name[0] = 0;
        RegCloseKey(app_key);
        if (!app.name[0]) continue;
        for (j = 0; j < installed_count; j++) if (!lstrcmpiW(installed[j].name, app.name)) duplicate = TRUE;
        if (duplicate) continue;

        if (installed_count == installed_capacity) {
            int capacity = installed_capacity ? installed_capacity * 2 : 32;
            struct installed_app *bigger = installed
                ? HeapReAlloc(GetProcessHeap(), 0, installed, capacity * sizeof(*installed))
                : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*installed));
            if (!bigger) break;
            installed = bigger;
            installed_capacity = capacity;
        }
        installed[installed_count++] = app;
    }
    RegCloseKey(key);
}

/* The icon an app's DisplayIcon names ("C:\\path\\app.exe,0", maybe in quotes) */
static HICON installed_icon(const WCHAR *location, int size)
{
    WCHAR path[MAX_PATH];
    WCHAR *comma;
    int index = 0;
    HICON icon = NULL;

    if (!location[0]) return NULL;
    lstrcpynW(path, location[0] == '"' ? location + 1 : location, MAX_PATH);
    comma = wcsrchr(path, ',');
    if (comma && (iswdigit(comma[1]) || comma[1] == '-')) {
        index = _wtoi(comma + 1);
        *comma = 0;
    }
    if (path[0] && path[lstrlenW(path) - 1] == '"') path[lstrlenW(path) - 1] = 0;
    if (PrivateExtractIconsW(path, index, size, size, &icon, NULL, 1, 0) != 1) icon = NULL;
    return icon;
}

static int compare_installed(const void *a, const void *b)
{
    const struct installed_app *x = a, *y = b;
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, x->name, -1, y->name, -1, NULL, NULL, 0) - 2;
}

static void fill_apps(void)
{
    static const WCHAR uninstall_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    int i;

    installed_count = 0;
    add_installed(HKEY_LOCAL_MACHINE, uninstall_key, KEY_WOW64_64KEY);
    add_installed(HKEY_LOCAL_MACHINE, uninstall_key, KEY_WOW64_32KEY);
    add_installed(HKEY_CURRENT_USER, uninstall_key, 0);
    if (installed_count) qsort(installed, installed_count, sizeof(*installed), compare_installed);

    SendMessageW(app_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(app_list, LVM_DELETEALLITEMS, 0, 0);
    set_row_height(app_list, SC(32), &app_images, SC(32));
    for (i = 0; i < installed_count; i++) {
        LVITEMW item = {0};
        WCHAR size[32] = L"";
        HICON icon = installed_icon(installed[i].icon, SC(32));
        item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        item.iItem = i;
        item.lParam = i;
        item.iImage = ImageList_AddIcon(app_images, icon ? icon : LoadIconW(NULL, (const WCHAR *)IDI_APPLICATION));
        if (icon) DestroyIcon(icon);
        item.pszText = installed[i].name;
        SendMessageW(app_list, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.mask = LVIF_TEXT;
        item.iSubItem = 1;
        item.pszText = installed[i].publisher;
        SendMessageW(app_list, LVM_SETITEMTEXTW, i, (LPARAM)&item);
        item.iSubItem = 2;
        item.pszText = installed[i].version;
        SendMessageW(app_list, LVM_SETITEMTEXTW, i, (LPARAM)&item);
        if (installed[i].size_kb) {
            if (installed[i].size_kb >= 1024 * 1024) wsprintfW(size, L"%u.%u GB", installed[i].size_kb / (1024 * 1024),
                                                             installed[i].size_kb % (1024 * 1024) * 10 / (1024 * 1024));
            else if (installed[i].size_kb >= 1024) wsprintfW(size, L"%u MB", installed[i].size_kb / 1024);
            else wsprintfW(size, L"%u KB", installed[i].size_kb);
        }
        item.iSubItem = 3;
        item.pszText = size;
        SendMessageW(app_list, LVM_SETITEMTEXTW, i, (LPARAM)&item);
    }
    SendMessageW(app_list, WM_SETREDRAW, TRUE, 0);
    EnableWindow(uninstall_button, FALSE);
}

static DWORD WINAPI wait_for_uninstaller(void *process)
{
    WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    PostMessageW(main_window, WM_APP_REFRESH_APPS, 0, 0);
    return 0;
}

static void uninstall_selected(void)
{
    int index = (int)SendMessageW(app_list, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    WCHAR command[MAX_PATH * 2], question[300];

    if (index < 0 || index >= installed_count) return;
    if (!installed[index].uninstall[0]) {
        MessageBoxW(main_window, TR(L"이 앱은 제거 방법을 알려 주지 않았어요.", L"This app has no uninstaller."),
                    installed[index].name, MB_ICONINFORMATION);
        return;
    }
    wsprintfW(question, TR(L"%s을(를) 제거할까요?", L"Uninstall %s?"), installed[index].name);
    if (MessageBoxW(main_window, question, TR(L"앱 제거", L"Uninstall"), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    lstrcpynW(command, installed[index].uninstall, ARRAY_SIZE(command));
    if (CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(CreateThread(NULL, 0, wait_for_uninstaller, pi.hProcess, 0, NULL));
    }
    else MessageBoxW(main_window, TR(L"제거 프로그램을 시작할 수 없어요.", L"The uninstaller could not start."),
                     installed[index].name, MB_ICONWARNING);
}

static void install_app(void)
{
    WCHAR file[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = main_window;
    ofn.lpstrFilter = korean ? L"설치 파일 (*.exe;*.msi)\0*.exe;*.msi\0모든 파일\0*.*\0"
                             : L"Installers (*.exe;*.msi)\0*.exe;*.msi\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) run(file, NULL);
}

/* ---------------------------------------------------------------------------------------- */
/* Fonts                                                                                     */
/* ---------------------------------------------------------------------------------------- */

static WCHAR (*families)[LF_FACESIZE];
static int family_count, family_capacity;
static WCHAR preview_family[LF_FACESIZE];

static int CALLBACK collect_family(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    int i;
    if (lf->lfFaceName[0] == '@') return 1;  /* vertical variants */
    if (type & RASTER_FONTTYPE) return 1;    /* old bitmap fonts (System, Courier, MS Sans Serif) */
    for (i = 0; i < family_count; i++) if (!lstrcmpiW(families[i], lf->lfFaceName)) return 1;
    if (family_count == family_capacity) {
        int capacity = family_capacity ? family_capacity * 2 : 128;
        void *bigger = families ? HeapReAlloc(GetProcessHeap(), 0, families, capacity * sizeof(*families))
                                : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*families));
        if (!bigger) return 0;
        families = bigger;
        family_capacity = capacity;
    }
    lstrcpynW(families[family_count++], lf->lfFaceName, LF_FACESIZE);
    return 1;
}

static int compare_families(const void *a, const void *b)
{
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a, -1, b, -1, NULL, NULL, 0) - 2;
}

static void fill_fonts(void)
{
    LOGFONTW lf = {0};
    HDC dc = GetDC(main_window);
    int i;

    family_count = 0;
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, collect_family, 0, 0);
    ReleaseDC(main_window, dc);
    if (family_count) qsort(families, family_count, sizeof(*families), compare_families);

    SendMessageW(font_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(font_list, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < family_count; i++) {
        LVITEMW item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = families[i];
        SendMessageW(font_list, LVM_INSERTITEMW, 0, (LPARAM)&item);
    }
    SendMessageW(font_list, WM_SETREDRAW, TRUE, 0);
}

static void install_fonts(void)
{
    WCHAR files[4096] = {0}, dir[MAX_PATH], fonts_dir[MAX_PATH], *name;
    OPENFILENAMEW ofn = {sizeof(ofn)};
    int installed_fonts = 0;

    ofn.hwndOwner = main_window;
    ofn.lpstrFilter = korean ? L"글꼴 (*.ttf;*.otf;*.ttc)\0*.ttf;*.otf;*.ttc\0" : L"Fonts (*.ttf;*.otf;*.ttc)\0*.ttf;*.otf;*.ttc\0";
    ofn.lpstrFile = files;
    ofn.nMaxFile = ARRAY_SIZE(files);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return;

    GetWindowsDirectoryW(fonts_dir, MAX_PATH);
    lstrcatW(fonts_dir, L"\\Fonts");
    /* one file: the full path; several: the folder, then the names */
    lstrcpynW(dir, files, MAX_PATH);
    name = files + lstrlenW(files) + 1;
    if (!*name) {
        WCHAR *slash = wcsrchr(dir, '\\');
        if (!slash) return;
        *slash = 0;
        name = files + (slash - dir) + 1;
    }
    for (; *name; name += lstrlenW(name) + 1) {
        WCHAR source[MAX_PATH], target[MAX_PATH], value[MAX_PATH];
        int len = lstrlenW(name);
        wsprintfW(source, L"%s\\%s", dir, name);
        wsprintfW(target, L"%s\\%s", fonts_dir, name);
        if (!CopyFileW(source, target, FALSE)) continue;
        AddFontResourceExW(target, 0, NULL);
        lstrcpynW(value, name, max(1, len - 4 + 1));
        lstrcatW(value, len > 4 && !lstrcmpiW(name + len - 4, L".otf") ? L" (OpenType)" : L" (TrueType)");
        reg_set_string(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", value, name);
        installed_fonts++;
    }
    if (installed_fonts) {
        WCHAR text[96];
        SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 1000, NULL);
        fill_fonts();
        wsprintfW(text, TR(L"글꼴 %d개를 설치했어요.", L"Installed %d font file(s)."), installed_fonts);
        MessageBoxW(main_window, text, TR(L"글꼴", L"Fonts"), MB_ICONINFORMATION);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* About                                                                                     */
/* ---------------------------------------------------------------------------------------- */

static WCHAR about_text[6][160];

static void fill_about(void)
{
    WCHAR value[128], build[32];
    const char *(CDECL *wine_get_version)(void);
    MEMORYSTATUSEX memory = {sizeof(memory)};
    SYSTEM_INFO info;

    if (!GetEnvironmentVariableW(L"S11_VERSION", value, ARRAY_SIZE(value))) lstrcpyW(value, L"-");
    wsprintfW(about_text[0], L"%s", value);
    if (GetEnvironmentVariableW(L"S11_CONTAINER", value, ARRAY_SIZE(value))) lstrcpynW(about_text[1], value, 160);
    else lstrcpyW(about_text[1], L"-");

    wine_get_version = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version");
    if (wine_get_version) MultiByteToWideChar(CP_UTF8, 0, wine_get_version(), -1, about_text[2], 160);

    reg_get_string(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", value, ARRAY_SIZE(value));
    reg_get_string(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber", build, ARRAY_SIZE(build));
    wsprintfW(about_text[3], TR(L"%s (빌드 %s)", L"%s (build %s)"), value[0] ? value : L"Windows", build);

    GetNativeSystemInfo(&info);
    reg_get_string(HKEY_LOCAL_MACHINE, L"Hardware\\Description\\System\\CentralProcessor\\0", L"ProcessorNameString",
                   value, ARRAY_SIZE(value));
    wsprintfW(about_text[4], TR(L"%s · 코어 %u개", L"%s · %u cores"), value[0] ? value : L"-", info.dwNumberOfProcessors);

    GlobalMemoryStatusEx(&memory);
    wsprintfW(about_text[5], TR(L"메모리 %u GB · 화면 %d × %d, 배율 %d%%", L"Memory %u GB · display %d × %d at %d%%"),
              (UINT)((memory.ullTotalPhys + (512ull << 20)) >> 30), GetSystemMetrics(SM_CXSCREEN),
              GetSystemMetrics(SM_CYSCREEN), (int)(dpi * 100 / 96));
}

/* ---------------------------------------------------------------------------------------- */
/* Layout and painting                                                                       */
/* ---------------------------------------------------------------------------------------- */

static int nav_w(void) { return SC(280); }

static void nav_item_rect(int index, RECT *rc)
{
    int top = SC(124) + index * SC(44);
    SetRect(rc, SC(12), top, nav_w() - SC(12), top + SC(40));
}

static void build_rows(void)
{
    struct row *r;
    row_count = 0;

    r = add_row(PAGE_SYSTEM, TR(L"디스플레이 배율", L"Display scale"), &scale_box);
    lstrcpyW(r->detail, TR(L"글자와 창의 크기. 다시 시작하면 적용돼요.", L"Size of text and windows. Applies after a restart."));
    r = add_row(PAGE_SYSTEM, TR(L"해상도", L"Resolution"), &resolution_box);
    lstrcpyW(r->detail, TR(L"높을수록 선명하고, 낮을수록 빨라요. 다시 시작하면 적용돼요.",
                           L"Higher is sharper, lower is faster. Applies after a restart."));
    r = add_row(PAGE_SYSTEM, TR(L"Windows 버전", L"Windows version"), &winver_box);
    lstrcpyW(r->detail, TR(L"프로그램에 알려 줄 Windows 버전. 새로 여는 프로그램부터 적용돼요.",
                           L"The version programs see. Applies to programs you open next."));

    r = add_row(PAGE_PERSONALIZE, TR(L"배경", L"Background"), &browse_button);
    lstrcpyW(r->detail, TR(L"그림을 고르거나 내 사진을 쓰세요.", L"Pick a picture or use your own."));
    r->height = 220;
    r->wallpapers = TRUE;
    r = add_row(PAGE_PERSONALIZE, TR(L"모드", L"Mode"), &mode_box);
    lstrcpyW(r->detail, TR(L"창과 메뉴의 밝기. 다시 시작하면 적용돼요.", L"Light or dark windows. Applies after a restart."));
    r = add_row(PAGE_PERSONALIZE, TR(L"작업 표시줄 맞춤", L"Taskbar alignment"), &align_box);
    lstrcpyW(r->detail, TR(L"아이콘을 가운데 또는 왼쪽에 둡니다.", L"Icons in the middle or on the left."));

    r = add_row(PAGE_APPS, TR(L"설치된 앱", L"Installed apps"), &install_button);
    r->control2 = &uninstall_button;
    lstrcpyW(r->detail, TR(L"앱을 고르고 [제거]를 누르세요.", L"Select an app and press Uninstall."));
    r->list_height = 360;

    r = add_row(PAGE_FONTS, TR(L"설치된 글꼴", L"Installed fonts"), &font_install_button);
    lstrcpyW(r->detail, TR(L".ttf, .otf, .ttc 파일을 설치할 수 있어요.", L"You can install .ttf, .otf and .ttc files."));
    r->list_height = 300;

    r = add_row(PAGE_ABOUT, L"Winlator S11", NULL);
    r->height = 96;
    r = add_row(PAGE_ABOUT, TR(L"사양", L"Specifications"), NULL);
    r->height = 184;
}

static void layout(void)
{
    RECT client;
    int x, y, w, i;
    HWND controls[] = {scale_box, resolution_box, winver_box, mode_box, align_box, browse_button, app_list,
                       uninstall_button, install_button, font_list, font_install_button, restart_button};

    GetClientRect(main_window, &client);
    for (i = 0; i < ARRAY_SIZE(controls); i++) ShowWindow(controls[i], SW_HIDE);

    x = nav_w() + SC(24);
    w = client.right - x - SC(32);
    y = SC(84) - scroll_y;
    if (restart_needed && (current_page == PAGE_SYSTEM || current_page == PAGE_PERSONALIZE)) {
        SetWindowPos(restart_button, NULL, x + w - SC(150), y + SC(12), SC(136), SC(32), SWP_NOZORDER | SWP_SHOWWINDOW);
        y += SC(64);
    }
    for (i = 0; i < row_count; i++) {
        struct row *r = &rows[i];
        int h = SC(r->height) + (r->list_height ? SC(r->list_height) : 0);
        if (r->page != current_page) continue;
        SetRect(&r->rc, x, y, x + w, y + h);
        if (r->control && *r->control) {
            HWND c = *r->control;
            BOOL is_button = c == browse_button || c == install_button || c == font_install_button;
            int cw = is_button ? SC(150) : SC(240), ch = SC(34), top = y + (SC(68) - ch) / 2;
            if (is_button) SetWindowPos(c, NULL, x + w - cw - SC(16), top, cw, ch, SWP_NOZORDER | SWP_SHOWWINDOW);
            else {
                /* a combo box's height includes its drop-down list; centre the field itself */
                RECT field;
                SetWindowPos(c, NULL, x + w - cw - SC(16), top, cw, SC(320), SWP_NOZORDER | SWP_SHOWWINDOW);
                GetWindowRect(c, &field);
                SetWindowPos(c, NULL, x + w - cw - SC(16), y + (SC(68) - (field.bottom - field.top)) / 2, 0, 0,
                             SWP_NOZORDER | SWP_NOSIZE);
            }
            if (r->control2 && *r->control2)
                SetWindowPos(*r->control2, NULL, x + w - cw * 2 - SC(24), top, cw, ch, SWP_NOZORDER | SWP_SHOWWINDOW);
        }
        if (r->list_height) {
            HWND list = current_page == PAGE_APPS ? app_list : font_list;
            int list_h = SC(r->list_height) - SC(16) - (current_page == PAGE_FONTS ? SC(70) : 0);
            int scrollbar = GetSystemMetrics(SM_CXVSCROLL) + SC(4);
            SetWindowPos(list, NULL, x + SC(16), y + SC(r->height), w - SC(32), list_h, SWP_NOZORDER | SWP_SHOWWINDOW);
            if (list == app_list)
                SendMessageW(list, LVM_SETCOLUMNWIDTH, 0, max(SC(160), w - SC(32) - SC(170 + 110 + 80) - scrollbar));
            else SendMessageW(list, LVM_SETCOLUMNWIDTH, 0, w - SC(32) - scrollbar);
        }
        if (r->wallpapers) {
            int tw = SC(THUMB_W), th = SC(THUMB_H), k, n = 0;
            for (k = 0; k < ARRAY_SIZE(wallpapers); k++) {
                int left = x + SC(16) + n * (tw + SC(12));
                if (!wallpapers[k].thumb) {
                    SetRectEmpty(&wallpapers[k].rc);
                    continue;
                }
                SetRect(&wallpapers[k].rc, left, y + SC(84), left + tw, y + SC(84) + th);
                n++;
            }
        }
        y += h + SC(8);
    }
    content_bottom = y + scroll_y + SC(16);
}

/* Scrolls the page (the navigation stays) */
static void scroll_page(int delta)
{
    RECT client;
    int limit;
    GetClientRect(main_window, &client);
    limit = max(0, content_bottom - client.bottom);
    delta = max(-scroll_y, min(delta, limit - scroll_y));
    if (!delta) return;
    scroll_y += delta;
    layout();
    InvalidateRect(main_window, NULL, FALSE);
}

static void draw_nav_glyph(struct canvas *c, enum page page, float cx, float cy, float s, COLORREF color)
{
    GpPen *pen = make_pen(argb(color, 255), s * 0.09f);
    switch (page) {
    case PAGE_SYSTEM: {    /* a screen on a stand */
        RECT rc = {(LONG)(cx - s * 0.42f), (LONG)(cy - s * 0.34f), (LONG)(cx + s * 0.42f), (LONG)(cy + s * 0.20f)};
        GdipDrawRectangle(c->g, pen, (float)rc.left, (float)rc.top, (float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
        GdipDrawLine(c->g, pen, cx, cy + s * 0.20f, cx, cy + s * 0.38f);
        GdipDrawLine(c->g, pen, cx - s * 0.22f, cy + s * 0.38f, cx + s * 0.22f, cy + s * 0.38f);
        break;
    }
    case PAGE_PERSONALIZE: { /* a brush over a stroke of paint */
        GpSolidFill *paint;
        GpPath *tip;
        GdipDrawLine(c->g, pen, cx + s * 0.38f, cy - s * 0.40f, cx + s * 0.02f, cy - s * 0.02f);
        GdipCreatePath(FillModeWinding, &tip);
        /* the bristles: a drop leaning down to the left */
        GdipAddPathBezier(tip, cx + s * 0.06f, cy - s * 0.10f, cx + s * 0.14f, cy + s * 0.02f,
                          cx + s * 0.04f, cy + s * 0.14f, cx - s * 0.06f, cy + s * 0.12f);
        GdipAddPathBezier(tip, cx - s * 0.06f, cy + s * 0.12f, cx - s * 0.16f, cy + s * 0.10f,
                          cx - s * 0.20f, cy + s * 0.20f, cx - s * 0.34f, cy + s * 0.22f);
        GdipAddPathBezier(tip, cx - s * 0.34f, cy + s * 0.22f, cx - s * 0.28f, cy + s * 0.06f,
                          cx - s * 0.16f, cy - s * 0.04f, cx - s * 0.06f, cy - s * 0.04f);
        GdipClosePathFigure(tip);
        GdipCreateSolidFill(argb(color, 255), &paint);
        GdipFillPath(c->g, (GpBrush *)paint, tip);
        GdipDeleteBrush((GpBrush *)paint);
        GdipDeletePath(tip);
        GdipDrawLine(c->g, pen, cx - s * 0.40f, cy + s * 0.40f, cx + s * 0.36f, cy + s * 0.40f);
        break;
    }
    case PAGE_APPS: {      /* a list of apps */
        int i;
        for (i = 0; i < 3; i++) {
            float y = cy - s * 0.30f + i * s * 0.30f;
            GdipDrawRectangle(c->g, pen, cx - s * 0.40f, y - s * 0.09f, s * 0.18f, s * 0.18f);
            GdipDrawLine(c->g, pen, cx - s * 0.08f, y, cx + s * 0.40f, y);
        }
        break;
    }
    case PAGE_FONTS:       /* the letter A */
        GdipDrawLine(c->g, pen, cx - s * 0.32f, cy + s * 0.36f, cx, cy - s * 0.38f);
        GdipDrawLine(c->g, pen, cx, cy - s * 0.38f, cx + s * 0.32f, cy + s * 0.36f);
        GdipDrawLine(c->g, pen, cx - s * 0.18f, cy + s * 0.08f, cx + s * 0.18f, cy + s * 0.08f);
        break;
    case PAGE_ABOUT:       /* an i in a circle */
        GdipDrawEllipse(c->g, pen, cx - s * 0.40f, cy - s * 0.40f, s * 0.80f, s * 0.80f);
        GdipDrawLine(c->g, pen, cx, cy - s * 0.04f, cx, cy + s * 0.22f);
        GdipDrawLine(c->g, pen, cx, cy - s * 0.20f, cx, cy - s * 0.19f);
        break;
    default:
        break;
    }
    GdipDeletePen(pen);
}

static void paint(HDC hdc)
{
    struct canvas c;
    RECT client, rc;
    int i, x, w;
    COLORREF card = pal.field, background = pal.menu;

    GetClientRect(main_window, &client);
    if (!canvas_begin(&c, hdc, client.right, client.bottom)) return;
    fill_rect(&c, 0, 0, client.right, client.bottom, argb(background, 255));

    /* navigation */
    draw_logo(&c, (float)(SC(24) + SC(24)), (float)SC(58), (float)SC(48));
    SetRect(&rc, SC(84), SC(30), nav_w(), SC(58));
    draw_text(&c, L"Winlator S11", &rc, title_font, pal.text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    SetRect(&rc, SC(84), SC(60), nav_w(), SC(84));
    draw_text(&c, TR(L"설정", L"Settings"), &rc, small_font, pal.text_dim, DT_LEFT | DT_TOP | DT_SINGLELINE);
    for (i = 0; i < PAGE_COUNT; i++) {
        RECT item;
        nav_item_rect(i, &item);
        if (i == (int)current_page) {
            RECT bar = {item.left, item.top + SC(10), item.left + SC(3), item.bottom - SC(10)};
            fill_round_rect(&c, &item, (float)SC(4), pal.active);
            fill_round_rect(&c, &bar, (float)SC(2), argb(pal.accent, 255));
        }
        else if (i == hot_nav) fill_round_rect(&c, &item, (float)SC(4), pal.hover);
        draw_nav_glyph(&c, (enum page)i, (float)(item.left + SC(26)), (float)((item.top + item.bottom) / 2), (float)SC(18), pal.text);
        rc = item;
        rc.left += SC(52);
        draw_text(&c, page_title((enum page)i), &rc, nav_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* page */
    x = nav_w() + SC(24);
    w = client.right - x - SC(32);
    SetRect(&rc, x, SC(20) - scroll_y, x + w, SC(72) - scroll_y);
    draw_text(&c, page_title(current_page), &rc, big_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (restart_needed && (current_page == PAGE_SYSTEM || current_page == PAGE_PERSONALIZE)) {
        RECT bar = {x, SC(84) - scroll_y, x + w, SC(84) - scroll_y + SC(56)};
        fill_round_rect(&c, &bar, (float)SC(6), argb(pal.accent, 40));
        bar.left += SC(16);
        draw_text(&c, TR(L"일부 변경 사항은 컨테이너를 다시 시작하면 적용돼요.", L"Some changes apply after the container restarts."),
                  &bar, ui_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    for (i = 0; i < row_count; i++) {
        struct row *r = &rows[i];
        RECT text;
        int text_right;
        if (r->page != current_page) continue;
        fill_round_rect(&c, &r->rc, (float)SC(6), argb(card, 255));
        stroke_round_rect(&c, &r->rc, (float)SC(6), argb(pal.menu_border, 160), 1);

        text_right = r->rc.right - (r->control2 ? SC(350) : r->control ? SC(280) : SC(20));
        SetRect(&text, r->rc.left + SC(20), r->rc.top + SC(14), text_right, r->rc.top + SC(38));
        draw_text(&c, r->title, &text, ui_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (r->detail[0]) {
            SetRect(&text, r->rc.left + SC(20), r->rc.top + SC(38), text_right, r->rc.top + SC(60));
            draw_text(&c, r->detail, &text, small_font, pal.text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        if (r->wallpapers) {
            int k;
            for (k = 0; k < ARRAY_SIZE(wallpapers); k++) {
                struct wallpaper *wp = &wallpapers[k];
                RECT frame = wp->rc;
                BOOL selected = !lstrcmpW(wallpaper_choice, wp->id);
                GpPath *path;
                if (!wp->thumb) continue;
                path = round_rect_path((float)frame.left, (float)frame.top, (float)(frame.right - frame.left),
                                       (float)(frame.bottom - frame.top), (float)SC(6));
                GdipSetClipPath(c.g, path, CombineModeReplace);
                GdipDrawImageRectI(c.g, (GpImage *)wp->thumb, frame.left, frame.top, frame.right - frame.left,
                                   frame.bottom - frame.top);
                GdipResetClip(c.g);
                GdipDeletePath(path);
                if (!lstrcmpW(wp->id, L"user")) {
                    /* "your picture" label on the tile */
                    RECT label = {frame.left, frame.bottom - SC(26), frame.right, frame.bottom};
                    GpPath *bottom = round_rect_path((float)label.left, (float)label.top, (float)(label.right - label.left),
                                                     (float)(label.bottom - label.top), 0);
                    GpSolidFill *shade;
                    GdipSetClipPath(c.g, path = round_rect_path((float)frame.left, (float)frame.top,
                                    (float)(frame.right - frame.left), (float)(frame.bottom - frame.top), (float)SC(6)),
                                    CombineModeReplace);
                    GdipCreateSolidFill(0x99000000, &shade);
                    GdipFillPath(c.g, (GpBrush *)shade, bottom);
                    GdipDeleteBrush((GpBrush *)shade);
                    GdipResetClip(c.g);
                    GdipDeletePath(path);
                    GdipDeletePath(bottom);
                    draw_text(&c, TR(L"내 사진", L"Your picture"), &label, small_font, RGB(255, 255, 255),
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                if (selected) {
                    InflateRect(&frame, SC(3), SC(3));
                    stroke_round_rect(&c, &frame, (float)SC(8), argb(pal.accent, 255), (float)SC(3));
                }
                else if (k == hot_wallpaper) {
                    InflateRect(&frame, SC(2), SC(2));
                    stroke_round_rect(&c, &frame, (float)SC(7), argb(pal.text_dim, 255), (float)SC(1));
                }
            }
        }

        if (current_page == PAGE_FONTS && r->list_height && preview_family[0]) {
            RECT box = {r->rc.left + SC(16), r->rc.bottom - SC(78), r->rc.right - SC(16), r->rc.bottom - SC(12)};
            LOGFONTW lf = {0};
            HFONT font;
            lf.lfHeight = -SC(26);
            lf.lfCharSet = DEFAULT_CHARSET;
            lstrcpynW(lf.lfFaceName, preview_family, LF_FACESIZE);
            font = CreateFontIndirectW(&lf);
            fill_round_rect(&c, &box, (float)SC(4), pal.hover);
            box.left += SC(12);
            draw_text(&c, TR(L"다람쥐 헌 쳇바퀴에 타고파 · The quick brown fox 0123", L"The quick brown fox jumps · 다람쥐 0123"),
                      &box, font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DeleteObject(font);
        }

        if (current_page == PAGE_ABOUT) {
            if (i == row_count - 2) {   /* Winlator S11 */
                RECT line = {r->rc.left + SC(20), r->rc.top + SC(44), r->rc.right - SC(20), r->rc.top + SC(66)};
                WCHAR text2[200];
                wsprintfW(text2, TR(L"버전 %s  ·  컨테이너: %s", L"Version %s  ·  Container: %s"), about_text[0], about_text[1]);
                draw_text(&c, text2, &line, small_font, pal.text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            else {
                static const WCHAR *const labels_ko[] = {L"Wine", L"Windows", L"프로세서", L"메모리·화면"};
                static const WCHAR *const labels_en[] = {L"Wine", L"Windows", L"Processor", L"Memory, display"};
                int k;
                for (k = 0; k < 4; k++) {
                    RECT label = {r->rc.left + SC(20), r->rc.top + SC(44) + k * SC(32), r->rc.left + SC(170), r->rc.top + SC(72) + k * SC(32)};
                    RECT value = {r->rc.left + SC(170), label.top, r->rc.right - SC(20), label.bottom};
                    draw_text(&c, korean ? labels_ko[k] : labels_en[k], &label, small_font, pal.text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    draw_text(&c, about_text[2 + k], &value, small_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
        }
    }
    canvas_end(&c, hdc, 0, 0);
}

static void select_page(enum page page)
{
    current_page = page;
    scroll_y = 0;
    switch (page) {
    case PAGE_SYSTEM: fill_system(); break;
    case PAGE_PERSONALIZE: fill_personalize(); break;
    case PAGE_APPS: fill_apps(); break;
    case PAGE_FONTS: fill_fonts(); break;
    case PAGE_ABOUT: fill_about(); break;
    default: break;
    }
    layout();
    InvalidateRect(main_window, NULL, FALSE);
}

static void need_restart(void)
{
    if (restart_needed) return;
    restart_needed = TRUE;
    layout();
    InvalidateRect(main_window, NULL, FALSE);
}

/* ---------------------------------------------------------------------------------------- */
/* Window                                                                                    */
/* ---------------------------------------------------------------------------------------- */

static LRESULT CALLBACK combo_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    if (msg == WM_MOUSEWHEEL && !SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0))
        return SendMessageW(main_window, WM_MOUSEWHEEL, wp, lp);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static HWND make_combo(int id)
{
    HWND combo = CreateWindowExW(0, WC_COMBOBOXW, NULL, WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST |
                                 CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                                 0, 0, 100, SC(300), main_window, (HMENU)(INT_PTR)id, instance, NULL);
    SendMessageW(combo, WM_SETFONT, (WPARAM)ui_font, FALSE);
    SetWindowSubclass(combo, combo_proc, 1, 0);
    return combo;
}

static HWND make_button(int id, const WCHAR *text)
{
    HWND button = CreateWindowExW(0, WC_BUTTONW, text, WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                  0, 0, 100, 30, main_window, (HMENU)(INT_PTR)id, instance, NULL);
    SendMessageW(button, WM_SETFONT, (WPARAM)ui_font, FALSE);
    return button;
}

static HWND make_list(int id, BOOL header)
{
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, NULL,
                                WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                                (header ? 0 : LVS_NOCOLUMNHEADER),
                                0, 0, 100, 100, main_window, (HMENU)(INT_PTR)id, instance, NULL);
    SendMessageW(list, WM_SETFONT, (WPARAM)ui_font, FALSE);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(list, L"Explorer", NULL);
    return list;
}

static COLORREF blend(COLORREF a, COLORREF b, int alpha /* of b, 0-255 */)
{
    return RGB((GetRValue(a) * (255 - alpha) + GetRValue(b) * alpha) / 255,
               (GetGValue(a) * (255 - alpha) + GetGValue(b) * alpha) / 255,
               (GetBValue(a) * (255 - alpha) + GetBValue(b) * alpha) / 255);
}

static LRESULT list_custom_draw(NMLVCUSTOMDRAW *cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        if (SendMessageW(cd->nmcd.hdr.hwndFrom, LVM_GETITEMSTATE, cd->nmcd.dwItemSpec, LVIS_SELECTED) & LVIS_SELECTED) {
            cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
            cd->clrTextBk = blend(GetSysColor(COLOR_WINDOW), pal.accent, 40);
            cd->clrText = GetSysColor(COLOR_WINDOWTEXT);
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}

/* Items of the drop-down lists: tall rows, a soft highlight and an accent mark on the choice */
static void draw_combo_item(const DRAWITEMSTRUCT *dis)
{
    WCHAR text[160] = L"";
    RECT rc = dis->rcItem;
    BOOL field = (dis->itemState & ODS_COMBOBOXEDIT) != 0;
    BOOL hot = !field && (dis->itemState & ODS_SELECTED);
    BOOL current = !field && (int)dis->itemID == (int)SendMessageW(dis->hwndItem, CB_GETCURSEL, 0, 0);
    struct canvas c;

    if (dis->itemID != (UINT)-1) SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM)text);
    if (!canvas_begin(&c, dis->hDC, rc.right - rc.left, rc.bottom - rc.top)) return;
    fill_rect(&c, 0, 0, rc.right - rc.left, rc.bottom - rc.top, argb(IsWindowEnabled(dis->hwndItem) ? pal.field : pal.menu, 255));
    if (hot || current) {
        RECT bg = {SC(3), SC(2), rc.right - rc.left - SC(3), rc.bottom - rc.top - SC(2)};
        fill_round_rect(&c, &bg, (float)SC(4), hot ? pal.hover : pal.active);
        if (current) {
            RECT mark = {SC(3), (rc.bottom - rc.top) / 2 - SC(8), SC(6), (rc.bottom - rc.top) / 2 + SC(8)};
            fill_round_rect(&c, &mark, SC(3) / 2.0f, argb(pal.accent, 255));
        }
    }
    {
        RECT text_rc = {field ? SC(6) : SC(14), 0, rc.right - rc.left - SC(6), rc.bottom - rc.top};
        draw_text(&c, text, &text_rc, ui_font, IsWindowEnabled(dis->hwndItem) ? pal.text : pal.text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    canvas_end(&c, dis->hDC, rc.left, rc.top);
}

static void add_column(HWND list, int index, const WCHAR *title, int width)
{
    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = (WCHAR *)title;
    col.cx = SC(width);
    SendMessageW(list, LVM_INSERTCOLUMNW, index, (LPARAM)&col);
}

static void create_controls(void)
{
    scale_box = make_combo(ID_SCALE);
    resolution_box = make_combo(ID_RESOLUTION);
    winver_box = make_combo(ID_WINVER);
    mode_box = make_combo(ID_MODE);
    align_box = make_combo(ID_ALIGN);
    restart_button = make_button(ID_RESTART, TR(L"지금 다시 시작", L"Restart now"));
    browse_button = make_button(ID_BROWSE, TR(L"내 사진 찾기…", L"Browse photos…"));
    install_button = make_button(ID_INSTALL, TR(L"앱 설치…", L"Install an app…"));
    uninstall_button = make_button(ID_UNINSTALL, TR(L"제거", L"Uninstall"));
    font_install_button = make_button(ID_FONT_INSTALL, TR(L"글꼴 설치…", L"Install fonts…"));

    app_list = make_list(ID_APP_LIST, TRUE);
    add_column(app_list, 0, TR(L"이름", L"Name"), 280);
    add_column(app_list, 1, TR(L"게시자", L"Publisher"), 170);
    add_column(app_list, 2, TR(L"버전", L"Version"), 110);
    add_column(app_list, 3, TR(L"크기", L"Size"), 80);
    font_list = make_list(ID_FONT_LIST, FALSE);
    add_column(font_list, 0, TR(L"글꼴", L"Font"), 560);
    set_row_height(font_list, SC(30), &font_images, 0);
}

static enum page initial_page;

/* "system", "personalization", "apps", "fonts" or "about"; PAGE_COUNT when none of them */
static enum page page_from_name(const WCHAR *name)
{
    static const WCHAR *const names[PAGE_COUNT] = {L"system", L"personalization", L"apps", L"fonts", L"about"};
    int i;
    for (i = 0; i < PAGE_COUNT; i++) if (name && !lstrcmpiW(name, names[i])) return (enum page)i;
    return PAGE_COUNT;
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        main_window = hwnd;
        create_controls();
        build_rows();
        select_page(initial_page);
        return 0;
    case WM_SIZE:
        layout();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lp;
        info->ptMinTrackSize.x = SC(820);
        info->ptMinTrackSize.y = SC(520);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lp;
        if (mis->CtlType == ODT_COMBOBOX) {
            mis->itemHeight = mis->itemID == (UINT)-1 ? SC(26) : SC(34);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lp;
        if (dis->CtlType == ODT_COMBOBOX) {
            draw_combo_item(dis);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        static HBRUSH brush;
        static COLORREF color;
        if (!brush || color != pal.field) {
            if (brush) DeleteObject(brush);
            brush = CreateSolidBrush(pal.field);
            color = pal.field;
        }
        SetBkColor((HDC)wp, pal.field);
        return (LRESULT)brush;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int hot = -1, hot_wp = -1, i;
        for (i = 0; i < PAGE_COUNT; i++) {
            RECT rc;
            nav_item_rect(i, &rc);
            if (PtInRect(&rc, pt)) hot = i;
        }
        if (current_page == PAGE_PERSONALIZE)
            for (i = 0; i < ARRAY_SIZE(wallpapers); i++) if (PtInRect(&wallpapers[i].rc, pt)) hot_wp = i;
        if (hot != hot_nav || hot_wp != hot_wallpaper) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            hot_nav = hot;
            hot_wallpaper = hot_wp;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        hot_nav = hot_wallpaper = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int i;
        for (i = 0; i < PAGE_COUNT; i++) {
            RECT rc;
            nav_item_rect(i, &rc);
            if (PtInRect(&rc, pt) && i != (int)current_page) select_page((enum page)i);
        }
        if (current_page == PAGE_PERSONALIZE) {
            for (i = 0; i < ARRAY_SIZE(wallpapers); i++) {
                if (PtInRect(&wallpapers[i].rc, pt) && lstrcmpW(wallpaper_choice, wallpapers[i].id)) {
                    apply_wallpaper(wallpapers[i].file, wallpapers[i].id, FALSE);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        scroll_page(-GET_WHEEL_DELTA_WPARAM(wp) * SC(60) / WHEEL_DELTA);
        return 0;
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lp;
        if ((hdr->idFrom == ID_APP_LIST || hdr->idFrom == ID_FONT_LIST) && hdr->code == NM_CUSTOMDRAW)
            return list_custom_draw((NMLVCUSTOMDRAW *)lp);
        if (hdr->idFrom == ID_APP_LIST && hdr->code == LVN_ITEMCHANGED)
            EnableWindow(uninstall_button, SendMessageW(app_list, LVM_GETNEXTITEM, -1, LVNI_SELECTED) >= 0);
        if (hdr->idFrom == ID_FONT_LIST && hdr->code == LVN_ITEMCHANGED) {
            int index = (int)SendMessageW(font_list, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
            if (index >= 0 && index < family_count) lstrcpynW(preview_family, families[index], LF_FACESIZE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_APP_REFRESH_APPS:
        if (current_page == PAGE_APPS) fill_apps();
        return 0;
    case WM_TIMER:
        if (wp == TIMER_REPAINT) {
            static int passes;
            EnumWindows(redraw_window, 0);
            if (++passes >= 2) {
                KillTimer(hwnd, TIMER_REPAINT);
                passes = 0;
            }
            else SetTimer(hwnd, TIMER_REPAINT, 1200, NULL);
        }
        return 0;
    case WM_APP_SELECT_PAGE:
        if (wp < PAGE_COUNT) select_page((enum page)wp);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW((HWND)lp, CB_GETCURSEL, 0, 0);
            if (sel < 0) return 0;
            switch (id) {
            case ID_SCALE:
                reg_set_dword(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"LogPixels", scales[sel]);
                need_restart();
                break;
            case ID_RESOLUTION: {
                char value[32];
                int old_width = pending_screen.cx ? pending_screen.cx : GetSystemMetrics(SM_CXSCREEN);
                int dpi_now = (int)reg_get_dword(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"LogPixels", dpi);
                pending_screen = resolution_items[sel];
                wsprintfA(value, "%dx%d", resolution_items[sel].cx, resolution_items[sel].cy);
                send_winlator_value(WINLATOR_SET_SCREEN, value);
                /* sharper, not smaller: the scale follows when it was the recommended one */
                if (dpi_now == recommended_dpi(old_width))
                    reg_set_dword(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"LogPixels",
                                  recommended_dpi(pending_screen.cx));
                fill_scales(pending_screen.cx);
                need_restart();
                break;
            }
            case ID_WINVER:
                set_win_version(sel);
                break;
            case ID_MODE:
                pending_mode = sel;
                send_winlator_value(WINLATOR_SET_THEME, sel ? "DARK" : "LIGHT");
                need_restart();
                break;
            case ID_ALIGN:
                reg_set_dword(HKEY_CURRENT_USER, S11_KEY, L"TaskbarAlignment", sel);
                PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"WinlatorS11SettingsChanged"), 0, 0);
                break;
            }
            return 0;
        }
        switch (id) {
        case ID_RESTART:
            send_winlator_request(WINLATOR_RESTART);
            break;
        case ID_BROWSE:
            browse_wallpaper();
            break;
        case ID_INSTALL:
            install_app();
            break;
        case ID_UNINSTALL:
            uninstall_selected();
            break;
        case ID_FONT_INSTALL:
            install_fonts();
            break;
        }
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        choose_palette();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, WCHAR *cmdline, int show)
{
    struct GdiplusStartupInput gdiplus_input = {1};
    WNDCLASSEXW wc = {sizeof(wc)};
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES};
    HWND existing;
    MSG msg;
    RECT work;
    int w, h;

    instance = inst;
    initial_page = page_from_name(cmdline);
    if ((existing = FindWindowW(L"S11Settings", NULL))) {
        if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
        if (initial_page != PAGE_COUNT) PostMessageW(existing, WM_APP_SELECT_PAGE, initial_page, 0);
        SetForegroundWindow(existing);
        return 0;
    }
    if (initial_page == PAGE_COUNT) initial_page = PAGE_SYSTEM;

    SetProcessDPIAware();
    dpi = GetDpiForSystem();
    korean = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN ||
             PRIMARYLANGID(LANGIDFROMLCID(GetUserDefaultLCID())) == LANG_KOREAN;
    choose_palette();
    GdiplusStartup(&gdiplus_token, &gdiplus_input, NULL);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitCommonControlsEx(&icc);

    ui_font = make_font(100, FW_NORMAL);
    small_font = make_font(90, FW_NORMAL);
    nav_font = make_font(100, FW_NORMAL);
    title_font = make_font(120, FW_SEMIBOLD);
    big_font = make_font(200, FW_SEMIBOLD);

    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"S11Settings";
    RegisterClassExW(&wc);

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    w = min(SC(1040), work.right - work.left);
    h = min(SC(720), work.bottom - work.top - SC(72));
    CreateWindowExW(0, L"S11Settings", TR(L"설정", L"Settings"), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                    (work.right - w) / 2, max(0, (work.bottom - SC(72) - h) / 2), w, h, NULL, NULL, inst, NULL);
    ShowWindow(main_window, SW_SHOWNORMAL);
    UpdateWindow(main_window);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (IsDialogMessageW(main_window, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
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
