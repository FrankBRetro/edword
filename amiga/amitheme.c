/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amitheme.c - the screen's colours, colour themes and colour pictures
 *              (after gemtheme.c and the colour picture code of gemobj.c)
 *
 * EdWord's own screen: pens 0-15 are EdWord's sixteen colours (0 the
 * face of windows and gadgets). With 32 colours or more, pens 20-26 hold
 * the colour theme's (pens 16-19 are the mouse pointer's on AGA), and the
 * screen's DrawInfo pens point at them, so a theme colours the windows,
 * menus and gadgets as well as EdWord's own toolbars, rulers and the desk
 * around the pages. With 256 colours, pens 32-247 are a 6x6x6 colour
 * cube for pictures. A graphics card's screen of 15 to 24 bits has the
 * same pens; pictures are drawn there in their own colours.
 *
 * The Workbench screen: EdWord's colours are the nearest pens it has
 * (shared pens from ObtainBestPen on Workbench 3), and a theme colours
 * only EdWord's own furniture, with pens of its own.
 *
 * One theme is for all documents. On a screen with a palette, a new
 * theme shows at once; on a graphics card's screen everything is drawn
 * again.
 */
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiui.h"

extern struct GfxBase *GfxBase;
struct Library *CyberGfxBase;

/* cybergraphics.library (Picasso96 and CyberGraphX have it): drawing R G B
 * pixels on a graphics card's screen. The NDK has no header for it. */
#define RECTFMT_RGB 0
#include <inline/macros.h>
/* Picasso96's own API (picasso96api.library), used first; no NDK header */
struct Library *P96Base;
struct p96RenderInfo { APTR Memory; WORD BytesPerRow; WORD pad; ULONG RGBFormat; };
#define RGBFB_R8G8B8    2
#define P96BMA_DEPTH    2
#define P96BMA_ISP96    8
#define p96GetBitMapAttr(bm, a) \
    LP2(0x2a, ULONG, p96GetBitMapAttr, struct BitMap *, bm, a0, ULONG, a, d0, , P96Base)
#define p96WritePixelArray(ri, sx, sy, rp, dx, dy, w, h) \
    LP8NR(0x66, p96WritePixelArray, struct p96RenderInfo *, ri, a0, ULONG, sx, d0, ULONG, sy, d1, \
          struct RastPort *, rp, a1, ULONG, dx, d2, ULONG, dy, d3, ULONG, w, d4, ULONG, h, d5, , P96Base)
#define p96ReadPixelArray(ri, dx, dy, rp, sx, sy, w, h) \
    LP8NR(0x6c, p96ReadPixelArray, struct p96RenderInfo *, ri, a0, ULONG, dx, d0, ULONG, dy, d1, \
          struct RastPort *, rp, a1, ULONG, sx, d2, ULONG, sy, d3, ULONG, w, d4, ULONG, h, d5, , P96Base)
#define cgx_read(dst, dx, dy, dmod, rp, sx, sy, w, h, fmt) \
    LP10(0x78, ULONG, ReadPixelArray, APTR, dst, a0, ULONG, dx, d0, ULONG, dy, d1, ULONG, dmod, d2, \
         struct RastPort *, rp, a1, ULONG, sx, d3, ULONG, sy, d4, ULONG, w, d5, ULONG, h, d6, ULONG, fmt, d7, \
         , CyberGfxBase)
#define cgx_write(src, sx, sy, smod, rp, dx, dy, w, h, fmt) \
    LP10(0x7e, ULONG, WritePixelArray, APTR, src, a0, ULONG, sx, d0, ULONG, sy, d1, ULONG, smod, d2, \
         struct RastPort *, rp, a1, ULONG, dx, d3, ULONG, dy, d4, ULONG, w, d5, ULONG, h, d6, ULONG, fmt, d7, \
         , CyberGfxBase)

/* ================================================================= */
/*  EdWord's colours and the themes                                   */
/* ================================================================= */

static const UWORD palette[16] = {
    0xAAA, 0x000, 0xFFF, 0x68B,  0xC00, 0x080, 0x00C, 0x088,
    0xA0A, 0xB80, 0x888, 0xFA0,  0x4F4, 0x222, 0xEEE, 0xDDD
};

/* the themes: Amiga (Workbench 2's colours) and the Windows 95 schemes,
 * as in the Atari version; colours 0-255 for the face, the light and dark
 * edges, the text, the ruler margins and the desk around the pages */
typedef struct { const char *name; unsigned char c[TH_NROLES][3]; } THEMEDEF;
static const THEMEDEF themes[NTHEMES] = {
    { "Amiga",            { { 170, 170, 170 }, { 255, 255, 255 }, { 0, 0, 0 }, { 0, 0, 0 }, { 221, 221, 221 }, { 136, 136, 136 } } },
    { "Aubergine",        { { 144, 176, 168 }, { 200, 216, 212 }, { 72, 104, 96 }, { 0, 0, 0 }, { 120, 152, 144 }, { 64, 128, 128 } } },
    { "Blush",            { { 207, 175, 183 }, { 231, 215, 219 }, { 140, 96, 108 }, { 0, 0, 0 }, { 186, 148, 158 }, { 128, 64, 64 } } },
    { "Classic",          { { 192, 192, 192 }, { 255, 255, 255 }, { 128, 128, 128 }, { 0, 0, 0 }, { 160, 160, 160 }, { 0, 128, 128 } } },
    { "Damson",           { { 168, 152, 144 }, { 212, 204, 200 }, { 96, 80, 72 }, { 0, 0, 0 }, { 144, 126, 118 }, { 64, 0, 64 } } },
    { "Drizzle",          { { 136, 144, 168 }, { 196, 200, 212 }, { 72, 80, 104 }, { 0, 0, 0 }, { 112, 120, 146 }, { 64, 64, 96 } } },
    { "Dune",             { { 213, 204, 187 }, { 234, 230, 221 }, { 162, 141, 104 }, { 0, 0, 0 }, { 190, 178, 156 }, { 162, 141, 104 } } },
    { "Harvest",          { { 216, 216, 168 }, { 236, 236, 212 }, { 144, 144, 96 }, { 0, 0, 0 }, { 192, 192, 140 }, { 128, 128, 0 } } },
    { "Lagoon",           { { 176, 208, 208 }, { 224, 240, 240 }, { 64, 128, 128 }, { 0, 0, 0 }, { 144, 184, 184 }, { 0, 128, 128 } } },
    { "Lavender",         { { 174, 168, 217 }, { 216, 213, 236 }, { 88, 80, 160 }, { 0, 0, 0 }, { 150, 143, 200 }, { 128, 128, 192 } } },
    { "Nautical",         { { 136, 192, 184 }, { 196, 224, 220 }, { 64, 120, 112 }, { 0, 0, 0 }, { 110, 168, 160 }, { 0, 64, 64 } } },
    { "Pine",             { { 162, 200, 169 }, { 208, 228, 212 }, { 88, 128, 96 }, { 0, 0, 0 }, { 138, 178, 146 }, { 0, 64, 0 } } },
    { "Shale",            { { 145, 168, 169 }, { 200, 212, 212 }, { 80, 100, 100 }, { 0, 0, 0 }, { 122, 146, 148 }, { 64, 96, 96 } } },
    { "Squash",           { { 227, 186, 100 }, { 241, 220, 176 }, { 150, 110, 30 }, { 0, 0, 0 }, { 205, 160, 70 }, { 0, 0, 0 } } },
    { "Sycamore",         { { 227, 210, 170 }, { 241, 232, 212 }, { 160, 128, 64 }, { 0, 0, 0 }, { 205, 182, 130 }, { 128, 64, 0 } } },
    { "Tempest",          { { 192, 192, 192 }, { 255, 255, 255 }, { 128, 128, 128 }, { 0, 0, 0 }, { 160, 160, 160 }, { 0, 0, 64 } } },
    { "Terracotta",       { { 194, 191, 165 }, { 225, 224, 210 }, { 130, 123, 99 }, { 0, 0, 0 }, { 166, 162, 136 }, { 128, 0, 0 } } },
};
#define NTPENS 8                        /* the 6 roles, the fill and its text */
#define TP0    20                       /* their pens on EdWord's own screen */
#define CUBE0  32                       /* the picture colour cube's first pen */

int theme_id;
int rtg, scr_colours;
UBYTE penmap[NLPENS];
static int own_theme;                   /* pens TP0.. are the theme's */
static int wb_theme;                    /* Workbench: theme pens obtained */
static LONG wbpen[NTPENS];
static int pens_obtained;               /* Workbench: EdWord's 16 colours obtained */

const char *theme_name(int id) { return id >= 0 && id < NTHEMES ? themes[id].name : ""; }
int theme_possible(void) { return own_theme || wb_theme; }

static long lum(const unsigned char *c) { return c[0] * 30L + c[1] * 59L + c[2] * 11L; }

/* the colour of a theme's role: 0-5 the table's, 6 the fill (title bars,
 * selected buttons), 7 the fill's text */
static void role_rgb(int id, int role, unsigned char *c)
{
    const THEMEDEF *t = &themes[id >= 0 && id < NTHEMES ? id : 0];
    if (role < TH_NROLES) { memcpy(c, t->c[role], 3); return; }
    if (id <= 0) {                      /* Workbench 2: blue bars, black text */
        static const unsigned char fill[3] = { 102, 136, 187 }, ft[3] = { 0, 0, 0 };
        memcpy(c, role == 6 ? fill : ft, 3);
        return;
    }
    if (role == 6) { memcpy(c, t->c[5], 3); return; }
    /* the light or the text colour, whichever stands out more on the fill */
    {
        long f = lum(t->c[5]), a = lum(t->c[1]) - f, b = lum(t->c[3]) - f;
        memcpy(c, (a < 0 ? -a : a) >= (b < 0 ? -b : b) ? t->c[1] : t->c[3], 3);
    }
}

/* DrawInfo pens for EdWord's own screen: with 32 colours or more, the
 * theme's pens; otherwise the usual ones */
void theme_screen_pens(UWORD *pens, int depth)
{
    if (depth < 5) { pens[0] = (UWORD)~0; return; }
    pens[DETAILPEN] = 0;          pens[BLOCKPEN] = TP0 + 3;     pens[TEXTPEN] = TP0 + 3;
    pens[SHINEPEN] = TP0 + 1;     pens[SHADOWPEN] = TP0 + 2;    pens[FILLPEN] = TP0 + 6;
    pens[FILLTEXTPEN] = TP0 + 7;  pens[BACKGROUNDPEN] = 0;      pens[HIGHLIGHTTEXTPEN] = TP0 + 1;
    pens[BARDETAILPEN] = TP0 + 3; pens[BARBLOCKPEN] = TP0 + 1;  pens[BARTRIMPEN] = TP0 + 3;
    pens[NUMDRIPENS] = (UWORD)~0;
}

static ULONG c32(unsigned v) { return (ULONG)(v & 255) * 0x01010101UL; }
static void load_theme_id(int id);

static int wb_excl;                     /* Workbench: the theme pens are ours alone */
static int wth[MAX_DOCS];               /* a document's own theme, -1 the global one */
static int loaded = -1;                 /* the theme whose colours are in the pens */

/* theme id's colours in pens 0 and TP0.. of EdWord's own screen (or the
 * Workbench pens it has to itself) */
static void load_theme_id(int id)
{
    static ULONG tab[2 + 4 + NTPENS * 3 + 1];
    unsigned char c[3];
    int i, k = 0;
    loaded = id;
    if (on_wb) {
        if (!wb_excl) return;
        for (i = 0; i < NTPENS; i++) {
            role_rgb(id, i, c);
            SetRGB32(&scr->ViewPort, (ULONG)wbpen[i], c32(c[0]), c32(c[1]), c32(c[2]));
        }
        return;
    }
    if (!own_theme) return;
    role_rgb(id, 0, c);
    tab[k++] = (1UL << 16) | 0;
    tab[k++] = c32(c[0]); tab[k++] = c32(c[1]); tab[k++] = c32(c[2]);
    tab[k++] = ((ULONG)NTPENS << 16) | TP0;
    for (i = 0; i < NTPENS; i++) { role_rgb(id, i, c); tab[k++] = c32(c[0]); tab[k++] = c32(c[1]); tab[k++] = c32(c[2]); }
    tab[k] = 0;
    LoadRGB32(&scr->ViewPort, tab);
}
static void load_theme_own(void) { load_theme_id(theme_id); }

/* EdWord's colours, the theme and (256 colours) the cube on its own screen */
static void load_own(void)
{
    int i;
    if (GfxBase->LibNode.lib_Version < 39) { LoadRGB4(&scr->ViewPort, (UWORD *)palette, scr_colours < 16 ? scr_colours : 16); return; }
    {
        static ULONG tab[2 + 16 * 3 + 216 * 3 + 2 + 16 * 4];
        int k = 0, n = scr_colours < 16 ? scr_colours : 16;
        tab[k++] = ((ULONG)n << 16) | 0;
        for (i = 0; i < n; i++) {
            tab[k++] = c32(((palette[i] >> 8) & 15) * 17); tab[k++] = c32(((palette[i] >> 4) & 15) * 17); tab[k++] = c32((palette[i] & 15) * 17);
        }
        if (scr_colours >= 256) {
            tab[k++] = (216UL << 16) | CUBE0;
            for (i = 0; i < 216; i++) { tab[k++] = c32(i / 36 * 51); tab[k++] = c32(i / 6 % 6 * 51); tab[k++] = c32(i % 6 * 51); }
        }
        /* Where every plane is complemented (a graphics card's 256 colours,
         * and some emulations ignore the write mask), pen n-1-i is pen i's
         * complement: it gets the colour of pen 15-i, as the lowest four
         * planes would give, where that pen is free */
        if (scr_colours > 16)
            for (i = 0; i < 16; i++) {
                int p = scr_colours - 1 - i;
                if ((p >= 16 && p < TP0 + NTPENS) || (scr_colours >= 256 && p >= CUBE0 && p < CUBE0 + 216)) continue;
                tab[k++] = (1UL << 16) | (ULONG)p;
                tab[k++] = c32(((palette[15 - i] >> 8) & 15) * 17); tab[k++] = c32(((palette[15 - i] >> 4) & 15) * 17); tab[k++] = c32((palette[15 - i] & 15) * 17);
            }
        tab[k] = 0;
        LoadRGB32(&scr->ViewPort, tab);
    }
    load_theme_own();
}

/* the nearest of the screen's colours to a 12-bit one (no ObtainBestPen
 * before Workbench 3.0) */
static int nearest_pen(struct ColorMap *cm, int n, UWORD rgb)
{
    int i, best = 0;
    long bd = 0x7fffffffL;
    for (i = 0; i < n; i++) {
        UWORD c = (UWORD)GetRGB4(cm, i);
        long dr = ((c >> 8) & 15) - ((rgb >> 8) & 15), dg = ((c >> 4) & 15) - ((rgb >> 4) & 15), db = (c & 15) - (rgb & 15);
        long dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (dd < bd) { bd = dd; best = i; }
    }
    return best;
}

/* EdWord's 16 colours as pens of the Workbench screen */
static void map_wb(void)
{
    struct ColorMap *cm = scr->ViewPort.ColorMap;
    int i, n = scr_colours;
    npens = (short)(n > 16 ? 16 : n);
    if (GfxBase->LibNode.lib_Version >= 39 && n >= 8) {
        for (i = 0; i < 16; i++) {
            ULONG r = (palette[i] >> 8) & 15, g = (palette[i] >> 4) & 15, b = palette[i] & 15;
            LONG p = ObtainBestPen(cm, r * 0x11111111UL, g * 0x11111111UL, b * 0x11111111UL,
                                   OBP_Precision, PRECISION_IMAGE, TAG_END);
            penmap[i] = (UBYTE)(p >= 0 ? p : nearest_pen(cm, n > 256 ? 256 : n, palette[i]));
        }
        pens_obtained = 1;
        npens = 16;
    } else
        for (i = 0; i < 16; i++) penmap[i] = (UBYTE)(i < n && i < 4 ? i : nearest_pen(cm, n, palette[i]));
}

/* the theme's pens on the Workbench screen (shared, nearest) */
static void wb_theme_pens(int get)
{
    struct ColorMap *cm = scr->ViewPort.ColorMap;
    int i;
    if (wb_theme) for (i = 0; i < NTPENS; i++) if (wbpen[i] >= 0) ReleasePen(cm, (ULONG)wbpen[i]);
    wb_theme = wb_excl = 0;
    if (!get || GfxBase->LibNode.lib_Version < 39 || scr_colours < 32) return;
    /* a graphics card's Workbench of 15 bits or more: pens of EdWord's own,
     * whose colours it sets as it draws each document (a theme each) */
    if (rtg) {
        for (i = 0; i < NTPENS; i++) {
            unsigned char c[3];
            role_rgb(theme_id, i, c);
            wbpen[i] = ObtainPen(cm, (ULONG)-1, c32(c[0]), c32(c[1]), c32(c[2]), PEN_EXCLUSIVE);
            if (wbpen[i] < 0) break;
        }
        if (i == NTPENS) { wb_theme = wb_excl = 1; loaded = theme_id; return; }
        while (--i >= 0) ReleasePen(cm, (ULONG)wbpen[i]);
    }
    for (i = 0; i < NTPENS; i++) {
        unsigned char c[3];
        role_rgb(theme_id, i, c);
        wbpen[i] = ObtainBestPen(cm, c32(c[0]), c32(c[1]), c32(c[2]), OBP_Precision, PRECISION_IMAGE, TAG_END);
    }
    wb_theme = 1;
}

/* the logical theme pens: the theme's, or EdWord's usual colours */
static void map_theme_pens(void)
{
    static const UBYTE plain[NTPENS] = { P_GREY, P_WHITE, P_BLACK, P_BLACK, P_LIGHT, P_TGREY, P_BLUE, P_BLACK };
    int i;
    for (i = 0; i < NTPENS; i++) {
        int p = plain[i];
        if (i == 4 && npens < 16) p = P_GREY;
        if (i == 5 && npens < 16) p = P_GREY;
        penmap[P_TFACE + i] = penmap[p];
    }
    if (own_theme) {
        penmap[P_TFACE] = 0;
        for (i = 1; i < NTPENS; i++) penmap[P_TFACE + i] = (UBYTE)(TP0 + i);
    } else if (wb_theme && (theme_id > 0 || wb_excl))
        for (i = 0; i < NTPENS; i++) if (wbpen[i] >= 0) penmap[P_TFACE + i] = (UBYTE)wbpen[i];
}

static void read_palette(void);

void pens_setup(void)
{
    int i, d = depth;
    if (GfxBase->LibNode.lib_Version >= 39) {
        ULONG bd = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
        if (bd > 0) d = (int)bd;
        /* a Picasso96 screen: its bits per pixel from Picasso96 itself */
        if (!P96Base) P96Base = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
        if (P96Base && p96GetBitMapAttr(scr->RastPort.BitMap, P96BMA_ISP96)) {
            bd = p96GetBitMapAttr(scr->RastPort.BitMap, P96BMA_DEPTH);
            if (bd > 0) d = (int)bd;
        }
    }
    depth = (short)d;
    rtg = d > 8;
    scr_colours = d >= 8 ? 256 : 1 << d;
    if (rtg && !P96Base && !CyberGfxBase) CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    for (i = 0; i < NLPENS; i++) penmap[i] = (UBYTE)(i < 16 ? i : 0);
    for (i = 0; i < MAX_DOCS; i++) wth[i] = -1;
    loaded = theme_id;
    if (on_wb) {
        map_wb();
        wb_theme_pens(theme_id > 0 || rtg);
    } else {
        npens = (short)(scr_colours > 16 ? 16 : scr_colours);
        own_theme = scr_colours >= 32 && GfxBase->LibNode.lib_Version >= 39;
        load_own();
    }
    map_theme_pens();
    read_palette();
    edlog("screen: depth %d, %d colours, rtg %d, own theme %d, wb theme %d, gfx v%d chiprev %x", depth, scr_colours, rtg, own_theme, wb_theme,
          GfxBase->LibNode.lib_Version, GfxBase->ChipRevBits0);
#ifdef EDLOG
    {
        ULONG c[3];
        int k;
        edlog("screen: bitmap depth %ld, p96 %s (isp96 %ld, depth %ld), cgx %s, on wb %d",
              (long)(GfxBase->LibNode.lib_Version >= 39 ? GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH) : 0),
              P96Base ? "yes" : "no", (long)(P96Base ? p96GetBitMapAttr(scr->RastPort.BitMap, P96BMA_ISP96) : 0),
              (long)(P96Base ? p96GetBitMapAttr(scr->RastPort.BitMap, P96BMA_DEPTH) : 0), CyberGfxBase ? "yes" : "no", on_wb);
        for (k = 0; k < 24; k++) {
            if (GfxBase->LibNode.lib_Version < 39) break;
            GetRGB32(scr->ViewPort.ColorMap, PEN(k), 1, c);
            edlog("  pen %2d -> %3d: %02lx %02lx %02lx", k, (int)PEN(k), c[0] >> 24, c[1] >> 24, c[2] >> 24);
        }
    }
#endif
}

void pens_free(void)
{
    int i;
    if (on_wb && scr) {
        wb_theme_pens(0);
        if (pens_obtained) for (i = 0; i < 16; i++) ReleasePen(scr->ViewPort.ColorMap, penmap[i]);
        pens_obtained = 0;
    }
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    CyberGfxBase = NULL;
    if (P96Base) CloseLibrary(P96Base);
    P96Base = NULL;
}

/* everything EdWord shows, drawn again (a theme on a screen without a palette) */
static void redraw_everything(void)
{
    struct Window *w;
    int i;
    if (!on_wb) { ShowTitle(scr, FALSE); ShowTitle(scr, TRUE); }   /* the screen's title bar */
    if (backdrop) {
        SetAPen(backdrop->RPort, PEN(P_TFACE));
        RectFill(backdrop->RPort, 0, 0, backdrop->Width - 1, backdrop->Height - 1);
    }
    for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i) && doc_win(docs[i])) { refresh_all(docs[i]); set_sliders(docs[i]); }
    for (w = scr->FirstWindow; w; w = w->NextWindow) {
        struct IntuiMessage m;
        if (w->UserPort != idcmp_port) continue;
        RefreshWindowFrame(w);
        if (w->FirstGadget) RefreshGList(w->FirstGadget, w, NULL, -1);
        memset(&m, 0, sizeof m);
        m.Class = IDCMP_REFRESHWINDOW;
        m.IDCMPWindow = w;
        tool_route(&m);
    }
}

/* Per-document themes: on a graphics card's screen of 15 bits or more a
 * pen's colour counts only when something is drawn with it, so each
 * document window can have a theme of its own, put into the pens while
 * EdWord draws in it (gfx_begin) and the global one's after (gfx_end).
 * The windows' frames (Intuition's) keep the global theme. On a screen
 * with a palette every pen's colour shows at once: one theme for all. */
static int ctx = -1;
static int active_slot = -1;            /* the active document: its theme is the screen's */

int theme_per_window(void) { return rtg && (own_theme || wb_excl); }

/* the theme the pens hold when nothing is being drawn: the active
 * document's, so the windows' frames, the menus and the title bar match it */
static int base_theme(void)
{
    if (active_slot >= 0 && active_slot < MAX_DOCS && DOC_USED(active_slot) && wth[active_slot] >= 0) return wth[active_slot];
    return theme_id;
}

/* What the pens hold when nothing is being drawn. The frames of inactive
 * windows are drawn in the background, shine, shadow and text pens: those
 * keep the global theme, so an inactive window's border never changes
 * colour. Only the fill pens (the active window's title bar) take the
 * active document's theme. Key: a theme id, or 1000 + id for that mix. */
static int base_key(void)
{
    int b = base_theme();
    return b == theme_id || on_wb ? b : 1000 + b;
}

static void load_key(int k)
{
    unsigned char c[3];
    int i;
    if (k < 1000) { load_theme_id(k); return; }
    load_theme_id(theme_id);
    if (own_theme)
        for (i = 6; i < 8 && i < NTPENS; i++) {
            role_rgb(k - 1000, i, c);
            SetRGB32(&scr->ViewPort, (ULONG)(TP0 + i), c32(c[0]), c32(c[1]), c32(c[2]));
        }
    loaded = k;
}

/* a document window became the active one */
void theme_activate(int slot)
{
    struct Window *w;
    if (!theme_per_window() || slot == active_slot) { active_slot = slot; return; }
    active_slot = slot;
    if (ctx < 0 && loaded != base_key()) {
        load_key(base_key());
        /* the frames and the screen's title bar again, in its colours */
        for (w = scr->FirstWindow; w; w = w->NextWindow) if (w->UserPort == idcmp_port) RefreshWindowFrame(w);
        if (!on_wb) { ShowTitle(scr, FALSE); ShowTitle(scr, TRUE); }
    }
}

/* the theme now in the pens has a dark light role (a dark theme): text
 * drawn on it wants a lighter colour */
int theme_dark(void)
{
    unsigned char c[3];
    role_rgb(loaded >= 1000 ? loaded - 1000 : loaded >= 0 ? loaded : theme_id, 1, c);
    return c[0] * 3 + c[1] * 6 + c[2] < 128 * 10;
}

int theme_of_doc(int slot) { return slot >= 0 && slot < MAX_DOCS && wth[slot] >= 0 ? wth[slot] : theme_id; }

void theme_for(int slot)
{
    int id;
    if (!theme_per_window() || slot < 0 || slot >= MAX_DOCS) return;
    id = wth[slot] >= 0 ? wth[slot] : theme_id;
    ctx = slot;
    (void)0;
    if (id != loaded) load_theme_id(id);
}

void theme_global(void)
{
    if (ctx < 0) return;
    ctx = -1;
    if (loaded != base_key()) load_key(base_key());
}

void theme_set_doc(int slot, int id)
{
    if (slot < 0 || slot >= MAX_DOCS) return;
    wth[slot] = id;
    if (DOC_USED(slot) && doc_win(docs[slot])) {
        refresh_all(docs[slot]);
        set_sliders(docs[slot]);
        if (slot == active_slot && theme_per_window()) { active_slot = -1; theme_activate(slot); }
    }
}

void theme_set(int id)
{
    int i;
    if (id < 0 || id >= NTHEMES) id = 0;
    theme_id = id;
    for (i = 0; i < MAX_DOCS; i++) wth[i] = -1;      /* for all documents */
    if (!scr) return;
    if (on_wb && !wb_excl) { wb_theme_pens(id > 0); map_theme_pens(); redraw_everything(); return; }
    if (!own_theme && !wb_excl) return;
    load_theme_id(id);
    if (rtg) redraw_everything();
}

/* View > Colour Theme: the theme, seen at once on the screen; with themes
 * per document, for the active one or for all */
static short th_this = -1, th_all = -1;
static DOC  *th_doc;
static int   th_first;

static void theme_apply_choice(DLG *g, int id)
{
    if (th_doc && th_this >= 0 && dlg_on(g, th_this)) theme_set_doc(th_doc->slot, id);
    else theme_set(id);
}

/* The picture of the theme (as the Atari version has it): a small
 * document window, its title bar, toolbar, ruler, the desk with a page
 * and the font line at the foot, in the theme being chosen */
static void fill(struct RastPort *rp, long pen, long x, long y, long w, long h)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(rp, pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}

static void mini_bevel(struct RastPort *rp, long x, long y, long w, long h, int in)
{
    fill(rp, PEN(P_TFACE), x, y, w, h);
    fill(rp, PEN(in ? P_TDARK : P_TLIGHT), x, y, w, 1);
    fill(rp, PEN(in ? P_TDARK : P_TLIGHT), x, y, 1, h);
    fill(rp, PEN(in ? P_TLIGHT : P_TDARK), x, y + h - 1, w, 1);
    fill(rp, PEN(in ? P_TLIGHT : P_TDARK), x + w - 1, y, 1, h);
}

static void theme_paint(void *g, struct RastPort *rp, long x, long y, long w, long h)
{
    long th = hchar + 3, bh, rh, sh, bx, px, pw, py, ph, ly, i, sq = dev_pixh < dev_pixw * 3 / 2;
    (void)g;
    if (th_doc) theme_for(th_doc->slot);          /* its colours, while drawing */
    bh = sq ? h / 6 : h / 5; rh = h / 10; sh = h / 9;
    if (bh < 9) bh = 9;
    if (rh < 5) rh = 5;
    if (sh < 6) sh = 6;
    fill(rp, PEN(P_BLACK), x, y, w, h);                               /* the frame */
    x++; y++; w -= 2; h -= 2;
    /* the title bar: the active window's colours */
    fill(rp, PEN(P_TFILL), x, y, w, th);
    SetAPen(rp, PEN(P_TFTEXT));
    SetDrMd(rp, JAM1);
    Move(rp, x + 6, y + 1 + sys_asc);
    Text(rp, "Untitled", 8);
    fill(rp, PEN(P_TDARK), x, y + th - 1, w, 1);
    y += th; h -= th;
    /* the toolbar: bevelled buttons, a mark on each */
    fill(rp, PEN(P_TFACE), x, y, w, bh);
    for (i = 0, bx = x + 3; bx + bh * 2 <= x + w - 3 && i < 12; i++, bx += bh * 2 - 2 + (i % 3 ? 1 : 6)) {
        long bw = sq ? bh - 3 : bh * 2 - 4;
        mini_bevel(rp, bx, y + 2, bw, bh - 4, i == 1);
        fill(rp, PEN(P_TTEXT), bx + bw / 3, y + 2 + (bh - 4) / 3, bw / 3, (bh - 4) / 3);
    }
    fill(rp, PEN(P_TDARK), x, y + bh - 1, w, 1);
    y += bh; h -= bh;
    /* the ruler: the margins at the ends, ticks along it */
    fill(rp, PEN(P_TFACE), x, y, w, rh);
    fill(rp, PEN(P_TMARGIN), x, y, w / 8, rh);
    fill(rp, PEN(P_TMARGIN), x + w - w / 8, y, w / 8, rh);
    for (bx = x + w / 8; bx < x + w - w / 8; bx += 8)
        fill(rp, PEN(P_TTEXT), bx, y + rh - ((bx - x) % 32 < 8 ? rh / 2 + 1 : 2), 1, rh / 2 + 1);
    fill(rp, PEN(P_TTEXT), x, y + rh - 1, w, 1);
    y += rh; h -= rh;
    /* the font line at the foot */
    fill(rp, PEN(P_TFACE), x, y + h - sh, w, sh);
    fill(rp, PEN(P_TDARK), x, y + h - sh, w, 1);
    fill(rp, PEN(P_TTEXT), x + 4, y + h - sh + sh / 2, w / 3, 1);
    fill(rp, PEN(P_TTEXT), x + 8 + w / 3, y + h - sh + sh / 2, w / 6, 1);
    h -= sh;
    /* the desk and a page on it, with lines of text */
    fill(rp, PEN(P_TDESK), x, y, w, h);
    px = x + w / 8; pw = w - w / 4;
    py = y + 4; ph = h - 4;
    fill(rp, PEN(P_BLACK), px + 3, py + 2, pw, ph);                     /* its shadow */
    fill(rp, PEN(P_WHITE), px, py, pw, ph);
    for (ly = py + (sq ? 6 : 4), i = 0; ly < py + ph - 2; ly += sq ? 6 : 4, i++)
        fill(rp, PEN(P_BLACK), px + 8, ly, i % 4 == 3 ? pw / 2 : pw - 16, 1);
    if (th_doc) theme_global();
}

static void theme_changed(DLG *g, short item)
{
    int i, id = 0;
    for (i = 0; i < NTHEMES; i++) if (dlg_on(g, (short)(th_first + i))) id = i;
    if (item == th_first || g->it[item].lead == th_first) { theme_apply_choice(g, id); return; }
    /* this document / all documents: the other goes back as it was */
    theme_apply_choice(g, id);
}

int dlg_theme(void)
{
    static DLG g;
    short ok, i;
    int old = theme_id, oldw[MAX_DOCS], per = theme_per_window();
    for (i = 0; i < MAX_DOCS; i++) oldw[i] = wth[i];
    th_doc = per ? active_doc() : NULL;
    th_this = th_all = -1;
    dlg_begin(&g, L(S_THEME_TITLE));
    dlg_label(&g, L(S_THEME));
    th_first = -1;
    for (i = 0; i < NTHEMES; i++) {
        short k = dlg_radio(&g, 1, themes[i].name, (th_doc ? theme_of_doc(th_doc->slot) : theme_id) == i);
        if (th_first < 0) th_first = k;
    }
    if (th_doc) {
        dlg_row(&g);
        dlg_label(&g, "");
        th_this = dlg_radio(&g, 2, L(S_THEME_THISDOC), 1);     /* this document unless told */
        th_all = dlg_radio(&g, 2, L(S_THEME_ALLDOCS), 0);
    }
    dlg_picture(&g, (short)(wchar * 40), (short)(dev_pixh < dev_pixw * 3 / 2 ? hchar * 12 : hchar * 8), theme_paint);
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run_cb(&g, theme_changed) != ok) {
        theme_set(old);
        for (i = 0; i < MAX_DOCS; i++) if (oldw[i] >= 0) theme_set_doc(i, oldw[i]);
        return 0;
    }
    return 1;
}

/* ================================================================= */
/*  Colour pictures                                                   */
/* ================================================================= */

/* The screen's colours, read once, and which pens a picture may use:
 * not the theme's (they change) nor the pointer's on EdWord's screen */
static short pal_rgb[256][3];
static UBYTE pal_use[256];
static int   pal_n, pal_ok;
static UWORD *pal_cube;                /* 32768: pen + 1, 0 not looked for yet */

static void read_palette(void)
{
    struct ColorMap *cm = scr->ViewPort.ColorMap;
    int i, n = scr_colours > 256 ? 256 : scr_colours;
    pal_ok = 0;
    pal_n = n;
    pic_flush();
    if (rtg) return;
    for (i = 0; i < n; i++) {
        if (GfxBase->LibNode.lib_Version >= 39) {
            ULONG c[3];
            GetRGB32(cm, (ULONG)i, 1, c);
            pal_rgb[i][0] = (short)(c[0] >> 24); pal_rgb[i][1] = (short)(c[1] >> 24); pal_rgb[i][2] = (short)(c[2] >> 24);
        } else {
            UWORD c = (UWORD)GetRGB4(cm, i);
            pal_rgb[i][0] = (short)(((c >> 8) & 15) * 17); pal_rgb[i][1] = (short)(((c >> 4) & 15) * 17); pal_rgb[i][2] = (short)((c & 15) * 17);
        }
        if (on_wb) pal_use[i] = 1;
        else pal_use[i] = (i >= 1 && i < 16) || (i >= CUBE0 && i < CUBE0 + 216 && n >= 256);
    }
    if (on_wb && wb_theme) for (i = 0; i < NTPENS; i++) if (wbpen[i] >= 0 && wbpen[i] < 256) pal_use[wbpen[i]] = 0;
    pal_ok = n >= 8;
}

void pic_flush(void) { if (pal_cube) memset(pal_cube, 0, 32768 * sizeof(UWORD)); }

/* (WritePixelArray8 is graphics.library 36, Workbench 2.0) */
int pic_colour(void) { return rtg ? P96Base != NULL || CyberGfxBase != NULL : pal_ok && GfxBase->LibNode.lib_Version >= 36; }

static int pal_nearest(int r, int g, int b)
{
    unsigned idx = (unsigned)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    if (!pal_cube[idx]) {
        long best = 0x7fffffffL;
        int i, bi = 1, cr = (r & ~7) | 4, cg = (g & ~7) | 4, cb = (b & ~7) | 4;
        for (i = 0; i < pal_n; i++) {
            long dr, dg, db, dd;
            if (!pal_use[i]) continue;
            dr = cr - pal_rgb[i][0]; dg = cg - pal_rgb[i][1]; db = cb - pal_rgb[i][2];
            dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (dd < best) { best = dd; bi = i; }
        }
        pal_cube[idx] = (UWORD)(bi + 1);
    }
    return pal_cube[idx] - 1;
}

/* w x h pixels of R G B made ready for the screen: on a graphics card a
 * copy; otherwise dithered (error diffusion) to the screen's pens, rows
 * of (w + 15) & ~15 bytes, as WritePixelArray8 wants them. NULL: no
 * memory. Free it with free(). */
unsigned char *pic_make(const unsigned char *rgb, long w, long h)
{
    long stride = (w + 15) & ~15L, x, y;
    unsigned char *out;
    short *e0, *e1, *t;
    if (rtg) {
        out = (unsigned char *)malloc((size_t)(w * h * 3));
        if (out) memcpy(out, rgb, (size_t)(w * h * 3));
        return out;
    }
    if (!pal_ok) return NULL;
    if (!pal_cube) pal_cube = (UWORD *)calloc(32768, sizeof(UWORD));
    if (!pal_cube) return NULL;
    out = (unsigned char *)calloc((size_t)(stride * h), 1);
    e0 = (short *)calloc((size_t)(w + 2) * 3, sizeof(short));
    e1 = (short *)calloc((size_t)(w + 2) * 3, sizeof(short));
    if (!out || !e0 || !e1) { free(out); free(e0); free(e1); return NULL; }
    for (y = 0; y < h; y++) {
        const unsigned char *s = rgb + y * w * 3;
        memset(e1, 0, (size_t)(w + 2) * 3 * sizeof(short));
        for (x = 0; x < w; x++) {
            int c[3], k, pen;
            for (k = 0; k < 3; k++) {
                int v = s[x * 3 + k] + e0[(x + 1) * 3 + k] / 16;
                c[k] = v < 0 ? 0 : v > 255 ? 255 : v;
            }
            pen = pal_nearest(c[0], c[1], c[2]);
            for (k = 0; k < 3; k++) {
                int err = c[k] - pal_rgb[pen][k];
                e0[(x + 2) * 3 + k] += (short)(err * 7);
                e1[x * 3 + k] += (short)(err * 3);
                e1[(x + 1) * 3 + k] += (short)(err * 5);
                e1[(x + 2) * 3 + k] += (short)err;
            }
            out[y * stride + x] = (unsigned char)pen;
        }
        t = e0; e0 = e1; e1 = t;
    }
    free(e0); free(e1);
    return out;
}

/* a picture from pic_make, pw x ph pixels, drawn at window (x, y) of the
 * RastPort being drawn into (amigfx.c's grp), clipped to the window */
void pic_put(const unsigned char *pic, long pw, long ph, long x, long y)
{
    long sx = 0, sy = 0, w = pw, h = ph, mw, mh;
    if (!grp || !gwin || !pic) return;
    mw = gwin->Width; mh = gwin->Height;
    if (x < 0) { sx = -x; w += x; x = 0; }
    if (y < 0) { sy = -y; h += y; y = 0; }
    if (x + w > mw) w = mw - x;
    if (y + h > mh) h = mh - y;
    if (w <= 0 || h <= 0) return;
    if (rtg) {
        if (P96Base) {
            struct p96RenderInfo ri;
            ri.Memory = (APTR)pic; ri.BytesPerRow = (WORD)(pw * 3); ri.pad = 0; ri.RGBFormat = RGBFB_R8G8B8;
            p96WritePixelArray(&ri, (ULONG)sx, (ULONG)sy, grp, (ULONG)x, (ULONG)y, (ULONG)w, (ULONG)h);
        } else if (CyberGfxBase) cgx_write((APTR)pic, (UWORD)sx, (UWORD)sy, (UWORD)(pw * 3), grp, (UWORD)x, (UWORD)y, (UWORD)w, (UWORD)h, RECTFMT_RGB);
        return;
    }
    {
        /* WritePixelArray8 16 rows at a time, through a one-line work bitmap */
        static struct RastPort trp;
        static struct BitMap tbm;
        PLANEPTR pl[8];
        long pstride = (pw + 15) & ~15L, tw = (w + 15) & ~15L, r;
        int k, dpt = depth > 8 ? 8 : depth, ok = 1;
        unsigned char *band = (unsigned char *)malloc((size_t)(tw * 16));
        if (!band) return;
        InitBitMap(&tbm, (BYTE)dpt, (UWORD)tw, 1);
        for (k = 0; k < dpt; k++) {
            pl[k] = AllocRaster((ULONG)tw, 1);
            if (!pl[k]) ok = 0;
            tbm.Planes[k] = pl[k];
        }
        if (ok) {
            InitRastPort(&trp);
            trp.BitMap = &tbm;
            for (r = 0; r < h; r += 16) {
                long n = h - r < 16 ? h - r : 16, i;
                for (i = 0; i < n; i++) memcpy(band + i * tw, pic + (sy + r + i) * pstride + sx, (size_t)w);
                WritePixelArray8(grp, (ULONG)x, (ULONG)(y + r), (ULONG)(x + w - 1), (ULONG)(y + r + n - 1), band, &trp);
            }
        }
        for (k = 0; k < dpt; k++) if (pl[k]) FreeRaster(pl[k], (ULONG)tw, 1);
        free(band);
    }
}

/* ================================================================= */
/*  Smoothed text                                                     */
/* ================================================================= */

/* On a graphics card's screen (Picasso96 or CyberGraphX) and on EdWord's
 * own 256-colour screen, Speedo text is smoothed: amispd.c gives each
 * pixel's coverage, and here it is blended with what the screen shows
 * there, read back first. */
int aa_text(void)
{
    if (rtg) return P96Base != NULL || CyberGfxBase != NULL;
    return !on_wb && scr_colours >= 256 && pal_ok && GfxBase->LibNode.lib_Version >= 36;
}

static void pen_rgb(int pen, int *c)
{
    ULONG v[3];
    if (GfxBase->LibNode.lib_Version >= 39) {
        GetRGB32(scr->ViewPort.ColorMap, PEN(pen), 1, v);
        c[0] = (int)(v[0] >> 24); c[1] = (int)(v[1] >> 24); c[2] = (int)(v[2] >> 24);
    } else { c[0] = c[1] = c[2] = 0; }
}

void aa_put(const unsigned char *cov, long cw_, long ch, long x, long y, int pen)
{
    long sx = 0, sy = 0, w = cw_, h = ch, mw, mh, i, j;
    int fg[3];
    if (!grp || !gwin || !cov) return;
    mw = gwin->Width; mh = gwin->Height;
    if (x < 0) { sx = -x; w += x; x = 0; }
    if (y < 0) { sy = -y; h += y; y = 0; }
    if (x + w > mw) w = mw - x;
    if (y + h > mh) h = mh - y;
    if (w <= 0 || h <= 0) return;
    pen_rgb(pen, fg);
    if (rtg) {
        unsigned char *px = (unsigned char *)malloc((size_t)(w * h * 3));
        struct p96RenderInfo ri;
        if (!px) return;
        ri.Memory = px; ri.BytesPerRow = (WORD)(w * 3); ri.pad = 0; ri.RGBFormat = RGBFB_R8G8B8;
        if (P96Base) p96ReadPixelArray(&ri, 0, 0, grp, (ULONG)x, (ULONG)y, (ULONG)w, (ULONG)h);
        else cgx_read(px, 0, 0, (ULONG)(w * 3), grp, (ULONG)x, (ULONG)y, (ULONG)w, (ULONG)h, RECTFMT_RGB);
        for (j = 0; j < h; j++) {
            const unsigned char *a = cov + (sy + j) * cw_ + sx;
            unsigned char *p = px + j * w * 3;
            for (i = 0; i < w; i++, p += 3) {
                int k, al = a[i];
                if (!al) continue;
                for (k = 0; k < 3; k++) p[k] = (unsigned char)(p[k] + (fg[k] - p[k]) * al / 255);
            }
        }
        if (P96Base) p96WritePixelArray(&ri, 0, 0, grp, (ULONG)x, (ULONG)y, (ULONG)w, (ULONG)h);
        else cgx_write(px, 0, 0, (ULONG)(w * 3), grp, (ULONG)x, (ULONG)y, (ULONG)w, (ULONG)h, RECTFMT_RGB);
        free(px);
        return;
    }
    {
        /* 256 colours: the pens read with ReadPixelArray8, blended, the
         * nearest pens written back, 16 rows at a time */
        static struct RastPort trp;
        static struct BitMap tbm;
        PLANEPTR pl[8];
        long tw = (w + 15) & ~15L, r;
        int k, dpt = depth > 8 ? 8 : depth, ok = 1, fp = PEN(pen);
        unsigned char *band = (unsigned char *)malloc((size_t)(tw * 16));
        if (!band) return;
        if (!pal_cube) pal_cube = (UWORD *)calloc(32768, sizeof(UWORD));
        if (!pal_cube) { free(band); return; }
        InitBitMap(&tbm, (BYTE)dpt, (UWORD)tw, 1);
        for (k = 0; k < dpt; k++) { pl[k] = AllocRaster((ULONG)tw, 1); if (!pl[k]) ok = 0; tbm.Planes[k] = pl[k]; }
        if (ok) {
            InitRastPort(&trp);
            trp.BitMap = &tbm;
            for (r = 0; r < h; r += 16) {
                long n = h - r < 16 ? h - r : 16;
                ReadPixelArray8(grp, (ULONG)x, (ULONG)(y + r), (ULONG)(x + w - 1), (ULONG)(y + r + n - 1), band, &trp);
                for (j = 0; j < n; j++) {
                    const unsigned char *a = cov + (sy + r + j) * cw_ + sx;
                    unsigned char *p = band + j * tw;
                    for (i = 0; i < w; i++) {
                        int al = a[i], b0, c[3], q;
                        if (!al) continue;
                        if (al >= 250) { p[i] = (unsigned char)fp; continue; }
                        b0 = p[i];
                        for (q = 0; q < 3; q++) c[q] = pal_rgb[b0][q] + (fg[q] - pal_rgb[b0][q]) * al / 255;
                        p[i] = (unsigned char)pal_nearest(c[0], c[1], c[2]);
                    }
                }
                WritePixelArray8(grp, (ULONG)x, (ULONG)(y + r), (ULONG)(x + w - 1), (ULONG)(y + r + n - 1), band, &trp);
            }
        }
        for (k = 0; k < dpt; k++) if (pl[k]) FreeRaster(pl[k], (ULONG)tw, 1);
        free(band);
    }
}

/* is there a graphics card (Picasso96 or CyberGraphX) for high and true
 * colour screens? */
int rtg_available(void)
{
    struct Library *b;
    if (P96Base || CyberGfxBase) return 1;
    b = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
    if (!b) b = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (b) CloseLibrary(b);
    return b != NULL;
}
