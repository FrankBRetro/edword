/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * wintool.c - tool windows of their own: the font selector and the
 *             print preview; and the window class they (and the drawing
 *             editor) share (after amiga/amitool.c)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winui.h"

#define TOOL_ID0 100        /* control ids, clear of IDOK and IDCANCEL */

/* ================================================================= */
/*  A tool window: its messages become events for tool_route         */
/* ================================================================= */

static LRESULT CALLBACK tool_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    EVT e;
    memset(&e, 0, sizeof e);
    e.IDCMPWindow = h;
    switch (msg) {
    case WM_COMMAND: {
        HWND c = (HWND)lp;
        int code = HIWORD(wp);
        char cls[16];
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {   /* Return and Esc, from IsDialogMessage */
            tool_route_key(h, LOWORD(wp) == IDOK ? K_RETURN : K_ESC, 0);
            return 0;
        }
        if (!c) return 0;
        GetClassNameA(c, cls, sizeof cls);
        if (!lstrcmpiA(cls, "LISTBOX")) {
            if (code != LBN_SELCHANGE && code != LBN_DBLCLK) return 0;
            e.Code = (UWORD)SendMessageA(c, LB_GETCURSEL, 0, 0);
            e.Micros = code == LBN_DBLCLK;
        } else if (!lstrcmpiA(cls, "BUTTON")) {
            if (code != BN_CLICKED) return 0;
            e.Code = (UWORD)(SendMessageA(c, BM_GETCHECK, 0, 0) == BST_CHECKED);
        } else if (!lstrcmpiA(cls, "COMBOBOX")) {
            if (code != CBN_SELCHANGE) return 0;
            e.Code = (UWORD)SendMessageA(c, CB_GETCURSEL, 0, 0);
        } else return 0;
        e.Class = IDCMP_GADGETUP;
        e.IAddress = (void *)(INT_PTR)(LOWORD(wp) - TOOL_ID0);
        e.Seconds = (ULONG)GetMessageTime();
        tool_route(&e);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        e.Class = IDCMP_REFRESHWINDOW;
        tool_route(&e);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)wp, &r, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    case WM_SIZE:
        e.Class = IDCMP_NEWSIZE;
        tool_route(&e);
        return 0;
    case WM_CLOSE:
        e.Class = IDCMP_CLOSEWINDOW;
        tool_route(&e);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) { e.Class = IDCMP_ACTIVEWINDOW; tool_route(&e); }
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        e.Class = IDCMP_MOUSEBUTTONS;
        e.Code = SELECTDOWN;
        e.MouseX = (short)LOWORD(lp);
        e.MouseY = (short)HIWORD(lp);
        e.Qualifier = (UWORD)key_qual();
        e.Micros = msg == WM_LBUTTONDBLCLK;
        e.Seconds = (ULONG)GetMessageTime();
        tool_route(&e);
        return 0;
    case WM_KEYDOWN: {
        int k = key_from_vk(wp);
        if (k) { tool_route_key(h, k, key_qual()); return 0; }
        break;
    }
    case WM_CHAR:
        if (wp >= 32 && wp != 127) tool_route_key(h, (int)wp, key_qual());
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static int tool_reg;


/* a tool window with a client area of w x h at (x, y) on the screen */
WIN tool_window(const char *title, long x, long y, long w, long h, int sizable)
{
    RECT r;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | (sizable ? WS_THICKFRAME : 0);
    if (!tool_reg) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = tool_proc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = "EdWordTool";
        RegisterClassA(&wc);
        tool_reg = 1;
    }
    r.left = 0; r.top = 0; r.right = w; r.bottom = h;
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_TOOLWINDOW);
    w = r.right - r.left;
    h = r.bottom - r.top;
    if (x + w > scr_w) x = scr_w - w;
    if (y + h > scr_h) y = scr_h - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return CreateWindowExA(WS_EX_TOOLWINDOW, "EdWordTool", title, style, (int)x, (int)y, (int)w, (int)h, frame, NULL, hinst, NULL);
}

HWND tool_ctl(WIN w, const char *cls, const char *text, DWORD style, long x, long y, long cw, long ch, int id)
{
    HWND c = CreateWindowExA(!lstrcmpiA(cls, "LISTBOX") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             (int)x, (int)y, (int)cw, (int)ch, w, (HMENU)(INT_PTR)(id + TOOL_ID0), hinst, NULL);
    if (c) SendMessageA(c, WM_SETFONT, (WPARAM)uifont, FALSE);
    return c;
}

static void client_rect(WIN w, GRECT *a)
{
    RECT r;
    GetClientRect(w, &r);
    a->g_x = 0; a->g_y = 0; a->g_w = (short)r.right; a->g_h = (short)r.bottom;
}

/* ================================================================= */
/*  Font selector                                                     */
/* ================================================================= */

static const short fsz[] = { 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 60, 72 };
#define NFSZ ((int)(sizeof fsz / sizeof fsz[0]))
enum { FG_FONTS = 1, FG_SIZES, FG_BOLD, FG_ITALIC, FG_UNDER, FG_APPLY, FG_CLOSE };

static struct {
    int open, font, size, attr;
    WIN w;
    HWND g[8];
    GRECT pv;
    char  title[40];
} fs;

static int size_index(int sz)
{
    int i;
    for (i = 0; i < NFSZ; i++) if (fsz[i] == sz) return i;
    return -1;
}

static void fs_sample(void)
{
    char st[80];
    long base, x;
    int px;
    const char *sample = L(S_FONT_SAMPLE);
    if (!fs.open) return;
    gfx_begin(fs.w);
    fill_g(&fs.pv, P_WHITE);
    bevel(&fs.pv, 1);
    {
        GRECT c = fs.pv;
        c.g_x += 2; c.g_y += 2; c.g_w -= 4; c.g_h -= 4;
        clip_set(&c);
    }
    stc(st, sample, sizeof st);
    px = (int)pt2px_y(fs.size);
    base = fs.pv.g_y + fs.pv.g_h * 2 / 3;
    x = fs.pv.g_x + 6;
    text_at(x, base, st, (int)strlen(st), fs.font, px, fs.attr & (A_BOLD | A_ITALIC), P_BLACK);
    if (fs.attr & A_UNDER) {
        const METRICS *m = get_metrics(fs.font, fs.size, fs.attr);
        long w = 0;
        int k;
        for (k = 0; st[k]; k++) w += m->w[(unsigned char)st[k]];
        line(x, base + 2, x + w, base + 2, P_BLACK);
    }
    clip_off();
    gfx_end();
}

/* set the controls from fs */
static void fs_show(void)
{
    int si = size_index(fs.size);
    if (!fs.open) return;
    SendMessageA(fs.g[FG_FONTS], LB_SETCURSEL, (WPARAM)fs.font, 0);
    SendMessageA(fs.g[FG_SIZES], LB_SETCURSEL, (WPARAM)si, 0);
    SendMessageA(fs.g[FG_BOLD], BM_SETCHECK, (fs.attr & A_BOLD) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(fs.g[FG_ITALIC], BM_SETCHECK, (fs.attr & A_ITALIC) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(fs.g[FG_UNDER], BM_SETCHECK, (fs.attr & A_UNDER) ? BST_CHECKED : BST_UNCHECKED, 0);
    fs_sample();
}

void fontsel_sync(void)
{
    DOC *d = active_doc();
    CELL c;
    long a0, b0;
    int f, sz, a;
    if (!fs.open || !d) return;
    c = doc_sel(d, &a0, &b0) ? d->text[a0] : d->ins;
    f = c.font < nfonts ? c.font : 0;
    sz = c.size;
    a = c.attr & (A_BOLD | A_ITALIC | A_UNDER);
    if (f == fs.font && sz == fs.size && a == fs.attr) return;
    fs.font = f; fs.size = sz; fs.attr = a;
    fs_show();
}

static void fontsel_close(void)
{
    if (!fs.open) return;
    fs.open = 0;
    DestroyWindow(fs.w);
    fs.w = NULL;
    SetActiveWindow(frame);
}

void fontsel_open(void)
{
    long lh = 9 * (hchar + 1) + 4, top = hchar + 8, W = 420, H, x;
    int i;
    RECT fr;
    char sn[8];
    if (fs.open) { SetActiveWindow(fs.w); fs.font = -1; fontsel_sync(); return; }
    H = top + lh + 10 + 56 + 10 + hchar + 8 + 10;
    sprintf(fs.title, "%s", L(S_FONT));
    GetWindowRect(frame, &fr);
    fs.w = tool_window(fs.title, fr.right - W - 40, fr.top + 80, W, H, 0);
    if (!fs.w) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    memset(fs.g, 0, sizeof fs.g);
    tool_ctl(fs.w, "STATIC", L(S_FONT), SS_LEFT, 10, 6, 200, hchar + 2, 0);
    tool_ctl(fs.w, "STATIC", L(S_SIZE), SS_LEFT, 220, 6, 64, hchar + 2, 0);
    fs.g[FG_FONTS] = tool_ctl(fs.w, "LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT, 10, top, 200, lh, FG_FONTS);
    fs.g[FG_SIZES] = tool_ctl(fs.w, "LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT, 220, top, 64, lh, FG_SIZES);
    for (i = 0; i < nfonts; i++) SendMessageA(fs.g[FG_FONTS], LB_ADDSTRING, 0, (LPARAM)fonts[i].name);
    for (i = 0; i < NFSZ; i++) { sprintf(sn, "%3d", fsz[i]); SendMessageA(fs.g[FG_SIZES], LB_ADDSTRING, 0, (LPARAM)sn); }
    for (i = 0; i < 3; i++) {
        static const int sty[3] = { S_BOLD, S_ITALIC, S_UNDERLINE };
        fs.g[FG_BOLD + i] = tool_ctl(fs.w, "BUTTON", L(sty[i]), BS_AUTOCHECKBOX | WS_TABSTOP, 300, top + i * (hchar + 10), 110, hchar + 6,
                                     FG_BOLD + i);
    }
    fs.pv.g_x = 10; fs.pv.g_y = (short)(top + lh + 10);
    fs.pv.g_w = (short)(W - 20); fs.pv.g_h = 56;
    x = W - 10;
    for (i = 1; i >= 0; i--) {
        const char *t = L(i ? S_CLOSE : S_APPLY);
        long bw = sys_width(t) + 24;
        if (bw < 75) bw = 75;
        x -= bw;
        fs.g[i ? FG_CLOSE : FG_APPLY] = tool_ctl(fs.w, "BUTTON", t, (i ? BS_PUSHBUTTON : BS_DEFPUSHBUTTON) | WS_TABSTOP,
                                                 x, fs.pv.g_y + fs.pv.g_h + 10, bw, hchar + 8, i ? FG_CLOSE : FG_APPLY);
        x -= 10;
    }
    fs.open = 1;
    fs.font = -1;
    ShowWindow(fs.w, SW_SHOW);
    fontsel_sync();
    fs_sample();
}

static void fs_apply(void)
{
    DOC *d = active_doc();
    int f = fs.font, sz = fs.size, a = fs.attr;
    if (!d) return;
    apply_style(d, AP_FONT, f);
    apply_style(d, AP_SIZE, sz);
    apply_style(d, AP_ATTRS, a);
}

static int fs_owns(WIN w) { return fs.open && w == fs.w; }

static void fs_msg(EVT *m)
{
    int id, dbl;
    switch (m->Class) {
    case IDCMP_CLOSEWINDOW: fontsel_close(); return;
    case IDCMP_REFRESHWINDOW: fs_sample(); return;
    case IDCMP_ACTIVEWINDOW: fontsel_sync(); return;
    case IDCMP_GADGETUP:
        id = (int)(INT_PTR)m->IAddress;
        dbl = m->Micros != 0;
        switch (id) {
        case FG_FONTS: if (m->Code < nfonts) fs.font = m->Code; fs_sample(); if (dbl) fs_apply(); break;
        case FG_SIZES: if (m->Code < NFSZ) fs.size = fsz[m->Code]; fs_sample(); if (dbl) fs_apply(); break;
        case FG_BOLD: case FG_ITALIC: case FG_UNDER:
            if (m->Code) fs.attr |= 1 << (id - FG_BOLD); else fs.attr &= ~(1 << (id - FG_BOLD));
            fs_sample();
            break;
        case FG_APPLY: fs_apply(); break;
        case FG_CLOSE: fontsel_close(); break;
        }
        return;
    }
}

static void fs_key(int key, int qual)
{
    (void)qual;
    if (key == K_RETURN || key == K_ENTER) fs_apply();
    else if (key == K_ESC) fontsel_close();
}

static void fs_docgone(DOC *d) { (void)d; }
const TOOL fontsel_tool = { fs_owns, fs_msg, fs_key, fontsel_close, fs_docgone };

/* ================================================================= */
/*  Print preview                                                     */
/* ================================================================= */

enum { PG_PREV = 1, PG_NEXT, PG_ZOOM, PG_PRINT, PG_PDF, PG_CLOSE };
static struct {
    int  open, stale, zoom;          /* zoom 0: whole page, 1: 100% */
    WIN  w;
    HWND zbtn;
    DOC *d;
    int  page;
    long sx, sy, lx, barh;
    char title[64], label[40];
} pv;

static void pv_area(GRECT *a)
{
    client_rect(pv.w, a);
    a->g_y = (short)(a->g_y + pv.barh);
    a->g_h = (short)(a->g_h - pv.barh);
}

static void pv_geom(const GRECT *a, long *n, long *den, long *ox, long *oy)
{
    long W = pt2px_x(PS(pv.d).w), H = pt2px_y(PS(pv.d).h), aw = a->g_w, ah = a->g_h;
    if (pv.zoom == 0) {
        long p = (aw - 16) * 1000L / W, q = (ah - 16) * 1000L / H;
        *den = 1000;
        *n = p < q ? p : q;
        if (*n > 1000) *n = 1000;
        if (*n < 1) *n = 1;
        *ox = a->g_x + (aw - W * *n / 1000) / 2;
        *oy = a->g_y + (ah - H * *n / 1000) / 2;
    } else {
        *n = *den = 1;
        *ox = a->g_x + 8 - pv.sx;
        *oy = a->g_y + 8 - pv.sy;
    }
}

static void pv_clamp(void)
{
    GRECT a;
    long W, H;
    int np;
    if (!pv.open) return;
    pv_area(&a);
    np = doc_npages(pv.d);
    if (pv.page >= np) pv.page = np - 1;
    if (pv.page < 0) pv.page = 0;
    W = pt2px_x(PS(pv.d).w) + 16; H = pt2px_y(PS(pv.d).h) + 16;
    if (pv.sx > W - a.g_w) pv.sx = W - a.g_w;
    if (pv.sy > H - a.g_h) pv.sy = H - a.g_h;
    if (pv.sx < 0) pv.sx = 0;
    if (pv.sy < 0) pv.sy = 0;
}

/* drawn into a bitmap first, so nothing flickers */
static void pv_draw(void)
{
    GRECT a, page, all;
    long n, den, ox, oy, W, H;
    HDC wdc, mdc;
    HBITMAP bm, ob;
    if (!pv.open) return;
    pv_clamp();
    gfx_begin(pv.w);
    sprintf(pv.label, L(S_PAGE_N_OF), pv.page + 1, doc_npages(pv.d));
    fill_rect(pv.lx, 2, pv.lx + sys_width(pv.label) + 40, pv.barh - 3, P_LIGHT);
    {
        HFONT of = (HFONT)SelectObject(gdc, uifont);
        SetBkMode(gdc, TRANSPARENT);
        SetTextColor(gdc, GetSysColor(COLOR_BTNTEXT));
        SelectObject(gdc, of);
    }
    gfx_end();
    {
        HDC dc = GetDC(pv.w);
        RECT r;
        HFONT of = (HFONT)SelectObject(dc, uifont);
        r.left = pv.lx; r.top = 2; r.right = pv.lx + sys_width(pv.label) + 40; r.bottom = pv.barh - 3;
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        TextOutA(dc, pv.lx, (pv.barh - hchar) / 2, pv.label, (int)strlen(pv.label));
        SelectObject(dc, of);
        ReleaseDC(pv.w, dc);
    }
    pv_area(&a);
    if (a.g_w <= 0 || a.g_h <= 0) return;
    client_rect(pv.w, &all);
    wdc = GetDC(pv.w);
    mdc = CreateCompatibleDC(wdc);
    bm = CreateCompatibleBitmap(wdc, a.g_w, a.g_h);
    ob = (HBITMAP)SelectObject(mdc, bm);
    gfx_dc(mdc, a.g_x, a.g_y, a.g_w, a.g_h);
    clip_set(&a);
    fill_g(&a, P_DESK);
    pv_geom(&a, &n, &den, &ox, &oy);
    W = pt2px_x(PS(pv.d).w) * n / den;
    H = pt2px_y(PS(pv.d).h) * n / den;
    fill_rect(ox + 4, oy + 4, ox + W + 3, oy + H + 3, P_SHADOW);
    fill_rect(ox, oy, ox + W - 1, oy + H - 1, P_WHITE);
    frame_rect(ox, oy, ox + W - 1, oy + H - 1, P_BLACK);
    page.g_x = (short)(ox + 1); page.g_y = (short)(oy + 1); page.g_w = (short)(W - 2); page.g_h = (short)(H - 2);
    if (rc_inter(&a, &page)) {
        clip_set(&page);
        draw_preview_page(pv.d, pv.page, ox, oy, n, den);
    }
    gfx_end();
    BitBlt(wdc, a.g_x, a.g_y, a.g_w, a.g_h, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, ob);
    DeleteObject(bm);
    DeleteDC(mdc);
    ReleaseDC(pv.w, wdc);
    (void)all;
}

static void pv_set_title(void)
{
    sprintf(pv.title, "%s: %.30s", L(S_PREVIEW), lat(pv.d->base));
    SetWindowTextA(pv.w, pv.title);
    if (pv.zbtn) SetWindowTextA(pv.zbtn, pv.zoom ? L(S_WHOLE_PAGE) : "100%");
}

static void preview_close(void)
{
    if (!pv.open) return;
    pv.open = 0;
    DestroyWindow(pv.w);
    pv.w = NULL;
    SetActiveWindow(frame);
}

void preview_open(DOC *d)
{
    long W, H, ww, wh, x;
    int i;
    RECT fr;
    static const char *arrows[2] = { "<", ">" };
    if (!d) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    doc_layout(d);
    pv.d = d;
    pv.page = d->lines[doc_find_line(d, d->cursor)].page;
    if (pv.open) { SetActiveWindow(pv.w); pv_set_title(); pv_draw(); return; }
    pv.zoom = 0;
    pv.sx = pv.sy = 0;
    W = pt2px_x(PS(d).w); H = pt2px_y(PS(d).h);
    GetWindowRect(frame, &fr);
    wh = (fr.bottom - fr.top) - 60;
    pv.barh = hchar + 14;
    ww = (wh - pv.barh - 20) * W / H + 40;
    {   /* room for the buttons */
        long need = 6 + 64 + 12 + sys_width(L(S_PAGE_N_OF)) + 24 + sys_width(L(S_WHOLE_PAGE)) + 26 +
                    sys_width(L(S_PRINT_BTN)) + 26 + sys_width(L(S_EXPORT_PDF)) + 26 + sys_width(L(S_CLOSE)) + 26 + 30;
        if (ww < need) ww = need;
    }
    if (ww > scr_w - 20) ww = scr_w - 20;
    pv.title[0] = 0;
    pv.w = tool_window("", fr.right - ww - 30, fr.top + 30, ww, wh, 1);
    if (!pv.w) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    x = 6;
    for (i = 0; i < 2; i++) {
        tool_ctl(pv.w, "BUTTON", arrows[i], BS_PUSHBUTTON, x, 3, 28, hchar + 8, PG_PREV + i);
        x += 32;
    }
    pv.lx = x + 6;
    x += 12 + sys_width(L(S_PAGE_N_OF)) + 24;
    {
        static const int lab[4] = { S_WHOLE_PAGE, S_PRINT_BTN, S_EXPORT_PDF, S_CLOSE };
        for (i = 0; i < 4; i++) {
            long bw = sys_width(L(lab[i])) + 20;
            HWND b;
            if (i == 0 && bw < sys_width("100%") + 20) bw = sys_width("100%") + 20;
            b = tool_ctl(pv.w, "BUTTON", i == 0 ? "100%" : L(lab[i]), BS_PUSHBUTTON, x, 3, bw, hchar + 8, PG_ZOOM + i);
            if (i == 0) pv.zbtn = b;
            x += bw + 6;
        }
    }
    pv.open = 1;
    pv.stale = 0;
    pv_set_title();
    ShowWindow(pv.w, SW_SHOW);
    pv_draw();
}

void preview_changed(DOC *d) { if (pv.open && pv.d == d) pv.stale = 1; }

int preview_idle(int run)
{
    if (!pv.open || !pv.stale) return 0;
    if (run) {
        pv.stale = 0;
        doc_layout(pv.d);
        pv_set_title();
        pv_draw();
    }
    return 1;
}

static void pv_goto(int page)
{
    int np = doc_npages(pv.d);
    if (page < 0) page = 0;
    if (page >= np) page = np - 1;
    if (page == pv.page) return;
    pv.page = page;
    pv.sy = 0;
    pv_draw();
}

static void pv_zoom_at(short mx, short my)
{
    GRECT a;
    long n, den, ox, oy;
    pv_area(&a);
    if (!pv.zoom) {
        pv_geom(&a, &n, &den, &ox, &oy);
        pv.sx = (mx - ox) * den / n - a.g_w / 2;
        pv.sy = (my - oy) * den / n - a.g_h / 2;
    }
    pv.zoom = !pv.zoom;
    pv_set_title();
    pv_draw();
}

static int pv_owns(WIN w) { return pv.open && w == pv.w; }

static void pv_msg(EVT *m)
{
    GRECT a;
    switch (m->Class) {
    case IDCMP_CLOSEWINDOW: preview_close(); return;
    case IDCMP_REFRESHWINDOW: pv_draw(); return;
    case IDCMP_NEWSIZE: InvalidateRect(pv.w, NULL, TRUE); return;
    case IDCMP_GADGETUP:
        switch ((int)(INT_PTR)m->IAddress) {
        case PG_PREV: pv_goto(pv.page - 1); break;
        case PG_NEXT: pv_goto(pv.page + 1); break;
        case PG_ZOOM: if (!pv.zoom) pv.sx = pv.sy = 0; pv.zoom = !pv.zoom; pv_set_title(); pv_draw(); break;
        case PG_PDF: cmd_pdf(pv.d); break;
        case PG_PRINT: cmd_print(pv.d); break;
        case PG_CLOSE: preview_close(); break;
        }
        return;
    case IDCMP_MOUSEBUTTONS:
        pv_area(&a);
        if (m->Code != SELECTDOWN || !in_rect(&a, m->MouseX, m->MouseY)) return;
        if (!pv.zoom) { pv_zoom_at(m->MouseX, m->MouseY); return; }
        {
            /* drag to move the page, click to see it whole */
            short x, y, x0 = m->MouseX, y0 = m->MouseY;
            long sx0 = pv.sx, sy0 = pv.sy;
            int q, moved = 0;
            track_begin(pv.w);
            while (track_next(&x, &y, &q)) {
                if (x == x0 && y == y0 && !moved) continue;
                moved = 1;
                pv.sx = sx0 - (x - x0);
                pv.sy = sy0 - (y - y0);
                pv_draw();
            }
            track_end();
            if (!moved) pv_zoom_at(x0, y0);
        }
        return;
    }
}

static void pv_key(int key, int qual)
{
    GRECT a;
    (void)qual;
    pv_area(&a);
    switch (key) {
    case K_LEFT: case K_PGUP: pv_goto(pv.page - 1); break;
    case K_RIGHT: case K_PGDN: pv_goto(pv.page + 1); break;
    case K_ESC: preview_close(); break;
    case ' ': pv.zoom = !pv.zoom; pv_set_title(); pv_draw(); break;
    case K_UP: case K_DOWN:
        if (!pv.zoom) { pv_goto(pv.page + (key == K_UP ? -1 : 1)); break; }
        pv.sy += (key == K_UP ? -1 : 1) * a.g_h / 2;
        pv_draw();
        break;
    }
}

static void pv_docgone(DOC *d) { if (pv.open && pv.d == d) preview_close(); }
const TOOL preview_tool = { pv_owns, pv_msg, pv_key, preview_close, pv_docgone };
