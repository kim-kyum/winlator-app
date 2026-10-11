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

/* The S11 mark (tools/s11/branding/make_logo.py): three windows rising to the upper right */
static void draw_logo(struct canvas *c, float cx, float cy, float size)
{
    static const struct { float left, top, right, bottom; BYTE alpha; } panes[] = {
        {14, 44, 50, 84, 70}, {28, 31, 66, 71, 140}, {44, 16, 86, 58, 255},
    };
    float x = cx - size / 2, y = cy - size / 2, u = size / 100;
    GpPointF p1 = {x, y}, p2 = {x + size, y + size};
    GpLineGradient *gradient;
    GpPath *path = round_rect_path(x, y, size, size, size * 0.23f);
    int i;

    GdipCreateLineBrush(&p1, &p2, 0xff2f7bff, 0xff7b5cff, WrapModeTile, &gradient);
    GdipFillPath(c->g, (GpBrush *)gradient, path);
    GdipDeletePath(path);

    for (i = 0; i < ARRAY_SIZE(panes); i++) {
        GpSolidFill *brush;
        GdipCreateSolidFill(((ARGB)panes[i].alpha << 24) | 0xffffff, &brush);
        path = round_rect_path(x + panes[i].left * u, y + panes[i].top * u, (panes[i].right - panes[i].left) * u,
                               (panes[i].bottom - panes[i].top) * u, 8 * u);
        GdipFillPath(c->g, (GpBrush *)brush, path);
        GdipDeletePath(path);
        GdipDeleteBrush((GpBrush *)brush);
    }
    /* the line under the front window's title bar shows the tile through */
    GdipFillRectangle(c->g, (GpBrush *)gradient, x + 44 * u, y + 27 * u, 42 * u, 3.2f * u);
    GdipDeleteBrush((GpBrush *)gradient);
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

/* page: NULL, "system", "personalization", "apps", "fonts" or "about" */
static void open_settings(const WCHAR *page)
{
    if (GetFileAttributesW(settings_exe()) != INVALID_FILE_ATTRIBUTES) run(settings_exe(), page);
    else run(L"control.exe", NULL);
}

#endif /* S11UI_H */
