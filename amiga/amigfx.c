/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amigfx.c - drawing helpers on graphics.library (clipping, boxes, lines,
 *            filled shapes, text in the screen font, XOR feedback), the
 *            toolbar icons, the busy pointer, tracking the mouse while a
 *            button is held, and the character set conversions
 */
#include <exec/memory.h>
#include <graphics/gfxmacros.h>
#include <graphics/regions.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <intuition/intuitionbase.h>
#include <stdio.h>
#include <string.h>
#include "amiui.h"

extern struct IntuitionBase *IntuitionBase;
struct RastPort *grp;
struct Window   *gwin;

static struct Region *creg;
static int clipped;
static struct RastPort mrp;                 /* for measuring text */

/* chip memory: area fill buffers, patterns, icons, the busy pointer */
static UBYTE *tmpbuf;
static ULONG  tmpsize;
static struct TmpRas tmpras;
static UWORD *areabuf;
#define AREAVERTS 80
static struct AreaInfo areainfo;
static UWORD *greypat;                      /* 2 rows: 50% grey */
static UWORD *busyptr;
static UWORD *icons16, *icons8;

struct RastPort *rp_of(struct Window *w) { return w->RPort; }

void gfx_begin(struct Window *w)
{
    DOC *d = doc_of_win(w);
    if (d) theme_for(d->slot);               /* its own colour theme (15 bits or more) */
    gwin = w;
    grp = w->RPort;
    SetDrMd(grp, JAM1);
    SetWrMsk(grp, 0xFF);
    SetDrPt(grp, 0xFFFF);
}

void gfx_end(void)
{
    clip_off();
    theme_global();
    if (grp) { SetDrMd(grp, JAM1); SetWrMsk(grp, 0xFF); SetDrPt(grp, 0xFFFF); SetAfPt(grp, NULL, 0); }
    grp = NULL;
    gwin = NULL;
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
    struct Rectangle rc;
    if (!gwin) return;
    if (!creg) creg = NewRegion();
    if (!creg) return;
    ClearRegion(creg);
    if (r->g_w > 0 && r->g_h > 0) {
        rc.MinX = r->g_x; rc.MinY = r->g_y;
        rc.MaxX = (short)(r->g_x + r->g_w - 1); rc.MaxY = (short)(r->g_y + r->g_h - 1);
        OrRectRegion(creg, &rc);
    }
    InstallClipRegion(gwin->WLayer, creg);
    clipped = 1;
}

void clip_off(void)
{
    if (clipped && gwin) InstallClipRegion(gwin->WLayer, NULL);
    clipped = 0;
}

/* keep coordinates inside the window, so graphics.library never sees
 * rectangles it can't handle */
static int clamp(long *x1, long *y1, long *x2, long *y2)
{
    long mw = gwin ? gwin->Width - 1 : 1023, mh = gwin ? gwin->Height - 1 : 1023;
    if (*x1 > *x2) { long t = *x1; *x1 = *x2; *x2 = t; }
    if (*y1 > *y2) { long t = *y1; *y1 = *y2; *y2 = t; }
    if (*x2 < 0 || *y2 < 0 || *x1 > mw || *y1 > mh) return 0;
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x2 > mw) *x2 = mw;
    if (*y2 > mh) *y2 = mh;
    return 1;
}

void fill_rect(long x1, long y1, long x2, long y2, int pen)
{
    if (!grp || !clamp(&x1, &y1, &x2, &y2)) return;
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
    RectFill(grp, x1, y1, x2, y2);
}

void fill_pat(long x1, long y1, long x2, long y2, int pen, int grey)
{
    if (!grp || !clamp(&x1, &y1, &x2, &y2)) return;
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
    if (grey && greypat) SetAfPt(grp, greypat, 1);
    RectFill(grp, x1, y1, x2, y2);
    SetAfPt(grp, NULL, 0);
}

void fill_g(const GRECT *r, int pen)
{
    if (r->g_w > 0 && r->g_h > 0) fill_rect(r->g_x, r->g_y, r->g_x + r->g_w - 1, r->g_y + r->g_h - 1, pen);
}

/* lines: graphics.library is happy with coordinates outside the window,
 * as long as they stay in 16 bits */
static short cw(long v) { return (short)(v < -16000 ? -16000 : v > 16000 ? 16000 : v); }

void line(long x1, long y1, long x2, long y2, int pen)
{
    if (!grp) return;
    SetAPen(grp, PEN(pen));
    Move(grp, cw(x1), cw(y1));
    Draw(grp, cw(x2), cw(y2));
}

void frame(long x1, long y1, long x2, long y2, int pen)
{
    if (!grp) return;
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
    Move(grp, cw(x1), cw(y1));
    Draw(grp, cw(x2), cw(y1));
    Draw(grp, cw(x2), cw(y2));
    Draw(grp, cw(x1), cw(y2));
    Draw(grp, cw(x1), cw(y1));
}

void hline(long x1, long x2, long y, int pen, int dotted)
{
    if (!grp) return;
    if (dotted) SetDrPt(grp, 0xAAAA);
    line(x1, y, x2, y, pen);
    SetDrPt(grp, 0xFFFF);
}

/* XOR feedback: only the two lowest bitplanes change, so black and white
 * swap and the text colours stay recognisable */
void xor_rect(long x1, long y1, long x2, long y2)
{
    if (!grp || !clamp(&x1, &y1, &x2, &y2)) return;
    SetDrMd(grp, COMPLEMENT);
    SetWrMsk(grp, 0x03);
    RectFill(grp, x1, y1, x2, y2);
    SetWrMsk(grp, 0xFF);
    SetDrMd(grp, JAM1);
}

void xor_line(long x1, long y1, long x2, long y2)
{
    if (!grp) return;
    SetDrMd(grp, COMPLEMENT);
    SetWrMsk(grp, 0x03);
    Move(grp, cw(x1), cw(y1));
    Draw(grp, cw(x2), cw(y2));
    SetWrMsk(grp, 0xFF);
    SetDrMd(grp, JAM1);
}

void xor_frame(long x1, long y1, long x2, long y2)
{
    if (!grp) return;
    SetDrMd(grp, COMPLEMENT);
    SetWrMsk(grp, 0x03);
    SetDrPt(grp, 0xCCCC);
    Move(grp, cw(x1), cw(y1));
    Draw(grp, cw(x2), cw(y1));
    Draw(grp, cw(x2), cw(y2));
    Draw(grp, cw(x1), cw(y2));
    Draw(grp, cw(x1), cw(y1 + 1));
    SetDrPt(grp, 0xFFFF);
    SetWrMsk(grp, 0xFF);
    SetDrMd(grp, JAM1);
}

/* a line w pixels wide: parallel lines across its direction */
void thick_line(long x1, long y1, long x2, long y2, int pen, int w)
{
    long dx = x2 - x1, dy = y2 - y1, k;
    int vert = (dx < 0 ? -dx : dx) < (dy < 0 ? -dy : dy);
    if (w <= 1) { line(x1, y1, x2, y2, pen); return; }
    for (k = -(w / 2); k < w - w / 2; k++) {
        if (vert) line(x1 + k, y1, x2 + k, y2, pen);
        else line(x1, y1 + k, x2, y2 + k, pen);
    }
}

void poly_line(const long *xy, int n, int pen, int w)
{
    int i;
    for (i = 0; i + 1 < n; i++) thick_line(xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], pen, w);
}

/* a filled polygon (the last point joins the first) with the area fill */
void poly_fill(const long *xy, int n, int pen, int grey)
{
    struct TmpRas *ot;
    struct AreaInfo *oa;
    int i;
    if (!grp || !tmpbuf || n < 3) return;
    if (n > AREAVERTS - 2) n = AREAVERTS - 2;
    memset(areabuf, 0, AREAVERTS * 5);
    InitArea(&areainfo, areabuf, AREAVERTS);
    ot = grp->TmpRas; oa = grp->AreaInfo;
    grp->TmpRas = &tmpras;
    grp->AreaInfo = &areainfo;
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
    SetOPen(grp, PEN(pen));
    grp->Flags &= ~AREAOUTLINE;
    if (grey && greypat) SetAfPt(grp, greypat, 1);
    AreaMove(grp, cw(xy[0]), cw(xy[1]));
    for (i = 1; i < n; i++) AreaDraw(grp, cw(xy[2 * i]), cw(xy[2 * i + 1]));
    AreaEnd(grp);
    SetAfPt(grp, NULL, 0);
    grp->TmpRas = ot;
    grp->AreaInfo = oa;
}

/* a filled circle of radius r (in x pixels), round on the screen */
void disc(long cx, long cy, long r, int pen)
{
    long ry = r * dev_pixw / dev_pixh, dy;
    if (ry < 1) ry = 1;
    for (dy = -ry; dy <= ry; dy++) {
        long t = (ry * ry - dy * dy) * r * r / (ry * ry), dx = 0;
        while ((dx + 1) * (dx + 1) <= t) dx++;
        fill_rect(cx - dx, cy + dy, cx + dx, cy + dy, pen);
    }
}

/* ---- text in the screen font (Latin-1) ------------------------------ */

void sys_text_n(long x, long top, const char *s, long n, int pen)
{
    if (!grp || n <= 0) return;
    SetFont(grp, sysfont);
    SetSoftStyle(grp, 0, 0xFF);
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
    Move(grp, cw(x), cw(top + sysfont->tf_Baseline));
    Text(grp, (STRPTR)s, (ULONG)n);
}

void sys_text(long x, long top, const char *s, int pen) { sys_text_n(x, top, s, (long)strlen(s), pen); }

void fix_text(long x, long top, const char *s, long n, int pen)
{
    if (!grp || n <= 0) return;
    SetFont(grp, fixfont);
    SetSoftStyle(grp, 0, 0xFF);
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
    Move(grp, cw(x), cw(top + fixfont->tf_Baseline));
    Text(grp, (STRPTR)s, (ULONG)n);
}

long sys_width(const char *s)
{
    return TextLength(&mrp, (STRPTR)s, (ULONG)strlen(s));
}

/* the 3D frame of the 2.0 look */
void bevel(const GRECT *r, int recessed)
{
    long x1 = r->g_x, y1 = r->g_y, x2 = r->g_x + r->g_w - 1, y2 = r->g_y + r->g_h - 1;
    int hi = recessed ? P_TDARK : P_TLIGHT, lo = recessed ? P_TLIGHT : P_TDARK;
    line(x1, y2, x1, y1, hi);
    line(x1, y1, x2 - 1, y1, hi);
    line(x1 + 1, y2, x2, y2, lo);
    line(x2, y2, x2, y1, lo);
}

void button_box(const GRECT *r, const char *label, int sel, int disabled)
{
    long tw = sys_width(label);
    fill_rect(r->g_x + 1, r->g_y + 1, r->g_x + r->g_w - 2, r->g_y + r->g_h - 2, sel ? P_TFILL : P_TFACE);
    bevel(r, sel);
    sys_text(r->g_x + (r->g_w - tw) / 2, r->g_y + (r->g_h - hchar) / 2, label, sel ? P_TFTEXT : P_TTEXT);
    if (disabled) fill_pat(r->g_x + 2, r->g_y + 1, r->g_x + r->g_w - 3, r->g_y + r->g_h - 2, P_TFACE, 1);
}

/* ---- the busy pointer --------------------------------------------------- */

static const UWORD busy_data[2 + 16 * 2 + 2] = {
    0, 0,
    0x0000, 0x3FFC,  0x1FF8, 0x2004,  0x1FF8, 0x2004,  0x0FF0, 0x1008,
    0x07E0, 0x0810,  0x03C0, 0x0420,  0x0180, 0x0240,  0x0000, 0x0180,
    0x0000, 0x0180,  0x0180, 0x0240,  0x0180, 0x0420,  0x03C0, 0x0810,
    0x07E0, 0x1008,  0x0FF0, 0x2004,  0x1FF8, 0x2004,  0x0000, 0x3FFC,
    0, 0
};
static int busy_n;

/* Workbench 3 and newer: the busy pointer chosen in the Pointer
 * preferences, at the size and resolution set there (a high resolution
 * pointer stays one on EdWord's screen); before that, EdWord's own */
static void pointer_all(int on)
{
    struct Window *w;
    int v39 = IntuitionBase->LibNode.lib_Version >= 39;
    if (!scr) return;
    for (w = scr->FirstWindow; w; w = w->NextWindow) {
        if (w->UserPort != idcmp_port) continue;
#ifndef AMI13
        if (v39) { SetWindowPointer(w, WA_BusyPointer, (ULONG)on, WA_PointerDelay, (ULONG)on, TAG_END); continue; }
#endif
        if (on && busyptr) SetPointer(w, busyptr, 16, 16, -7, -7);
        else ClearPointer(w);
    }
    (void)v39;
}

void busy(int on)
{
    if (on) { if (busy_n++ == 0) pointer_all(1); }
    else if (busy_n > 0 && --busy_n == 0) pointer_all(0);
}

/* ---- the toolbar icons (16x16; 16x8 on screens with tall pixels) ------- */

/* the icons and their colour layers are the Atari version's (tools/icons.h) */
#include "../tools/icons.h"
#define icon16_src icon16
#define NLAYER ((int)(sizeof layer16 / sizeof layer16[0]))
static UWORD *layers16, *layers8, *rest16, *rest8;   /* chip: layers; each icon less its layers */

/* the VDI pens of the layers as EdWord's */
static int layer_pen(int vdi)
{
    static const unsigned char map[16] = { P_WHITE, P_BLACK, P_RED, P_GREEN, P_DBLUE, P_CYAN, P_YELLOW, P_MAGENTA,
                                           P_LIGHT, P_TGREY, P_RED, P_GREEN, P_DBLUE, P_CYAN, P_AMBER, P_MAGENTA };
    return map[vdi & 15];
}

void icons_init(void)
{
    int i, r, k;
    icons16 = (UWORD *)AllocMem(sizeof icon16_src, MEMF_CHIP);
    icons8 = (UWORD *)AllocMem(sizeof icon16_src / 2, MEMF_CHIP);
    layers16 = (UWORD *)AllocMem(NLAYER * 32, MEMF_CHIP);
    layers8 = (UWORD *)AllocMem(NLAYER * 16, MEMF_CHIP);
    rest16 = (UWORD *)AllocMem(sizeof icon16_src, MEMF_CHIP);
    rest8 = (UWORD *)AllocMem(sizeof icon16_src / 2, MEMF_CHIP);
    if (!icons16 || !icons8) return;
    memcpy(icons16, icon16_src, sizeof icon16_src);
    for (i = 0; i < IC_COUNT; i++)
        for (r = 0; r < 8; r++) icons8[i * 8 + r] = (UWORD)(icon16_src[i][2 * r] | icon16_src[i][2 * r + 1]);
    if (!layers16 || !layers8 || !rest16 || !rest8) return;
    memcpy(rest16, icon16_src, sizeof icon16_src);
    for (k = 0; k < NLAYER; k++)
        for (r = 0; r < 16; r++) {
            layers16[k * 16 + r] = layer16[k].m[r];
            rest16[layer16[k].ic * 16 + r] &= (UWORD)~layer16[k].m[r];
        }
    for (k = 0; k < NLAYER; k++)
        for (r = 0; r < 8; r++) layers8[k * 8 + r] = (UWORD)(layer16[k].m[2 * r] | layer16[k].m[2 * r + 1]);
    for (i = 0; i < IC_COUNT; i++)
        for (r = 0; r < 8; r++) rest8[i * 8 + r] = (UWORD)(rest16[i * 16 + 2 * r] | rest16[i * 16 + 2 * r + 1]);
}

void icons_free(void)
{
    if (icons16) FreeMem(icons16, sizeof icon16_src);
    if (icons8) FreeMem(icons8, sizeof icon16_src / 2);
    if (layers16) FreeMem(layers16, NLAYER * 32);
    if (layers8) FreeMem(layers8, NLAYER * 16);
    if (rest16) FreeMem(rest16, sizeof icon16_src);
    if (rest8) FreeMem(rest8, sizeof icon16_src / 2);
    icons16 = icons8 = layers16 = layers8 = rest16 = rest8 = NULL;
}

int icon_h(void) { return small_icons ? 8 : 16; }

/* a toolbar icon: in colour on screens of 16 colours or more (its colour
 * layers, then the rest of it in pen), else all in pen */
void draw_tbicon(int ic, long x, long y, int pen)
{
    UWORD *src = small_icons ? icons8 : icons16;
    int k, ih = icon_h(), colour = npens >= 16 && layers16 && rest16;
    if (!src || !grp) return;
    SetDrMd(grp, JAM1);
    if (colour) {
        int any = 0;
        for (k = 0; k < NLAYER; k++) {
            if (layer16[k].ic != ic) continue;
            any = 1;
            SetAPen(grp, PEN(layer_pen(layer16[k].pen)));
            BltTemplate((PLANEPTR)((small_icons ? layers8 : layers16) + k * ih), 0, 2, grp, cw(x), cw(y), 16, ih);
        }
        if (any) src = small_icons ? rest8 : rest16;
    }
    SetAPen(grp, PEN(pen));
    BltTemplate((PLANEPTR)(src + ic * ih), 0, 2, grp, cw(x), cw(y), 16, ih);
}

/* a 1-bit picture (rows of rb bytes, 1 = ink) drawn in pen, 0 bits
 * transparent; the bits are copied to chip memory a band at a time */
void blit_bits(const unsigned char *bits, long rb, long w, long h, long x, long y, int pen)
{
    long mod = (w + 15) / 16 * 2, band = 128, r;
    UBYTE *chip;
    if (!grp || w <= 0 || h <= 0 || w > 4000) return;
    if (band > h) band = h;
    chip = (UBYTE *)AllocMem((ULONG)(mod * band), MEMF_CHIP | MEMF_CLEAR);
    if (!chip) return;
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
    for (r = 0; r < h; r += band) {
        long n = h - r < band ? h - r : band, k;
        if (y + r + n < -16000 || y + r > 16000) continue;
        for (k = 0; k < n; k++) {
            memcpy(chip + k * mod, bits + (r + k) * rb, (size_t)rb);
            if (mod > rb) memset(chip + k * mod + rb, 0, (size_t)(mod - rb));
        }
        BltTemplate((PLANEPTR)chip, 0, mod, grp, cw(x), cw(y + r), w > 1008 ? 1008 : w, n);
    }
    FreeMem(chip, (ULONG)(mod * band));
}

/* ---- set-up ----------------------------------------------------------------- */

void gfx_init(void)
{
    InitRastPort(&mrp);
    SetFont(&mrp, sysfont);
    tmpsize = RASSIZE(scr_w, scr_h);
    tmpbuf = (UBYTE *)AllocMem(tmpsize, MEMF_CHIP | MEMF_CLEAR);
    if (tmpbuf) InitTmpRas(&tmpras, tmpbuf, tmpsize);
    areabuf = (UWORD *)AllocMem(AREAVERTS * 5 + 8, MEMF_CLEAR);
    greypat = (UWORD *)AllocMem(4, MEMF_CHIP);
    if (greypat) { greypat[0] = 0x5555; greypat[1] = 0xAAAA; }
    busyptr = (UWORD *)AllocMem(sizeof busy_data, MEMF_CHIP);
    if (busyptr) memcpy(busyptr, busy_data, sizeof busy_data);
    icons_init();
}

void gfx_free(void)
{
    if (creg) DisposeRegion(creg);
    creg = NULL;
    if (tmpbuf) FreeMem(tmpbuf, tmpsize);
    if (areabuf) FreeMem(areabuf, AREAVERTS * 5 + 8);
    if (greypat) FreeMem(greypat, 4);
    if (busyptr) FreeMem(busyptr, sizeof busy_data);
    tmpbuf = NULL; areabuf = NULL; greypat = NULL; busyptr = NULL;
    icons_free();
}

/* ---- the mouse while a button is held ----------------------------------- */

static struct Window *tw;
static short tx, ty;
static int   tq, released, tracking;
static ULONG t_idcmp;                   /* the window's own events, put back after */

void track_begin(struct Window *w)
{
    tw = w;
    released = 0;
    tracking = 1;
    tx = w->MouseX; ty = w->MouseY;
    tq = 0;
    t_idcmp = w->IDCMPFlags;
    ModifyIDCMP(w, w->IDCMPFlags | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS);
    ReportMouse(TRUE, w);
}

void track_end(void)
{
    if (!tracking) return;
    tracking = 0;
    if (tw) {
        ReportMouse(FALSE, tw);
        ModifyIDCMP(tw, t_idcmp);           /* (a document window keeps its ticks: the caret flashes) */
    }
    tw = NULL;
}

/* wait for the mouse to move or the next tick; 0 once the button is up.
 * Other windows' refresh requests are dealt with on the way. */
int track_next(short *mx, short *my, int *qual)
{
    struct IntuiMessage *m;
    if (released || !tracking) { *mx = tx; *my = ty; return 0; }
    for (;;) {
        int got = 0;
        while ((m = GT_GetIMsg(idcmp_port)) != NULL) {
            ULONG cl = m->Class;
            UWORD code = m->Code;
            struct Window *w = m->IDCMPWindow;
            short x = m->MouseX, y = m->MouseY;
            UWORD q = m->Qualifier;
            if (cl == IDCMP_REFRESHWINDOW) {
                GT_ReplyIMsg(m);
                GT_BeginRefresh(w);
                GT_EndRefresh(w, TRUE);
                continue;
            }
            GT_ReplyIMsg(m);
            if (w != tw) continue;
            if (cl == IDCMP_MOUSEMOVE || cl == IDCMP_INTUITICKS) { tx = x; ty = y; got = 1; }
            if (cl == IDCMP_MOUSEBUTTONS && (code == SELECTUP || code == (SELECTUP | IECODE_UP_PREFIX))) {
                tx = x; ty = y;
                released = 1;
            }
            tq = ((q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? KQ_SHIFT : 0) |
                 ((q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? KQ_ALT : 0) |
                 ((q & IEQUALIFIER_CONTROL) ? KQ_CTRL : 0);
        }
        *mx = tx; *my = ty;
        if (qual) *qual = tq;
        if (released) return 0;
        if (got) return 1;
        Wait(1UL << idcmp_port->mp_SigBit);
    }
}

/* held for half a second without moving? (spelling suggestions) */
int long_press(struct Window *w, short mx, short my)
{
    short x, y;
    int ticks = 0, q;
    (void)w;
    while (track_next(&x, &y, &q)) {
        if (x < mx - 3 || x > mx + 3 || y < my - 3 || y > my + 3) return 0;
        if (++ticks >= 6) return 1;
    }
    return 0;
}

static ULONG lsecs, lmics;
int double_click(ULONG secs, ULONG micros)
{
    int r = (lsecs || lmics) && DoubleClick(lsecs, lmics, secs, micros);
    lsecs = r ? 0 : secs;
    lmics = r ? 0 : micros;
    return r;
}

/* clicks in quick succession on about the same spot: 1, 2, 3, 4, then 1 again */
static ULONG csecs, cmics;
static short cx, cy, cn;
int click_count(ULONG secs, ULONG micros, short x, short y)
{
    if (cn && DoubleClick(csecs, cmics, secs, micros) && x >= cx - 4 && x <= cx + 4 && y >= cy - 4 && y <= cy + 4)
        cn = (short)(cn % 4 + 1);
    else cn = 1;
    csecs = secs; cmics = micros; cx = x; cy = y;
    return cn;
}

/* ---- character sets ----------------------------------------------------------- */

unsigned char lat_ch(unsigned char c)
{
    unsigned char w = st_to_win(c);
    if (w >= 0x80 && w < 0xA0) {
        switch (w) {
        case 0x83: return 'f';
        case 0x86: return '+';
        case 0x8C: return 'O';
        case 0x9C: return 'o';
        case 0x99: return 'T';
        }
        return '?';
    }
    return w;
}

unsigned char st_ch(unsigned char c) { return win_to_st(c); }

void lat_n(char *out, const char *st, long n)
{
    long i;
    for (i = 0; i < n; i++) out[i] = (char)lat_ch((unsigned char)st[i]);
    out[n] = 0;
}

const char *lat(const char *st)
{
    static char ring[6][256];
    static int k;
    char *o = ring[k++ % 6];
    long n = (long)strlen(st);
    if (n > 255) n = 255;
    lat_n(o, st, n);
    return o;
}

void stc(char *out, const char *latin, int max)
{
    int i;
    for (i = 0; latin[i] && i < max - 1; i++) out[i] = (char)st_ch((unsigned char)latin[i]);
    out[i] = 0;
}
