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
    S11_ICON_REGISTRY
};

/* Minimal line-art symbols keep built-in apps distinct but visually related. */
static void draw_s11_builtin_glyph(struct canvas *c, int kind, int x, int y, int size)
{
    float l = (float)x, t = (float)y, s = (float)size;
    float cx = l + s / 2, cy = t + s / 2;
    GpPen *pen;
    GpSolidFill *brush;

    switch (kind) {
    case S11_ICON_SETTINGS:
        draw_gear_glyph(c, cx, cy, s * 0.76f, pal.accent);
        break;
    case S11_ICON_EXPLORER:
        draw_folder_glyph(c, cx, cy, s * 0.78f, pal.accent);
        break;
    case S11_ICON_GALLERY: {
        RECT frame = {x + size / 6, y + size / 6, x + size - size / 6, y + size - size / 6};
        GpPointF mountain[] = {
            {l + s * 0.23f, t + s * 0.69f}, {l + s * 0.43f, t + s * 0.48f},
            {l + s * 0.56f, t + s * 0.59f}, {l + s * 0.70f, t + s * 0.43f},
            {l + s * 0.80f, t + s * 0.69f}
        };
        pen = make_pen(argb(pal.accent, 255), max(1.0f, s * 0.075f));
        GdipDrawRectangle(c->g, pen, (float)frame.left, (float)frame.top,
                          (float)(frame.right - frame.left), (float)(frame.bottom - frame.top));
        GdipDrawEllipse(c->g, pen, l + s * 0.62f, t + s * 0.26f, s * 0.11f, s * 0.11f);
        GdipDrawLines(c->g, pen, mountain, ARRAY_SIZE(mountain));
        GdipDeletePen(pen);
        break;
    }
    case S11_ICON_TERMINAL: {
        RECT screen = {x + size / 7, y + size / 5, x + size - size / 7, y + size - size / 5};
        stroke_round_rect(c, &screen, (float)SC(3), argb(pal.accent, 255), max(1.0f, s * 0.075f));
        pen = make_pen(argb(pal.accent, 255), max(1.0f, s * 0.08f));
        GdipDrawLine(c->g, pen, l + s * 0.29f, cy - s * 0.10f, l + s * 0.42f, cy);
        GdipDrawLine(c->g, pen, l + s * 0.42f, cy, l + s * 0.29f, cy + s * 0.10f);
        GdipDrawLine(c->g, pen, l + s * 0.49f, cy + s * 0.11f, l + s * 0.67f, cy + s * 0.11f);
        GdipDeletePen(pen);
        break;
    }
    case S11_ICON_NOTEPAD: {
        RECT page = {x + size / 5, y + size / 7, x + size - size / 5, y + size - size / 7};
        pen = make_pen(argb(pal.accent, 255), max(1.0f, s * 0.075f));
        GdipDrawRectangle(c->g, pen, (float)page.left, (float)page.top,
                          (float)(page.right - page.left), (float)(page.bottom - page.top));
        GdipDrawLine(c->g, pen, l + s * 0.32f, t + s * 0.42f, l + s * 0.68f, t + s * 0.42f);
        GdipDrawLine(c->g, pen, l + s * 0.32f, t + s * 0.54f, l + s * 0.68f, t + s * 0.54f);
        GdipDrawLine(c->g, pen, l + s * 0.32f, t + s * 0.66f, l + s * 0.60f, t + s * 0.66f);
        GdipDeletePen(pen);
        break;
    }
    case S11_ICON_TASK_MANAGER:
        pen = make_pen(argb(pal.accent, 255), max(1.0f, s * 0.07f));
        GdipDrawLine(c->g, pen, l + s * 0.22f, t + s * 0.76f, l + s * 0.78f, t + s * 0.76f);
        GdipDeletePen(pen);
        GdipCreateSolidFill(argb(pal.accent, 255), &brush);
        GdipFillRectangle(c->g, (GpBrush *)brush, l + s * 0.29f, t + s * 0.55f, s * 0.11f, s * 0.21f);
        GdipFillRectangle(c->g, (GpBrush *)brush, l + s * 0.46f, t + s * 0.37f, s * 0.11f, s * 0.39f);
        GdipFillRectangle(c->g, (GpBrush *)brush, l + s * 0.63f, t + s * 0.25f, s * 0.11f, s * 0.51f);
        GdipDeleteBrush((GpBrush *)brush);
        break;
    case S11_ICON_REGISTRY: {
        float r = s * 0.095f;
        pen = make_pen(argb(pal.accent, 255), max(1.0f, s * 0.07f));
        GdipDrawLine(c->g, pen, cx, t + s * 0.31f, l + s * 0.31f, t + s * 0.57f);
        GdipDrawLine(c->g, pen, cx, t + s * 0.31f, l + s * 0.69f, t + s * 0.57f);
        GdipDrawLine(c->g, pen, l + s * 0.31f, t + s * 0.57f, l + s * 0.31f, t + s * 0.76f);
        GdipDrawLine(c->g, pen, l + s * 0.69f, t + s * 0.57f, l + s * 0.69f, t + s * 0.76f);
        GdipDeletePen(pen);
        GdipCreateSolidFill(argb(pal.field, 255), &brush);
        GdipFillEllipse(c->g, (GpBrush *)brush, cx - r, t + s * 0.22f, r * 2, r * 2);
        GdipFillEllipse(c->g, (GpBrush *)brush, l + s * 0.22f, t + s * 0.49f, r * 2, r * 2);
        GdipFillEllipse(c->g, (GpBrush *)brush, l + s * 0.60f, t + s * 0.49f, r * 2, r * 2);
        GdipFillEllipse(c->g, (GpBrush *)brush, l + s * 0.22f, t + s * 0.68f, r * 2, r * 2);
        GdipFillEllipse(c->g, (GpBrush *)brush, l + s * 0.60f, t + s * 0.68f, r * 2, r * 2);
        GdipDeleteBrush((GpBrush *)brush);
        pen = make_pen(argb(pal.accent, 255), max(1.0f, s * 0.07f));
        GdipDrawEllipse(c->g, pen, cx - r, t + s * 0.22f, r * 2, r * 2);
        GdipDrawEllipse(c->g, pen, l + s * 0.22f, t + s * 0.49f, r * 2, r * 2);
        GdipDrawEllipse(c->g, pen, l + s * 0.60f, t + s * 0.49f, r * 2, r * 2);
        GdipDrawEllipse(c->g, pen, l + s * 0.22f, t + s * 0.68f, r * 2, r * 2);
        GdipDrawEllipse(c->g, pen, l + s * 0.60f, t + s * 0.68f, r * 2, r * 2);
        GdipDeletePen(pen);
        break;
    }
    default:
        break;
    }
}

/* App icons share the soft, rounded surface and accent line art of S11 Settings. */
static void draw_s11_app_icon(struct canvas *c, HICON icon, int x, int y, int box_size, int icon_size, int kind)
{
    RECT box = {x, y, x + box_size, y + box_size};
    fill_round_rect(c, &box, (float)SC(9), argb(pal.field, 255));
    stroke_round_rect(c, &box, (float)SC(9), argb(pal.field_border, 190), 1);
    if (kind != S11_ICON_DEFAULT)
        draw_s11_builtin_glyph(c, kind, x + (box_size - icon_size) / 2, y + (box_size - icon_size) / 2, icon_size);
    else draw_icon(c, icon, x + (box_size - icon_size) / 2, y + (box_size - icon_size) / 2, icon_size);
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
