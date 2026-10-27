/*
 * Shared by the Winlator S11 shell and settings: drawing (GDI+ for anti-aliased shapes,
 * GDI for text and icons), the S11 colours and mark, and messages to Winlator.
 */
#ifndef S11UI_H
#define S11UI_H

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <winternl.h>
#include <gdiplus.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* each program uses only some of the helpers below */
#pragma clang diagnostic ignored "-Wunused-function"

/* ---------------------------------------------------------------------------------------- */
/* Basics                                                                                   */
/* ---------------------------------------------------------------------------------------- */

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static UINT dpi = 96;
static int SC(int value) { return MulDiv(value, (int)dpi, 96); }

static BOOL korean;
#define TR(ko, en) (korean ? (ko) : (en))

/* Messages Winlator understands on winhandler's UDP port (WinHandler.java) */
#define WINLATOR_PORT 7947
#define WINLATOR_SHUTDOWN 100
#define WINLATOR_RESTART 101
#define WINLATOR_OPEN_WIFI 105
#define WINLATOR_OPEN_BLUETOOTH 106
#define WINLATOR_SCREENSHOT 107

struct palette {
    COLORREF bar, bar_border, text, text_dim, accent, indicator;
    COLORREF menu, menu_border, field, field_border;
    ARGB hover, pressed, active;
};

static const struct palette light_palette = {
    RGB(238, 241, 247), RGB(213, 217, 224), RGB(26, 26, 26), RGB(96, 96, 96), RGB(0, 95, 184), RGB(138, 138, 138),
    RGB(243, 244, 248), RGB(200, 204, 212), RGB(255, 255, 255), RGB(214, 218, 224),
    0x14000000, 0x0c000000, 0x0f000000
};
static const struct palette dark_palette = {
    RGB(32, 32, 34), RGB(54, 54, 58), RGB(255, 255, 255), RGB(196, 196, 196), RGB(76, 194, 255), RGB(160, 160, 160),
    RGB(40, 40, 43), RGB(70, 70, 74), RGB(52, 52, 56), RGB(78, 78, 82),
    0x1affffff, 0x10ffffff, 0x14ffffff
};
static struct palette pal;

static ARGB argb(COLORREF c, BYTE alpha)
{
    return ((ARGB)alpha << 24) | ((ARGB)GetRValue(c) << 16) | ((ARGB)GetGValue(c) << 8) | GetBValue(c);
}

static void choose_palette(void)
{
    COLORREF face = GetSysColor(COLOR_BTNFACE);
    int luminance = (GetRValue(face) * 299 + GetGValue(face) * 587 + GetBValue(face) * 114) / 1000;
    pal = luminance < 128 ? dark_palette : light_palette;
}

static HFONT make_font(int points_x10, int weight)
{
    LOGFONTW lf = {0};
    lf.lfHeight = -MulDiv(points_x10, (int)dpi, 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpyW(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

/* ---------------------------------------------------------------------------------------- */
/* Drawing helpers (GDI+ for anti-aliased shapes, GDI for text and icons)                   */
/* ---------------------------------------------------------------------------------------- */

struct canvas {
    HDC dc;
    HBITMAP bitmap, old_bitmap;
    GpGraphics *g;
    int width, height;
};

static BOOL canvas_begin(struct canvas *c, HDC target, int width, int height)
{
    BITMAPINFO bmi = {{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB}};
    void *bits;

    c->width = width;
    c->height = height;
    c->dc = CreateCompatibleDC(target);
    c->bitmap = CreateDIBSection(target, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!c->dc || !c->bitmap) return FALSE;
    c->old_bitmap = SelectObject(c->dc, c->bitmap);
    GdipCreateFromHDC(c->dc, &c->g);
    GdipSetSmoothingMode(c->g, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(c->g, PixelOffsetModeHalf);
    SetBkMode(c->dc, TRANSPARENT);
    return TRUE;
}

static void canvas_end(struct canvas *c, HDC target, int x, int y)
{
    GdipFlush(c->g, FlushIntentionSync);
    BitBlt(target, x, y, c->width, c->height, c->dc, 0, 0, SRCCOPY);
    GdipDeleteGraphics(c->g);
    SelectObject(c->dc, c->old_bitmap);
    DeleteObject(c->bitmap);
    DeleteDC(c->dc);
}

static GpPath *round_rect_path(float x, float y, float w, float h, float r)
{
    GpPath *path;
    float d = r * 2;

    GdipCreatePath(FillModeAlternate, &path);
    if (r <= 0.5f) {
        GdipAddPathRectangle(path, x, y, w, h);
        return path;
    }
    GdipAddPathArc(path, x, y, d, d, 180, 90);
    GdipAddPathArc(path, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(path, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(path, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(path);
    return path;
}

static void fill_round_rect(struct canvas *c, const RECT *rc, float radius, ARGB color)
{
    GpPath *path = round_rect_path((float)rc->left, (float)rc->top, (float)(rc->right - rc->left),
                                   (float)(rc->bottom - rc->top), radius);
    GpSolidFill *brush;
    GdipCreateSolidFill(color, &brush);
    GdipFillPath(c->g, (GpBrush *)brush, path);
    GdipDeleteBrush((GpBrush *)brush);
    GdipDeletePath(path);
}

static void stroke_round_rect(struct canvas *c, const RECT *rc, float radius, ARGB color, float width)
{
    float inset = width / 2;
    GpPath *path = round_rect_path(rc->left + inset, rc->top + inset, rc->right - rc->left - width,
                                   rc->bottom - rc->top - width, radius);
    GpPen *pen;
    GdipCreatePen1(color, width, UnitPixel, &pen);
    GdipDrawPath(c->g, pen, path);
    GdipDeletePen(pen);
    GdipDeletePath(path);
}

static void fill_rect(struct canvas *c, int x, int y, int w, int h, ARGB color)
{
    GpSolidFill *brush;
    GdipCreateSolidFill(color, &brush);
    GdipFillRectangleI(c->g, (GpBrush *)brush, x, y, w, h);
    GdipDeleteBrush((GpBrush *)brush);
}

static GpPen *make_pen(ARGB color, float width)
{
    GpPen *pen;
    GdipCreatePen1(color, width, UnitPixel, &pen);
    GdipSetPenStartCap(pen, LineCapRound);
    GdipSetPenEndCap(pen, LineCapRound);
    GdipSetPenLineJoin(pen, LineJoinRound);
    return pen;
}

/* Start-button mark: keep the S11 windows, but omit the launcher tile so it reads as Start. */
static void draw_logo(struct canvas *c, float cx, float cy, float size)
{
    static const struct { float left, top, right, bottom; ARGB color; } panes[] = {
        {14, 44, 50, 84, 0xff55a7ff}, {28, 31, 66, 71, 0xff9ad5ff}, {44, 16, 86, 58, 0xffffffff},
    };
    float x = cx - size / 2, y = cy - size / 2, u = size / 100;
    int i;

    for (i = 0; i < ARRAY_SIZE(panes); i++) {
        GpSolidFill *brush;
        GpPath *path = round_rect_path(x + panes[i].left * u, y + panes[i].top * u,
                                       (panes[i].right - panes[i].left) * u,
                                       (panes[i].bottom - panes[i].top) * u, 8 * u);
        GdipCreateSolidFill(panes[i].color, &brush);
        GdipFillPath(c->g, (GpBrush *)brush, path);
        GdipDeletePath(path);
        GdipDeleteBrush((GpBrush *)brush);

        if (i == ARRAY_SIZE(panes) - 1) {
            GpPen *outline = make_pen(argb(pal.accent, 255), max(1.0f, size * 0.045f));
            GpSolidFill *title_line;
            path = round_rect_path(x + panes[i].left * u, y + panes[i].top * u,
                                   (panes[i].right - panes[i].left) * u,
                                   (panes[i].bottom - panes[i].top) * u, 8 * u);
            GdipDrawPath(c->g, outline, path);
            GdipDeletePath(path);
            GdipDeletePen(outline);
            GdipCreateSolidFill(argb(pal.accent, 255), &title_line);
            GdipFillRectangle(c->g, (GpBrush *)title_line, x + 44 * u, y + 27 * u, 42 * u, 3.2f * u);
            GdipDeleteBrush((GpBrush *)title_line);
        }
    }
}

static void draw_search_glyph(struct canvas *c, float cx, float cy, float size, COLORREF color)
{
    float r = size * 0.32f;
    GpPen *pen = make_pen(argb(color, 255), size * 0.09f);
    GdipDrawEllipse(c->g, pen, cx - size * 0.12f - r, cy - size * 0.12f - r, r * 2, r * 2);
    GdipDrawLine(c->g, pen, cx + size * 0.12f, cy + size * 0.12f, cx + size * 0.40f, cy + size * 0.40f);
    GdipDeletePen(pen);
}

static void draw_power_glyph(struct canvas *c, float cx, float cy, float size, COLORREF color)
{
    float r = size * 0.36f;
    GpPen *pen = make_pen(argb(color, 255), size * 0.08f);
    GdipDrawArc(c->g, pen, cx - r, cy - r, r * 2, r * 2, -50, 280);
    GdipDrawLine(c->g, pen, cx, cy - size * 0.46f, cx, cy - size * 0.06f);
    GdipDeletePen(pen);
}

static void draw_gear_glyph(struct canvas *c, float cx, float cy, float size, COLORREF color)
{
    GpPen *pen = make_pen(argb(color, 255), size * 0.08f);
    float r = size * 0.30f, ri = size * 0.12f;
    int i;
    GdipDrawEllipse(c->g, pen, cx - r, cy - r, r * 2, r * 2);
    GdipDrawEllipse(c->g, pen, cx - ri, cy - ri, ri * 2, ri * 2);
    for (i = 0; i < 8; i++) {
        float a = i * 3.14159265f / 4;
        float ca = cosf(a), sa = sinf(a);
        GdipDrawLine(c->g, pen, cx + ca * r, cy + sa * r, cx + ca * (r + size * 0.12f), cy + sa * (r + size * 0.12f));
    }
    GdipDeletePen(pen);
}

static void draw_folder_glyph(struct canvas *c, float cx, float cy, float size, COLORREF color)
{
    GpPen *pen = make_pen(argb(color, 255), size * 0.08f);
    float l = cx - size * 0.40f, r = cx + size * 0.40f, t = cy - size * 0.30f, b = cy + size * 0.32f;
    GpPointF points[] = {
        {l, b}, {l, t}, {l + size * 0.28f, t}, {l + size * 0.36f, t + size * 0.10f}, {r, t + size * 0.10f}, {r, b}
    };
    GdipDrawPolygon(c->g, pen, points, ARRAY_SIZE(points));
    GdipDrawLine(c->g, pen, l, t + size * 0.22f, r, t + size * 0.22f);
    GdipDeletePen(pen);
}

static void draw_wifi_glyph(struct canvas *c, float cx, float cy, float size, COLORREF color)
{
    GpPen *pen = make_pen(argb(color, 255), size * 0.10f);
    float r = size * 0.43f;
    GdipDrawArc(c->g, pen, cx - r, cy - r * 0.62f, r * 2, r * 1.55f, 220, 100);
    r = size * 0.27f;
    GdipDrawArc(c->g, pen, cx - r, cy - r * 0.38f, r * 2, r * 1.10f, 220, 100);
    GdipDeletePen(pen);
    {
        GpSolidFill *brush;
        float d = size * 0.14f;
        GdipCreateSolidFill(argb(color, 255), &brush);
        GdipFillEllipse(c->g, (GpBrush *)brush, cx - d / 2, cy + size * 0.27f, d, d);
        GdipDeleteBrush((GpBrush *)brush);
    }
}

static void draw_bluetooth_glyph(struct canvas *c, float cx, float cy, float size, COLORREF color)
{
    GpPen *pen = make_pen(argb(color, 255), size * 0.10f);
    GdipDrawLine(c->g, pen, cx, cy - size * 0.46f, cx, cy + size * 0.46f);
    GdipDrawLine(c->g, pen, cx, cy - size * 0.46f, cx + size * 0.32f, cy - size * 0.16f);
    GdipDrawLine(c->g, pen, cx + size * 0.32f, cy - size * 0.16f, cx - size * 0.30f, cy + size * 0.34f);
    GdipDrawLine(c->g, pen, cx - size * 0.30f, cy - size * 0.34f, cx + size * 0.32f, cy + size * 0.16f);
    GdipDrawLine(c->g, pen, cx + size * 0.32f, cy + size * 0.16f, cx, cy + size * 0.46f);
    GdipDeletePen(pen);
}

static void draw_overflow_glyph(struct canvas *c, float cx, float cy, float size, COLORREF color)
{
    GpPen *pen = make_pen(argb(color, 255), size * 0.11f);
    GdipDrawLine(c->g, pen, cx - size * 0.30f, cy - size * 0.06f, cx, cy + size * 0.20f);
    GdipDrawLine(c->g, pen, cx, cy + size * 0.20f, cx + size * 0.30f, cy - size * 0.06f);
    GdipDeletePen(pen);
}

static void draw_text(struct canvas *c, const WCHAR *text, RECT *rc, HFONT font, COLORREF color, UINT flags)
{
    HFONT old;
    GdipFlush(c->g, FlushIntentionSync);
    old = SelectObject(c->dc, font);
    SetTextColor(c->dc, color);
    DrawTextW(c->dc, text, -1, rc, flags | DT_NOPREFIX);
    SelectObject(c->dc, old);
}

static void draw_icon(struct canvas *c, HICON icon, int x, int y, int size)
{
    if (!icon) return;
    GdipFlush(c->g, FlushIntentionSync);
    DrawIconEx(c->dc, x, y, icon, size, size, 0, NULL, DI_NORMAL);
}

enum s11_builtin_icon {
    S11_ICON_DEFAULT = 0,
    S11_ICON_SETTINGS,
    S11_ICON_EXPLORER,
    S11_ICON_GALLERY,
    S11_ICON_TERMINAL,
    S11_ICON_NOTEPAD,
    S11_ICON_TASK_MANAGER,
    S11_ICON_REGISTRY,
    S11_ICON_WORDPAD,
    S11_ICON_WINECFG,
    S11_ICON_INSTALLER,
    S11_ICON_GPU,
    S11_ICON_D3D,
    S11_ICON_MINES,
    S11_ICON_ARCHIVE
};

/*
 * The built-in programs' icons: a white symbol on the blue tile of the S11 mark. The shapes
 * come from tools/s11/branding/make_app_icons.py (s11tiles.h), which also draws the .ico files.
 */
#include "s11tiles.h"

static const float *s11_tile_symbol(int kind)
{
    switch (kind) {
    case S11_ICON_SETTINGS: return s11_tile_settings;
    case S11_ICON_EXPLORER: return s11_tile_explorer;
    case S11_ICON_GALLERY: return s11_tile_gallery;
    case S11_ICON_TERMINAL: return s11_tile_terminal;
    case S11_ICON_NOTEPAD: return s11_tile_notepad;
    case S11_ICON_TASK_MANAGER: return s11_tile_taskmgr;
    case S11_ICON_REGISTRY: return s11_tile_registry;
    case S11_ICON_WORDPAD: return s11_tile_wordpad;
    case S11_ICON_WINECFG: return s11_tile_winecfg;
    case S11_ICON_INSTALLER: return s11_tile_installer;
    case S11_ICON_GPU: return s11_tile_gpu;
    case S11_ICON_D3D: return s11_tile_d3d;
    case S11_ICON_MINES: return s11_tile_mines;
    case S11_ICON_ARCHIVE: return s11_tile_archive;
    default: return NULL;
    }
}

/* Adds a filled shape (rounded rectangle, polygon or circle) to a path; the numbers it used */
static int s11_tile_add_shape(GpPath *path, const float *op, float x, float y, float u)
{
    switch ((int)op[0]) {
    case 1: {
        GpPath *rr = round_rect_path(x + op[2] * u, y + op[3] * u, (op[4] - op[2]) * u, (op[5] - op[3]) * u, op[6] * u);
        GdipAddPathPath(path, rr, FALSE);
        GdipDeletePath(rr);
        return 7;
    }
    case 2: {
        GpPointF points[128];
        int n = (int)op[2], i;
        for (i = 0; i < n && i < ARRAY_SIZE(points); i++) {
            points[i].X = x + op[3 + 2 * i] * u;
            points[i].Y = y + op[4 + 2 * i] * u;
        }
        GdipAddPathPolygon(path, points, i);
        return 3 + 2 * n;
    }
    case 3:
        GdipAddPathEllipse(path, x + (op[2] - op[4]) * u, y + (op[3] - op[4]) * u, 2 * op[4] * u, 2 * op[4] * u);
        return 5;
    }
    return 0;
}

static void draw_s11_tile(struct canvas *c, const float *ops, int x, int y, int size)
{
    float fx = (float)x, fy = (float)y, u = size / 100.0f;
    GpPointF from = {fx, fy}, to = {fx + size, fy + size};
    GpLineGradient *tile;
    GpPath *path;

    GdipCreateLineBrush(&from, &to, 0xff5cc0ff, 0xff568cff, WrapModeTileFlipXY, &tile);
    path = round_rect_path(fx, fy, (float)size, (float)size, size * 0.23f);
    GdipFillPath(c->g, (GpBrush *)tile, path);
    GdipDeletePath(path);

    while (ops && ops[0] != 0) {
        int code = (int)ops[0];
        float alpha = ops[1];
        GpSolidFill *white = NULL;
        GpBrush *brush;

        /* cut-outs are painted with the tile itself */
        if (alpha < 0) brush = (GpBrush *)tile;
        else {
            GdipCreateSolidFill(((ARGB)(BYTE)alpha << 24) | 0xffffff, &white);
            brush = (GpBrush *)white;
        }
        if (code == 4) {
            GpPointF points[16];
            GpPen *pen;
            int n = (int)ops[3], i;
            for (i = 0; i < n && i < ARRAY_SIZE(points); i++) {
                points[i].X = fx + ops[4 + 2 * i] * u;
                points[i].Y = fy + ops[5 + 2 * i] * u;
            }
            GdipCreatePen2(brush, ops[2] * u, UnitPixel, &pen);
            GdipSetPenStartCap(pen, LineCapRound);
            GdipSetPenEndCap(pen, LineCapRound);
            GdipSetPenLineJoin(pen, LineJoinRound);
            GdipDrawLines(c->g, pen, points, i);
            GdipDeletePen(pen);
            ops += 4 + 2 * n;
        }
        else {
            int used, count = 1, k;
            GdipCreatePath(FillModeWinding, &path);
            if (code == 5) {      /* a group: its shapes are filled together */
                count = (int)ops[2];
                ops += 3;
            }
            for (k = 0; k < count && ops[0] != 0; k++) {
                if (!(used = s11_tile_add_shape(path, ops, fx, fy, u))) break;
                ops += used;
            }
            GdipFillPath(c->g, brush, path);
            GdipDeletePath(path);
            if (k < count) break;  /* an unknown shape: stop rather than misread the rest */
        }
        if (white) GdipDeleteBrush((GpBrush *)white);
    }
    GdipDeleteBrush((GpBrush *)tile);
}

/* Built-in programs show their S11 tile; others their own icon on a soft rounded surface */
static void draw_s11_app_icon(struct canvas *c, HICON icon, int x, int y, int box_size, int icon_size, int kind)
{
    RECT box = {x, y, x + box_size, y + box_size};
    const float *symbol = s11_tile_symbol(kind);
    if (symbol) {
        /* a little smaller than the surface other icons sit on: the tile is all colour */
        int size = box_size - (box_size - icon_size) / 3;
        draw_s11_tile(c, symbol, x + (box_size - size) / 2, y + (box_size - size) / 2, size);
        return;
    }
    fill_round_rect(c, &box, (float)SC(9), argb(pal.field, 255));
    stroke_round_rect(c, &box, (float)SC(9), argb(pal.field_border, 190), 1);
    draw_icon(c, icon, x + (box_size - icon_size) / 2, y + (box_size - icon_size) / 2, icon_size);
}

/* ---------------------------------------------------------------------------------------- */
/* Talking to Winlator                                                                      */
/* ---------------------------------------------------------------------------------------- */

static void send_winlator_request(BYTE code)
{
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in addr = {0};

    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        addr.sin_family = AF_INET;
        addr.sin_port = htons(WINLATOR_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sendto(s, (const char *)&code, 1, 0, (struct sockaddr *)&addr, sizeof(addr));
        closesocket(s);
    }
    WSACleanup();
}

static void run(const WCHAR *file, const WCHAR *args)
{
    ShellExecuteW(NULL, NULL, file, args, NULL, SW_SHOWNORMAL);
}

static void run_in_directory(const WCHAR *file, const WCHAR *args, const WCHAR *directory)
{
    ShellExecuteW(NULL, NULL, file, args, directory, SW_SHOWNORMAL);
}

/* Our programs sit next to s11shell.exe (Z:\usr\share\s11\shell in the rootfs) */
static void sibling_exe(WCHAR *path, const WCHAR *name)
{
    WCHAR *slash;
    GetModuleFileNameW(NULL, path, MAX_PATH);
    slash = wcsrchr(path, '\\');
    if (slash && (slash - path) + lstrlenW(name) + 2 < MAX_PATH) lstrcpyW(slash + 1, name);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) wsprintfW(path, L"Z:\\usr\\share\\s11\\shell\\%s", name);
}

static const WCHAR *settings_exe(void)
{
    static WCHAR path[MAX_PATH];
    if (!path[0]) sibling_exe(path, L"s11settings.exe");
    return path;
}

static const WCHAR *explorer_exe(void)
{
    static WCHAR path[MAX_PATH];
    if (!path[0]) sibling_exe(path, L"s11explorer.exe");
    return path;
}

static const WCHAR *gallery_exe(void)
{
    static WCHAR path[MAX_PATH];
    if (!path[0]) sibling_exe(path, L"s11gallery.exe");
    return path;
}

/* File Explorer: ours, else Winlator's file manager, else Wine's explorer */
static void open_files(void)
{
    WCHAR path[MAX_PATH];
    if (GetFileAttributesW(explorer_exe()) != INVALID_FILE_ATTRIBUTES) {
        run(explorer_exe(), NULL);
        return;
    }
    GetWindowsDirectoryW(path, MAX_PATH);
    lstrcatW(path, L"\\wfm.exe");
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) run(path, NULL);
    else run(L"explorer.exe", NULL);
}

/* page: NULL, "system", "wine", "connectivity", "personalization", "apps", "fonts" or "about" */
static void open_settings(const WCHAR *page)
{
    if (GetFileAttributesW(settings_exe()) != INVALID_FILE_ATTRIBUTES) run(settings_exe(), page);
    else run(L"control.exe", NULL);
}

#endif /* S11UI_H */
