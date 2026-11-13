/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemtheme.c - colour themes for EdWord's own furniture: the toolbars (the
 *              document's, the preview's and the drawing window's), the
 *              rulers and the desk around the pages
 *
 * Only on screens of 256 colours or more. A theme has six colours, set in
 * pens 250-255, which the AES doesn't use (its colours are pens 0-15 and
 * stay as they are), and which colour pictures leave alone. The pens are
 * put back as they were when EdWord quits or the theme is Atari (none).
 * The toolbars' boxes and buttons are then user-defined objects that EdWord
 * draws in those pens; the icons on them are still the AES's images. The
 * dialogs, the menus and the windows' frames are the AES's own and keep
 * its colours. The documents are drawn as before.
 */
#include <gem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gemui.h"
#include "edrsc.h"

/* the themes: Atari (none) and the Windows 95 schemes under other names
 * (Classic is Windows Standard, Terracotta Brick, Dune Desert, Aubergine
 * Eggplant, Lavender Lilac, Sycamore Maple, Nautical Marine, Damson Plum,
 * Squash Pumpkin, Drizzle Rainy Day, Blush Rose, Shale Slate, Pine Spruce,
 * Tempest Storm, Lagoon Teal, Harvest Wheat); colours
 * 0-255 for the face, the light and dark edges, the text, the ruler
 * margins and the desk around the pages */
typedef struct { const char *name; unsigned char c[TH_NROLES][3]; } THEMEDEF;
static const THEMEDEF themes[NTHEMES] = {
    { "Atari",            { { 255, 255, 255 }, { 255, 255, 255 }, { 128, 128, 128 }, { 0, 0, 0 }, { 192, 192, 192 }, { 128, 128, 128 } } },
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

int theme_id;                                  /* 0 Atari (none), THEME_CUSTOM: edited */
unsigned char theme_rgb[TH_NROLES][3];
static short saved[TH_NROLES][3];
static int   saved_ok, can, rgb_read, direct;

/* On a screen without a palette (high and true colour) a pen's colour
 * only counts when something is drawn, so each document window can have
 * a theme of its own: its colours are put in the pens before EdWord draws
 * in it (theme_for) and everyone's after (theme_global). A palette
 * screen shows every pen's new colour at once, so there one theme is for
 * all. */
typedef struct { int id; unsigned char rgb[TH_NROLES][3]; } WTHEME;
static WTHEME wth[MAX_DOCS];                     /* id -1: as the others */
static int    ctx = -1;                          /* the window being drawn, -1 none */
static unsigned char loaded[TH_NROLES][3];
static int    loaded_ok;

const char *theme_name(int id) { return id >= 0 && id < NTHEMES ? themes[id].name : ""; }
int theme_possible(void) { return can; }
int theme_per_window(void) { return can && direct; }
int theme_on(void)
{
    if (!can) return 0;
    if (ctx >= 0) return wth[ctx].id != 0;
    return theme_id != 0;
}

static void load_pens(unsigned char rgb[TH_NROLES][3])
{
    short c[3];
    int i, k;
    if (loaded_ok && !memcmp(loaded, rgb, sizeof loaded)) return;
    for (i = 0; i < TH_NROLES; i++) {
        for (k = 0; k < 3; k++) c[k] = (short)(rgb[i][k] * 1000L / 255);
        vs_color(vhandle, TH_PEN0 + i, c);
    }
    memcpy(loaded, rgb, sizeof loaded);
    loaded_ok = 1;
}

/* drawing in a document window: its own theme, if it has one */
void theme_for(int slot)
{
    if (!direct || slot < 0 || slot >= MAX_DOCS || wth[slot].id < 0) { theme_global(); return; }
    ctx = slot;
    if (wth[slot].id) load_pens(wth[slot].rgb);
}

/* ... and done: the theme for all again */
void theme_global(void)
{
    if (ctx < 0) return;
    ctx = -1;
    if (theme_on()) load_pens(theme_rgb);
}

/* the theme of a document's window: id and colours, or -1 for the one
 * for all */
int theme_window(int slot, unsigned char rgb[TH_NROLES][3])
{
    if (slot < 0 || slot >= MAX_DOCS) return -1;
    if (rgb && wth[slot].id >= 0) memcpy(rgb, wth[slot].rgb, sizeof wth[slot].rgb);
    return wth[slot].id;
}

void theme_set_window(int slot, int id, unsigned char rgb[TH_NROLES][3])
{
    if (slot < 0 || slot >= MAX_DOCS) return;
    wth[slot].id = id;
    if (rgb) memcpy(wth[slot].rgb, rgb, sizeof wth[slot].rgb);
}
int theme_pen(int role) { return TH_PEN0 + role; }

void theme_pick(int id)
{
    if (id < 0 || id >= NTHEMES) return;
    theme_id = id;
    memcpy(theme_rgb, themes[id].c, sizeof theme_rgb);
}

/* the theme's colours in its pens, or the pens back as they were */
void theme_apply(void)
{
    short rgb[3];
    int i, k;
    if (!can) return;
    loaded_ok = 0;
    ctx = -1;
    for (i = 0; i < TH_NROLES; i++) {
        if (theme_on()) for (k = 0; k < 3; k++) rgb[k] = (short)(theme_rgb[i][k] * 1000L / 255);
        else for (k = 0; k < 3; k++) rgb[k] = saved[i][k];
        vs_color(vhandle, TH_PEN0 + i, rgb);
    }
}

/* the colours being chosen in the pens, for the theme dialog's picture,
 * the Atari look's too (theme_apply puts them right afterwards) */
void theme_preview_pens(void)
{
    short rgb[3];
    int i, k;
    if (!can) return;
    loaded_ok = 0;
    ctx = -1;
    for (i = 0; i < TH_NROLES; i++) {
        for (k = 0; k < 3; k++) rgb[k] = (short)(theme_rgb[i][k] * 1000L / 255);
        vs_color(vhandle, TH_PEN0 + i, rgb);
    }
}

/* at start-up, after the preferences: 256 colours or more */
void theme_init(void)
{
    int i;
    short ext[57];
    if (!rgb_read && theme_id >= 0 && theme_id < NTHEMES) theme_pick(theme_id);
    /* 256 colours or more: by the pens or, as some VDIs report high and
     * true colour, by the planes */
    vq_extnd(vhandle, 1, ext);
    can = npens >= 256 || ext[4] >= 8;
    direct = ext[4] > 8;                         /* high or true colour: no palette */
    for (i = 0; i < MAX_DOCS; i++) wth[i].id = -1;
    edlog("themes: %d pens, %d planes: %s%s", npens, ext[4], can ? "yes" : "no", direct ? ", per window" : "");
    if (!can) return;
    for (i = 0; i < TH_NROLES; i++) vq_color(vhandle, TH_PEN0 + i, 1, saved[i]);
    saved_ok = 1;
    if (theme_id) theme_apply();
}

void theme_exit(void)
{
    int i;
    if (!saved_ok) return;
    for (i = 0; i < TH_NROLES; i++) vs_color(vhandle, TH_PEN0 + i, saved[i]);
}

/* the preferences: "theme=N" and "themergb=" six R,G,B triples */
void theme_load(const char *key, const char *val)
{
    if (!strcmp(key, "theme")) {
        int v = atoi(val);
        if (v >= 0 && v < NTHEMES) theme_pick(v);
        else if (v == THEME_CUSTOM) theme_id = THEME_CUSTOM;
    } else if (!strcmp(key, "themergb")) {
        int i, k, v;
        rgb_read = 1;
        const char *p = val;
        for (i = 0; i < TH_NROLES; i++)
            for (k = 0; k < 3; k++) {
                v = atoi(p);
                theme_rgb[i][k] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
                while (*p && *p != ',') p++;
                if (*p) p++;
            }
    }
}

void theme_save(void *fp)
{
    FILE *f = (FILE *)fp;
    int i;
    fprintf(f, "theme=%d\nthemergb=", theme_id);
    for (i = 0; i < TH_NROLES; i++)
        fprintf(f, "%d,%d,%d%s", theme_rgb[i][0], theme_rgb[i][1], theme_rgb[i][2], i + 1 < TH_NROLES ? "," : "\n");
}

/* ---- drawing ---------------------------------------------------------- */

static void box(short x, short y, short w, short h, int pen)
{
    short p[4];
    if (w <= 0 || h <= 0) return;
    p[0] = x; p[1] = y; p[2] = (short)(x + w - 1); p[3] = (short)(y + h - 1);
    vswr_mode(vhandle, MD_REPLACE);
    vsf_interior(vhandle, FIS_SOLID);
    vsf_color(vhandle, pen);
    vr_recfl(vhandle, p);
}

/* a raised (or, pressed, sunken) button face with its two edges */
void theme_bevel(short x, short y, short w, short h, int pressed)
{
    int lt = theme_pen(pressed ? TH_DARK : TH_LIGHT), dk = theme_pen(pressed ? TH_LIGHT : TH_DARK);
    box(x, y, w, h, theme_pen(pressed ? TH_MARGIN : TH_FACE));
    box(x, y, w, 1, lt); box(x, y, 1, h, lt);
    box(x, (short)(y + h - 1), w, 1, dk); box((short)(x + w - 1), y, 1, h, dk);
}

static void clip_on(PARMBLK *pb)
{
    short c[4];
    c[0] = pb->pb_xc; c[1] = pb->pb_yc; c[2] = (short)(pb->pb_xc + pb->pb_wc - 1); c[3] = (short)(pb->pb_yc + pb->pb_hc - 1);
    vs_clip(vhandle, 1, c);
}

/* the theme dialog's picture of a document window in the theme: a
 * toolbar, the ruler with its margins, a page on the desk and the font
 * line at the foot (the pens hold the colours being chosen) */
static void preview(short x, short y, short w, short h)
{
    int face = theme_pen(TH_FACE), text = theme_pen(TH_TEXT), marg = theme_pen(TH_MARGIN);
    short bh = (short)(h / 6), rh = (short)(h / 10), sh = (short)(h / 9), i, bx, px, pw, py, ph, ly;
    if (bh < 8) bh = 8;
    if (rh < 5) rh = 5;
    if (sh < 6) sh = 6;
    box(x, y, w, h, 1);                                       /* the frame */
    x++; y++; w -= 2; h -= 2;
    /* the toolbar: bevelled buttons, a mark on each */
    box(x, y, w, bh, face);
    for (i = 0, bx = (short)(x + 3); bx + bh <= x + w - 3 && i < 9; i++, bx = (short)(bx + bh + (i % 3 ? 1 : 5))) {
        theme_bevel(bx, (short)(y + 2), (short)(bh - 3), (short)(bh - 4), i == 1);
        box((short)(bx + (bh - 3) / 3), (short)(y + 2 + (bh - 4) / 3), (short)((bh - 3) / 3), (short)((bh - 4) / 3), text);
    }
    box(x, (short)(y + bh - 1), w, 1, theme_pen(TH_DARK));
    y = (short)(y + bh); h = (short)(h - bh);
    /* the ruler: the margins at the ends, ticks along it */
    box(x, y, w, rh, face);
    box(x, y, (short)(w / 8), rh, marg);
    box((short)(x + w - w / 8), y, (short)(w / 8), rh, marg);
    for (bx = (short)(x + w / 8); bx < x + w - w / 8; bx = (short)(bx + 6))
        box(bx, (short)(y + rh - ((bx - x) % 24 < 6 ? rh / 2 + 1 : 2)), 1, (short)(rh / 2 + 1), text);
    box(x, (short)(y + rh - 1), w, 1, text);
    y = (short)(y + rh); h = (short)(h - rh);
    /* the font line at the foot */
    box(x, (short)(y + h - sh), w, sh, face);
    box(x, (short)(y + h - sh), w, 1, theme_pen(TH_DARK));
    box((short)(x + 4), (short)(y + h - sh + sh / 2), (short)(w / 3), 1, text);
    box((short)(x + 8 + w / 3), (short)(y + h - sh + sh / 2), (short)(w / 6), 1, text);
    h = (short)(h - sh);
    /* the desk and a page on it, with lines of text */
    box(x, y, w, h, theme_pen(TH_DESK));
    px = (short)(x + w / 8); pw = (short)(w - w / 4);
    py = (short)(y + 4); ph = (short)(h - 4);
    box((short)(px + 2), (short)(py + 2), pw, ph, 1);         /* its shadow */
    box(px, py, pw, ph, 0);
    for (ly = (short)(py + 5), i = 0; ly < py + ph - 2; ly = (short)(ly + 5), i++)
        box((short)(px + 5), ly, (short)(i % 4 == 3 ? pw / 2 : pw - 10), 1, 1);
}

/* v_gtext for the user-defined objects. The AES calls them on its own
 * stack, which is small (MultiTOS's especially), and gemlib's v_gtext
 * keeps its 2 KB character array on the stack: under MultiTOS that ran
 * over into the AES's own data and crashed it later. So the arrays are
 * static here, and the text short. */
static void small_gtext(short x, short y, const char *s)
{
    static short contrl[12], intin[64], ptsin[4], intout[4], ptsout[4];
    VDIPB pb;
    short n = 0;
    while (s[n] && n < 64) { intin[n] = (unsigned char)s[n]; n++; }
    contrl[0] = 8; contrl[1] = 1; contrl[3] = n; contrl[6] = vhandle;
    ptsin[0] = x; ptsin[1] = y;
    pb.control = contrl; pb.intin = intin; pb.ptsin = ptsin; pb.intout = intout; pb.ptsout = ptsout;
    vdi(&pb);
}

/* the user-defined objects: ub_parm 0 a bar's background, 1 a button
 * (its icons are its children), 2 the theme dialog's picture, else a
 * button with that text */
static short __CDECL theme_draw(PARMBLK *pb)
{
    clip_on(pb);
    if (pb->pb_parm == 0) box(pb->pb_x, pb->pb_y, pb->pb_w, pb->pb_h, theme_pen(TH_FACE));
    else if (pb->pb_parm == 2) preview(pb->pb_x, pb->pb_y, pb->pb_w, pb->pb_h);
    else {
        theme_bevel(pb->pb_x, pb->pb_y, pb->pb_w, pb->pb_h, (pb->pb_currstate & OS_SELECTED) != 0);
        if (pb->pb_parm != 1) {
            const char *s = (const char *)pb->pb_parm;
            short d, sel = (short)((pb->pb_currstate & OS_SELECTED) != 0);
            short x = (short)(pb->pb_x + (pb->pb_w - (short)strlen(s) * wchar) / 2 + sel);
            short y = (short)(pb->pb_y + (pb->pb_h - hchar) / 2 + sel);
            set_sys_font();
            vst_alignment(vhandle, 0, 5, &d, &d);        /* the top of the text */
            vswr_mode(vhandle, MD_TRANS);
            vst_color(vhandle, theme_pen(TH_TEXT));
            small_gtext(x, y, s);
            vst_alignment(vhandle, 0, 0, &d, &d);
            vst_color(vhandle, 1);
        }
    }
    vswr_mode(vhandle, MD_REPLACE);
    vs_clip(vhandle, 0, NULL);
    /* the AES greys a disabled one itself */
    return (short)(pb->pb_currstate & OS_DISABLED);
}

/* make o one of ours: background (0), button with children (1), or a
 * button with text; ub must last as long as the tree is drawn */
void theme_userdef(OBJECT *o, USERBLK *ub, long parm)
{
    ub->ub_code = theme_draw;
    ub->ub_parm = parm;
    o->ob_type = G_USERDEF;
    o->ob_spec.userblk = ub;
    o->ob_flags &= ~OF_FL3DMASK;
}
