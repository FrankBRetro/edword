/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * wingfx.c - drawing helpers on GDI (clipping, boxes, lines, filled
 *            shapes, text in the interface font, inverted feedback), the
 *            toolbar icons, the busy pointer, tracking the mouse while a
 *            button is held, and the character set conversions
 *            (after amiga/amigfx.c)
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "winui.h"

HDC  gdc;
WIN  gwin;
static int gox, goy;                        /* window position of the DC's origin */
static int own_dc, clipped;
static HBRUSH brushes[16];
static HPEN   pens[16];
static HBITMAP greybm;
static HBRUSH greybrush;

COLORREF pal[16] = {
    RGB(0xAA, 0xAA, 0xAA), RGB(0, 0, 0), RGB(0xFF, 0xFF, 0xFF), RGB(0x66, 0x88, 0xBB),
    RGB(0xCC, 0, 0), RGB(0, 0x88, 0), RGB(0, 0, 0xCC), RGB(0, 0x88, 0x88),
    RGB(0xAA, 0, 0xAA), RGB(0xBB, 0x88, 0), RGB(0x88, 0x88, 0x88), RGB(0xFF, 0xAA, 0),
    RGB(0x44, 0xFF, 0x44), RGB(0x22, 0x22, 0x22), RGB(0xEE, 0xEE, 0xEE), RGB(0xDD, 0xDD, 0xDD)
};

static HBRUSH brush(int p) { return brushes[p & 15]; }
static HPEN pen_of(int p) { return pens[p & 15]; }

void gfx_begin(WIN w)
{
    gwin = w;
    gdc = GetDC(w);
    own_dc = 1;
    gox = goy = 0;
    clipped = 0;
    SetBkMode(gdc, TRANSPARENT);
}

/* draw into dc, whose (0, 0) is window position (ox, oy) */
void gfx_dc(HDC dc, int ox, int oy, int w, int h)
{
    (void)w; (void)h;
    gwin = NULL;
    gdc = dc;
    own_dc = 0;
    gox = ox; goy = oy;
    clipped = 0;
    SetViewportOrgEx(dc, -ox, -oy, NULL);
    SetBkMode(gdc, TRANSPARENT);
}

void gfx_end(void)
{
    if (!gdc) return;
    clip_off();
    if (own_dc) ReleaseDC(gwin, gdc);
    else SetViewportOrgEx(gdc, 0, 0, NULL);
    gdc = NULL;
    gwin = NULL;
    own_dc = 0;
}

int rc_inter(const GRECT *a, GRECT *b)
{
    short x1 = a->g_x > b->g_x ? a->g_x : b->g_x;
    short y1 = a->g_y > b->g_y ? a->g_y : b->g_y;
    short x2 = (a->g_x + a->g_w < b->g_x + b->g_w) ? a->g_x + a->g_w : b->g_x + b->g_w;
    short y2 = (a->g_y + a->g_h < b->g_y + b->g_h) ? a->g_y + a->g_h : b->g_y + b->g_h;
    b->g_x = x1; b->g_y = y1; b->g_w = x2 - x1; b->g_h = y2 - y1;
    return x2 > x1 && y2 > y1;
}

int in_rect(const GRECT *r, short x, short y)
{
    return r->g_h > 0 && r->g_w > 0 && x >= r->g_x && y >= r->g_y && x < r->g_x + r->g_w && y < r->g_y + r->g_h;
}

/* drawing is limited to r until clip_off(); an empty r hides everything */
void clip_set(const GRECT *r)
{
    HRGN rg;
    if (!gdc) return;
    if (r->g_w > 0 && r->g_h > 0) rg = CreateRectRgn(r->g_x - gox, r->g_y - goy, r->g_x + r->g_w - gox, r->g_y + r->g_h - goy);
    else rg = CreateRectRgn(0, 0, 0, 0);
    SelectClipRgn(gdc, rg);
    DeleteObject(rg);
    clipped = 1;
}

void clip_off(void)
{
    if (clipped && gdc) SelectClipRgn(gdc, NULL);
    clipped = 0;
}

/* coordinates GDI on Windows 95 takes: 16 bits */
static int cw(long v) { return (int)(v < -16000 ? -16000 : v > 16000 ? 16000 : v); }

void fill_rect(long x1, long y1, long x2, long y2, int pen)
{
    RECT r;
    if (!gdc) return;
    if (x1 > x2) { long t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { long t = y1; y1 = y2; y2 = t; }
    r.left = cw(x1); r.top = cw(y1); r.right = cw(x2 + 1); r.bottom = cw(y2 + 1);
    FillRect(gdc, &r, brush(pen));
}

/* a 50% stipple in pen; the other pixels stay as they are */
static void pattern_rect(const RECT *r, int pen)
{
    HBRUSH ob;
    COLORREF ot = SetTextColor(gdc, RGB(0, 0, 0)), obk = SetBkColor(gdc, RGB(255, 255, 255));
    ob = (HBRUSH)SelectObject(gdc, greybrush);
    PatBlt(gdc, r->left, r->top, r->right - r->left, r->bottom - r->top, 0x00A000C9);   /* D and P */
    SetTextColor(gdc, PEN(pen));
    SetBkColor(gdc, RGB(0, 0, 0));
    PatBlt(gdc, r->left, r->top, r->right - r->left, r->bottom - r->top, 0x00FA0089);   /* D or P */
    SelectObject(gdc, ob);
    SetTextColor(gdc, ot);
    SetBkColor(gdc, obk);
}

void fill_pat(long x1, long y1, long x2, long y2, int pen, int grey)
{
    RECT r;
    if (!gdc) return;
    if (!grey) { fill_rect(x1, y1, x2, y2, pen); return; }
    if (x1 > x2) { long t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { long t = y1; y1 = y2; y2 = t; }
    r.left = cw(x1); r.top = cw(y1); r.right = cw(x2 + 1); r.bottom = cw(y2 + 1);
    pattern_rect(&r, pen);
}

void fill_g(const GRECT *r, int pen)
{
    if (r->g_w > 0 && r->g_h > 0) fill_rect(r->g_x, r->g_y, r->g_x + r->g_w - 1, r->g_y + r->g_h - 1, pen);
}

void line(long x1, long y1, long x2, long y2, int pen)
{
    HPEN op;
    if (!gdc) return;
    op = (HPEN)SelectObject(gdc, pen_of(pen));
    MoveToEx(gdc, cw(x1), cw(y1), NULL);
    LineTo(gdc, cw(x2), cw(y2));
    SetPixelV(gdc, cw(x2), cw(y2), PEN(pen));            /* GDI leaves out the last point */
    SelectObject(gdc, op);
}

void frame_rect(long x1, long y1, long x2, long y2, int pen)
{
    line(x1, y1, x2, y1, pen);
    line(x2, y1, x2, y2, pen);
    line(x2, y2, x1, y2, pen);
    line(x1, y2, x1, y1, pen);
}

void hline(long x1, long x2, long y, int pen, int dotted)
{
    long x;
    if (!gdc) return;
    if (!dotted) { line(x1, y, x2, y, pen); return; }
    if (x1 > x2) { long t = x1; x1 = x2; x2 = t; }
    if (x1 < -16000) x1 = -16000;
    if (x2 > 16000) x2 = 16000;
    for (x = x1; x <= x2; x += 2) SetPixelV(gdc, (int)x, cw(y), PEN(pen));
}

void vline_dotted(long x, long y1, long y2, int pen)
{
    long y;
    if (!gdc) return;
    if (y1 > y2) { long t = y1; y1 = y2; y2 = t; }
    if (y1 < -16000) y1 = -16000;
    if (y2 > 16000) y2 = 16000;
    for (y = y1; y <= y2; y += 2) SetPixelV(gdc, cw(x), (int)y, PEN(pen));
}

/* inverted feedback: drawing it twice takes it away */
void xor_rect(long x1, long y1, long x2, long y2)
{
    if (!gdc) return;
    if (x1 > x2) { long t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { long t = y1; y1 = y2; y2 = t; }
    PatBlt(gdc, cw(x1), cw(y1), cw(x2 - x1 + 1), cw(y2 - y1 + 1), DSTINVERT);
}

void xor_line(long x1, long y1, long x2, long y2)
{
    int om;
    if (!gdc) return;
    om = SetROP2(gdc, R2_NOT);
    MoveToEx(gdc, cw(x1), cw(y1), NULL);
    LineTo(gdc, cw(x2), cw(y2));
    SetROP2(gdc, om);
}

void xor_frame(long x1, long y1, long x2, long y2)
{
    int om;
    HPEN p, op;
    if (!gdc) return;
    p = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
    om = SetROP2(gdc, R2_NOT);
    op = (HPEN)SelectObject(gdc, p);
    MoveToEx(gdc, cw(x1), cw(y1), NULL);
    LineTo(gdc, cw(x2), cw(y1));
    LineTo(gdc, cw(x2), cw(y2));
    LineTo(gdc, cw(x1), cw(y2));
    LineTo(gdc, cw(x1), cw(y1 + 1));
    SelectObject(gdc, op);
    SetROP2(gdc, om);
    DeleteObject(p);
}

void thick_line(long x1, long y1, long x2, long y2, int pen, int w)
{
    HPEN p, op;
    if (!gdc) return;
    if (w <= 1) { line(x1, y1, x2, y2, pen); return; }
    p = CreatePen(PS_SOLID, w, PEN(pen));
    op = (HPEN)SelectObject(gdc, p);
    MoveToEx(gdc, cw(x1), cw(y1), NULL);
    LineTo(gdc, cw(x2), cw(y2));
    SelectObject(gdc, op);
    DeleteObject(p);
}

void poly_line(const long *xy, int n, int pen, int w)
{
    int i;
    for (i = 0; i + 1 < n; i++) thick_line(xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], pen, w);
}

/* a filled polygon (the last point joins the first) */
void poly_fill(const long *xy, int n, int pen, int grey)
{
    POINT pt[128];
    int i;
    if (!gdc || n < 3) return;
    if (n > 128) n = 128;
    for (i = 0; i < n; i++) { pt[i].x = cw(xy[2 * i]); pt[i].y = cw(xy[2 * i + 1]); }
    if (!grey) {
        HBRUSH ob = (HBRUSH)SelectObject(gdc, brush(pen));
        HPEN op = (HPEN)SelectObject(gdc, GetStockObject(NULL_PEN));
        Polygon(gdc, pt, n);
        SelectObject(gdc, ob);
        SelectObject(gdc, op);
    } else {
        /* the stipple, clipped to the polygon */
        HRGN rg, old = CreateRectRgn(0, 0, 0, 0);
        RECT b;
        int had = GetClipRgn(gdc, old), k;
        POINT dp[128];
        for (k = 0; k < n; k++) { dp[k].x = pt[k].x - gox; dp[k].y = pt[k].y - goy; }
        rg = CreatePolygonRgn(dp, n, ALTERNATE);
        ExtSelectClipRgn(gdc, rg, RGN_AND);
        GetRgnBox(rg, &b);
        OffsetRect(&b, gox, goy);
        pattern_rect(&b, pen);
        SelectClipRgn(gdc, had == 1 ? old : NULL);
        DeleteObject(rg);
        DeleteObject(old);
    }
}

/* a filled circle of radius r (in x pixels), round on the screen */
void disc(long cx, long cy, long r, int pen)
{
    long ry = r * dev_pixw / dev_pixh;
    HBRUSH ob;
    HPEN op;
    if (!gdc) return;
    if (ry < 1) ry = 1;
    ob = (HBRUSH)SelectObject(gdc, brush(pen));
    op = (HPEN)SelectObject(gdc, pen_of(pen));
    Ellipse(gdc, cw(cx - r), cw(cy - ry), cw(cx + r + 1), cw(cy + ry + 1));
    SelectObject(gdc, ob);
    SelectObject(gdc, op);
}

/* ---- text in the interface font (Windows-1252) ------------------------ */

void sys_text_n(long x, long top, const char *s, long n, int pen)
{
    HFONT of;
    if (!gdc || n <= 0) return;
    of = (HFONT)SelectObject(gdc, uifont);
    SetTextColor(gdc, PEN(pen));
    SetBkMode(gdc, TRANSPARENT);
    SetTextAlign(gdc, TA_TOP | TA_LEFT);
    TextOutA(gdc, cw(x), cw(top), s, (int)n);
    SelectObject(gdc, of);
}

void sys_text(long x, long top, const char *s, int pen) { sys_text_n(x, top, s, (long)strlen(s), pen); }

void fix_text(long x, long top, const char *s, long n, int pen)
{
    HFONT of;
    if (!gdc || n <= 0) return;
    of = (HFONT)SelectObject(gdc, fixfont);
    SetTextColor(gdc, PEN(pen));
    SetBkMode(gdc, TRANSPARENT);
    SetTextAlign(gdc, TA_TOP | TA_LEFT);
    TextOutA(gdc, cw(x), cw(top), s, (int)n);
    SelectObject(gdc, of);
}

long sys_width(const char *s)
{
    HDC dc = GetDC(NULL);
    HFONT of = (HFONT)SelectObject(dc, uifont);
    SIZE sz;
    GetTextExtentPoint32A(dc, s, (int)strlen(s), &sz);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    return sz.cx;
}

/* a raised (or sunk) frame, in the system's colours */
void bevel(const GRECT *r, int recessed)
{
    long x1 = r->g_x, y1 = r->g_y, x2 = r->g_x + r->g_w - 1, y2 = r->g_y + r->g_h - 1;
    int hi = recessed ? P_SHADOW : P_WHITE, lo = recessed ? P_WHITE : P_SHADOW;
    line(x1, y2, x1, y1, hi);
    line(x1, y1, x2 - 1, y1, hi);
    line(x1 + 1, y2, x2, y2, lo);
    line(x2, y2, x2, y1, lo);
}

void button_box(const GRECT *r, const char *label, int sel, int disabled)
{
    long tw = sys_width(label);
    fill_rect(r->g_x + 1, r->g_y + 1, r->g_x + r->g_w - 2, r->g_y + r->g_h - 2, sel ? P_BLUE : P_LIGHT);
    bevel(r, sel);
    sys_text(r->g_x + (r->g_w - tw) / 2, r->g_y + (r->g_h - hchar) / 2, label, sel ? P_WHITE : P_BLACK);
    if (disabled) fill_pat(r->g_x + 2, r->g_y + 1, r->g_x + r->g_w - 3, r->g_y + r->g_h - 2, P_LIGHT, 1);
}

/* ---- the busy pointer --------------------------------------------------- */

static int busy_n;

void busy(int on)
{
    POINT p;
    if (on) busy_n++;
    else if (busy_n > 0) busy_n--;
    SetCursor(LoadCursor(NULL, busy_n ? IDC_WAIT : IDC_ARROW));
    GetCursorPos(&p);
    SetCursorPos(p.x, p.y);                         /* so the pointer shows it now */
}

int busy_now(void) { return busy_n > 0; }

/* ---- the toolbar icons (16 x 16) -------------------------------------------- */

static const UWORD icon16[IC_COUNT][16] = {
    /* BOLD    */ { 0x0000, 0x0000, 0x3FF0, 0x3878, 0x3838, 0x3838, 0x3878, 0x3FF0, 0x387C, 0x383C, 0x383C, 0x383C, 0x3878, 0x3FF8, 0x0000, 0x0000 },
    /* ITALIC  */ { 0x0000, 0x0000, 0x01FC, 0x0070, 0x0060, 0x00E0, 0x00C0, 0x01C0, 0x0180, 0x0380, 0x0300, 0x0700, 0x0600, 0x1FC0, 0x0000, 0x0000 },
    /* UNDER   */ { 0x0000, 0x3C3C, 0x1818, 0x1818, 0x1818, 0x1818, 0x1818, 0x1818, 0x1818, 0x0C30, 0x07E0, 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x0000 },
    /* LEFT    */ { 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x7FC0, 0x7FC0, 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x7FC0, 0x7FC0 },
    /* CENTRE  */ { 0x0000, 0x0000, 0x3FFC, 0x3FFC, 0x0000, 0x0000, 0x0FF0, 0x0FF0, 0x0000, 0x0000, 0x3FFC, 0x3FFC, 0x0000, 0x0000, 0x0FF0, 0x0FF0 },
    /* RIGHT   */ { 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x03FE, 0x03FE, 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x03FE, 0x03FE },
    /* JUSTIFY */ { 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x0000, 0x0000, 0x7FFE, 0x7FFE },
    /* FONT    */ { 0x0000, 0x0180, 0x03C0, 0x03C0, 0x0660, 0x0660, 0x0C30, 0x0C30, 0x1FF8, 0x1FF8, 0x300C, 0x300C, 0x700E, 0x700E, 0x0000, 0x0000 },
    /* SIZE    */ { 0x0000, 0xFFC0, 0xFFC0, 0x0C00, 0x0C00, 0x0C00, 0x0C7E, 0x0C7E, 0x0C18, 0x0C18, 0x0C18, 0x0C18, 0x0C18, 0x0C18, 0x0000, 0x0000 },
    /* COLOUR  */ { 0x0000, 0x0100, 0x0380, 0x0380, 0x07C0, 0x07C0, 0x0FE0, 0x0FE0, 0x1FF0, 0x1FB0, 0x1F70, 0x1EF0, 0x0FE0, 0x07C0, 0x0000, 0x0000 },
    /* SPELL   */ { 0x0000, 0x3000, 0x4800, 0x8400, 0xFC00, 0x840C, 0x8418, 0x0030, 0x3060, 0x18C0, 0x0D80, 0x0700, 0x0200, 0x0000, 0x0000, 0x0000 },
    /* CODES   */ { 0xFFFF, 0x8001, 0xB3E1, 0x8001, 0xBFB9, 0x8001, 0xFFFF, 0xFFFF, 0x8001, 0xB00D, 0xA005, 0xA005, 0xB00D, 0x8001, 0xFFFF, 0x0000 },
    /* BULLETS */ { 0x0000, 0x0000, 0x67FE, 0x6000, 0x0000, 0x0000, 0x67FE, 0x6000, 0x0000, 0x0000, 0x67FE, 0x6000, 0x0000, 0x0000, 0x0000, 0x0000 },
    /* NUMBERS */ { 0x4000, 0xC000, 0x43FE, 0x4000, 0xE000, 0xE000, 0x2000, 0xE3FE, 0x8000, 0xE000, 0xE000, 0x2000, 0xE3FE, 0x2000, 0xE000, 0x0000 },
    /* UNDO    */ { 0x0000, 0x0000, 0x1000, 0x3000, 0x7FC0, 0xFFE0, 0x7FF0, 0x3038, 0x1018, 0x0018, 0x0018, 0x0030, 0x0060, 0x0000, 0x0000, 0x0000 },
    /* REDO    */ { 0x0000, 0x0000, 0x0008, 0x000C, 0x03FE, 0x07FF, 0x0FFE, 0x1C0C, 0x1808, 0x1800, 0x1800, 0x0C00, 0x0600, 0x0000, 0x0000, 0x0000 },
    /* ZOOM    */ { 0x0000, 0x0F00, 0x30C0, 0x4020, 0x4020, 0x8010, 0x8010, 0x8010, 0x4020, 0x4020, 0x30E0, 0x0F70, 0x0038, 0x001C, 0x000E, 0x0004 },
    /* TABLE   */ { 0x0000, 0xFFFF, 0x8421, 0x8421, 0x8421, 0xFFFF, 0x8421, 0x8421, 0x8421, 0xFFFF, 0x8421, 0x8421, 0x8421, 0xFFFF, 0x0000, 0x0000 },
    /* RULER   */ { 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0x9249, 0x9249, 0x8209, 0x8001, 0x8001, 0x8001, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000 },
    /* GUIDES  */ { 0x0000, 0x3FFC, 0x2004, 0x2AA4, 0x2004, 0x2824, 0x2004, 0x2824, 0x2004, 0x2824, 0x2004, 0x2AA4, 0x2004, 0x3FFC, 0x0000, 0x0000 },
    /* PGBRK   */ { 0x3FF8, 0x2008, 0x2BE8, 0x2008, 0x2BE8, 0x2008, 0x0000, 0xE739, 0xE739, 0x0000, 0x3FF8, 0x2008, 0x2BE8, 0x2008, 0x2BE8, 0x2008 },
};

int icon_h(void) { return 16; }

void draw_tbicon(int ic, long x, long y, int pen)
{
    unsigned char bits[32];
    int r;
    if (ic < 0 || ic >= IC_COUNT) return;
    for (r = 0; r < 16; r++) { bits[2 * r] = (unsigned char)(icon16[ic][r] >> 8); bits[2 * r + 1] = (unsigned char)icon16[ic][r]; }
    blit_bits(bits, 2, 16, 16, x, y, pen);
}

/* a 1-bit picture (rows of rb bytes, 1 = ink) drawn in pen, 0 bits
 * transparent: ink is first made black, then given the pen's colour */
void blit_bits(const unsigned char *bits, long rb, long w, long h, long x, long y, int pen)
{
    struct { BITMAPINFOHEADER h; RGBQUAD c[2]; } bi;
    long drb = (w + 31) / 32 * 4, r, band = 512;
    unsigned char *buf;
    COLORREF c = PEN(pen);
    if (!gdc || w <= 0 || h <= 0 || w > 16000) return;
    if (band > h) band = h;
    buf = (unsigned char *)malloc((size_t)(drb * band));
    if (!buf) return;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = w;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 1;
    bi.h.biCompression = BI_RGB;
    for (r = 0; r < h; r += band) {
        long n = h - r < band ? h - r : band, k;
        if (y + r + n < -16000 || y + r > 16000) continue;
        /* bottom-up rows, as every Windows takes them */
        for (k = 0; k < n; k++) {
            memcpy(buf + (n - 1 - k) * drb, bits + (r + k) * rb, (size_t)(rb < drb ? rb : drb));
            if (drb > rb) memset(buf + (n - 1 - k) * drb + rb, 0, (size_t)(drb - rb));
        }
        bi.h.biHeight = n;
        bi.c[0].rgbRed = bi.c[0].rgbGreen = bi.c[0].rgbBlue = 255;
        bi.c[1].rgbRed = bi.c[1].rgbGreen = bi.c[1].rgbBlue = 0;
        StretchDIBits(gdc, cw(x), cw(y + r), (int)w, (int)n, 0, 0, (int)w, (int)n, buf, (BITMAPINFO *)&bi,
                      DIB_RGB_COLORS, SRCAND);
        if (c != RGB(0, 0, 0)) {
            bi.c[0].rgbRed = bi.c[0].rgbGreen = bi.c[0].rgbBlue = 0;
            bi.c[1].rgbRed = GetRValue(c); bi.c[1].rgbGreen = GetGValue(c); bi.c[1].rgbBlue = GetBValue(c);
            StretchDIBits(gdc, cw(x), cw(y + r), (int)w, (int)n, 0, 0, (int)w, (int)n, buf, (BITMAPINFO *)&bi,
                          DIB_RGB_COLORS, SRCPAINT);
        }
    }
    free(buf);
}

/* a colour picture (rows of RGB bytes) scaled to w x h at (x, y) */
void blit_rgb(const unsigned char *rgb, long iw, long ih, long x, long y, long w, long h)
{
    BITMAPINFOHEADER bi;
    long drb = (iw * 3 + 3) & ~3L, r, i;
    unsigned char *buf;
    if (!gdc || iw <= 0 || ih <= 0) return;
    buf = (unsigned char *)malloc((size_t)(drb * ih));
    if (!buf) return;
    for (r = 0; r < ih; r++) {
        unsigned char *o = buf + (ih - 1 - r) * drb;
        const unsigned char *s = rgb + r * iw * 3;
        for (i = 0; i < iw; i++) { o[3 * i] = s[3 * i + 2]; o[3 * i + 1] = s[3 * i + 1]; o[3 * i + 2] = s[3 * i]; }
    }
    memset(&bi, 0, sizeof bi);
    bi.biSize = sizeof bi;
    bi.biWidth = iw;
    bi.biHeight = ih;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    SetStretchBltMode(gdc, COLORONCOLOR);
    StretchDIBits(gdc, cw(x), cw(y), (int)w, (int)h, 0, 0, (int)iw, (int)ih, buf, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    free(buf);
}

/* ---- set-up ----------------------------------------------------------------- */

void gfx_init(void)
{
    static const WORD grey[8] = { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA };
    int i;
    for (i = 0; i < 16; i++) {
        brushes[i] = CreateSolidBrush(pal[i]);
        pens[i] = CreatePen(PS_SOLID, 1, pal[i]);
    }
    /* the pattern's set bits draw: they are the 0 (black) ones of the brush */
    {
        WORD inv[8];
        for (i = 0; i < 8; i++) inv[i] = (WORD)(~grey[i] & 0xFF);
        greybm = CreateBitmap(8, 8, 1, 1, inv);
    }
    greybrush = CreatePatternBrush(greybm);
}

void gfx_free(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (brushes[i]) DeleteObject(brushes[i]);
        if (pens[i]) DeleteObject(pens[i]);
        brushes[i] = NULL; pens[i] = NULL;
    }
    if (greybrush) DeleteObject(greybrush);
    if (greybm) DeleteObject(greybm);
    greybrush = NULL; greybm = NULL;
}

/* ---- the mouse while a button is held ----------------------------------- */

static WIN tw;
static short tx, ty;
static int   tq, released, tracking;

int key_qual(void)
{
    return ((GetKeyState(VK_SHIFT) & 0x8000) ? KQ_SHIFT : 0) | ((GetKeyState(VK_MENU) & 0x8000) ? KQ_ALT : 0) |
           ((GetKeyState(VK_CONTROL) & 0x8000) ? KQ_CTRL : 0);
}

void track_begin(WIN w)
{
    POINT p;
    tw = w;
    released = 0;
    tracking = 1;
    GetCursorPos(&p);
    ScreenToClient(w, &p);
    tx = (short)p.x; ty = (short)p.y;
    tq = key_qual();
    SetCapture(w);
}

void track_end(void)
{
    if (!tracking) return;
    tracking = 0;
    if (GetCapture() == tw) ReleaseCapture();
    tw = NULL;
}

/* wait for the mouse to move or a tenth of a second; 0 once the button
 * is up. Other windows' messages (painting) are dealt with on the way. */
int track_next(short *mx, short *my, int *qual)
{
    MSG msg;
    if (released || !tracking) { *mx = tx; *my = ty; return 0; }
    for (;;) {
        int got = 0;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.hwnd == tw && (msg.message == WM_MOUSEMOVE || msg.message == WM_LBUTTONUP)) {
                tx = (short)LOWORD(msg.lParam); ty = (short)HIWORD(msg.lParam);
                tq = ((msg.wParam & MK_SHIFT) ? KQ_SHIFT : 0) | ((msg.wParam & MK_CONTROL) ? KQ_CTRL : 0);
                if (msg.message == WM_LBUTTONUP) released = 1;
                else got = 1;
                continue;
            }
            if (msg.message == WM_CAPTURECHANGED && msg.hwnd == tw) { released = 1; continue; }
            if ((msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) ||
                (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST)) continue;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        *mx = tx; *my = ty;
        if (qual) *qual = tq;
        if (released || GetCapture() != tw) { released = 1; return 0; }
        if (got) return 1;
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT) == WAIT_TIMEOUT) return 1;
    }
}

/* held for half a second without moving? (spelling suggestions) */
int long_press(WIN w, short mx, short my)
{
    short x, y;
    int q;
    DWORD t0 = GetTickCount();
    (void)w;
    while (track_next(&x, &y, &q)) {
        if (x < mx - 3 || x > mx + 3 || y < my - 3 || y > my + 3) return 0;
        if (GetTickCount() - t0 >= 500) return 1;
    }
    return 0;
}

/* secs: the message time in milliseconds */
static ULONG lclick;
static int   lvalid;
int double_click(ULONG secs, ULONG micros)
{
    int r = lvalid && secs - lclick <= GetDoubleClickTime();
    (void)micros;
    lclick = secs;
    lvalid = !r;
    return r;
}

/* ---- character sets ----------------------------------------------------------- */

unsigned char lat_ch(unsigned char c) { return st_to_win(c); }
unsigned char st_ch(unsigned char c) { return win_to_st(c); }

void lat_n(char *out, const char *st, long n)
{
    long i;
    for (i = 0; i < n; i++) out[i] = (char)lat_ch((unsigned char)st[i]);
    out[n] = 0;
}

const char *lat(const char *st)
{
    static char ring[8][256];
    static int k;
    char *o = ring[k++ % 8];
    long n = (long)strlen(st);
    if (n > 255) n = 255;
    lat_n(o, st, n);
    return o;
}

void stc(char *out, const char *w, int max)
{
    int i;
    for (i = 0; w[i] && i < max - 1; i++) out[i] = (char)st_ch((unsigned char)w[i]);
    out[i] = 0;
}
