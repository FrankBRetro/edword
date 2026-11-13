/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemtool.c - the non-modal tool windows: font selector and print preview
 *
 * The font selector is an object tree (with the sample drawn by the VDI);
 * the preview is drawn with the VDI, as a live page. They act on the
 * active document: the document window that was last on top.
 */
#include <gem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gemui.h"
#include "edrsc.h"

/* ================================================================= */
/*  Shared drawing helpers                                            */
/* ================================================================= */

static void setr(GRECT *r, long x, long y, long w, long h)
{
    r->g_x = (short)x; r->g_y = (short)y; r->g_w = (short)w; r->g_h = (short)h;
}

static int in_r(const GRECT *r, short x, short y)
{
    return x >= r->g_x && y >= r->g_y && x < r->g_x + r->g_w && y < r->g_y + r->g_h;
}

static int rc_and(const GRECT *a, GRECT *b)
{
    short x1 = a->g_x > b->g_x ? a->g_x : b->g_x;
    short y1 = a->g_y > b->g_y ? a->g_y : b->g_y;
    short x2 = (a->g_x + a->g_w < b->g_x + b->g_w) ? a->g_x + a->g_w : b->g_x + b->g_w;
    short y2 = (a->g_y + a->g_h < b->g_y + b->g_h) ? a->g_y + a->g_h : b->g_y + b->g_h;
    b->g_x = x1; b->g_y = y1; b->g_w = x2 - x1; b->g_h = y2 - y1;
    return x2 > x1 && y2 > y1;
}

static void clip_to(const GRECT *r)
{
    short p[4];
    p[0] = r->g_x; p[1] = r->g_y; p[2] = r->g_x + r->g_w - 1; p[3] = r->g_y + r->g_h - 1;
    vs_clip(vhandle, 1, p);
}

static void t_fill(const GRECT *r, int col, int pattern)
{
    short p[4];
    p[0] = r->g_x; p[1] = r->g_y; p[2] = r->g_x + r->g_w - 1; p[3] = r->g_y + r->g_h - 1;
    vswr_mode(vhandle, MD_REPLACE);
    vsf_interior(vhandle, pattern ? FIS_PATTERN : FIS_SOLID);
    if (pattern) vsf_style(vhandle, pattern);
    vsf_color(vhandle, col);
    vr_recfl(vhandle, p);
    vsf_interior(vhandle, FIS_SOLID);
}

static void t_box(const GRECT *r)
{
    short p[10];
    p[0] = p[6] = p[8] = r->g_x; p[2] = p[4] = r->g_x + r->g_w - 1;
    p[1] = p[3] = p[9] = r->g_y; p[5] = p[7] = r->g_y + r->g_h - 1;
    vsl_color(vhandle, 1); vsl_type(vhandle, 1); vsl_width(vhandle, 1);
    v_pline(vhandle, 5, p);
}

/* walk a window's rectangle list and call draw for each piece */
static void t_redraw(short wh, const GRECT *area, void (*draw)(const GRECT *clip))
{
    GRECT r, a = *area, scr;
    scr.g_x = 0; scr.g_y = 0; scr.g_w = scr_w; scr.g_h = scr_h;
    if (!rc_and(&scr, &a)) return;
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    wind_get(wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        if (rc_and(&a, &r)) {
            clip_to(&r);
            set_sys_font();
            vswr_mode(vhandle, MD_TRANS);
            draw(&r);
            vs_clip(vhandle, 0, NULL);
            vswr_mode(vhandle, MD_REPLACE);
            style_reset();
        }
        wind_get(wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
}

static void t_redraw_all(short wh, void (*draw)(const GRECT *clip))
{
    GRECT w;
    wind_get(wh, WF_WORKXYWH, &w.g_x, &w.g_y, &w.g_w, &w.g_h);
    t_redraw(wh, &w, draw);
}

static int is_top(short wh)
{
    short h, d1, d2, d3;
    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    return h == wh;
}

static void wait_release(void)
{
    short x, y, b, k;
    do graf_mkstate(&x, &y, &b, &k); while (b & 1);
}

/* ================================================================= */
/*  Font selector                                                     */
/* ================================================================= */

#define FSKIND (NAME | CLOSER | MOVER)
#define FROWS  9
static const short fsz[] = { 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 60, 72 };
#define NFSZ ((int)(sizeof fsz / sizeof fsz[0]))

static struct { int open; short wh; int font, size, attr, ftop, stop; char name[40]; } fs = { 0, -1, 0, 0, 0, 0, 0, "" };

static void fs_scroll_into_view(void)
{
    int i, si = -1;
    if (fs.font < fs.ftop) fs.ftop = fs.font;
    if (fs.font >= fs.ftop + FROWS) fs.ftop = fs.font - FROWS + 1;
    if (fs.ftop > nfonts - FROWS) fs.ftop = nfonts - FROWS;
    if (fs.ftop < 0) fs.ftop = 0;
    for (i = 0; i < NFSZ; i++) if (fsz[i] == fs.size) si = i;
    if (si >= 0) {
        if (si < fs.stop) fs.stop = si;
        if (si >= fs.stop + FROWS) fs.stop = si - FROWS + 1;
    }
    if (fs.stop > NFSZ - FROWS) fs.stop = NFSZ - FROWS;
    if (fs.stop < 0) fs.stop = 0;
}

/* The window holds an AES object tree: lists of strings, arrow and
 * scroll bar objects, check boxes and buttons, in the AES's own look (3D
 * and MagiC's check boxes where it has them) and with its feedback. Only
 * the sample of the font is drawn with the VDI. */

enum { FO_ROOT, FO_LFONT, FO_LSIZE, FO_LSTYLE, FO_FBOX, FO_FROW0,
       FO_FUP = FO_FROW0 + FROWS, FO_FDN, FO_FTRK, FO_FTHUMB, FO_SBOX, FO_SROW0,
       FO_SUP = FO_SROW0 + FROWS, FO_SDN, FO_ST0, FO_PV = FO_ST0 + 3, FO_APPLY, FO_CLOSE, FO_N };
static OBJECT ft[FO_N];
static char frow[FROWS][30], srow[FROWS][8], slabel[40];

/* the font list's scroll bar thumb (relative to the track) */
static void fs_thumb(const GRECT *trk, GRECT *th)
{
    long h = trk->g_h, tw, ty;
    *th = *trk;
    th->g_y = 0;
    th->g_x = 0;
    if (nfonts <= FROWS) return;
    tw = h * FROWS / nfonts;
    if (tw < hchar) tw = hchar;
    ty = (h - tw) * fs.ftop / (nfonts - FROWS);
    th->g_y = (short)ty;
    th->g_h = (short)tw;
}

/* the tree, laid out for the window where it is now, with the current
 * font, size and styles */
typedef char fs_rsc_check[FS_COUNT == FO_N && FS_FUP == FO_FUP && FS_SROW0 == FO_SROW0 && FS_CLOSE == FO_CLOSE ? 1 : -1];

/* the tree from the resource file: the texts that change and the states */
/* the 3D look where the AES has it: a grey window, raised arrows and
 * buttons. 3D objects stand 2 pixels out from their rectangles, so the
 * arrows and the scroll bar move off the lists' edges. The style buttons
 * share one width either way. */
static void fs_3d(void)
{
    static const int sty[3] = { S_BOLD, S_ITALIC, S_UNDERLINE };
    int i;
    short w = 0;
    for (i = 0; i < 3; i++) if ((short)((strlen(L(sty[i])) + 3) * wchar) > w) w = (short)((strlen(L(sty[i])) + 3) * wchar);
    for (i = 0; i < 3; i++) ft[FO_ST0 + i].ob_width = w;
    ft[FO_ROOT].ob_spec.index = 0x00001100L;     /* hollow white: the AES's grey where it has 3D */
    if (!aes_3d) return;
    ft[FO_ROOT].ob_flags |= OF_FL3DBAK;
    for (i = FO_LFONT; i <= FO_LSTYLE; i++) ft[i].ob_flags |= OF_FL3DBAK;
    ft[FO_FUP].ob_flags |= OF_FL3DACT; ft[FO_FDN].ob_flags |= OF_FL3DACT;
    ft[FO_SUP].ob_flags |= OF_FL3DACT; ft[FO_SDN].ob_flags |= OF_FL3DACT;
    ft[FO_FTHUMB].ob_flags |= OF_FL3DACT;
    ft[FO_APPLY].ob_flags |= OF_FL3DACT; ft[FO_CLOSE].ob_flags |= OF_FL3DACT;
    /* the AES greys only hollow white objects: a solid white one stays white */
    ft[FO_FUP].ob_spec.index &= ~0x70L; ft[FO_FDN].ob_spec.index &= ~0x70L;
    ft[FO_SUP].ob_spec.index &= ~0x70L; ft[FO_SDN].ob_spec.index &= ~0x70L;
    ft[FO_FTHUMB].ob_spec.index &= ~0x70L;
    for (i = FO_FUP; i <= FO_FTRK; i++) ft[i].ob_x += 3;
    ft[FO_SUP].ob_x += 3; ft[FO_SDN].ob_x += 3;
    ft[FO_FTRK].ob_y += 2; ft[FO_FTRK].ob_height -= 4;
    {
        GRECT trk, z;
        setr(&trk, 0, 0, ft[FO_FTRK].ob_width, ft[FO_FTRK].ob_height);
        fs_thumb(&trk, &z);
        ft[FO_FTHUMB].ob_x = z.g_x; ft[FO_FTHUMB].ob_y = z.g_y;
        ft[FO_FTHUMB].ob_width = z.g_w; ft[FO_FTHUMB].ob_height = z.g_h;
    }
}

static int fs_tree_rsc(void)
{
    OBJECT *t = rsc_tree(RT_FONTSEL);
    GRECT trk, z;
    int i, k;
    if (!t) return 0;
    memcpy(ft, t, sizeof ft);
    wind_get(fs.wh, WF_WORKXYWH, &ft[FO_ROOT].ob_x, &ft[FO_ROOT].ob_y, &z.g_w, &z.g_h);
    sprintf(slabel, "%s %d", L(S_SIZE), fs.size);
    ft[FO_LSIZE].ob_spec.free_string = slabel;
    ft[FO_LSIZE].ob_width = (short)(strlen(slabel) * wchar);
    for (i = 0; i < FROWS; i++) {
        int f = fs.ftop + i;
        if (f < nfonts) sprintf(frow[i], " %.25s", fonts[f].name); else frow[i][0] = 0;
        ft[FO_FROW0 + i].ob_spec.free_string = frow[i];
        if (f == fs.font) ft[FO_FROW0 + i].ob_state |= OS_SELECTED;
        k = fs.stop + i;
        if (k < NFSZ) sprintf(srow[i], " %3d", fsz[k]); else srow[i][0] = 0;
        ft[FO_SROW0 + i].ob_spec.free_string = srow[i];
        if (k < NFSZ && fsz[k] == fs.size) ft[FO_SROW0 + i].ob_state |= OS_SELECTED;
    }
    /* the scroll bar between its arrows, whatever the font */
    ft[FO_FTRK].ob_y = (short)(ft[FO_FUP].ob_y + ft[FO_FUP].ob_height + 1);
    ft[FO_FTRK].ob_height = (short)(ft[FO_FDN].ob_y - 1 - ft[FO_FTRK].ob_y);
    setr(&trk, 0, 0, ft[FO_FTRK].ob_width, ft[FO_FTRK].ob_height);
    fs_thumb(&trk, &z);
    ft[FO_FTHUMB].ob_y = z.g_y; ft[FO_FTHUMB].ob_height = z.g_h; ft[FO_FTHUMB].ob_width = z.g_w;
    for (i = 0; i < 3; i++) {
        if (fs.attr & (1 << i)) ft[FO_ST0 + i].ob_state |= OS_SELECTED;
        if (aes_magic) ft[FO_ST0 + i].ob_state |= (short)(OS_WHITEBAK | 0x8000 | (0x7F << 8));
        else if (aes_3d) ft[FO_ST0 + i].ob_flags |= OF_FL3DIND;
    }
    fs_3d();
    return 1;
}

static void fs_tree(void) { fs_tree_rsc(); }

static void fs_draw(const GRECT *clip)
{
    struct { GRECT pv; } FL;
    GRECT r;
    fs_tree();
    objc_offset(ft, FO_PV, &FL.pv.g_x, &FL.pv.g_y);
    FL.pv.g_w = ft[FO_PV].ob_width; FL.pv.g_h = ft[FO_PV].ob_height;
    objc_draw(ft, FO_ROOT, 8, clip->g_x, clip->g_y, clip->g_w, clip->g_h);

    /* the sample in the chosen font */
    r = FL.pv;
    r.g_x++; r.g_y++; r.g_w -= 2; r.g_h -= 2;
    if (rc_and(clip, &r)) {
        short ext[8], p[4];
        const char *sample = L(S_FONT_SAMPLE);
        int sz = fs.size, at = fs.attr & (A_BOLD | A_ITALIC);
        long avail = FL.pv.g_h - 6, base;
        const METRICS *m = get_metrics(fs.font, sz, at);
        /* a size too big for the box: shown as large as fits */
        if (m && m->asc + m->desc > avail && m->asc + m->desc > 0) {
            sz = (int)((long)sz * avail / (m->asc + m->desc));
            if (sz < 6) sz = 6;
            m = get_metrics(fs.font, sz, at);
        }
        /* the line in the middle of the box */
        base = m ? FL.pv.g_y + (FL.pv.g_h - m->asc - m->desc) / 2 + m->asc : FL.pv.g_y + FL.pv.g_h * 2 / 3;
        clip_to(&r);
        vswr_mode(vhandle, MD_TRANS);
        set_text_style(fs.font, sz, at, 0);
        v_gtext(vhandle, (short)(FL.pv.g_x + 6), (short)base, sample);
        if (fs.attr & A_UNDER) {
            vqt_extent(vhandle, sample, ext);
            p[0] = (short)(FL.pv.g_x + 6); p[2] = (short)(p[0] + ext[2] - ext[0]);
            p[1] = p[3] = (short)(base + (m && m->desc > 4 ? m->desc / 2 : 2));
            vsl_color(vhandle, 1);
            v_pline(vhandle, 2, p);
        }
        clip_to(clip);
        set_sys_font();
        vswr_mode(vhandle, MD_TRANS);
    }
}

/* redraw one object (and what's in it) through the window's rectangles */
static void fs_redraw_obj(int o)
{
    GRECT r;
    if (!fs.open) return;
    fs_tree();
    objc_offset(ft, (short)o, &r.g_x, &r.g_y);
    r.g_w = ft[o].ob_width; r.g_h = ft[o].ob_height;
    r.g_x -= 3; r.g_y -= 3; r.g_w += 6; r.g_h += 6;           /* the 3D edges */
    t_redraw(fs.wh, &r, fs_draw);
}

static void fs_refresh(void) { if (fs.open) t_redraw_all(fs.wh, fs_draw); }

/* follow the active document: the first selected character, or the
 * typing style when nothing is selected */
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
    fs_scroll_into_view();
    fs_refresh();
}

void fontsel_open(void)
{
    short x, y, w, h;
    long ww, wh;
    if (fs.open) { wind_set(fs.wh, WF_TOP, 0, 0, 0, 0); fs.font = -1; fontsel_sync(); return; }
    if (!rsc_tree(RT_FONTSEL)) { alert(1, 3, AL_NO_RSC, AB_OK); return; }
    ww = rsc_tree(RT_FONTSEL)->ob_width; wh = rsc_tree(RT_FONTSEL)->ob_height;
    wind_calc(WC_BORDER, FSKIND, 0, 0, (short)ww, (short)wh, &x, &y, &w, &h);
    x = (short)(desk.g_x + desk.g_w - w - 8);
    y = (short)(desk.g_y + 8);
    winpos_get(" fontsel ", &x, &y, NULL, NULL);
    winpos_clamp(&x, &y, w, h);
    fs.wh = wind_create(FSKIND, x, y, w, h);
    if (fs.wh < 0) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    sprintf(fs.name, " %s ", L(S_FONT));
    wind_set_str(fs.wh, WF_NAME, fs.name);
    fs.open = 1;
    fs.font = -1;
    wind_open(fs.wh, x, y, w, h);
    fontsel_sync();
}

void fontsel_close(void)
{
    if (!fs.open) return;
    winpos_save(" fontsel ", fs.wh);
    wind_close(fs.wh);
    wind_delete(fs.wh);
    fs.open = 0;
    fs.wh = -1;
}

int fontsel_owns(short wh) { return fs.open && wh == fs.wh; }

static void fs_apply(void)
{
    DOC *d = active_doc();
    int f = fs.font, sz = fs.size, a = fs.attr;     /* applying re-syncs fs */
    if (!d) return;
    apply_style(d, AP_FONT, f);
    apply_style(d, AP_SIZE, sz);
    apply_style(d, AP_ATTRS, a);
}

void fontsel_msg(short *msg)
{
    GRECT r;
    switch (msg[0]) {
    case WM_REDRAW:
        r.g_x = msg[4]; r.g_y = msg[5]; r.g_w = msg[6]; r.g_h = msg[7];
        t_redraw(fs.wh, &r, fs_draw);
        break;
    case WM_TOPPED:
        wind_set(fs.wh, WF_TOP, 0, 0, 0, 0);
        fontsel_sync();
        break;
    case WM_CLOSED:
        fontsel_close();
        break;
    case WM_MOVED:
        wind_set(fs.wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
        break;
    case WM_BOTTOMED:
        wind_set(fs.wh, WF_BOTTOM, 0, 0, 0, 0);
        break;
    }
}

void fontsel_click(short mx, short my, short clicks)
{
    int o, i;
    short x, y, b, k;
    if (!fs.open) return;
    if (!is_top(fs.wh)) { wind_set(fs.wh, WF_TOP, 0, 0, 0, 0); fontsel_sync(); return; }
    fs_tree();
    o = objc_find(ft, FO_ROOT, 8, mx, my);
    if (o >= FO_FROW0 && o < FO_FROW0 + FROWS) {
        i = fs.ftop + o - FO_FROW0;
        if (i < nfonts) { fs.font = i; fs_redraw_obj(FO_FBOX); fs_redraw_obj(FO_PV); if (clicks >= 2) fs_apply(); }
    } else if (o >= FO_SROW0 && o < FO_SROW0 + FROWS) {
        i = fs.stop + o - FO_SROW0;
        if (i < NFSZ) {
            fs.size = fsz[i];
            fs_redraw_obj(FO_SBOX); fs_redraw_obj(FO_LSIZE); fs_redraw_obj(FO_PV);
            if (clicks >= 2) fs_apply();
        }
    } else if (o == FO_FUP || o == FO_FDN || o == FO_SUP || o == FO_SDN) {
        /* pressed while held, and the list scrolls on */
        int fonts_list = o == FO_FUP || o == FO_FDN, dir = o == FO_FUP || o == FO_SUP ? -1 : 1, t = 0;
        ft[o].ob_state |= OS_SELECTED;
        fs_redraw_obj(o);
        do {
            if (fonts_list) { int n = fs.ftop + dir; if (n >= 0 && n <= nfonts - FROWS) { fs.ftop = n; fs_redraw_obj(FO_FBOX); fs_redraw_obj(FO_FTRK); } }
            else { int n = fs.stop + dir; if (n >= 0 && n <= NFSZ - FROWS) { fs.stop = n; fs_redraw_obj(FO_SBOX); } }
            evnt_timer(t++ ? 60L : 300L);
            graf_mkstate(&x, &y, &b, &k);
        } while (b & 1);
        fs_redraw_obj(o);
    } else if (o == FO_FTHUMB && nfonts > FROWS) {
        long pos;
        wind_update(BEG_MCTRL);
        pos = graf_slidebox(ft, FO_FTRK, FO_FTHUMB, 1);
        wind_update(END_MCTRL);
        fs.ftop = (int)(pos * (nfonts - FROWS) / 1000);
        fs_redraw_obj(FO_FBOX); fs_redraw_obj(FO_FTRK);
    } else if (o == FO_FTRK && nfonts > FROWS) {
        short tx, ty;
        objc_offset(ft, FO_FTHUMB, &tx, &ty);
        fs.ftop += my < ty ? -(FROWS - 1) : FROWS - 1;
        if (fs.ftop > nfonts - FROWS) fs.ftop = nfonts - FROWS;
        if (fs.ftop < 0) fs.ftop = 0;
        fs_redraw_obj(FO_FBOX); fs_redraw_obj(FO_FTRK);
    } else if (o >= FO_ST0 && o < FO_ST0 + 3) {
        fs.attr ^= 1 << (o - FO_ST0);
        wait_release();
        fs_redraw_obj(o); fs_redraw_obj(FO_PV);
    } else if (o == FO_APPLY || o == FO_CLOSE) {
        int in;
        wind_update(BEG_UPDATE);
        wind_update(BEG_MCTRL);
        in = graf_watchbox(ft, (short)o, OS_SELECTED, 0);
        wind_update(END_MCTRL);
        wind_update(END_UPDATE);
        ft[o].ob_state &= ~OS_SELECTED;
        fs_redraw_obj(o);
        if (in) { if (o == FO_APPLY) fs_apply(); else fontsel_close(); }
    }
}

void fontsel_key(short ks, short key)
{
    short scan = (key >> 8) & 0xff;
    if (scan == 0x1C || scan == 0x72) fs_apply();
    else if (scan == 0x01) fontsel_close();
    else if (scan == 0x48 && fs.font > 0) { fs.font--; fs_scroll_into_view(); fs_refresh(); }
    else if (scan == 0x50 && fs.font < nfonts - 1) { fs.font++; fs_scroll_into_view(); fs_refresh(); }
    else if (scan == 0x47) { fs.font = (ks & 3) ? nfonts - 1 : 0; fs_scroll_into_view(); fs_refresh(); }
    else if ((key & 0xff) > ' ') {
        /* a letter: the next font whose name starts with it */
        int c = st_upper((unsigned char)(key & 0xff)), i, k;
        for (k = 1; k <= nfonts; k++) {
            i = (fs.font + k) % nfonts;
            if (st_upper((unsigned char)fonts[i].name[0]) == c) { fs.font = i; fs_scroll_into_view(); fs_refresh(); break; }
        }
    }
}

/* ================================================================= */
/*  Print preview                                                     */
/* ================================================================= */

#define PVKIND (NAME | CLOSER | MOVER | SIZER | FULLER | UPARROW | DNARROW | VSLIDE | LFARROW | RTARROW | HSLIDE)

static struct {
    int  open, stale, zoom;          /* zoom 0: whole page, 1: 100% */
    short wh, vsz, vpos, hsz, hpos;
    DOC *d;
    int  page;
    long sx, sy;
    char name[64];
} pv = { 0, 0, 0, -1, -1, -1, -1, -1, NULL, 0, 0, 0, "" };

typedef struct { GRECT w, bar, area, prev, next, zoom, pdf, prt, close; long lx; } PVLAY;

static void pv_layout(PVLAY *Y)
{
    long x, by, bh = hchar + 4;
    wind_get(pv.wh, WF_WORKXYWH, &Y->w.g_x, &Y->w.g_y, &Y->w.g_w, &Y->w.g_h);
    setr(&Y->bar, Y->w.g_x, Y->w.g_y, Y->w.g_w, hchar + 10);
    setr(&Y->area, Y->w.g_x, Y->w.g_y + Y->bar.g_h, Y->w.g_w, Y->w.g_h - Y->bar.g_h);
    long gap = aes_3d ? 6 : 4;             /* 3D buttons stand 2 pixels out */
    by = Y->w.g_y + 3;
    x = Y->w.g_x + gap;
    setr(&Y->prev, x, by, 4 * wchar, bh); x += 4 * wchar + gap;
    setr(&Y->next, x, by, 4 * wchar, bh); x += 4 * wchar + 2 * wchar;
    Y->lx = x; x += ((long)strlen(L(S_PAGE_N_OF)) + 2) * wchar;
    setr(&Y->zoom, x, by, ((long)strlen(L(S_WHOLE_PAGE)) + 2) * wchar, bh); x += Y->zoom.g_w + gap;
    setr(&Y->prt, x, by, ((long)strlen(L(S_PRINT_BTN)) + 2) * wchar, bh); x += Y->prt.g_w + gap;
    setr(&Y->pdf, x, by, ((long)strlen(L(S_EXPORT_PDF)) + 2) * wchar, bh); x += Y->pdf.g_w + gap;
    setr(&Y->close, x, by, ((long)strlen(L(S_CLOSE)) + 2) * wchar, bh);
}

/* page placement: drawn at n/den of its size, top left at ox,oy */
static void pv_geom(const PVLAY *Y, long *n, long *den, long *ox, long *oy)
{
    long W = pt2px_x(PS(pv.d).w), H = pt2px_y(PS(pv.d).h), aw = Y->area.g_w, ah = Y->area.g_h;
    if (pv.zoom == 0) {
        long a = (aw - 16) * 1000L / W, b = (ah - 16) * 1000L / H;
        *den = 1000;
        *n = a < b ? a : b;
        if (*n > 1000) *n = 1000;
        if (*n < 1) *n = 1;
        *ox = Y->area.g_x + (aw - W * *n / 1000) / 2;
        *oy = Y->area.g_y + (ah - H * *n / 1000) / 2;
    } else {
        *n = *den = 1;
        *ox = Y->area.g_x + 8 - pv.sx;
        *oy = Y->area.g_y + 8 - pv.sy;
    }
}

static void pv_clamp(void)
{
    PVLAY Y;
    long W, H;
    int np;
    if (!pv.open) return;
    pv_layout(&Y);
    np = doc_npages(pv.d);
    if (pv.page >= np) pv.page = np - 1;
    if (pv.page < 0) pv.page = 0;
    W = pt2px_x(PS(pv.d).w) + 16; H = pt2px_y(PS(pv.d).h) + 16;
    if (pv.sx > W - Y.area.g_w) pv.sx = W - Y.area.g_w;
    if (pv.sy > H - Y.area.g_h) pv.sy = H - Y.area.g_h;
    if (pv.sx < 0) pv.sx = 0;
    if (pv.sy < 0) pv.sy = 0;
}

static void pv_sliders(void)
{
    PVLAY Y;
    long W, H;
    short sz, pos;
    pv_layout(&Y);
    W = pt2px_x(PS(pv.d).w) + 16; H = pt2px_y(PS(pv.d).h) + 16;
    if (pv.zoom == 0) W = H = 0;
    sz = H > Y.area.g_h ? (short)(Y.area.g_h * 1000L / H) : 1000;
    pos = H > Y.area.g_h ? (short)(pv.sy * 1000L / (H - Y.area.g_h)) : 0;
    if (sz != pv.vsz) { wind_set(pv.wh, WF_VSLSIZE, sz, 0, 0, 0); pv.vsz = sz; }
    if (pos != pv.vpos) { wind_set(pv.wh, WF_VSLIDE, pos, 0, 0, 0); pv.vpos = pos; }
    sz = W > Y.area.g_w ? (short)(Y.area.g_w * 1000L / W) : 1000;
    pos = W > Y.area.g_w ? (short)(pv.sx * 1000L / (W - Y.area.g_w)) : 0;
    if (sz != pv.hsz) { wind_set(pv.wh, WF_HSLSIZE, sz, 0, 0, 0); pv.hsz = sz; }
    if (pos != pv.hpos) { wind_set(pv.wh, WF_HSLIDE, pos, 0, 0, 0); pv.hpos = pos; }
}

static void pv_draw(const GRECT *clip)
{
    PVLAY Y;
    GRECT r, page, sh;
    long n, den, ox, oy, W, H;

    pv_layout(&Y);
    /* button bar */
    r = Y.bar;
    if (rc_and(clip, &r)) {
        /* an AES object tree: the AES's buttons, 3D where it has them */
        static OBJECT t[8];
        static char pg[40];
        const GRECT *b[6];
        const char *lab[6];
        short p[4], i;
        b[0] = &Y.prev; b[1] = &Y.next; b[2] = &Y.zoom; b[3] = &Y.prt; b[4] = &Y.pdf; b[5] = &Y.close;
        lab[0] = "\004"; lab[1] = "\003"; lab[2] = pv.zoom ? L(S_WHOLE_PAGE) : "100%";
        lab[3] = L(S_PRINT_BTN); lab[4] = L(S_EXPORT_PDF); lab[5] = L(S_CLOSE);
        sprintf(pg, L(S_PAGE_N_OF), pv.page + 1, doc_npages(pv.d));
        memset(t, 0, sizeof t);
        t[0].ob_next = -1; t[0].ob_head = 1; t[0].ob_tail = 7;
        t[0].ob_type = G_BOX; t[0].ob_spec.index = 0x00001100L;
        t[0].ob_flags = (short)(aes_3d ? OF_FL3DBAK : 0);
        t[0].ob_x = Y.bar.g_x; t[0].ob_y = Y.bar.g_y; t[0].ob_width = Y.bar.g_w; t[0].ob_height = (short)(Y.bar.g_h - 1);
        for (i = 0; i < 6; i++) {
            OBJECT *o = &t[i + 1];
            o->ob_next = (short)(i + 2); o->ob_head = o->ob_tail = -1;
            o->ob_type = G_BUTTON; o->ob_spec.free_string = (char *)lab[i];
            o->ob_flags = (short)(OF_SELECTABLE | (aes_3d ? OF_FL3DACT : 0));
            o->ob_x = (short)(b[i]->g_x - Y.bar.g_x); o->ob_y = (short)(b[i]->g_y - Y.bar.g_y);
            o->ob_width = b[i]->g_w; o->ob_height = b[i]->g_h;
        }
        t[7].ob_next = 0; t[7].ob_head = t[7].ob_tail = -1;
        t[7].ob_type = G_STRING; t[7].ob_spec.free_string = pg;
        t[7].ob_flags = (short)(OF_LASTOB | (aes_3d ? OF_FL3DBAK : 0));
        t[7].ob_x = (short)(Y.lx - Y.bar.g_x); t[7].ob_y = (short)((Y.bar.g_h - hchar) / 2);
        t[7].ob_width = (short)(strlen(pg) * wchar); t[7].ob_height = hchar;
        if (theme_on()) {                   /* drawn in the colour theme's colours */
            static USERBLK ub[7];
            theme_userdef(&t[0], &ub[0], 0);
            for (i = 0; i < 6; i++) theme_userdef(&t[i + 1], &ub[i + 1], (long)lab[i]);
            t[7].ob_flags &= ~OF_FL3DMASK;
        }
        objc_draw(t, 0, 1, r.g_x, r.g_y, r.g_w, r.g_h);
        p[0] = Y.bar.g_x; p[2] = Y.bar.g_x + Y.bar.g_w - 1;
        p[1] = p[3] = Y.bar.g_y + Y.bar.g_h - 1;
        v_pline(vhandle, 2, p);
    }
    /* the desk behind the page */
    r = Y.area;
    if (!rc_and(clip, &r)) return;
    clip_to(&r);
    if (theme_on()) t_fill(&Y.area, theme_pen(TH_DESK), 0);       /* the desk around the page */
    else t_fill(&Y.area, 1, 2);
    pv_geom(&Y, &n, &den, &ox, &oy);
    W = pt2px_x(PS(pv.d).w) * n / den;
    H = pt2px_y(PS(pv.d).h) * n / den;
    setr(&sh, ox + 4, oy + 4, W, H);
    t_fill(&sh, 1, 0);                       /* shadow */
    setr(&page, ox, oy, W, H);
    t_fill(&page, 0, 0);
    t_box(&page);
    page.g_x++; page.g_y++; page.g_w -= 2; page.g_h -= 2;
    if (rc_and(&r, &page)) {
        clip_to(&page);
        vswr_mode(vhandle, MD_TRANS);
        draw_preview_page(pv.d, pv.page, ox, oy, n, den);
    }
}

static void pv_refresh(void) { if (pv.open) { pv_clamp(); t_redraw_all(pv.wh, pv_draw); pv_sliders(); } }

static void pv_set_name(void)
{
    sprintf(pv.name, " %s: %.30s ", L(S_PREVIEW), pv.d->base);
    wind_set_str(pv.wh, WF_NAME, pv.name);
}

void preview_open(DOC *d)
{
    short x;
    long W, H, ww, wh;
    if (!d) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    doc_layout(d);
    pv.d = d;
    pv.page = d->lines[doc_find_line(d, d->cursor)].page;
    if (pv.open) {
        wind_set(pv.wh, WF_TOP, 0, 0, 0, 0);
        pv_set_name();
        pv_refresh();
        return;
    }
    pv.zoom = 0;
    pv.sx = pv.sy = 0;
    W = pt2px_x(PS(d).w); H = pt2px_y(PS(d).h);
    wh = desk.g_h - 40;
    ww = (wh - hchar - 30) * W / H + 24;
    {   /* room for the button bar */
        long need = (4 + 4 + 2 + (long)strlen(L(S_PAGE_N_OF)) + 2 + (long)strlen(L(S_WHOLE_PAGE)) + 2 +
                     (long)strlen(L(S_PRINT_BTN)) + 2 + (long)strlen(L(S_EXPORT_PDF)) + 2 + (long)strlen(L(S_CLOSE)) + 2) * wchar + 40;
        if (ww < need) ww = need;
    }
    if (ww > desk.g_w - 20) ww = desk.g_w - 20;
    pv.wh = wind_create(PVKIND, desk.g_x, desk.g_y, desk.g_w, desk.g_h);
    if (pv.wh < 0) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    pv.open = 1;
    pv.stale = 0;
    pv.vsz = pv.vpos = pv.hsz = pv.hpos = -1;
    pv_set_name();
    x = (short)(desk.g_x + desk.g_w - ww - 10);
    {
        short y = (short)(desk.g_y + 10), w = (short)ww, h = (short)wh;
        if (winpos_get(" preview ", &x, &y, &w, &h)) winpos_clamp(&x, &y, w, h);
        wind_open(pv.wh, x, y, w, h);
    }
    pv_clamp();
    pv_sliders();
}

void preview_close(void)
{
    if (!pv.open) return;
    winpos_save(" preview ", pv.wh);
    wind_close(pv.wh);
    wind_delete(pv.wh);
    pv.open = 0;
    pv.wh = -1;
}

int  preview_owns(short wh) { return pv.open && wh == pv.wh; }

/* the preview's work area, for centring the export gauge on it */
int preview_area(GRECT *r)
{
    if (!pv.open) return 0;
    wind_get(pv.wh, WF_WORKXYWH, &r->g_x, &r->g_y, &r->g_w, &r->g_h);
    return 1;
}
DOC *preview_doc(void) { return pv.open ? pv.d : NULL; }
int  preview_page(void) { return pv.page; }

void preview_changed(DOC *d) { if (pv.open && pv.d == d) pv.stale = 1; }

void preview_doc_closed(DOC *d) { if (pv.open && pv.d == d) preview_close(); }

/* redraw a stale preview once the user pauses */
int preview_idle(int run)
{
    if (!pv.open || !pv.stale) return 0;
    if (run) {
        pv.stale = 0;
        doc_layout(pv.d);
        pv_set_name();
        pv_refresh();
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
    pv_refresh();
}

void preview_msg(short *msg)
{
    GRECT r;
    PVLAY Y;
    long W, H;
    short x, y, w, h;
    switch (msg[0]) {
    case WM_REDRAW:
        r.g_x = msg[4]; r.g_y = msg[5]; r.g_w = msg[6]; r.g_h = msg[7];
        t_redraw(pv.wh, &r, pv_draw);
        return;
    case WM_TOPPED:
        wind_set(pv.wh, WF_TOP, 0, 0, 0, 0);
        return;
    case WM_CLOSED:
        preview_close();
        return;
    case WM_BOTTOMED:
        wind_set(pv.wh, WF_BOTTOM, 0, 0, 0, 0);
        return;
    case WM_MOVED:
        wind_set(pv.wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
        return;
    case WM_SIZED:
        w = msg[6] < 52 * wchar ? (short)(52 * wchar) : msg[6];
        h = msg[7] < 160 ? 160 : msg[7];
        wind_set(pv.wh, WF_CURRXYWH, msg[4], msg[5], w, h);
        pv_refresh();
        return;
    case WM_FULLED: {
        short cx, cy, cw, ch;
        wind_get(pv.wh, WF_CURRXYWH, &cx, &cy, &cw, &ch);
        wind_get(pv.wh, WF_FULLXYWH, &x, &y, &w, &h);
        if (cx == x && cy == y && cw == w && ch == h) wind_get(pv.wh, WF_PREVXYWH, &x, &y, &w, &h);
        wind_set(pv.wh, WF_CURRXYWH, x, y, w, h);
        pv_refresh();
        return;
    }
    }
    pv_layout(&Y);
    W = pt2px_x(PS(pv.d).w) + 16; H = pt2px_y(PS(pv.d).h) + 16;
    if (msg[0] == WM_ARROWED) {
        switch (msg[4]) {
        case WA_UPPAGE: if (pv.zoom) pv.sy -= Y.area.g_h - hchar; else { pv_goto(pv.page - 1); return; } break;
        case WA_DNPAGE: if (pv.zoom) pv.sy += Y.area.g_h - hchar; else { pv_goto(pv.page + 1); return; } break;
        case WA_UPLINE: pv.sy -= 2 * hchar; break;
        case WA_DNLINE: pv.sy += 2 * hchar; break;
        case WA_LFPAGE: pv.sx -= Y.area.g_w - wchar; break;
        case WA_RTPAGE: pv.sx += Y.area.g_w - wchar; break;
        case WA_LFLINE: pv.sx -= 4 * wchar; break;
        case WA_RTLINE: pv.sx += 4 * wchar; break;
        }
    } else if (msg[0] == WM_VSLID) pv.sy = H > Y.area.g_h ? (long)msg[4] * (H - Y.area.g_h) / 1000L : 0;
    else if (msg[0] == WM_HSLID) pv.sx = W > Y.area.g_w ? (long)msg[4] * (W - Y.area.g_w) / 1000L : 0;
    else return;
    if (!pv.zoom) return;
    pv_refresh();
}

void preview_click(short mx, short my, short clicks)
{
    PVLAY Y;
    (void)clicks;
    if (!pv.open) return;
    if (!is_top(pv.wh)) { wind_set(pv.wh, WF_TOP, 0, 0, 0, 0); return; }
    pv_layout(&Y);
    if (in_r(&Y.prev, mx, my)) pv_goto(pv.page - 1);
    else if (in_r(&Y.next, mx, my)) pv_goto(pv.page + 1);
    else if (in_r(&Y.zoom, mx, my) || in_r(&Y.area, mx, my)) {
        if (!pv.zoom && in_r(&Y.area, mx, my)) {
            /* zoom in around the click */
            long n, den, ox, oy;
            pv_geom(&Y, &n, &den, &ox, &oy);
            pv.sx = (mx - ox) * den / n - Y.area.g_w / 2;
            pv.sy = (my - oy) * den / n - Y.area.g_h / 2;
        } else if (!pv.zoom) pv.sx = pv.sy = 0;
        pv.zoom = !pv.zoom;
        wait_release();
        pv_refresh();
    } else if (in_r(&Y.pdf, mx, my)) { wait_release(); cmd_pdf(pv.d); }
    else if (in_r(&Y.prt, mx, my)) { wait_release(); cmd_print(pv.d); }
    else if (in_r(&Y.close, mx, my)) { wait_release(); preview_close(); }
}

void preview_key(short ks, short key)
{
    short scan = (key >> 8) & 0xff, msg[8];
    (void)ks;
    memset(msg, 0, sizeof msg);
    switch (scan) {
    case 0x4B: pv_goto(pv.page - 1); break;            /* left */
    case 0x4D: pv_goto(pv.page + 1); break;            /* right */
    case 0x47: pv_goto(0); break;                      /* Clr/Home */
    case 0x01: preview_close(); break;                 /* Esc */
    case 0x39: pv.zoom = !pv.zoom; pv_refresh(); break; /* space: zoom */
    case 0x48: case 0x50:
        msg[0] = WM_ARROWED;
        msg[4] = scan == 0x48 ? WA_UPLINE : WA_DNLINE;
        if (!pv.zoom) { pv_goto(pv.page + (scan == 0x48 ? -1 : 1)); break; }
        preview_msg(msg);
        break;
    }
}

static void fs_docgone(DOC *d) { (void)d; }
const TOOL fontsel_tool = { fontsel_owns, fontsel_msg, fontsel_click, fontsel_key, fontsel_close, fs_docgone };
const TOOL preview_tool = { preview_owns, preview_msg, preview_click, preview_key, preview_close, preview_doc_closed };
