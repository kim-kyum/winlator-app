/*
 * S11 Gallery: a small, Korean-first viewer for screenshots made with Win+Shift+S.
 * Images are read from C:\\S11\\Screenshots, where the Android capture bridge stores them.
 */
#include "s11ui.h"

#define MAX_PHOTOS 512
#define CARD_W 240
#define CARD_H 190
#define IMAGE_H 142

struct photo {
    WCHAR path[MAX_PATH];
    WCHAR name[128];
    GpImage *image;
};

static HINSTANCE instance;
static HWND main_window;
static HFONT ui_font, small_font, title_font;
static ULONG_PTR gdiplus_token;
static struct photo photos[MAX_PHOTOS];
static int photo_count, selected_photo = -1, preview_photo = -1, scroll_y;
static RECT refresh_button;

static void dispose_photos(void)
{
    int i;
    for (i = 0; i < photo_count; i++) {
        if (photos[i].image) GdipDisposeImage(photos[i].image);
        photos[i].image = NULL;
    }
    photo_count = 0;
    selected_photo = preview_photo = -1;
}

static void scan_photos(void)
{
    WIN32_FIND_DATAW data;
    HANDLE find;
    WCHAR pattern[MAX_PATH];
    dispose_photos();
    CreateDirectoryW(L"C:\\S11", NULL);
    CreateDirectoryW(L"C:\\S11\\Screenshots", NULL);
    lstrcpyW(pattern, L"C:\\S11\\Screenshots\\*.png");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && photo_count < MAX_PHOTOS) {
            struct photo *p = &photos[photo_count];
            wsprintfW(p->path, L"C:\\S11\\Screenshots\\%s", data.cFileName);
            lstrcpynW(p->name, data.cFileName, ARRAY_SIZE(p->name));
            if (GdipLoadImageFromFile(p->path, &p->image) == Ok && p->image) photo_count++;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static void draw_cover(struct canvas *c, GpImage *image, const RECT *rc)
{
    UINT iw = 0, ih = 0;
    float w = (float)(rc->right - rc->left), h = (float)(rc->bottom - rc->top);
    float scale, sw, sh;
    if (!image || w <= 0 || h <= 0) return;
    GdipGetImageWidth(image, &iw);
    GdipGetImageHeight(image, &ih);
    if (!iw || !ih) return;
    scale = w / iw > h / ih ? w / iw : h / ih;
    sw = w / scale;
    sh = h / scale;
    GdipSetInterpolationMode(c->g, InterpolationModeHighQualityBicubic);
    GdipDrawImageRectRect(c->g, image, (float)rc->left, (float)rc->top, w, h,
                          (iw - sw) / 2, (ih - sh) / 2, sw, sh, UnitPixel, NULL, NULL, NULL);
}

static void draw_contain(struct canvas *c, GpImage *image, const RECT *rc)
{
    UINT iw = 0, ih = 0;
    float w = (float)(rc->right - rc->left), h = (float)(rc->bottom - rc->top);
    float scale, dw, dh;
    if (!image || w <= 0 || h <= 0) return;
    GdipGetImageWidth(image, &iw);
    GdipGetImageHeight(image, &ih);
    if (!iw || !ih) return;
    scale = w / iw < h / ih ? w / iw : h / ih;
    dw = iw * scale;
    dh = ih * scale;
    GdipSetInterpolationMode(c->g, InterpolationModeHighQualityBicubic);
    GdipDrawImageRect(c->g, image, rc->left + (w - dw) / 2, rc->top + (h - dh) / 2, dw, dh);
}

static int columns_for_width(int width)
{
    int n = (width - SC(40)) / SC(CARD_W + 16);
    return max(1, min(6, n));
}

static BOOL photo_rect(int index, int width, RECT *rc)
{
    int columns = columns_for_width(width);
    int gap = SC(16), card_w = (width - SC(32) - (columns - 1) * gap) / columns;
    int row = index / columns, col = index % columns;
    int left = SC(16) + col * (card_w + gap);
    int top = SC(104) + row * (SC(CARD_H) + gap) - scroll_y;
    SetRect(rc, left, top, left + card_w, top + SC(CARD_H));
    return TRUE;
}

static void paint_gallery(HDC hdc)
{
    struct canvas c;
    RECT client, rc, image_rc, text_rc;
    int i;
    WCHAR label[96];
    GetClientRect(main_window, &client);
    if (!canvas_begin(&c, hdc, client.right, client.bottom)) return;
    fill_rect(&c, 0, 0, client.right, client.bottom, argb(pal.menu, 255));
    fill_rect(&c, 0, 0, client.right, SC(84), argb(pal.bar, 255));
    fill_rect(&c, 0, SC(83), client.right, 1, argb(pal.bar_border, 255));

    if (preview_photo >= 0 && preview_photo < photo_count) {
        rc = (RECT){SC(18), SC(14), SC(160), SC(68)};
        draw_text(&c, TR(L"갤러리", L"Gallery"), &rc, title_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        rc = (RECT){client.right - SC(180), SC(18), client.right - SC(16), SC(62)};
        draw_text(&c, TR(L"뒤로  ·  Esc", L"Back  ·  Esc"), &rc, small_font, pal.text_dim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SetRect(&image_rc, SC(16), SC(92), client.right - SC(16), client.bottom - SC(16));
        fill_round_rect(&c, &image_rc, (float)SC(8), argb(RGB(16, 18, 24), 255));
        draw_contain(&c, photos[preview_photo].image, &image_rc);
        canvas_end(&c, hdc, 0, 0);
        return;
    }

    rc = (RECT){SC(20), SC(10), client.right - SC(168), SC(50)};
    draw_text(&c, TR(L"갤러리", L"Gallery"), &rc, title_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    wsprintfW(label, TR(L"캡처 %d개", L"%d screenshots"), photo_count);
    rc = (RECT){SC(22), SC(48), client.right - SC(168), SC(78)};
    draw_text(&c, label, &rc, small_font, pal.text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetRect(&refresh_button, client.right - SC(156), SC(18), client.right - SC(20), SC(62));
    fill_round_rect(&c, &refresh_button, (float)SC(8), argb(pal.field, 255));
    stroke_round_rect(&c, &refresh_button, (float)SC(8), argb(pal.field_border, 200), 1);
    draw_text(&c, TR(L"새로 고침", L"Refresh"), &refresh_button, ui_font, pal.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (!photo_count) {
        rc = (RECT){SC(24), SC(104), client.right - SC(24), client.bottom - SC(24)};
        draw_text(&c, TR(L"아직 캡처한 이미지가 없어요", L"No screenshots yet"), &rc, title_font, pal.text_dim,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        canvas_end(&c, hdc, 0, 0);
        return;
    }

    for (i = 0; i < photo_count; i++) {
        RECT card;
        photo_rect(i, client.right, &card);
        fill_round_rect(&c, &card, (float)SC(8), argb(pal.field, 255));
        if (i == selected_photo) stroke_round_rect(&c, &card, (float)SC(8), argb(pal.accent, 255), (float)SC(2));
        else stroke_round_rect(&c, &card, (float)SC(8), argb(pal.field_border, 200), 1);
        SetRect(&image_rc, card.left + SC(6), card.top + SC(6), card.right - SC(6), card.top + SC(6 + IMAGE_H));
        draw_cover(&c, photos[i].image, &image_rc);
        SetRect(&text_rc, card.left + SC(10), image_rc.bottom + SC(6), card.right - SC(10), card.bottom - SC(4));
        draw_text(&c, photos[i].name, &text_rc, small_font, pal.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    canvas_end(&c, hdc, 0, 0);
}

static LRESULT CALLBACK gallery_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        main_window = hwnd;
        scan_photos();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        choose_palette();
        paint_gallery(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp), i;
        RECT client;
        GetClientRect(hwnd, &client);
        if (preview_photo >= 0) {
            preview_photo = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&refresh_button, (POINT){x, y})) {
            scan_photos();
            scroll_y = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        for (i = 0; i < photo_count; i++) {
            RECT card;
            photo_rect(i, client.right, &card);
            if (PtInRect(&card, (POINT){x, y})) {
                selected_photo = i;
                InvalidateRect(hwnd, NULL, FALSE);
                break;
            }
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp), i;
        RECT client;
        GetClientRect(hwnd, &client);
        for (i = 0; i < photo_count; i++) {
            RECT card;
            photo_rect(i, client.right, &card);
            if (PtInRect(&card, (POINT){x, y})) {
                selected_photo = preview_photo = i;
                InvalidateRect(hwnd, NULL, FALSE);
                break;
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (preview_photo < 0) {
            RECT client;
            int columns;
            int rows;
            GetClientRect(hwnd, &client);
            columns = columns_for_width(client.right);
            rows = (photo_count + columns - 1) / columns;
            scroll_y = max(0, min(scroll_y - GET_WHEEL_DELTA_WPARAM(wp) * SC(48) / WHEEL_DELTA,
                                  max(0, rows * (SC(CARD_H) + SC(16)) - (client.bottom - SC(104)))));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_BACK) {
            if (preview_photo >= 0) preview_photo = -1;
            else selected_photo = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wp == VK_F5) {
            scan_photos();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        dispose_photos();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, WCHAR *cmdline, int show)
{
    struct GdiplusStartupInput input = {1};
    WNDCLASSEXW wc = {sizeof(wc)};
    RECT rc = {0, 0, SC(1024), SC(720)};
    MSG msg;
    instance = inst;
    dpi = GetDpiForSystem();
    korean = TRUE;
    choose_palette();
    GdiplusStartup(&gdiplus_token, &input, NULL);
    ui_font = make_font(100, FW_NORMAL);
    small_font = make_font(90, FW_NORMAL);
    title_font = make_font(180, FW_SEMIBOLD);
    wc.lpfnWndProc = gallery_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"S11Gallery";
    wc.style = CS_DBLCLKS;
    if (!RegisterClassExW(&wc)) return 1;
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    main_window = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, TR(L"갤러리", L"Gallery"), WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                                  NULL, NULL, inst, NULL);
    if (!main_window) return 2;
    ShowWindow(main_window, show ? show : SW_SHOWNORMAL);
    UpdateWindow(main_window);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (ui_font) DeleteObject(ui_font);
    if (small_font) DeleteObject(small_font);
    if (title_font) DeleteObject(title_font);
    GdiplusShutdown(gdiplus_token);
    return (int)msg.wParam;
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
