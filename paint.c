#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>

#define ID_FILE_NEW       1001
#define ID_FILE_OPEN      1002
#define ID_FILE_SAVE      1003
#define ID_FILE_EXIT      1004
#define ID_FILE_CUSTOM    1005
#define ID_TOOL_PENCIL    1101
#define ID_TOOL_ERASER    1102
#define ID_TOOL_LINE      1103
#define ID_TOOL_RECT      1104
#define ID_TOOL_ELLIPSE   1105
#define ID_TOOL_FRECT     1106
#define ID_TOOL_FELLIPSE  1107
#define ID_TOOL_BUCKET    1108
#define ID_HELP_ABOUT     1201

#define IDM_ACCEL  9000

#define CANVAS_W  900
#define CANVAS_H  600
#define PALETTE_H 80

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

static HINSTANCE g_hInst;
static HWND      g_hMain, g_hCanvas, g_hPalette;
static HBITMAP   g_hBitmap  = NULL;
static HDC       g_hMemDC   = NULL;
static HBITMAP   g_hSnapBmp = NULL;
static HDC       g_hSnapDC  = NULL;
static int       g_tool     = ID_TOOL_PENCIL;
static COLORREF  g_color    = RGB(0, 0, 0);
static int       g_size     = 3;
static BOOL      g_drawing  = FALSE;
static POINT     g_startPt, g_lastPt;
static HBRUSH    g_hBgBrush = NULL;

static COLORREF g_palette[] = {
    RGB(0,0,0),       RGB(127,127,127), RGB(136,0,21),   RGB(237,28,36),
    RGB(255,127,39),  RGB(255,242,0),    RGB(34,177,76),   RGB(0,162,232),
    RGB(63,72,204),   RGB(163,73,164),
    RGB(255,255,255), RGB(195,195,195),  RGB(185,122,87),  RGB(255,174,201),
    RGB(255,201,14),  RGB(239,228,176),  RGB(181,230,29),  RGB(153,217,234),
    RGB(112,146,190), RGB(200,191,231)
};
#define PALETTE_COUNT (sizeof(g_palette)/sizeof(COLORREF))
#define SWATCH_SIZE   24
#define SWATCH_PAD    4

static void UpdateTitle(void) {
    char buf[128];
    const char *tool_name = "?";
    switch (g_tool) {
        case ID_TOOL_PENCIL:   tool_name = "Pencil"; break;
        case ID_TOOL_ERASER:   tool_name = "Eraser"; break;
        case ID_TOOL_LINE:     tool_name = "Line"; break;
        case ID_TOOL_RECT:     tool_name = "Rect"; break;
        case ID_TOOL_ELLIPSE:  tool_name = "Ellipse"; break;
        case ID_TOOL_FRECT:    tool_name = "Filled Rect"; break;
        case ID_TOOL_FELLIPSE: tool_name = "Filled Ellipse"; break;
        case ID_TOOL_BUCKET:   tool_name = "Bucket"; break;
    }
    wsprintfA(buf, "Mini Paint  -  [%s]  size=%d", tool_name, g_size);
    SetWindowTextA(g_hMain, buf);
}

static void Canvas_Create(int w, int h) {
    HDC screen = GetDC(NULL);
    if (g_hMemDC)  DeleteDC(g_hMemDC);
    if (g_hBitmap) DeleteObject(g_hBitmap);
    g_hMemDC  = CreateCompatibleDC(screen);
    g_hBitmap = CreateCompatibleBitmap(screen, w, h);
    SelectObject(g_hMemDC, g_hBitmap);

    if (!g_hSnapDC) g_hSnapDC = CreateCompatibleDC(screen);
    if (g_hSnapBmp) DeleteObject(g_hSnapBmp);
    g_hSnapBmp = CreateCompatibleBitmap(screen, w, h);
    SelectObject(g_hSnapDC, g_hSnapBmp);

    RECT r = {0, 0, w, h};
    FillRect(g_hMemDC, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    ReleaseDC(NULL, screen);
}

static void Canvas_TakeSnapshot(void) {
    if (!g_hMemDC || !g_hSnapDC) return;
    BitBlt(g_hSnapDC, 0, 0, CANVAS_W, CANVAS_H, g_hMemDC, 0, 0, SRCCOPY);
}

static void Canvas_RestoreSnapshot(void) {
    if (!g_hMemDC || !g_hSnapDC) return;
    BitBlt(g_hMemDC, 0, 0, CANVAS_W, CANVAS_H, g_hSnapDC, 0, 0, SRCCOPY);
}

static HPEN MakePen(COLORREF color, int width) {
    return CreatePen(PS_SOLID, width, color);
}

static void Draw_Freehand(int x1, int y1, int x2, int y2) {
    COLORREF c = (g_tool == ID_TOOL_ERASER) ? RGB(255,255,255) : g_color;
    int w = (g_tool == ID_TOOL_ERASER) ? g_size * 2 : g_size;
    HPEN pen = MakePen(c, w);
    HGDIOBJ old = SelectObject(g_hMemDC, pen);
    MoveToEx(g_hMemDC, x1, y1, NULL);
    LineTo(g_hMemDC, x2, y2);
    if (w > 1) {
        HBRUSH b = CreateSolidBrush(c);
        HGDIOBJ ob = SelectObject(g_hMemDC, b);
        int r = w / 2;
        Ellipse(g_hMemDC, x2 - r, y2 - r, x2 + r + 1, y2 + r + 1);
        SelectObject(g_hMemDC, ob);
        DeleteObject(b);
    } else {
        SetPixel(g_hMemDC, x2, y2, c);
    }
    SelectObject(g_hMemDC, old);
    DeleteObject(pen);
}

static void Draw_Shape(int x1, int y1, int x2, int y2) {
    BOOL filled = (g_tool == ID_TOOL_FRECT || g_tool == ID_TOOL_FELLIPSE);
    HPEN pen = MakePen(g_color, g_size);
    HGDIOBJ oldPen = SelectObject(g_hMemDC, pen);
    HGDIOBJ oldBrush = SelectObject(g_hMemDC, GetStockObject(NULL_BRUSH));
    HBRUSH brush = NULL;
    if (filled) {
        brush = CreateSolidBrush(g_color);
        SelectObject(g_hMemDC, brush);
    }
    int l = MIN(x1, x2), t = MIN(y1, y2);
    int r = MAX(x1, x2), b = MAX(y1, y2);

    switch (g_tool) {
        case ID_TOOL_LINE:
            MoveToEx(g_hMemDC, x1, y1, NULL);
            LineTo(g_hMemDC, x2, y2);
            break;
        case ID_TOOL_RECT:
        case ID_TOOL_FRECT:
            Rectangle(g_hMemDC, l, t, r, b);
            break;
        case ID_TOOL_ELLIPSE:
        case ID_TOOL_FELLIPSE:
            Ellipse(g_hMemDC, l, t, r, b);
            break;
    }
    SelectObject(g_hMemDC, oldPen);
    SelectObject(g_hMemDC, oldBrush);
    DeleteObject(pen);
    if (brush) DeleteObject(brush);
}

static void Draw_Bucket(int x, int y) {
    COLORREF target = GetPixel(g_hMemDC, x, y);
    if (target == CLR_INVALID) return;
    if (target == g_color) return;
    ExtFloodFill(g_hMemDC, x, y, target, FLOODFILLSURFACE);
}

static BOOL SaveBmp(LPCSTR path) {
    BITMAP bm;
    if (!GetObject(g_hBitmap, sizeof(bm), &bm)) return FALSE;
    int w = bm.bmWidth, h = bm.bmHeight;

    BITMAPFILEHEADER fh = {0};
    BITMAPINFOHEADER ih = {0};
    ih.biSize        = sizeof(ih);
    ih.biWidth       = w;
    ih.biHeight      = h;
    ih.biPlanes      = 1;
    ih.biBitCount    = 24;
    ih.biCompression = BI_RGB;

    int row = ((w * 3 + 3) / 4) * 4;
    int img_size = row * h;

    fh.bfType      = 0x4D42;
    fh.bfOffBits   = sizeof(fh) + sizeof(ih);
    fh.bfSize      = fh.bfOffBits + img_size;

    BITMAPINFO bi = {0};
    bi.bmiHeader = ih;

    BYTE *bits = (BYTE *)malloc(img_size);
    if (!bits) return FALSE;

    if (!GetDIBits(g_hMemDC, g_hBitmap, 0, h, bits, &bi, DIB_RGB_COLORS)) {
        free(bits);
        return FALSE;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) { free(bits); return FALSE; }
    fwrite(&fh, sizeof(fh), 1, fp);
    fwrite(&ih, sizeof(ih), 1, fp);
    fwrite(bits, 1, img_size, fp);
    fclose(fp);
    free(bits);
    return TRUE;
}

static BOOL LoadBmp(LPCSTR path) {
    HBITMAP hb = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0,
                                     LR_LOADFROMFILE);
    if (!hb) return FALSE;

    HDC screen = GetDC(NULL);
    HDC tempDC = CreateCompatibleDC(screen);
    HBITMAP oldBmp = (HBITMAP)SelectObject(tempDC, hb);

    BITMAP bm;
    GetObject(hb, sizeof(bm), &bm);
    int w = MIN(bm.bmWidth, CANVAS_W);
    int h = MIN(bm.bmHeight, CANVAS_H);

    RECT r = {0, 0, CANVAS_W, CANVAS_H};
    FillRect(g_hMemDC, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    BitBlt(g_hMemDC, 0, 0, w, h, tempDC, 0, 0, SRCCOPY);

    SelectObject(tempDC, oldBmp);
    DeleteDC(tempDC);
    DeleteObject(hb);
    ReleaseDC(NULL, screen);
    InvalidateRect(g_hCanvas, NULL, FALSE);
    return TRUE;
}

static BOOL SaveDialog(LPSTR out, int out_len) {
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hMain;
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = out_len;
    ofn.lpstrFilter = "Bitmap (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "bmp";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    out[0] = 0;
    return GetSaveFileNameA(&ofn);
}

static BOOL OpenDialog(LPSTR out, int out_len) {
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hMain;
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = out_len;
    ofn.lpstrFilter = "Bitmap (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    out[0] = 0;
    return GetOpenFileNameA(&ofn);
}

static void PickCustomColor(void) {
    CHOOSECOLORA cc = {0};
    static COLORREF acrCust[16];
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner     = g_hMain;
    cc.rgbResult     = g_color;
    cc.lpCustColors = acrCust;
    cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorA(&cc)) {
        g_color = cc.rgbResult;
        InvalidateRect(g_hPalette, NULL, FALSE);
        UpdateTitle();
    }
}

static void SyncToolMenuChecks(HWND hwnd) {
    HMENU menu = GetMenu(hwnd);
    HMENU tools = GetSubMenu(menu, 1);
    int tools_list[] = { ID_TOOL_PENCIL, ID_TOOL_ERASER, ID_TOOL_LINE,
                        ID_TOOL_RECT, ID_TOOL_ELLIPSE,
                        ID_TOOL_FRECT, ID_TOOL_FELLIPSE, ID_TOOL_BUCKET };
    for (int i = 0; i < 8; i++) {
        CheckMenuItem(tools, tools_list[i],
                      MF_BYCOMMAND | (tools_list[i] == g_tool ? MF_CHECKED : MF_UNCHECKED));
    }
}

static HMENU BuildMenu(void) {
    HMENU file = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, ID_FILE_NEW,    "&New\tCtrl+N");
    AppendMenuA(file, MF_STRING, ID_FILE_OPEN,   "&Open...\tCtrl+O");
    AppendMenuA(file, MF_STRING, ID_FILE_SAVE,    "&Save As...\tCtrl+S");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_CUSTOM, "&Custom Color...\tB");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_EXIT,   "E&xit\tAlt+F4");

    HMENU tools = CreatePopupMenu();
    AppendMenuA(tools, MF_STRING, ID_TOOL_PENCIL,   "&Pencil\tP");
    AppendMenuA(tools, MF_STRING, ID_TOOL_ERASER,   "&Eraser\tE");
    AppendMenuA(tools, MF_SEPARATOR, 0, NULL);
    AppendMenuA(tools, MF_STRING, ID_TOOL_LINE,     "&Line\tL");
    AppendMenuA(tools, MF_STRING, ID_TOOL_RECT,     "&Rectangle\tR");
    AppendMenuA(tools, MF_STRING, ID_TOOL_ELLIPSE,  "&Ellipse\tO");
    AppendMenuA(tools, MF_STRING, ID_TOOL_FRECT,    "Filled &Rectangle\tShift+R");
    AppendMenuA(tools, MF_STRING, ID_TOOL_FELLIPSE,  "Filled &Ellipse\tShift+O");
    AppendMenuA(tools, MF_SEPARATOR, 0, NULL);
    AppendMenuA(tools, MF_STRING, ID_TOOL_BUCKET,    "&Bucket Fill\tF");

    HMENU help = CreatePopupMenu();
    AppendMenuA(help, MF_STRING, ID_HELP_ABOUT, "&About");

    HMENU bar = CreateMenu();
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file,  "&File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)tools, "&Tools");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)help,  "&Help");
    return bar;
}

static HACCEL BuildAccel(void) {
    ACCEL a[] = {
        { FCONTROL|FVIRTKEY, 'N', ID_FILE_NEW },
        { FCONTROL|FVIRTKEY, 'O', ID_FILE_OPEN },
        { FCONTROL|FVIRTKEY, 'S', ID_FILE_SAVE },
    };
    return CreateAcceleratorTableA(a, 3);
}

static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            BitBlt(hdc, 0, 0, CANVAS_W, CANVAS_H, g_hMemDC, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN: {
            g_startPt.x = g_lastPt.x = LOWORD(lp);
            g_startPt.y = g_lastPt.y = HIWORD(lp);
            g_drawing = TRUE;
            SetCapture(hwnd);

            if (g_tool == ID_TOOL_PENCIL || g_tool == ID_TOOL_ERASER) {
                Draw_Freehand(g_startPt.x, g_startPt.y,
                              g_startPt.x, g_startPt.y);
            } else if (g_tool == ID_TOOL_BUCKET) {
                Draw_Bucket(g_startPt.x, g_startPt.y);
                g_drawing = FALSE;
                ReleaseCapture();
            } else {
                Canvas_TakeSnapshot();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!g_drawing) return 0;
            int x = LOWORD(lp), y = HIWORD(lp);
            if (g_tool == ID_TOOL_PENCIL || g_tool == ID_TOOL_ERASER) {
                Draw_Freehand(g_lastPt.x, g_lastPt.y, x, y);
                g_lastPt.x = x; g_lastPt.y = y;
            } else if (g_tool != ID_TOOL_BUCKET) {
                Canvas_RestoreSnapshot();
                Draw_Shape(g_startPt.x, g_startPt.y, x, y);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!g_drawing) return 0;
            int x = LOWORD(lp), y = HIWORD(lp);
            if (g_tool != ID_TOOL_PENCIL && g_tool != ID_TOOL_ERASER
                && g_tool != ID_TOOL_BUCKET) {
                Canvas_RestoreSnapshot();
                Draw_Shape(g_startPt.x, g_startPt.y, x, y);
            }
            g_drawing = FALSE;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK PaletteProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT r; GetClientRect(hwnd, &r);
            FillRect(hdc, &r, (HBRUSH)GetStockObject(DKGRAY_BRUSH));

            int per_row = 10;
            for (int i = 0; i < (int)PALETTE_COUNT; i++) {
                int col = i % per_row, row = i / per_row;
                RECT cr = {
                    SWATCH_PAD + col * (SWATCH_SIZE + SWATCH_PAD),
                    SWATCH_PAD + row * (SWATCH_SIZE + SWATCH_PAD),
                    SWATCH_PAD + col * (SWATCH_SIZE + SWATCH_PAD) + SWATCH_SIZE,
                    SWATCH_PAD + row * (SWATCH_SIZE + SWATCH_PAD) + SWATCH_SIZE
                };
                HBRUSH b = CreateSolidBrush(g_palette[i]);
                FillRect(hdc, &cr, b);
                DeleteObject(b);
                FrameRect(hdc, &cr, (HBRUSH)GetStockObject(BLACK_BRUSH));
                if (g_palette[i] == g_color) {
                    RECT hr = { cr.left - 2, cr.top - 2,
                                cr.right + 2, cr.bottom + 2 };
                    FrameRect(hdc, &hr, (HBRUSH)GetStockObject(WHITE_BRUSH));
                }
            }
            const char *hint = "  Left-click to pick color. Right-click for custom color.";
            RECT tr; GetClientRect(hwnd, &tr);
            tr.left += 300;
            SetTextColor(hdc, RGB(255,255,255));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, hint, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_RBUTTONUP:
            PickCustomColor();
            return 0;
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lp), y = HIWORD(lp);
            int per_row = 10;
            int col = x / (SWATCH_SIZE + SWATCH_PAD);
            int row = y / (SWATCH_SIZE + SWATCH_PAD);
            int idx = row * per_row + col;
            if (idx >= 0 && idx < (int)PALETTE_COUNT) {
                g_color = g_palette[idx];
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateTitle();
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hCanvas = CreateWindowExA(0, "MiniPaintCanvas", "",
                WS_CHILD | WS_VISIBLE | WS_BORDER,
                0, 0, CANVAS_W, CANVAS_H, hwnd, NULL, g_hInst, NULL);
            Canvas_Create(CANVAS_W, CANVAS_H);

            g_hPalette = CreateWindowExA(0, "MiniPaintPalette", "",
                WS_CHILD | WS_VISIBLE | WS_BORDER,
                0, 0, 100, PALETTE_H, hwnd, NULL, g_hInst, NULL);

            SyncToolMenuChecks(hwnd);
            UpdateTitle();
            return 0;
        }
        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            int cx = (w > CANVAS_W) ? (w - CANVAS_W) / 2 : 0;
            MoveWindow(g_hCanvas, cx, 0, CANVAS_W, CANVAS_H, TRUE);
            MoveWindow(g_hPalette, 0, h - PALETTE_H, w, PALETTE_H, TRUE);
            return 0;
        }
        case WM_COMMAND: {
            WORD cmd = LOWORD(wp);
            switch (cmd) {
                case ID_FILE_EXIT:
                    PostQuitMessage(0);
                    return 0;
                case ID_FILE_NEW: {
                    RECT r = {0, 0, CANVAS_W, CANVAS_H};
                    FillRect(g_hMemDC, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
                    InvalidateRect(g_hCanvas, NULL, FALSE);
                    return 0;
                }
                case ID_FILE_SAVE: {
                    char path[MAX_PATH];
                    if (SaveDialog(path, MAX_PATH)) {
                        if (!SaveBmp(path))
                            MessageBoxA(hwnd, "Failed to save file.", "Error",
                                       MB_ICONERROR);
                    }
                    return 0;
                }
                case ID_FILE_OPEN: {
                    char path[MAX_PATH];
                    if (OpenDialog(path, MAX_PATH)) {
                        if (!LoadBmp(path))
                            MessageBoxA(hwnd, "Failed to load BMP file.",
                                        "Error", MB_ICONERROR);
                    }
                    return 0;
                }
                case ID_FILE_CUSTOM:
                    PickCustomColor();
                    return 0;
                case ID_HELP_ABOUT:
                    MessageBoxA(hwnd,
                        "Mini Paint  -  C / Win32 / GDI edition\r\n\r\n"
                        "A tiny Microsoft Paint clone.\r\n"
                        "Pure Win32 + GDI. No dependencies.\r\n\r\n"
                        "Compile:\r\n"
                        "  gcc paint.c -o paint.exe -mwindows -lgdi32 "
                        "-luser32 -lcomdlg32",
                        "About Mini Paint", MB_OK | MB_ICONINFORMATION);
                    return 0;
                default:
                    if (cmd >= ID_TOOL_PENCIL && cmd <= ID_TOOL_BUCKET) {
                        g_tool = cmd;
                        SyncToolMenuChecks(hwnd);
                        UpdateTitle();
                        return 0;
                    }
            }
            break;
        }
        case WM_KEYDOWN: {
            int new_tool = 0;
            BOOL shift = (GetKeyState(VK_SHIFT) & 0x80) != 0;
            switch (wp) {
                case 'P': new_tool = ID_TOOL_PENCIL;   break;
                case 'E': new_tool = ID_TOOL_ERASER;   break;
                case 'L': new_tool = ID_TOOL_LINE;     break;
                case 'R': new_tool = shift ? ID_TOOL_FRECT   : ID_TOOL_RECT;   break;
                case 'O': new_tool = shift ? ID_TOOL_FELLIPSE : ID_TOOL_ELLIPSE; break;
                case 'F': new_tool = ID_TOOL_BUCKET;   break;
                case 'B': PickCustomColor(); return 0;
                case VK_OEM_PLUS:
                case VK_ADD:
                    if (g_size < 60) { g_size += 2; UpdateTitle(); }
                    return 0;
                case VK_OEM_MINUS:
                case VK_SUBTRACT:
                    if (g_size > 1) { g_size -= 2; if (g_size < 1) g_size = 1; UpdateTitle(); }
                    return 0;
            }
            if (new_tool) {
                g_tool = new_tool;
                SyncToolMenuChecks(hwnd);
                UpdateTitle();
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd;
    g_hInst = hInst;
    g_hBgBrush = CreateSolidBrush(RGB(220, 220, 220));

    WNDCLASSA wc = {0};
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = "MiniPaintMain";
    wc.lpfnWndProc   = MainProc;
    RegisterClassA(&wc);

    wc.lpfnWndProc   = CanvasProc;
    wc.hCursor       = LoadCursor(NULL, IDC_CROSS);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "MiniPaintCanvas";
    RegisterClassA(&wc);

    wc.lpfnWndProc   = PaletteProc;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "MiniPaintPalette";
    RegisterClassA(&wc);

    HMENU menu  = BuildMenu();
    HACCEL acc  = BuildAccel();

    g_hMain = CreateWindowExA(0, "MiniPaintMain", "Mini Paint",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CANVAS_W + 40, CANVAS_H + PALETTE_H + 90,
        NULL, menu, hInst, NULL);

    ShowWindow(g_hMain, show);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(g_hMain, acc, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return (int)msg.wParam;
}

