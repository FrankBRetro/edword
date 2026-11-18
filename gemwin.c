/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemwin.c - document windows: toolbar, page view, codes pane, drawing,
 *            scrolling, keyboard and mouse editing, views, zoom, tiling,
 *            iconify
 *
 * The document view shows the pages one below the other, at the zoom
 * set for the window. Layout positions are screen pixels at 100%; zx()
 * scales them to the zoom.
 */
#include <gem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gemui.h"
#include "edrsc.h"

static long  tb_state[MAX_DOCS], rul_state[MAX_DOCS];
static void  draw_guides(DOC *d, int page, long x, long y, const GRECT *clip);
static long  ruler_key(DOC *d);
static char *cclip;
static long  cclen;

static int rc_inter(const GRECT *a, GRECT *b)
{
    short x1 = a->g_x > b->g_x ? a->g_x : b->g_x;
    short y1 = a->g_y > b->g_y ? a->g_y : b->g_y;
    short x2 = (a->g_x + a->g_w < b->g_x + b->g_w) ? a->g_x + a->g_w : b->g_x + b->g_w;
    short y2 = (a->g_y + a->g_h < b->g_y + b->g_h) ? a->g_y + a->g_h : b->g_y + b->g_h;
    b->g_x = x1; b->g_y = y1; b->g_w = x2 - x1; b->g_h = y2 - y1;
    return x2 > x1 && y2 > y1;
}

static int in_rect(const GRECT *r, short x, short y)
{
    return r->g_h > 0 && x >= r->g_x && y >= r->g_y && x < r->g_x + r->g_w && y < r->g_y + r->g_h;
}

/* the rulers are being drawn (their white and black are the theme's) */
static int in_ruler;

/* the furniture's colours in a theme: the desk's pattern, the rulers'
 * grey margins, white and black */
static int ink(void) { return theme_on() && in_ruler ? theme_pen(TH_TEXT) : 1; }

static void fill(const GRECT *r, int color, int pattern)
{
    short p[4];
    if (theme_on()) {
        if (color == 1 && pattern == 4) { color = theme_pen(TH_DESK); pattern = 0; }
        else if (color == 1 && pattern == 2) { color = theme_pen(TH_MARGIN); pattern = 0; }
        else if (in_ruler && color == 0 && pattern == 0) color = theme_pen(TH_FACE);
    }
    p[0] = r->g_x; p[1] = r->g_y; p[2] = r->g_x + r->g_w - 1; p[3] = r->g_y + r->g_h - 1;
    vswr_mode(vhandle, MD_REPLACE);
    vsf_interior(vhandle, pattern ? FIS_PATTERN : FIS_SOLID);
    if (pattern) vsf_style(vhandle, pattern);
    vsf_color(vhandle, color);
    vr_recfl(vhandle, p);
    vsf_interior(vhandle, FIS_SOLID);
}

static void xor_box(long x1, long y1, long x2, long y2)
{
    short p[4];
    p[0] = (short)x1; p[1] = (short)y1; p[2] = (short)x2; p[3] = (short)y2;
    vr_recfl(vhandle, p);
}

/* ---- display modes for the editing panes ------------------------------- */

/* Normal is black on white; Dark white on black; Green and Amber are
 * green or amber text on black (in monochrome they look like Dark).
 * Amber needs a pen of its own: with 16 or more pens, pen 6 (yellow) is
 * set to amber while the mode is on and put back afterwards. */
static short amber_saved[3], amber_set;

int ed_bg(void) { return pref_display == DISP_NORMAL ? page_white() : 1; }

int ed_fg(void)
{
    switch (pref_display) {
    case DISP_DARK:  return 0;
    case DISP_GREEN: return npens >= 4 ? 3 : 0;
    case DISP_AMBER: return npens >= 16 ? 6 : npens >= 4 ? 2 : 0;
    }
    return 1;
}

void display_restore(void)
{
    if (amber_set) { vs_color(vhandle, 6, amber_saved); amber_set = 0; }
}

void display_apply(void)
{
    static const short amber[3] = { 1000, 690, 0 };
    if (pref_display == DISP_AMBER && npens >= 16) {
        if (!amber_set) { vq_color(vhandle, 6, 0, amber_saved); amber_set = 1; }
        vs_color(vhandle, 6, amber);
    } else display_restore();
}

/* ================================================================= */
/*  Geometry                                                          */
/* ================================================================= */

void work_area(DOC *d, GRECT *g)
{
    wind_get(d->wh, WF_WORKXYWH, &g->g_x, &g->g_y, &g->g_w, &g->g_h);
}

/* the rulers: along the top and down the left of the page pane */
#define RULH (hchar + 7)
#define RULW (3 * wchar + 4)
static int rulers_on(DOC *d) { return pref_rulers && d->view != VIEW_CODES && d->view != VIEW_PREVIEW && !d->iconified; }

/* the status line along the top: the font, the alignment, the page, the
 * zoom (drawn by EdWord, so a colour theme colours it too) */
#define SBH (hchar + 4)
static void status_rect(DOC *d, GRECT *sb)
{
    work_area(d, sb);
    if (d->iconified) sb->g_h = 0;
    else if (sb->g_h > SBH) sb->g_h = SBH;
}

/* Split window: the text (or in Quick Edit the codes) is in two panes,
 * one above the other, each with its own scroll. The pane being worked
 * in has its scroll in scroll_x, scroll_y and cv.top, the other's in
 * scroll2_x... (pane_swap() changes them over); pane2 says which of
 * the two is the one worked in, the top (0) or the bottom (1). */
#ifdef EDLITE
#define split_on(d) 0           /* EdWord Lite: one pane */
#else
static int split_on(DOC *d) { return d->split2 && d->view != VIEW_PREVIEW && !d->iconified; }
#endif
#define SPLITH 9                /* the bar between the panes, with its grip */

/* the area r cut in two: the active pane in r, the other in o, the bar
 * between them in dv */
static void split_cut(DOC *d, GRECT *r, GRECT *o, GRECT *dv)
{
    short h = r->g_h - SPLITH, h1;
    if (h < 2) h = 2;
    h1 = (short)((long)h * d->split_at / 1000);
    if (h1 < 1) h1 = 1;
    if (h1 > h - 1) h1 = (short)(h - 1);
    *o = *dv = *r;
    dv->g_y = r->g_y + h1; dv->g_h = SPLITH;
    if (d->pane2) { o->g_h = h1; r->g_y = dv->g_y + SPLITH; r->g_h = (short)(h - h1); }
    else { r->g_h = h1; o->g_y = dv->g_y + SPLITH; o->g_h = (short)(h - h1); }
}

static void panes_ex(DOC *d, GRECT *tb, GRECT *hr, GRECT *vr, GRECT *tp, GRECT *cp, GRECT *op, GRECT *dv)
{
    GRECT wa, o2, d2;
    short y, h;
    if (!op) op = &o2;
    if (!dv) dv = &d2;
    work_area(d, &wa);
    *tb = *tp = *cp = *hr = *vr = *op = *dv = wa;
    tb->g_h = cp->g_h = hr->g_h = vr->g_h = op->g_h = dv->g_h = 0;
    if (d->iconified) return;
    y = wa.g_y; h = wa.g_h;
    y += SBH; h -= SBH;
    tb->g_y = y;
    if (d->toolbar || d->view == VIEW_PREVIEW) { tb->g_h = TBH; y += TBH; h -= TBH; }
    if (h < 0) h = 0;
    tp->g_y = y; tp->g_h = h;
    if (d->view == VIEW_SPLIT) {
        short th = h / 2;
        tp->g_h = th;
        cp->g_y = y + th + DIVH;
        cp->g_h = h - th - DIVH;
        if (cp->g_h < 0) cp->g_h = 0;
    } else if (d->view == VIEW_CODES) {
        tp->g_h = 0;
        cp->g_y = y;
        cp->g_h = h;
    }
    if (split_on(d)) split_cut(d, d->view == VIEW_CODES ? cp : tp, op, dv);
    if (rulers_on(d) && tp->g_h > 2 * RULH && tp->g_w > 3 * RULW) {
        hr->g_y = tp->g_y; hr->g_h = RULH;
        vr->g_y = tp->g_y + RULH; vr->g_h = tp->g_h - RULH; vr->g_x = tp->g_x; vr->g_w = RULW;
        hr->g_x = tp->g_x; hr->g_w = tp->g_w;
        tp->g_x += RULW; tp->g_w -= RULW;
        tp->g_y += RULH; tp->g_h -= RULH;
    }
}

static void panes(DOC *d, GRECT *tb, GRECT *hr, GRECT *vr, GRECT *tp, GRECT *cp)
{
    panes_ex(d, tb, hr, vr, tp, cp, NULL, NULL);
}

/* the other pane becomes the one worked in */
static void pane_swap(DOC *d)
{
    long t;
    if (d->view == VIEW_CODES) { t = d->cv.top; d->cv.top = d->cv_top2; d->cv_top2 = t; }
    else {
        t = d->scroll_x; d->scroll_x = d->scroll2_x; d->scroll2_x = t;
        t = d->scroll_y; d->scroll_y = d->scroll2_y; d->scroll2_y = t;
    }
    d->pane2 = !d->pane2;
}
static int other_pane;          /* drawing (or refreshing) the pane not worked in */
void pane_swap_doc(DOC *d) { pane_swap(d); }

/* A document's file selector is a sheet, as on a Mac: fixed
 * in the middle of its window; they can't be moved on their
 * own, they go where the window goes. Where a sheet of size w x h goes. */
void sheet_pos(DOC *d, short w, short h, short *x, short *y)
{
    GRECT o;
    if (!doc_area(d, &o)) return;
    *x = (short)(o.g_x + (o.g_w - w) / 2);
    *y = (short)(o.g_y + (o.g_h - h) / 2);          /* in the middle of the window */
    if (*y < o.g_y) *y = o.g_y;
}

void sheets_place(DOC *d)
{
    fsel_follow(d);
    sheets_top(d);
}

/* the window came to the front: its sheets in front of it again */
void sheets_top(DOC *d)
{
    fsel_top(d);
}

/* where a document's window is, for a dialog or gauge that belongs to it */
int doc_area(DOC *d, GRECT *r)
{
    if (!d || !d->used || d->wh < 0 || d->iconified) return 0;
    work_area(d, r);
    return 1;
}

void get_panes(DOC *d, GRECT *tb, GRECT *tp, GRECT *cp)
{
    GRECT hr, vr;
    panes(d, tb, &hr, &vr, tp, cp);
}

int codes_focus(DOC *d)
{
    return d->view == VIEW_CODES || (d->view == VIEW_SPLIT && d->focus == 1);
}

long zx(DOC *d, long v) { return v * d->zoom / 100; }

static long page_h_view(DOC *d) { return zx(d, page_h_px(d)); }

long view_page_top(DOC *d, int page) { return PGAP + (long)page * (page_h_view(d) + PGAP); }

long view_page_left(DOC *d, GRECT *tp)
{
    long w = zx(d, page_w_px(d)), l = (tp->g_w - w) / 2;
    return l > PGAP ? l : PGAP;
}

static long content_w(DOC *d) { return zx(d, page_w_px(d)) + 2 * PGAP; }
static long content_h(DOC *d) { return (long)doc_npages(d) * (page_h_view(d) + PGAP) + PGAP; }

/* the page and page position (100%) under a window point */
int view_to_page(DOC *d, GRECT *tp, short mx, short my, long *px, long *py)
{
    long cx = mx - tp->g_x + d->scroll_x - view_page_left(d, tp);
    long cy = my - tp->g_y + d->scroll_y - PGAP, step = page_h_view(d) + PGAP;
    int p = (int)(cy < 0 ? 0 : cy / step);
    if (p >= doc_npages(d)) p = doc_npages(d) - 1;
    if (p < 0) p = 0;
    *px = cx * 100 / d->zoom;
    *py = (cy - (long)p * step) * 100 / d->zoom;
    return p;
}

/* where page coordinates are on the screen */
static long scr_x(DOC *d, GRECT *tp, long px) { return tp->g_x - d->scroll_x + view_page_left(d, tp) + zx(d, px); }
static long scr_y(DOC *d, GRECT *tp, int page, long py) { return tp->g_y - d->scroll_y + view_page_top(d, page) + zx(d, py); }

static long codes_vis(DOC *d)
{
    GRECT tb, tp, cp;
    long v;
    get_panes(d, &tb, &tp, &cp);
    v = (cp.g_h - 4) / hchar;
    return v < 1 ? 1 : v;
}

static long codes_cols_of(DOC *d)
{
    GRECT wa;
    work_area(d, &wa);
    return (wa.g_w - 8) / wchar;
}

void clamp_scroll(DOC *d)
{
    GRECT tb, tp, cp;
    long my, mx, mt;
    get_panes(d, &tb, &tp, &cp);
    my = content_h(d) - tp.g_h;
    mx = content_w(d) - tp.g_w;
    if (d->scroll_y > my) d->scroll_y = my;
    if (d->scroll_x > mx) d->scroll_x = mx;
    if (d->scroll_y < 0) d->scroll_y = 0;
    if (d->scroll_x < 0) d->scroll_x = 0;
    if (d->view != VIEW_DOC) {
        mt = d->cv.nl - codes_vis(d);
        if (d->cv.top > mt) d->cv.top = mt;
        if (d->cv.top < 0) d->cv.top = 0;
    }
}

/* ================================================================= */
/*  Drawing                                                           */
/* ================================================================= */

static void draw_selection(DOC *d, GRECT *tp, GRECT *r)
{
    long a, b, li;
    vswr_mode(vhandle, MD_XOR);
    vsf_interior(vhandle, FIS_SOLID);
    vsf_color(vhandle, 1);
    if (d->selobj >= 0) { vswr_mode(vhandle, MD_REPLACE); return; }
    if (d->colsel && d->anchor >= 0) {             /* a block: a span of each line */
        long la, lb, x0, x1, ca, cb;
        if (col_block(d, &la, &lb, &x0, &x1))
            for (li = la; li <= lb && li < d->nlines; li++) {
                LINE *ln = &d->lines[li];
                long y1 = scr_y(d, tp, ln->page, ln->py), xa, xb;
                if (y1 > r->g_y + r->g_h || y1 + zx(d, ln->asc + ln->desc) < r->g_y) continue;
                col_span(d, li, x0, x1, &ca, &cb);
                xa = doc_x_of(d, li, ca); xb = doc_x_of(d, li, cb);
                if (xb > xa) xor_box(scr_x(d, tp, xa), y1, scr_x(d, tp, xb) - 1, y1 + zx(d, ln->asc + ln->desc) - 1);
            }
    } else if (doc_sel(d, &a, &b)) {
        long la = doc_find_line(d, a), lb = doc_find_line(d, b);
        for (li = la; li <= lb && li < d->nlines; li++) {
            LINE *ln = &d->lines[li];
            long x1, x2, y1;
            if (ln->start + ln->len <= a && ln->len) continue;
            if (ln->start >= b && li > la) continue;
            x1 = ln->start >= a ? ln->x + ln->xoff : doc_x_of(d, li, a);
            if (ln->start + ln->len > b) x2 = doc_x_of(d, li, b);
            else {
                x2 = doc_x_of(d, li, ln->start + ln->len);
                if (ln->len && ISEND(d->text[ln->start + ln->len - 1].ch)) x2 += 6;
            }
            y1 = scr_y(d, tp, ln->page, ln->py);
            if (y1 > r->g_y + r->g_h || y1 + zx(d, ln->asc + ln->desc) < r->g_y) continue;
            if (x2 > x1)
                xor_box(scr_x(d, tp, x1), y1, scr_x(d, tp, x2) - 1, y1 + zx(d, ln->asc + ln->desc) - 1);
        }
    } else if ((d->view == VIEW_DOC || d->focus == 0) && !d->caret_off && !other_pane) {
        long li2 = doc_find_line(d, d->cursor);
        LINE *ln = &d->lines[li2];
        long x = scr_x(d, tp, doc_x_of(d, li2, d->cursor)), y = scr_y(d, tp, ln->page, ln->py);
        xor_box(x, y, x + (d->zoom >= 150 ? 2 : 1), y + zx(d, ln->asc + ln->desc) - 1);
    }
    vswr_mode(vhandle, MD_REPLACE);
}

/* fill the part of a with the desk pattern, leaving out the column
 * [px, px + pw) between rows y0 and y1 (where a page is) */
static void desk_around(const GRECT *a, long px, long pw, long y0, long y1, int dark)
{
    GRECT r;
    long ax2 = a->g_x + a->g_w, ay2 = a->g_y + a->g_h;
    if (y1 <= a->g_y || y0 >= ay2 || px >= ax2 || px + pw <= a->g_x) { fill(a, 1, dark ? 0 : 4); return; }
    if (y0 > a->g_y) { r = *a; r.g_h = (short)(y0 - a->g_y); fill(&r, 1, dark ? 0 : 4); }
    if (y1 < ay2) { r = *a; r.g_y = (short)y1; r.g_h = (short)(ay2 - y1); fill(&r, 1, dark ? 0 : 4); }
    if (y0 < a->g_y) y0 = a->g_y;
    if (y1 > ay2) y1 = ay2;
    if (px > a->g_x) { r.g_x = a->g_x; r.g_w = (short)(px - a->g_x); r.g_y = (short)y0; r.g_h = (short)(y1 - y0); fill(&r, 1, dark ? 0 : 4); }
    if (px + pw < ax2) { r.g_x = (short)(px + pw); r.g_w = (short)(ax2 - px - pw); r.g_y = (short)y0; r.g_h = (short)(y1 - y0); fill(&r, 1, dark ? 0 : 4); }
}

static void draw_text_pane(DOC *d, const GRECT *tpc, GRECT *clip)
{
    GRECT r = *clip, tp = *tpc, pg;
    short pxy[4];
    int p, np = doc_npages(d), dark = pref_display != DISP_NORMAL;
    long ph = page_h_view(d), pw = zx(d, page_w_px(d));

    if (!rc_inter(&tp, &r)) return;
    pxy[0] = r.g_x; pxy[1] = r.g_y; pxy[2] = r.g_x + r.g_w - 1; pxy[3] = r.g_y + r.g_h - 1;
    vs_clip(vhandle, 1, pxy);
    /* the desk around the pages; never under a page, so nothing flashes */
    {
        long y0 = scr_y(d, &tp, 0, 0) - 1, y1 = scr_y(d, &tp, np - 1, 0) + ph + 1, x = scr_x(d, &tp, 0) - 1;
        desk_around(&r, x, pw + 2, y0, y1, dark);
        /* the gaps between pages */
        for (p = 0; p + 1 < np; p++) {
            GRECT g;
            g.g_x = (short)x; g.g_w = (short)(pw + 2);
            g.g_y = (short)(scr_y(d, &tp, p, 0) + ph + 1);
            g.g_h = (short)(scr_y(d, &tp, p + 1, 0) - 1 - g.g_y);
            if (g.g_h > 0 && rc_inter(&r, &g)) fill(&g, 1, dark ? 0 : 4);
        }
    }
    for (p = 0; p < np; p++) {
        long y = scr_y(d, &tp, p, 0), x = scr_x(d, &tp, 0);
        GRECT sh;
        if (y > r.g_y + r.g_h) break;
        if (y + ph + 4 < r.g_y) continue;
        if (!dark) {                                         /* shadow: right and bottom edges */
            sh.g_x = (short)(x + pw + 1); sh.g_y = (short)(y + 3); sh.g_w = 3; sh.g_h = (short)ph;
            fill(&sh, 1, 0);
            sh.g_x = (short)(x + 3); sh.g_y = (short)(y + ph + 1); sh.g_w = (short)pw; sh.g_h = 3;
            fill(&sh, 1, 0);
        }
        pg.g_x = (short)x; pg.g_y = (short)y; pg.g_w = (short)pw; pg.g_h = (short)ph;
        fill(&pg, ed_bg(), 0);
        if (!dark) {
            short b[10];
            vsl_color(vhandle, 1); vsl_type(vhandle, 1); vsl_width(vhandle, 1);
            b[0] = b[6] = b[8] = pg.g_x - 1; b[2] = b[4] = pg.g_x + pg.g_w;
            b[1] = b[3] = b[9] = pg.g_y - 1; b[5] = b[7] = pg.g_y + pg.g_h;
            v_pline(vhandle, 5, b);
        }
        {
            GRECT c = pg;
            if (rc_inter(&r, &c)) {
                short cp[4];
                cp[0] = c.g_x; cp[1] = c.g_y; cp[2] = c.g_x + c.g_w - 1; cp[3] = c.g_y + c.g_h - 1;
                vs_clip(vhandle, 1, cp);
                if (pref_guides) draw_guides(d, p, x, y, &c);
                draw_page(d, p, x, y, d->zoom, 100, &c, dark ? ed_fg() : -1, 1);
                vs_clip(vhandle, 1, pxy);
            }
        }
    }
    draw_selection(d, &tp, &r);
    if (d->selobj >= 0) obj_draw_handles(d, &tp);
    vs_clip(vhandle, 0, pxy);
}

/* one wrapped line of the codes text; codes are drawn in another colour
 * (or light text in monochrome) */
static void draw_codes_line(CODES *c, long li, long x, long base, int cols)
{
    long s = c->ls[li], n = codes_line_len(c, li), i = 0;
    char buf[256];
    short fg = (short)(theme_on() && pref_display == DISP_NORMAL ? theme_pen(TH_TEXT) : ed_fg());
    short pen = pref_display == DISP_NORMAL && npens >= 4 ? 2 : fg;
    if (n > cols) n = cols;
    if (n > 255) n = 255;
    while (i < n) {
        long j = i;
        int code = 0;
        if (c->t[s + i] == '[') {
            if (i + 1 < n && c->t[s + i + 1] == '[') j = i + 2;
            else {
                j = i + 1;
                while (j < n && c->t[s + j] != ']' && c->t[s + j] != '[') j++;
                if (j < n && c->t[s + j] == ']') { j++; code = 1; } else j = i + 1;
            }
        } else
            while (j < n && c->t[s + j] != '[') j++;
        memcpy(buf, c->t + s + i, j - i);
        buf[j - i] = 0;
        if (code) { if (pen != fg) vst_color(vhandle, pen); else vst_effects(vhandle, 2); }
        v_gtext(vhandle, (short)(x + i * wchar), (short)base, buf);
        if (code) { vst_color(vhandle, fg); vst_effects(vhandle, 0); }
        i = j;
    }
}

static void draw_codes_pane(DOC *d, const GRECT *cp, GRECT *clip)
{
    GRECT r = *clip;
    CODES *c = &d->cv;
    short pxy[4];
    long li, a, b, x0 = cp->g_x + 4, y0 = cp->g_y + 2;
    int cols = (int)((cp->g_w - 8) / wchar), th;

    if (!rc_inter(cp, &r)) return;
    pxy[0] = r.g_x; pxy[1] = r.g_y; pxy[2] = r.g_x + r.g_w - 1; pxy[3] = r.g_y + r.g_h - 1;
    vs_clip(vhandle, 1, pxy);
    /* the quick codes in the colour theme's, on the paper display */
    th = theme_on() && pref_display == DISP_NORMAL;
    fill(&r, th ? theme_pen(TH_LIGHT) : ed_bg(), 0);
    set_sys_font();
    vst_color(vhandle, th ? theme_pen(TH_TEXT) : ed_fg());
    vswr_mode(vhandle, MD_TRANS);
    for (li = c->top; li < c->nl; li++) {
        long y = y0 + (li - c->top) * hchar;
        if (y >= r.g_y + r.g_h) break;
        if (y + hchar <= r.g_y) continue;
        draw_codes_line(c, li, x0, y + sys_asc, cols);
    }
    vswr_mode(vhandle, MD_XOR);
    vsf_interior(vhandle, FIS_SOLID);
    vsf_color(vhandle, 1);
    if (codes_sel(c, &a, &b)) {
        long la = codes_line_of(c, a), lb = codes_line_of(c, b);
        for (li = la; li <= lb; li++) {
            long c1 = li == la ? a - c->ls[li] : 0;
            long c2 = li == lb ? b - c->ls[li] : codes_line_len(c, li) + 1;
            long y = y0 + (li - c->top) * hchar;
            if (li < c->top || y > r.g_y + r.g_h) continue;
            if (c2 > c1) xor_box(x0 + c1 * wchar, y, x0 + c2 * wchar - 1, y + hchar - 1);
        }
    } else if (codes_focus(d) && !d->caret_off && !other_pane) {
        long li2 = codes_line_of(c, c->cursor);
        long x = x0 + (c->cursor - c->ls[li2]) * wchar, y = y0 + (li2 - c->top) * hchar;
        if (li2 >= c->top) xor_box(x, y, x + 1, y + hchar - 1);
    }
    vswr_mode(vhandle, MD_REPLACE);
    vs_clip(vhandle, 0, pxy);
    style_reset();
}

/* ---- toolbar ------------------------------------------------------- */

/* The toolbar is a tree in the resource file: a box for each button with
 * its icon as images (and a small arrow on the font, size, colour and
 * zoom buttons, which open popups), drawn and found by the AES. These
 * stand for those four in RSC_TB_CMDS. */
#define TB_FONT  (-1)
#define TB_SIZE  (-2)
#define TB_COLOR (-3)
#define TB_ZOOM  (-4)

static int tb_active(DOC *d, int cmd)
{
    PARA *p = doc_para_at(d, d->cursor);
    switch (cmd) {
    case C_BOLD:    return (d->ins.attr & A_BOLD) != 0;
    case C_ITALIC:  return (d->ins.attr & A_ITALIC) != 0;
    case C_UNDER:   return (d->ins.attr & A_UNDER) != 0;
    case C_JLEFT:   return p->just == J_LEFT;
    case C_JCENTRE: return p->just == J_CENTRE;
    case C_JRIGHT:  return p->just == J_RIGHT;
    case C_JFULL:   return p->just == J_FULL;
    case C_BULLETS: return p->list == LIST_BULLET;
    case C_NUMBERS: return p->list == LIST_NUMBER;
    case C_SPLIT:   return d->view == VIEW_SPLIT || d->view == VIEW_CODES;
    case C_SPLITWIN: return d->split2;
    case C_PVZOOM:  return d->pv_zoom;
    case C_RULERS:  return pref_rulers;
    case C_GUIDES:  return pref_guides;
    }
    return 0;
}

static int tb_disabled(DOC *d, int cmd)
{
    if (cmd == C_UNDO) return !undo_can(d) && d->ch_lo < 0;
    if (cmd == C_REDO) return !redo_can(d);
    if (cmd == C_PVPREV) return d->pv_page <= 0;
#ifdef EDLITE
    if (cmd == C_PDF || cmd == C_SPLITWIN) return 1;   /* not in EdWord Lite */
#endif
    if (cmd == C_PVNEXT) return d->pv_page >= doc_npages(d) - 1;
    return 0;
}

static long tb_key(DOC *d)
{
    PARA *p = doc_para_at(d, d->cursor);
    return ((long)d->ins.attr << 8) ^ ((long)p->just << 2) ^ ((long)p->list << 12) ^ d->view ^ ((long)colour_on << 30) ^
           ((long)undo_can(d) << 16) ^ ((long)redo_can(d) << 17) ^ ((long)d->zoom << 18) ^
           ((long)pref_rulers << 26) ^ ((long)pref_guides << 27) ^ ((long)d->split2 << 28) ^
           (d->view == VIEW_PREVIEW ? (d->pv_page * 977L) ^ ((long)d->pv_zoom << 25) ^ ((long)doc_npages(d) << 19) : 0);
}

/* the two bars: the toolbar, and the print preview's */
static const short tbcmd[][2] = { RSC_TB_CMDS }, pvcmd[][2] = { RSC_PV_CMDS };
static const short tb_mono[] = { RSC_TB_MONO }, tb_colour[] = { RSC_TB_COLOUR }, tb_coloured[] = { RSC_TB_COLOURED };
static const short pv_mono[] = { RSC_PV_MONO }, pv_colour[] = { RSC_PV_COLOUR }, pv_coloured[] = { RSC_PV_COLOURED };
typedef struct {
    short rt16, rt8, line;
    const short (*cmd)[2]; int ncmd;
    const short *mono, *colour, *coloured; int nm, nl;
} BARDEF;
#define NOF(a) ((int)(sizeof a / sizeof a[0]))
static const BARDEF bar_tb = { RT_TOOLBAR, RT_TOOLBAR8, TBO_LINE, tbcmd, NOF(tbcmd),
                               tb_mono, tb_colour, tb_coloured, NOF(tb_mono), NOF(tb_colour) };
static const BARDEF bar_pv = { RT_PVBAR, RT_PVBAR8, PVB_LINE, pvcmd, NOF(pvcmd),
                               pv_mono, pv_colour, pv_coloured, NOF(pv_mono), NOF(pv_colour) };
static const BARDEF *bar_of(DOC *d) { return d->view == VIEW_PREVIEW ? &bar_pv : &bar_tb; }

static int is_layer(const BARDEF *b, short o)
{
    int i;
    for (i = 0; i < b->nl; i++) if (b->colour[i] == o) return 1;
    return 0;
}

/* the bar's tree, where the bar is; the icons in colour on 16 colours
 * and more */
static OBJECT *tb_tree(DOC *d, const GRECT *tb)
{
    const BARDEF *b = bar_of(d);
    OBJECT *t = rsc_tree(hchar >= 16 ? b->rt16 : b->rt8);
    int i, col = npens >= 16;
    if (!t) return NULL;
    t[0].ob_x = tb->g_x; t[0].ob_y = tb->g_y;
    t[0].ob_width = t[b->line].ob_width = tb->g_w;
    t[0].ob_height = tb->g_h;
    t[b->line].ob_y = (short)(tb->g_h - 1);
    for (i = 0; i < b->nm; i++) {
        if (col) t[b->mono[i]].ob_flags |= OF_HIDETREE; else t[b->mono[i]].ob_flags &= ~OF_HIDETREE;
        if (col) t[b->coloured[i]].ob_flags &= ~OF_HIDETREE; else t[b->coloured[i]].ob_flags |= OF_HIDETREE;
    }
    for (i = 0; i < b->nl; i++)
        if (col) t[b->colour[i]].ob_flags &= ~OF_HIDETREE; else t[b->colour[i]].ob_flags |= OF_HIDETREE;
    if (b == &bar_pv) {                     /* the page number */
        static char pg[40];
        sprintf(pg, L(S_PAGE_N_OF), (int)d->pv_page + 1, doc_npages(d));
        t[PVB_PAGE].ob_spec.free_string = pg;
        t[PVB_PAGE].ob_width = (short)(strlen(pg) * wchar);
        t[PVB_PAGE].ob_height = hchar;
        t[PVB_PAGE].ob_y = (short)((tb->g_h - 1 - hchar) / 2);
    }
    /* a colour theme: the bar and its buttons are drawn by EdWord in the
     * theme's colours, the icons on them still by the AES */
    {
        static USERBLK ub[2][1 + 40];
        USERBLK *u = ub[b == &bar_pv];
        if (theme_on()) {
            theme_userdef(&t[0], &u[0], 0);
            for (i = 0; i < b->ncmd && i < 40; i++) theme_userdef(&t[b->cmd[i][0]], &u[1 + i], 1);
            if (b == &bar_pv) t[PVB_PAGE].ob_flags &= ~OF_FL3DMASK;
        } else {
            t[0].ob_type = G_BOX; t[0].ob_spec.index = 0x00001100L;
            for (i = 0; i < b->ncmd; i++) { t[b->cmd[i][0]].ob_type = G_BOX; t[b->cmd[i][0]].ob_spec.index = 0x00011170L; }
            if (b == &bar_pv && aes_3d) t[PVB_PAGE].ob_flags |= OF_FL3DBAK;
        }
    }
    return t;
}

/* the states for this document: on, greyed, the 3D look */
static void tb_states(DOC *d, OBJECT *t)
{
    const BARDEF *b = bar_of(d);
    int i, th = theme_on(), look3d = aes_3d && !th;
    if (look3d) t[0].ob_flags |= OF_FL3DBAK;
    for (i = 0; i < b->ncmd; i++) {
        short o = b->cmd[i][0], c, on = (short)tb_active(d, b->cmd[i][1]);
        short off = (short)((b->cmd[i][1] == TB_COLOR && !colour_on) || tb_disabled(d, b->cmd[i][1]));
        t[o].ob_state = (short)((on ? OS_SELECTED : 0) | (off ? OS_DISABLED : 0));
        if (look3d) t[o].ob_flags |= OF_FL3DACT;
        for (c = t[o].ob_head; c > o; c = t[c].ob_next) {
            /* white on the black of a selected button, without 3D or a theme */
            if (!is_layer(b, c)) t[c].ob_spec.bitblk->bi_color = (short)(on && !aes_3d && !th ? 0 : 1);
            t[c].ob_state = (short)(off ? OS_DISABLED : 0);
        }
    }
}

/* left edge of the toolbar button for cmd (also used by the tests) */
short tb_item_x(DOC *d, int cmd)
{
    GRECT tb, tp, cp;
    int i;
    OBJECT *t;
    const BARDEF *b = bar_of(d);
    get_panes(d, &tb, &tp, &cp);
    if ((t = tb_tree(d, &tb)) == NULL) return -1;
    for (i = 0; i < b->ncmd; i++) if (b->cmd[i][1] == cmd) return (short)(tb.g_x + t[b->cmd[i][0]].ob_x);
    return -1;
}

static void draw_toolbar(DOC *d, const GRECT *tb, GRECT *clip)
{
    GRECT r = *clip;
    OBJECT *t;
    if (!rc_inter(tb, &r) || (t = tb_tree(d, tb)) == NULL) return;
    tb_states(d, t);
    objc_draw(t, 0, 8, r.g_x, r.g_y, r.g_w, r.g_h);
    tb_state[d->slot] = tb_key(d);
}

static void toolbar_click(DOC *d, const GRECT *tb, short mx)
{
    static const char *items[MAX_FONTS + 2];
    static char sz[NSIZES + 1][16], zm[NZOOM][8];
    int i, k, n, sel, cmd = 0;
    short px, py, o, dummy;
    OBJECT *t = tb_tree(d, tb);
    const BARDEF *b = bar_of(d);

    if (!t) return;
    o = objc_find(t, 0, 8, mx, (short)(tb->g_y + tb->g_h / 2));
    while (o > 0 && t[o].ob_type == G_IMAGE) o = t[o].ob_next;      /* the button it's in */
    for (i = 0; i < b->ncmd && b->cmd[i][0] != o; i++) ;
    if (i == b->ncmd) return;
    cmd = b->cmd[i][1];
    objc_offset(t, o, &px, &dummy);
    py = (short)(tb->g_y + tb->g_h);
    switch (cmd) {
    case TB_FONT: {
        /* as many as a popup can hold; the rest are in the font selector */
        int nshow = nfonts, rows = (scr_h - py) / hchar - 1;
        if (rows > 36) rows = 36;
        if (rows < 8) rows = 8;
        if (nshow > rows) nshow = rows - 1;
        for (k = 0; k < nshow; k++) items[k] = fonts[k].name;
        if (nshow < nfonts) items[nshow] = L(M_MOREFONTS);
        sel = popup(items, nshow < nfonts ? nshow + 1 : nshow, d->ins.font < nshow ? d->ins.font : -1, px, py);
        if (sel >= 0 && sel < nshow) apply_style(d, AP_FONT, sel);
        else if (sel == nshow) do_cmd(C_FONTSEL);
        break;
    }
    case TB_SIZE:
        for (k = 0; k < NSIZES; k++) { sprintf(sz[k], "%d %s", sizes[k], L(S_POINT)); items[k] = sz[k]; }
        items[NSIZES] = L(M_SIZEOTHER);
        n = -1;
        for (k = 0; k < NSIZES; k++) if (sizes[k] == d->ins.size) n = k;
        sel = popup_rsc(RT_POPSIZE, n, px, py);
        if (sel == -3) sel = popup(items, NSIZES + 1, n, px, py);
        if (sel >= 0 && sel < NSIZES) apply_style(d, AP_SIZE, sizes[sel]);
        else if (sel == NSIZES) do_cmd(C_SIZEOTHER);
        break;
    case TB_COLOR:
        if (!colour_on) { alert(1, 1, AL_COLOUR_OFF, AB_OK); break; }
        for (k = 0; k < NCOLORS; k++) items[k] = L(S_COL_BLACK + k);
        sel = popup_rsc(RT_POPCOLOUR, d->ins.color, px, py);
        if (sel == -3) sel = popup(items, NCOLORS, d->ins.color, px, py);
        if (sel >= 0) apply_style(d, AP_COLOR, sel);
        break;
    case TB_ZOOM:
        n = -1;
        for (k = 0; k < NZOOM; k++) { sprintf(zm[k], "%d%%", zooms[k]); items[k] = zm[k]; if (zooms[k] == d->zoom && !d->fitw) n = k; }
        items[NZOOM] = L(M_ZOOMFIT);
        items[NZOOM + 1] = L(M_ZOOMPAGE);
        if (d->fitw) n = d->fitw == FIT_PAGE ? NZOOM + 1 : NZOOM;
        sel = popup_rsc(RT_POPZOOM, n, px, py);
        if (sel == -3) sel = popup(items, NZOOM + 2, n, px, py);
        if (sel >= 0 && sel < NZOOM) set_zoom(d, zooms[sel]);
        else if (sel == NZOOM) set_fit_width(d, FIT_WIDTH);
        else if (sel == NZOOM + 1) set_fit_width(d, FIT_PAGE);
        break;
    default:
        if (!tb_disabled(d, cmd)) do_cmd(cmd);
    }
}

/* ---- iconified window ------------------------------------------------ */

static void draw_icon(DOC *d, const GRECT *wa, GRECT *clip)
{
    GRECT r = *clip;
    short pxy[4], pl[12];
    long cx = wa->g_x + wa->g_w / 2, y = wa->g_y + 3;
    char s[12];

    if (!rc_inter(wa, &r)) return;
    pxy[0] = r.g_x; pxy[1] = r.g_y; pxy[2] = r.g_x + r.g_w - 1; pxy[3] = r.g_y + r.g_h - 1;
    vs_clip(vhandle, 1, pxy);
    fill(&r, 0, 0);
    vsl_type(vhandle, 1); vsl_color(vhandle, 1); vsl_width(vhandle, 1);
    pl[0] = (short)(cx - 10); pl[1] = (short)y;                 /* a page with a folded corner */
    pl[2] = (short)(cx + 4);  pl[3] = (short)y;
    pl[4] = (short)(cx + 10); pl[5] = (short)(y + 6);
    pl[6] = (short)(cx + 10); pl[7] = (short)(y + 26);
    pl[8] = (short)(cx - 10); pl[9] = (short)(y + 26);
    pl[10] = (short)(cx - 10); pl[11] = (short)y;
    v_pline(vhandle, 6, pl);
    for (pl[1] = (short)(y + 10); pl[1] < y + 24; pl[1] += 4) {
        pl[0] = (short)(cx - 7); pl[2] = (short)(cx + 7); pl[3] = pl[1];
        v_pline(vhandle, 2, pl);
    }
    set_sys_font();
    vswr_mode(vhandle, MD_TRANS);
    sprintf(s, "%.8s", d->base);
    v_gtext(vhandle, (short)(cx - (long)strlen(s) * wchar / 2), (short)(y + 28 + sys_asc), s);
    vswr_mode(vhandle, MD_REPLACE);
    vs_clip(vhandle, 0, pxy);
    style_reset();
}

/* ---- rulers and guides ------------------------------------------------ */

static void hline2(long x1, long x2, long y)
{
    short p[4];
    p[0] = (short)x1; p[1] = (short)y; p[2] = (short)x2; p[3] = (short)y;
    v_pline(vhandle, 2, p);
}

static void vline2(long x, long y1, long y2)
{
    short p[4];
    p[0] = (short)x; p[1] = (short)y1; p[2] = (short)x; p[3] = (short)y2;
    v_pline(vhandle, 2, p);
}

/* a small triangle pointing down (up = 0) or up, its tip at x,y */
static void tri(long x, long y, int up)
{
    short p[6], s = (short)(hchar >= 16 ? 4 : 3);
    p[0] = (short)x; p[1] = (short)y;
    p[2] = (short)(x - s); p[3] = (short)(up ? y + s : y - s);
    p[4] = (short)(x + s); p[5] = p[3];
    vsf_interior(vhandle, FIS_SOLID);
    vsf_color(vhandle, ink());
    v_fillarea(vhandle, 3, p);
}

/* the rulers' backgrounds are drawn without painting any pixel twice:
 * on a screen of several planes the VDI fills a plane at a time, and
 * white over the desk's pattern, then grey over that, showed colours */
static void fill_span(long x, long y, long w, long h, int col, int pat)
{
    GRECT g;
    if (w <= 0 || h <= 0) return;
    g.g_x = (short)x; g.g_y = (short)y; g.g_w = (short)w; g.g_h = (short)h;
    fill(&g, col, pat);
}

/* a margin: grey, with a white line along both long edges */
static void fill_margin(long x, long y, long w, long h, int across)
{
    if (w <= 0 || h <= 0) return;
    if (across) {               /* a horizontal ruler: lines top and bottom */
        fill_span(x, y, w, 1, 0, 0); fill_span(x, y + h - 1, w, 1, 0, 0);
        fill_span(x, y + 1, w, h - 2, 1, 2);
    } else {
        fill_span(x, y, 1, h, 0, 0); fill_span(x + w - 1, y, 1, h, 0, 0);
        fill_span(x + 1, y, w - 2, h, 1, 2);
    }
}

/* mm from the page edge to page pixels at 100% */
static long mm_x(long mm) { return pt2px_x(mm * 720L / 254); }
static long mm_y(long mm) { return pt2px_y(mm * 720L / 254); }

/* the step between labelled marks, in mm, so the labels don't touch */
static long label_step(DOC *d, long (*mm)(long), long room)
{
    static const long st[] = { 10, 20, 50, 100 };
    int i;
    for (i = 0; i < 3; i++) if (zx(d, mm(st[i])) >= room) break;
    return st[i];
}

static void draw_hruler(DOC *d, const GRECT *hr, const GRECT *tp, GRECT *clip)
{
    GRECT r = *clip;
    short pxy[4];
    LINE *ln = &d->lines[doc_find_line(d, d->cursor)];
    PARA *p = doc_para_at(d, d->cursor);
    PGFMT f;
    long x0 = scr_x(d, (GRECT *)tp, 0), pw = zx(d, page_w_px(d)), mm, wmm, step, base = hr->g_y + hr->g_h - 1;
    long colx, colw, xl, xf, xr;
    char s[8];

    if (!rc_inter(hr, &r)) return;
    pxy[0] = r.g_x; pxy[1] = r.g_y; pxy[2] = r.g_x + r.g_w - 1; pxy[3] = r.g_y + r.g_h - 1;
    vs_clip(vhandle, 1, pxy);
    in_ruler = 1;
    page_fmt(d, ln->page, &f);
    /* off the page: the desk; the margins: grey; between: white */
    {
        long L = hr->g_x, R = (long)hr->g_x + hr->g_w, pl = x0 > L ? x0 : L, pr = x0 + pw < R ? x0 + pw : R;
        long ml = x0 + zx(d, pt2px_x(f.ml)), mr = x0 + pw - zx(d, pt2px_x(f.mr));
        if (ml < pl) ml = pl;
        if (ml > pr) ml = pr;
        if (mr > pr) mr = pr;
        if (mr < ml) mr = ml;
        if (pl > pr) pl = pr = R;
        fill_span(L, hr->g_y, pl - L, hr->g_h, 1, 4);
        fill_span(pr, hr->g_y, R - pr, hr->g_h, 1, 4);
        fill_margin(pl, hr->g_y, ml - pl, hr->g_h, 1);
        fill_span(ml, hr->g_y, mr - ml, hr->g_h, 0, 0);
        fill_margin(mr, hr->g_y, pr - mr, hr->g_h, 1);
    }
    /* the marks, every millimetre when there's room */
    vsl_color(vhandle, ink()); vsl_type(vhandle, 1); vsl_width(vhandle, 1);
    vswr_mode(vhandle, MD_TRANS);
    set_sys_font();
    vst_color(vhandle, ink());
    wmm = (long)PS(d).w * 254 / 720;
    step = label_step(d, mm_x, 4L * wchar);   /* two digits and two spaces */
    for (mm = 0; mm <= wmm; mm++) {
        long x = x0 + zx(d, mm_x(mm)), h;
        if (x < r.g_x - 2 * wchar || x > r.g_x + r.g_w + 2 * wchar) continue;
        if (mm % 10 == 0) h = hchar >= 16 ? 6 : 4;
        else if (mm % 5 == 0) h = hchar >= 16 ? 4 : 3;
        else if (zx(d, mm_x(1)) >= 3) h = 2;
        else continue;
        vline2(x, base - h, base);
        if (mm % step == 0 && mm > 0 && mm < wmm) {
            sprintf(s, "%ld", mm / 10);
            v_gtext(vhandle, (short)(x - (long)strlen(s) * wchar / 2), (short)(hr->g_y + 1 + sys_asc - (hchar >= 16 ? 2 : 0)), s);
        }
    }
    /* the caret's column and paragraph: its edges, indents, table cells */
    colx = ln->x - ln->tx0;
    if (f.cols > 1) colw = (pt2px_x(PS(d).w - f.ml - f.mr) - pt2px_x(f.colgap) * (f.cols - 1)) / f.cols;
    else colw = pt2px_x(PS(d).w - f.ml - f.mr);
    if (ln->flags & LF_CELL) {
        int t = doc_in_table(d, d->cursor, NULL, NULL), c;
        if (t > 0 && t <= d->a.ntbl) {
            TBL *T = &d->a.tbls[t - 1];
            long cx = pt2px_x(f.ml);
            for (c = 0; c <= T->ncols; c++) {
                long x = x0 + zx(d, cx);
                vline2(x, hr->g_y + 2, base);
                vline2(x + 1, hr->g_y + 2, base);
                if (c < T->ncols) cx += pt2px_x(T->w[c]);
            }
        }
    } else {
        xl = x0 + zx(d, colx + pt2px_x(p->left));
        xf = x0 + zx(d, colx + pt2px_x(p->left + p->first));
        xr = x0 + zx(d, colx + colw);
        tri(xf, hr->g_y + 1 + (hchar >= 16 ? 4 : 3), 0);
        tri(xl, base - (hchar >= 16 ? 4 : 3), 1);
        tri(xr, base - (hchar >= 16 ? 4 : 3), 1);
    }
    vsl_color(vhandle, ink());
    hline2(hr->g_x, hr->g_x + hr->g_w - 1, base);
    vswr_mode(vhandle, MD_REPLACE);
    vs_clip(vhandle, 0, pxy);
    in_ruler = 0;
    vsl_color(vhandle, 1);
    style_reset();
    rul_state[d->slot] = ruler_key(d);
}

static void draw_vruler(DOC *d, const GRECT *vr, const GRECT *tp, GRECT *clip)
{
    GRECT r = *clip;
    short pxy[4];
    int pg, np = doc_npages(d);
    long ph = page_h_view(d), hmm = (long)PS(d).h * 254 / 720, step, right = vr->g_x + vr->g_w - 1;
    long cur, bottom = (long)vr->g_y + vr->g_h;
    char s[8];

    if (!rc_inter(vr, &r)) return;
    pxy[0] = r.g_x; pxy[1] = r.g_y; pxy[2] = r.g_x + r.g_w - 1; pxy[3] = r.g_y + r.g_h - 1;
    vs_clip(vhandle, 1, pxy);
    in_ruler = 1;
    vsl_color(vhandle, ink()); vsl_type(vhandle, 1); vsl_width(vhandle, 1);
    set_sys_font();
    vst_color(vhandle, ink());
    step = label_step(d, mm_y, hchar + 2);
    cur = vr->g_y;                         /* drawn down to here */
    for (pg = 0; pg < np; pg++) {
        long y0 = scr_y(d, (GRECT *)tp, pg, 0), mm, mt, mb, top, bot;
        PGFMT f;
        if (y0 > r.g_y + r.g_h) break;
        if (y0 + ph < r.g_y) continue;
        page_fmt(d, pg, &f);
        /* the desk above the page, the page's margins and middle */
        top = y0 > cur ? y0 : cur;
        bot = y0 + ph < bottom ? y0 + ph : bottom;
        fill_span(vr->g_x, cur, vr->g_w, top - cur, 1, 4);
        mt = y0 + zx(d, pt2px_y(f.mt)); mb = y0 + ph - zx(d, pt2px_y(f.mb));
        if (mt < top) mt = top;
        if (mt > bot) mt = bot;
        if (mb > bot) mb = bot;
        if (mb < mt) mb = mt;
        fill_margin(vr->g_x, top, vr->g_w, mt - top, 0);
        fill_span(vr->g_x, mt, vr->g_w, mb - mt, 0, 0);
        fill_margin(vr->g_x, mb, vr->g_w, bot - mb, 0);
        if (bot > cur) cur = bot;
        vswr_mode(vhandle, MD_TRANS);
        for (mm = 0; mm <= hmm; mm++) {
            long y = y0 + zx(d, mm_y(mm)), w;
            if (y < r.g_y - hchar || y > r.g_y + r.g_h + hchar) continue;
            if (mm % 10 == 0) w = vr->g_w / 3;
            else if (mm % 5 == 0) w = vr->g_w / 5;
            else if (zx(d, mm_y(1)) >= 3) w = 2;
            else continue;
            hline2(right - w, right, y);
            if (mm % step == 0 && mm > 0 && mm < hmm) {
                sprintf(s, "%ld", mm / 10);
                v_gtext(vhandle, (short)(vr->g_x + 2), (short)(y + sys_asc / 2), s);
            }
        }
        vswr_mode(vhandle, MD_REPLACE);
    }
    fill_span(vr->g_x, cur, vr->g_w, bottom - cur, 1, 4);   /* the desk below the last page */
    vline2(right, vr->g_y, vr->g_y + vr->g_h - 1);
    vs_clip(vhandle, 0, pxy);
    in_ruler = 0;
    vsl_color(vhandle, 1);
    style_reset();
}

/* the corner where the rulers meet */
static void draw_corner(const GRECT *hr, const GRECT *vr, GRECT *clip)
{
    GRECT c = *clip, k;
    short pxy[4];
    k.g_x = vr->g_x; k.g_y = hr->g_y; k.g_w = vr->g_w; k.g_h = hr->g_h;
    if (!rc_inter(&k, &c)) return;
    pxy[0] = c.g_x; pxy[1] = c.g_y; pxy[2] = c.g_x + c.g_w - 1; pxy[3] = c.g_y + c.g_h - 1;
    vs_clip(vhandle, 1, pxy);
    fill(&k, 1, 4);
    vs_clip(vhandle, 0, pxy);
}

/* dotted lines where the margins and column gaps of a page are */
/* a dotted line down from (x, y1) to y2, drawn only inside clip, its dots
 * where they would be if all of it were drawn: a part drawn again (or
 * moved there by scrolling) matches what is around it */
static void vdots(long x, long y1, long y2, const GRECT *clip)
{
    short p[4];
    long a = y1 > clip->g_y ? y1 : clip->g_y, b = y2 < clip->g_y + clip->g_h - 1 ? y2 : clip->g_y + clip->g_h - 1;
    unsigned k, pat = 0xC0C0;
    if (x < clip->g_x || x >= clip->g_x + clip->g_w || a > b) return;
    k = (unsigned)((a - y1) & 15);
    if (k) pat = (unsigned)(((pat << k) | (pat >> (16 - k))) & 0xFFFF);
    vsl_udsty(vhandle, (short)pat);
    vsl_type(vhandle, 7);
    p[0] = p[2] = (short)x; p[1] = (short)a; p[3] = (short)b;
    v_pline(vhandle, 2, p);
}

static void draw_guides(DOC *d, int page, long x, long y, const GRECT *clip)
{
    PGFMT f;
    short p[4];
    long l, t, rt, b, colw;
    int c;
    page_fmt(d, page, &f);
    l = x + zx(d, pt2px_x(f.ml)); rt = x + zx(d, pt2px_x(PS(d).w - f.mr));
    t = y + zx(d, pt2px_y(f.mt)); b = y + zx(d, pt2px_y(PS(d).h - f.mb));
    vswr_mode(vhandle, MD_TRANS);
    vsl_type(vhandle, 3);                       /* dots */
    vsl_width(vhandle, 1);
    vsl_color(vhandle, pref_display != DISP_NORMAL ? ed_fg() : npens >= 16 ? 9 : 1);
    p[0] = (short)l; p[2] = (short)rt;
    p[1] = p[3] = (short)t;
    v_pline(vhandle, 2, p);
    p[1] = p[3] = (short)b;
    v_pline(vhandle, 2, p);
    vdots(l, t, b, clip);
    vdots(rt, t, b, clip);
    if (f.cols > 1) {
        colw = (pt2px_x(PS(d).w - f.ml - f.mr) - pt2px_x(f.colgap) * (f.cols - 1)) / f.cols;
        for (c = 1; c < f.cols; c++) {
            long cx = pt2px_x(f.ml) + c * colw + (c - 1) * pt2px_x(f.colgap);
            vdots(x + zx(d, cx), t, b, clip);
            vdots(x + zx(d, cx + pt2px_x(f.colgap)), t, b, clip);
        }
    }
    vsl_type(vhandle, 1);
#ifndef EDLITE
    {   /* bookmarks: a small flag where each is */
        int k;
        for (k = 0; k < NBMARK; k++) {
            long li, bx, by, h;
            short f[8];
            LINE *ln;
            if (d->bmark[k] < 0 || !d->nlines) continue;
            li = doc_find_line(d, d->bmark[k] <= d->len ? d->bmark[k] : d->len);
            ln = &d->lines[li];
            if (ln->page != page) continue;
            bx = x + zx(d, doc_x_of(d, li, d->bmark[k] <= d->len ? d->bmark[k] : d->len));
            by = y + zx(d, ln->py);
            h = zx(d, ln->asc + ln->desc);
            vsl_color(vhandle, npens >= 16 ? 2 : 1);
            f[0] = f[2] = (short)(bx - 1); f[1] = (short)(by - 4); f[3] = (short)(by + h);
            v_pline(vhandle, 2, f);
            vsf_interior(vhandle, FIS_SOLID);
            vsf_color(vhandle, npens >= 16 ? 2 : 1);
            vswr_mode(vhandle, MD_REPLACE);
            f[0] = (short)(bx - 1); f[1] = (short)(by - 4);
            f[2] = (short)(bx + 5); f[3] = (short)(by - 2);
            f[4] = (short)(bx - 1); f[5] = (short)(by);
            v_fillarea(vhandle, 3, f);
        }
    }
#endif
    vsl_color(vhandle, 1);
    vswr_mode(vhandle, MD_REPLACE);
}

/* the status line: white and black, or the colour theme's */
static void draw_status(DOC *d, GRECT *clip)
{
    GRECT sb, r = *clip;
    short pxy[4], dmy;
    int th = theme_on();
    status_rect(d, &sb);
    if (!rc_inter(&sb, &r)) return;
    pxy[0] = r.g_x; pxy[1] = r.g_y; pxy[2] = r.g_x + r.g_w - 1; pxy[3] = r.g_y + r.g_h - 1;
    vs_clip(vhandle, 1, pxy);
    sb.g_h--;
    fill(&sb, th ? theme_pen(TH_FACE) : 0, 0);
    pxy[0] = sb.g_x; pxy[2] = sb.g_x + sb.g_w - 1; pxy[1] = pxy[3] = sb.g_y + sb.g_h;
    vsl_color(vhandle, th ? theme_pen(TH_DARK) : 1); vsl_type(vhandle, 1); vsl_width(vhandle, 1);
    vswr_mode(vhandle, MD_REPLACE);
    v_pline(vhandle, 2, pxy);
    vsl_color(vhandle, 1);
    set_sys_font();
    vst_color(vhandle, th ? theme_pen(TH_TEXT) : 1);
    vst_alignment(vhandle, 0, 5, &dmy, &dmy);           /* the top of the text */
    vswr_mode(vhandle, MD_TRANS);
    v_gtext(vhandle, (short)(sb.g_x + 2), (short)(sb.g_y + (sb.g_h - hchar) / 2), d->info);
    vst_alignment(vhandle, 0, 0, &dmy, &dmy);
    vswr_mode(vhandle, MD_REPLACE);
    vs_clip(vhandle, 0, pxy);
    style_reset();
}

static void draw_divider(const GRECT *dv, GRECT *clip)
{
    GRECT r = *clip;
    short pxy[4];
    if (!dv->g_h || !rc_inter(dv, &r)) return;
    pxy[0] = r.g_x; pxy[1] = r.g_y; pxy[2] = r.g_x + r.g_w - 1; pxy[3] = r.g_y + r.g_h - 1;
    vs_clip(vhandle, 1, pxy);
    if (dv->g_h < SPLITH) fill(&r, 1, 4);            /* the codes' divider */
    else {
        /* the split bar: a raised strip, lines above and below, and in its
         * middle a grip to drag */
        GRECT b = *dv;
        short l[4], cx = (short)(dv->g_x + dv->g_w / 2), k;
        int th = theme_on();
        fill(&b, th ? theme_pen(TH_FACE) : 0, 0);
        vsl_type(vhandle, 1); vsl_width(vhandle, 1); vswr_mode(vhandle, MD_REPLACE);
        vsl_color(vhandle, th ? theme_pen(TH_DARK) : 1);
        l[0] = dv->g_x; l[2] = (short)(dv->g_x + dv->g_w - 1);
        l[1] = l[3] = dv->g_y; v_pline(vhandle, 2, l);
        l[1] = l[3] = (short)(dv->g_y + dv->g_h - 1); v_pline(vhandle, 2, l);
        if (th) {                                    /* a light edge: it stands out */
            vsl_color(vhandle, theme_pen(TH_LIGHT));
            l[1] = l[3] = (short)(dv->g_y + 1); v_pline(vhandle, 2, l);
            vsl_color(vhandle, theme_pen(TH_DARK));
        }
        for (k = 0; k < 3; k++) {                    /* the grip: three short lines */
            l[0] = (short)(cx - 12); l[2] = (short)(cx + 12);
            l[1] = l[3] = (short)(dv->g_y + 2 + k * 2);
            v_pline(vhandle, 2, l);
        }
        vsl_color(vhandle, 1);
    }
    vs_clip(vhandle, 0, pxy);
}

static void draw_panes(DOC *d, GRECT *clip);

static void draw_window2(DOC *d, GRECT *clip);

/* While its file selector is open the window is greyed: every other pixel
 * whitened, as the AES greys a disabled item */
static void draw_window(DOC *d, GRECT *clip)
{
    draw_window2(d, clip);
    if (!d->iconified && fsel_for(d)) {
        GRECT wa, r = *clip;
        short p[4];
        work_area(d, &wa);
        if (!rc_inter(&wa, &r)) return;
        p[0] = r.g_x; p[1] = r.g_y; p[2] = (short)(r.g_x + r.g_w - 1); p[3] = (short)(r.g_y + r.g_h - 1);
        vs_clip(vhandle, 1, p);
        vswr_mode(vhandle, MD_TRANS);
        vsf_interior(vhandle, FIS_PATTERN);
        vsf_style(vhandle, 4);
        vsf_color(vhandle, 0);
        vsf_perimeter(vhandle, 0);
        v_bar(vhandle, p);
        vsf_perimeter(vhandle, 1);
        vsf_interior(vhandle, FIS_SOLID);
        vswr_mode(vhandle, MD_REPLACE);
        vs_clip(vhandle, 0, p);
    }
}

static void draw_window2(DOC *d, GRECT *clip)
{
    GRECT tb, tp, cp, hr, vr, op, sdv;
    panes_ex(d, &tb, &hr, &vr, &tp, &cp, &op, &sdv);
    if (d->iconified) { draw_icon(d, &tp, clip); return; }
    draw_status(d, clip);
    if (tb.g_h) draw_toolbar(d, &tb, clip);
    if (d->view == VIEW_PREVIEW) { preview_draw(d, &tp, clip); return; }
    draw_panes(d, clip);
    if (split_on(d)) {
        GRECT r = *clip;
        draw_divider(&sdv, clip);
        if (rc_inter(&op, &r)) {           /* the other pane, with its own scroll */
            pane_swap(d);
            other_pane = 1;
            draw_panes(d, &r);
            other_pane = 0;
            pane_swap(d);
        }
    }
}

/* the text and codes panes (and the rulers) of the pane worked in */
static void draw_panes(DOC *d, GRECT *clip)
{
    GRECT tb, tp, cp, dv, hr, vr;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (hr.g_h) { draw_hruler(d, &hr, &tp, clip); draw_vruler(d, &vr, &tp, clip); draw_corner(&hr, &vr, clip); }
    if (tp.g_h) draw_text_pane(d, &tp, clip);
    if (other_pane && d->view != VIEW_CODES) return;     /* the codes pane isn't split */
    if (d->view == VIEW_SPLIT) {                         /* the bar over the codes */
        work_area(d, &dv);
        dv.g_y = cp.g_y - DIVH;
        dv.g_h = DIVH;
        draw_divider(&dv, clip);
    }
    if (cp.g_h) draw_codes_pane(d, &cp, clip);
}

void redraw_area(DOC *d, GRECT *area)
{
    GRECT r, a, scr;
    scr.g_x = 0; scr.g_y = 0; scr.g_w = scr_w; scr.g_h = scr_h;
    a = *area;
    if (!rc_inter(&scr, &a)) return;
#ifdef EDLOG
    clock_t t0 = clock();
    int nr = 0;
#endif
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    theme_for(d->slot);                          /* this window's colour theme */
    wind_get(d->wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        if (rc_inter(&a, &r)) {
            draw_window(d, &r);
#ifdef EDLOG
            nr++;
#endif
        }
        wind_get(d->wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    theme_global();
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
#ifdef EDLOG
    {   /* slow redraws, to find what holds the screen */
        long ms = (long)(clock() - t0) * 1000L / CLOCKS_PER_SEC;
        if (ms >= 250) edlog("redraw: window %d, %dx%d area in %d pieces: %ld ms", d->wh, a.g_w, a.g_h, nr, ms);
    }
#endif
}

void refresh_all(DOC *d)
{
    GRECT wa;
    work_area(d, &wa);
    redraw_area(d, &wa);
}

void refresh_text(DOC *d)
{
    GRECT tb, hr, vr, tp, cp, op;
    panes_ex(d, &tb, &hr, &vr, &tp, &cp, &op, NULL);
    if (d->view == VIEW_PREVIEW) { if (tp.g_h) redraw_area(d, &tp); return; }
    if (hr.g_h) { tp.g_h += (short)(tp.g_y - hr.g_y); tp.g_y = hr.g_y; tp.g_w += (short)(tp.g_x - vr.g_x); tp.g_x = vr.g_x; }
    if (tp.g_h) redraw_area(d, &tp);
    if (op.g_h && d->view != VIEW_CODES) redraw_area(d, &op);
}

/* ---- scrolling by moving what's on screen -------------------------------- */

/* is the whole of a on screen and in the window's first rectangle (the
 * window on top, nothing over it)? */
static int unobscured(DOC *d, const GRECT *a)
{
    GRECT r;
    short top = -1, dmy;
    wind_get(0, WF_TOP, &top, &dmy, &dmy, &dmy);
    if (top != d->wh) return 0;
    if (a->g_x < 0 || a->g_y < 0 || a->g_x + a->g_w > scr_w || a->g_y + a->g_h > scr_h) return 0;
    wind_get(d->wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    return r.g_x <= a->g_x && r.g_y <= a->g_y && r.g_x + r.g_w >= a->g_x + a->g_w && r.g_y + r.g_h >= a->g_y + a->g_h;
}

/* The view has scrolled from (osx, osy): copy what is still visible to
 * its new place and draw only the strip that came into view (and the
 * ruler that moved). Anything else draws the pane again. */
static void scroll_view(DOC *d, long osx, long osy)
{
    GRECT tb, hr, vr, tp, cp, ex;
    MFDB scr;
    short pxy[8];
    long dx, dy;
    /* the desk's pattern repeats every two pixels: move by an even
     * number, so what is moved still lines up with what is drawn */
    if ((d->scroll_y - osy) & 1) {
        d->scroll_y++; clamp_scroll(d);
        if ((d->scroll_y - osy) & 1) { d->scroll_y -= 2; clamp_scroll(d); }
    }
    if ((d->scroll_x - osx) & 1) {
        d->scroll_x++; clamp_scroll(d);
        if ((d->scroll_x - osx) & 1) { d->scroll_x -= 2; clamp_scroll(d); }
    }
    dx = d->scroll_x - osx; dy = d->scroll_y - osy;
    if (!dx && !dy) return;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (!tp.g_h || !tp.g_w || (dx && dy) || (dx & 1) || (dy & 1) || (dx < 0 ? -dx : dx) >= tp.g_w - 8 || (dy < 0 ? -dy : dy) >= tp.g_h - 8 ||
        (dx && !theme_on()) || !unobscured(d, &tp)) {
        refresh_text(d);
        return;
    }
    memset(&scr, 0, sizeof scr);
    ex = tp;
    if (dy) {
        long ady = dy < 0 ? -dy : dy;
        pxy[0] = pxy[4] = tp.g_x;
        pxy[2] = pxy[6] = (short)(tp.g_x + tp.g_w - 1);
        if (dy > 0) {           /* the text moves up */
            pxy[1] = (short)(tp.g_y + ady); pxy[3] = (short)(tp.g_y + tp.g_h - 1);
            pxy[5] = tp.g_y; pxy[7] = (short)(tp.g_y + tp.g_h - 1 - ady);
            ex.g_y = (short)(tp.g_y + tp.g_h - ady);
        } else {
            pxy[1] = tp.g_y; pxy[3] = (short)(tp.g_y + tp.g_h - 1 - ady);
            pxy[5] = (short)(tp.g_y + ady); pxy[7] = (short)(tp.g_y + tp.g_h - 1);
        }
        ex.g_h = (short)ady;
    } else {
        long adx = dx < 0 ? -dx : dx;
        pxy[1] = pxy[5] = tp.g_y;
        pxy[3] = pxy[7] = (short)(tp.g_y + tp.g_h - 1);
        if (dx > 0) {           /* the text moves left */
            pxy[0] = (short)(tp.g_x + adx); pxy[2] = (short)(tp.g_x + tp.g_w - 1);
            pxy[4] = tp.g_x; pxy[6] = (short)(tp.g_x + tp.g_w - 1 - adx);
            ex.g_x = (short)(tp.g_x + tp.g_w - adx);
        } else {
            pxy[0] = tp.g_x; pxy[2] = (short)(tp.g_x + tp.g_w - 1 - adx);
            pxy[4] = (short)(tp.g_x + adx); pxy[6] = (short)(tp.g_x + tp.g_w - 1);
        }
        ex.g_w = (short)adx;
    }
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    vs_clip(vhandle, 0, pxy);
    vro_cpyfm(vhandle, S_ONLY, pxy, &scr, &scr);
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
    redraw_area(d, &ex);
    /* The desk's VDI pattern: TOS's repeats every two pixels, but other
     * VDIs' can be coarser, and then what was moved no longer lines up
     * with the desk drawn beside it (bands that look like droppings).
     * So with a patterned desk (no colour theme) the desk either side of
     * the pages and between them is drawn again. */
    if (dy && !theme_on()) {
        long x = scr_x(d, &tp, 0) - 1, pw = zx(d, page_w_px(d)), ph = page_h_view(d);
        int p, np = doc_npages(d);
        GRECT k;
        k = tp; if (x > tp.g_x) { k.g_w = (short)(x - tp.g_x); redraw_area(d, &k); }
        k = tp; if (x + pw + 2 < tp.g_x + tp.g_w) { k.g_x = (short)(x + pw + 2); k.g_w = (short)(tp.g_x + tp.g_w - k.g_x); redraw_area(d, &k); }
        for (p = 0; p + 1 < np; p++) {
            long g0 = scr_y(d, &tp, p, 0) + ph + 1, g1 = scr_y(d, &tp, p + 1, 0) - 1;
            if (g1 <= tp.g_y) continue;
            if (g0 >= tp.g_y + tp.g_h) break;
            if (g0 < tp.g_y) g0 = tp.g_y;
            if (g1 > tp.g_y + tp.g_h) g1 = tp.g_y + tp.g_h;
            k.g_x = (short)x; k.g_w = (short)(pw + 2); k.g_y = (short)g0; k.g_h = (short)(g1 - g0);
            if (k.g_h > 0) redraw_area(d, &k);
        }
    }
    if (dy && vr.g_w) redraw_area(d, &vr);
    if (dx && hr.g_h) redraw_area(d, &hr);
}

/* what the rulers show: the caret's page and paragraph, the scroll */
static long ruler_key(DOC *d)
{
    PARA *p = doc_para_at(d, d->cursor);
    LINE *ln = d->nlines ? &d->lines[doc_find_line(d, d->cursor)] : NULL;
    long k = d->scroll_x * 7 + d->zoom * 131L + p->left * 17L + p->first * 23L + PS(d).ml * 3L + PS(d).w;
    if (ln) k += ln->page * 1009L + (ln->x - ln->tx0) * 29L + (ln->flags & LF_CELL ? 5 : 0) + ln->cell * 3L;
    return k;
}

static void refresh_hruler(DOC *d)
{
    GRECT tb, hr, vr, tp, cp;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (hr.g_h) redraw_area(d, &hr);
}

static void refresh_ruler_if_changed(DOC *d)
{
    if (rulers_on(d) && rul_state[d->slot] != ruler_key(d)) refresh_hruler(d);
}
void refresh_codes(DOC *d)
{
    GRECT tb, hr, vr, tp, cp, op;
    panes_ex(d, &tb, &hr, &vr, &tp, &cp, &op, NULL);
    if (cp.g_h) redraw_area(d, &cp);
    if (op.g_h && d->view == VIEW_CODES) redraw_area(d, &op);
}
void refresh_toolbar(DOC *d) { GRECT tb, tp, cp; get_panes(d, &tb, &tp, &cp); if (tb.g_h) redraw_area(d, &tb); }

static void refresh_tb_if_changed(DOC *d)
{
    if (d->toolbar && tb_state[d->slot] != tb_key(d)) refresh_toolbar(d);
}

/* redraw from line li to the bottom of the pane; from the top of its page
 * when the page has columns, tables or pictures (they move together) */
static void refresh_from1(DOC *d, long li)
{
    GRECT tb, tp, cp, r;
    LINE *ln;
    long sy, ey;
    PGFMT f;
    get_panes(d, &tb, &tp, &cp);
    if (!tp.g_h) return;
    if (li < 0) li = 0;
    if (li >= d->nlines) li = d->nlines - 1;
    ln = &d->lines[li];
    page_fmt(d, ln->page, &f);
    if (f.cols > 1 || (ln->flags & LF_CELL) || d->a.nobj || ln->mark) sy = scr_y(d, &tp, ln->page, 0) - 2;
    else sy = scr_y(d, &tp, ln->page, ln->py) - 1;
    ey = tp.g_y + tp.g_h;
    if (sy < tp.g_y) sy = tp.g_y;
    if (ey <= sy) return;
    r.g_x = tp.g_x; r.g_w = tp.g_w;
    r.g_y = (short)sy; r.g_h = (short)(ey - sy);
    redraw_area(d, &r);
}

static void refresh_lines1(DOC *d, long la, long lb)
{
    GRECT tb, tp, cp, r;
    long sy = 32767, ey = -32767, li;
    get_panes(d, &tb, &tp, &cp);
    if (!tp.g_h) return;
    if (la < 0) la = 0;
    if (lb >= d->nlines) lb = d->nlines - 1;
    for (li = la; li <= lb; li++) {
        LINE *ln = &d->lines[li];
        long y = scr_y(d, &tp, ln->page, ln->py);
        if (y < sy) sy = y;
        if (y + zx(d, ln->lh > ln->asc + ln->desc ? ln->lh : ln->asc + ln->desc) > ey)
            ey = y + zx(d, ln->lh > ln->asc + ln->desc ? ln->lh : ln->asc + ln->desc);
    }
    sy -= 1; ey += 2;
    if (sy < tp.g_y) sy = tp.g_y;
    if (ey > tp.g_y + tp.g_h) ey = tp.g_y + tp.g_h;
    if (ey <= sy) return;
    r.g_x = tp.g_x; r.g_w = tp.g_w; r.g_y = (short)sy; r.g_h = (short)(ey - sy);
    redraw_area(d, &r);
}

/* the same in the other pane of a split window, which shows the same text */
static void refresh_from(DOC *d, long li)
{
    refresh_from1(d, li);
    if (split_on(d) && d->view != VIEW_CODES && !other_pane) {
        pane_swap(d); other_pane = 1;
        refresh_from1(d, li);
        other_pane = 0; pane_swap(d);
    }
}

static void refresh_lines(DOC *d, long la, long lb)
{
    refresh_lines1(d, la, lb);
    if (split_on(d) && d->view != VIEW_CODES && !other_pane) {
        pane_swap(d); other_pane = 1;
        refresh_lines1(d, la, lb);
        other_pane = 0; pane_swap(d);
    }
}

/* ================================================================= */
/*  Window furniture                                                  */
/* ================================================================= */

void set_sliders(DOC *d)
{
    GRECT tb, tp, cp;
    long th, tw, vis;
    short sz, pos;
    if (d->iconified) return;
    get_panes(d, &tb, &tp, &cp);
    if (d->view == VIEW_PREVIEW) {
        short vs, vp, hs, hp;
        preview_sliders(d, &tp, &vs, &vp, &hs, &hp);
        if (vs != d->vsz)  { wind_set(d->wh, WF_VSLSIZE, vs, 0, 0, 0); d->vsz = vs; }
        if (vp != d->vpos) { wind_set(d->wh, WF_VSLIDE, vp, 0, 0, 0); d->vpos = vp; }
        if (hs != d->hsz)  { wind_set(d->wh, WF_HSLSIZE, hs, 0, 0, 0); d->hsz = hs; }
        if (hp != d->hpos) { wind_set(d->wh, WF_HSLIDE, hp, 0, 0, 0); d->hpos = hp; }
        return;
    }
    if (codes_focus(d)) {
        th = d->cv.nl; vis = codes_vis(d);
        sz  = th > vis ? (short)(vis * 1000L / th) : 1000;
        pos = th > vis ? (short)(d->cv.top * 1000L / (th - vis)) : 0;
        tw = 0;
    } else {
        th = content_h(d);
        sz  = th > tp.g_h ? (short)(tp.g_h * 1000L / th) : 1000;
        pos = th > tp.g_h ? (short)(d->scroll_y * 1000L / (th - tp.g_h)) : 0;
        tw = content_w(d);
    }
    if (sz != d->vsz)  { wind_set(d->wh, WF_VSLSIZE, sz, 0, 0, 0); d->vsz = sz; }
    if (pos != d->vpos) { wind_set(d->wh, WF_VSLIDE, pos, 0, 0, 0); d->vpos = pos; }
    sz  = tw > tp.g_w ? (short)(tp.g_w * 1000L / tw) : 1000;
    pos = tw > tp.g_w ? (short)(d->scroll_x * 1000L / (tw - tp.g_w)) : 0;
    if (sz != d->hsz)  { wind_set(d->wh, WF_HSLSIZE, sz, 0, 0, 0); d->hsz = sz; }
    if (pos != d->hpos) { wind_set(d->wh, WF_HSLIDE, pos, 0, 0, 0); d->hpos = pos; }
}

void set_name(DOC *d)
{
    if (d->iconified) sprintf(d->name, " %.10s ", d->base);
    else sprintf(d->name, " %s%s ", d->dirty ? "* " : "", d->base);
    wind_set_str(d->wh, WF_NAME, d->name);
    d->shown_dirty = d->dirty;
    if (d->wh >= 0 && d->nlines && !d->iconified) set_info(d);     /* Edited / Saved */
}

void set_info(DOC *d)
{
    static const int jn[4] = { S_J_LEFT, S_J_CENTRE, S_J_RIGHT, S_J_FULL };
    char s[200], extra[40], state[40];
    CELL *c = &d->ins;
    PARA *p;
    LINE *ln;
    if (d->iconified) return;
    ln = &d->lines[doc_find_line(d, d->cursor)];
    p = doc_para_at(d, d->cursor);
    extra[0] = 0;
    if (p->list) sprintf(extra + strlen(extra), " %s", L(p->list == LIST_BULLET ? S_I_BULLET : S_I_NUMBER));
    if (p->toc) sprintf(extra + strlen(extra), " H%d", p->toc);
    if (doc_in_table(d, d->cursor, NULL, NULL)) sprintf(extra + strlen(extra), " %s", L(S_I_TABLE));
    /* edited or saved first, where it's seen; a print in the background */
    if (d->bgprint) sprintf(state, L(S_I_PRINTING), d->bgprint, doc_npages(d));
    else strcpy(state, d->dirty ? L(S_I_EDITED) : d->path[0] || d->slotdoc ? L(S_I_SAVED) : "");
    if (d->view == VIEW_PREVIEW) {
        sprintf(s, " %s%s%s  %s", state[0] ? "[" : "", state, state[0] ? "]" : "", L(M_PREVIEW));
        if (strcmp(s, d->info)) { GRECT sb; strcpy(d->info, s); status_rect(d, &sb); redraw_area(d, &sb); }
        return;
    }
    if (d->colmode) strcat(extra, L(S_I_COLUMN));
    sprintf(s, " %s%s%s %.18s %dpt %s%s%s  %s%s  %s %d/%d  %d%%%s", state[0] ? "[" : "", state, state[0] ? "]" : "", fonts[c->font < nfonts ? c->font : 0].name, c->size,
            c->attr & A_BOLD ? "B" : "", c->attr & A_ITALIC ? "I" : "", c->attr & A_UNDER ? "U" : "",
            L(jn[p->just & 3]), extra, L(S_I_PAGE), ln->page + 1, doc_npages(d), d->zoom,
            d->view == VIEW_CODES ? L(S_I_QUICK) : d->view == VIEW_SPLIT ? (d->focus ? L(S_I_CODES) : L(S_I_TEXT)) : "");
    if (strcmp(s, d->info)) {
        GRECT sb;
        strcpy(d->info, s);
        status_rect(d, &sb);
        redraw_area(d, &sb);
    }
}

/* scroll so the caret is visible; returns 1 if the view moved */
static int ensure_visible(DOC *d)
{
    GRECT tb, tp, cp;
    long li, x, osx = d->scroll_x, osy = d->scroll_y, top, bot;
    LINE *ln;
    get_panes(d, &tb, &tp, &cp);
    if (!tp.g_h) return 0;
    li = doc_find_line(d, d->cursor);
    ln = &d->lines[li];
    x = view_page_left(d, &tp) + zx(d, doc_x_of(d, li, d->cursor));
    top = view_page_top(d, ln->page) + zx(d, ln->py) - 4;
    bot = view_page_top(d, ln->page) + zx(d, ln->py + ln->asc + ln->desc) + 4;
    if (top < d->scroll_y) d->scroll_y = top;
    else if (bot > d->scroll_y + tp.g_h) d->scroll_y = bot - tp.g_h;
    if (x < d->scroll_x + 8) d->scroll_x = x - tp.g_w / 3;
    else if (x + 16 > d->scroll_x + tp.g_w) d->scroll_x = x + 16 - tp.g_w + tp.g_w / 3;
    clamp_scroll(d);
    return osx != d->scroll_x || osy != d->scroll_y;
}

void ensure_caret(DOC *d) { ensure_visible(d); }

/* Previous page, Next page, Go to page: the caret to the page's first
 * line, the page's top to the top of the window */
void goto_page(DOC *d, int page)
{
    long oc = d->cursor, oa = d->anchor;
    int np = doc_npages(d);
    if (d->iconified || !d->nlines) return;
    if (d->view == VIEW_CODES) set_view(d, VIEW_DOC);
    if (page < 0) page = 0;
    if (page >= np) page = np - 1;
    d->anchor = -1;
    d->cursor = d->lines[d->pgline[page]].start;
    d->want_x = -1;
    d->scroll_y = view_page_top(d, page) - PGAP / 2;
    clamp_scroll(d);
    refresh_text(d);
    set_sliders(d);
    after_move(d, oc, oa);
}

static void ensure_codes_visible(DOC *d)
{
    long li = codes_line_of(&d->cv, d->cv.cursor), vis = codes_vis(d);
    if (li < d->cv.top) d->cv.top = li;
    else if (li >= d->cv.top + vis) d->cv.top = li - vis + 1;
    clamp_scroll(d);
}

/* the lines before an edit, to find which ones it changed */
static LINE *old_lines;
static long old_n = -1, old_cap, old_len, old_cur;

static void keep_lines(DOC *d)
{
    old_n = -1;
    if (d->nlines > old_cap) {
        LINE *n = (LINE *)realloc(old_lines, d->nlines * sizeof(LINE));
        if (!n) return;
        old_lines = n;
        old_cap = d->nlines;
    }
    memcpy(old_lines, d->lines, d->nlines * sizeof(LINE));
    old_n = d->nlines;
    old_len = d->len;
    old_cur = doc_find_line(d, d->cursor);         /* where the caret was drawn */
}

static int same_line(const LINE *a, const LINE *b, long shift)
{
    LINE t = *a;
    t.start += shift;
    return !memcmp(&t, b, sizeof(LINE));
}

/* the screen rows of lines [a, b] of an array */
static void lines_span(DOC *d, GRECT *tp, LINE *l, long a, long b, long *sy, long *ey)
{
    long i;
    for (i = a; i <= b; i++) {
        long y = scr_y(d, tp, l[i].page, l[i].py), h = l[i].lh > l[i].asc + l[i].desc ? l[i].lh : l[i].asc + l[i].desc;
        if (y - 1 < *sy) *sy = y - 1;
        if (y + zx(d, h) + 2 > *ey) *ey = y + zx(d, h) + 2;
    }
}

/* redraw only the lines that the edit changed; 0 if that can't be told */
static int refresh_changed1(DOC *d)
{
    GRECT tb, tp, cp, r;
    long a = 0, eo, en, shift = d->len - old_len, sy = 0x7fffffffL, ey = -0x7fffffffL;
    if (old_n < 1 || !d->nlines) return 0;
    get_panes(d, &tb, &tp, &cp);
    if (!tp.g_h) return 1;
    /* unchanged at the front, then at the back (shifted by the edit) */
    while (a < old_n && a < d->nlines && same_line(&old_lines[a], &d->lines[a], 0)) a++;
    eo = old_n - 1; en = d->nlines - 1;
    while (eo >= a && en >= a && same_line(&old_lines[eo], &d->lines[en], shift)) { eo--; en--; }
    /* the caret's old line, if the caret jumped (Tab adding a table row) */
    if (old_cur >= 0 && old_cur < old_n && (old_cur < a || old_cur > eo))
        lines_span(d, &tp, old_lines, old_cur, old_cur, &sy, &ey);
    if (a > eo && a > en && sy > ey) return 1;           /* nothing moved */
    if (a <= eo) lines_span(d, &tp, old_lines, a, eo, &sy, &ey);
    if (a <= en) lines_span(d, &tp, d->lines, a, en, &sy, &ey);
    if (sy < tp.g_y) sy = tp.g_y;
    if (ey > tp.g_y + tp.g_h) ey = tp.g_y + tp.g_h;
    if (ey <= sy) return 1;
    r.g_x = tp.g_x; r.g_w = tp.g_w; r.g_y = (short)sy; r.g_h = (short)(ey - sy);
    redraw_area(d, &r);
    return 1;
}

/* in the other pane of a split window too */
static int refresh_changed(DOC *d)
{
    int r = refresh_changed1(d);
    if (r && split_on(d) && d->view != VIEW_CODES) {
        pane_swap(d); other_pane = 1;
        refresh_changed1(d);
        other_pane = 0; pane_swap(d);
    }
    return r;
}

static void refresh_chart_at(DOC *d, long pos);

static void edited(DOC *d, long from, int typing)
{
    long li;
    int oldpages = d->npages;
    undo_commit(d, typing);
    doc_layout(d);
    d->want_x = -1;
    d->selobj = -1;
    if (d->view != VIEW_DOC) d->codes_stale = 1;
    if (ensure_visible(d) || d->npages != oldpages) refresh_text(d);
    else if (!refresh_changed(d)) {
        li = doc_find_line(d, from);
        refresh_from(d, li > 0 ? li - 1 : 0);
    }
    old_n = -1;
    refresh_chart_at(d, from);
    if (d->dirty != d->shown_dirty) set_name(d);
    set_sliders(d);
    set_info(d);
    refresh_tb_if_changed(d);
    refresh_ruler_if_changed(d);
    preview_changed(d);
}

void after_edit(DOC *d, long from)   { edited(d, from, 0); update_menu(d); }
void after_typing(DOC *d, long from) { edited(d, from, 1); }

static int fit_zoom(DOC *d);
static void zoom_apply(DOC *d, int zoom);

/* a change that may move anything (page set-up, tables, pictures...) */
void doc_changed(DOC *d)
{
    undo_commit(d, 0);
    doc_layout(d);
    d->want_x = -1;
    if (d->cursor > d->len) d->cursor = d->len;
    doc_sync_ins(d);
    if (d->view != VIEW_DOC) d->codes_stale = 1;
    clamp_scroll(d);
    ensure_visible(d);
    refresh_all(d);
    set_name(d);
    set_sliders(d);
    set_info(d);
    update_menu(d);
    preview_changed(d);
    /* a new page size: the fit to the window again */
    if (d->fitw && d->view != VIEW_CODES && fit_zoom(d) != d->zoom) zoom_apply(d, fit_zoom(d));
}

/* the caret at pos, inverted where the window shows it: drawing it twice
 * takes it away again, so a move is two small boxes, not a redraw */
/* invert c where the window shows it (through its rectangle list) */
static void xor_visible(DOC *d, GRECT *c)
{
    GRECT r;
    short pxy[4];
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    vswr_mode(vhandle, MD_XOR);
    vsf_interior(vhandle, FIS_SOLID);
    vsf_color(vhandle, 1);
    wind_get(d->wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        if (rc_inter(c, &r)) {
            pxy[0] = r.g_x; pxy[1] = r.g_y; pxy[2] = (short)(r.g_x + r.g_w - 1); pxy[3] = (short)(r.g_y + r.g_h - 1);
            vs_clip(vhandle, 1, pxy);
            xor_box(c->g_x, c->g_y, c->g_x + c->g_w - 1, c->g_y + c->g_h - 1);
        }
        wind_get(d->wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    vs_clip(vhandle, 0, pxy);
    vswr_mode(vhandle, MD_REPLACE);
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
}

static void caret_xor(DOC *d, long pos)
{
    GRECT tb, tp, cp, c;
    long li, x, y;
    LINE *ln;
    get_panes(d, &tb, &tp, &cp);
    if (!tp.g_h || !d->nlines) return;
    li = doc_find_line(d, pos);
    ln = &d->lines[li];
    x = scr_x(d, &tp, doc_x_of(d, li, pos));
    y = scr_y(d, &tp, ln->page, ln->py);
    c.g_x = (short)x; c.g_y = (short)y;
    c.g_w = (short)(d->zoom >= 150 ? 3 : 2);
    c.g_h = (short)zx(d, ln->asc + ln->desc);
    if (!rc_inter(&tp, &c)) return;
    xor_visible(d, &c);
}

void after_move(DOC *d, long oc, long oa)
{
    long lo = oc, hi = oc;
#define SPAN(v) do { if ((v) >= 0) { if ((v) < lo) lo = (v); if ((v) > hi) hi = (v); } } while (0)
    SPAN(oa); SPAN(d->cursor); SPAN(d->anchor);
#undef SPAN
    long osx = d->scroll_x, osy = d->scroll_y;
    int caret_shown = d->view == VIEW_DOC || d->focus == 0;
    if (d->selobj < 0 && oa < 0 && d->anchor < 0 && caret_shown && !ensure_visible(d)) {
        /* only the caret moved: take it away and put it back */
        if (oc != d->cursor) { caret_xor(d, oc); caret_xor(d, d->cursor); }
        goto done;
    }
    d->scroll_x = osx; d->scroll_y = osy;
    if (d->selobj < 0 && oa >= 0 && oa == d->anchor && !d->colsel) {
        /* a selection grew or shrank: only between the old end and the new */
        lo = oc < d->cursor ? oc : d->cursor;
        hi = oc < d->cursor ? d->cursor : oc;
    }
    if (d->selobj >= 0) { d->selobj = -1; refresh_text(d); }
    else if (ensure_visible(d)) {
        /* move the view by blitting, then the caret's old and new lines */
        scroll_view(d, osx, osy);
        refresh_lines(d, doc_find_line(d, lo), doc_find_line(d, hi));
        set_sliders(d);
    }
    else refresh_lines(d, doc_find_line(d, lo), doc_find_line(d, hi));
done:
    if (d->anchor < 0) doc_sync_ins(d);
    update_menu(d);
    set_info(d);
    refresh_tb_if_changed(d);
    refresh_ruler_if_changed(d);
    fontsel_sync();
}

void select_range(DOC *d, long a, long b)
{
    long oc = d->cursor, oa = d->anchor;
    if (d->colsel) {                            /* a block goes: its lines again */
        d->colsel = 0;
        if (oa >= 0) refresh_lines(d, doc_find_line(d, oa < oc ? oa : oc), doc_find_line(d, oa < oc ? oc : oa));
    }
    d->anchor = a;
    d->cursor = b;
    after_move(d, oc, oa);
}

/* ---- views ------------------------------------------------------------- */

void apply_codes(DOC *d)
{
    if (!d->cv.dirty) return;
    busy_ptr(1);
    codes_apply(d);
    undo_commit(d, 0);
    doc_layout(d);
    busy_ptr(0);
    d->codes_stale = 0;
    ensure_visible(d);
    refresh_text(d);
    if (d->dirty != d->shown_dirty) set_name(d);
    set_sliders(d);
    set_info(d);
    refresh_tb_if_changed(d);
    preview_changed(d);
}

void rebuild_codes(DOC *d)
{
    d->cv.cols = codes_cols_of(d);
    codes_build(d);
    d->codes_stale = 0;
    ensure_codes_visible(d);
    refresh_codes(d);
    if (codes_focus(d)) set_sliders(d);
}

void set_view(DOC *d, int view)
{
    if (view == d->view) return;
    if (view == VIEW_PREVIEW || d->view == VIEW_PREVIEW) {
        /* the print preview: the pages as they will print, in this window */
        if (view == VIEW_PREVIEW) {
            if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
            doc_layout(d);
            d->pv_view = d->view;
            d->pv_page = d->nlines ? d->lines[doc_find_line(d, d->cursor)].page : 0;
            d->pv_sx = d->pv_sy = 0;
            d->view = VIEW_PREVIEW;
        } else d->view = d->pv_view == VIEW_PREVIEW ? VIEW_DOC : d->pv_view;
        d->vsz = d->vpos = d->hsz = d->hpos = -1;
        if (d->view != VIEW_PREVIEW) { clamp_scroll(d); ensure_visible(d); }
        refresh_all(d);
        set_sliders(d);
        set_info(d);
        update_menu(d);
        if (view != VIEW_PREVIEW && view != d->view) set_view(d, view);
        return;
    }
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    if (d->view == VIEW_DOC) {
        d->cv.cols = codes_cols_of(d);
        codes_build(d);
        d->codes_stale = 0;
    }
    d->view = view;
    d->focus = view == VIEW_CODES ? 1 : 0;
    d->vsz = d->vpos = -1;
    if (view != VIEW_DOC) {
        d->cv.cols = codes_cols_of(d);
        codes_wrap(&d->cv, d->cv.cols);
        ensure_codes_visible(d);
    }
    clamp_scroll(d);
    ensure_visible(d);
    refresh_all(d);
    set_sliders(d);
    set_info(d);
    update_menu(d);
}

void set_toolbar(DOC *d, int on)
{
    d->toolbar = on;
    clamp_scroll(d);
    refresh_all(d);
    set_sliders(d);
    update_menu(d);
}

/* the zoom at which the page, its shadow and the gaps fill the page pane's width */
static int fit_zoom(DOC *d)
{
    GRECT tb, tp, cp;
    long pw = page_w_px(d), ph = page_h_px(d), z;
    get_panes(d, &tb, &tp, &cp);
    if (pw <= 0 || tp.g_w <= 0 || ph <= 0 || tp.g_h <= 0) return d->zoom;
    z = (tp.g_w - 2L * PGAP - 4) * 100 / pw;
    if (d->fitw == FIT_PAGE) {                    /* the whole page: its height too */
        long zh = (tp.g_h - 2L * PGAP - 4) * 100 / ph;
        if (zh < z) z = zh;
    }
    return (int)(z < ZOOM_MIN ? ZOOM_MIN : z > ZOOM_MAX ? ZOOM_MAX : z);
}

static void zoom_apply(DOC *d, int zoom)
{
    if (zoom < ZOOM_MIN) zoom = ZOOM_MIN;
    if (zoom > ZOOM_MAX) zoom = ZOOM_MAX;
    if (zoom == d->zoom) { update_menu(d); return; }
    d->zoom = zoom;
    if (d->view == VIEW_CODES) set_view(d, VIEW_DOC);
    d->vsz = d->vpos = d->hsz = d->hpos = -1;
    clamp_scroll(d);
    ensure_visible(d);
    refresh_all(d);
    set_sliders(d);
    set_info(d);
    update_menu(d);
}

/* a zoom chosen by the user: the fit to the window's width ends */
void set_zoom(DOC *d, int zoom) { d->fitw = 0; zoom_apply(d, zoom); }

/* on: FIT_WIDTH or FIT_PAGE, 0 off */
void set_fit_width(DOC *d, int on)
{
    d->fitw = on;
    if (on && d->view == VIEW_CODES) set_view(d, VIEW_DOC);
    if (on) zoom_apply(d, fit_zoom(d));
    else update_menu(d);
}

void win_resized(DOC *d)
{
    if (d->view != VIEW_DOC) {
        long cols = codes_cols_of(d);
        if (cols != d->cv.cols) codes_wrap(&d->cv, cols);
    }
    /* fitting the page to the window: the zoom follows the new width */
    if (d->fitw && !d->iconified && d->view != VIEW_CODES && fit_zoom(d) != d->zoom) { zoom_apply(d, fit_zoom(d)); return; }
    clamp_scroll(d);
    refresh_all(d);
    set_sliders(d);
}

/* ---- window creation, iconify, tiling ------------------------------------ */

static short win_kind(void) { return (short)(WKIND | (native_iconify ? SMALLER : 0)); }
#define IKIND (NAME | MOVER)

/* the name a document window's place is kept under */
static const char *doc_wkey(DOC *d)
{
    static char k[40];
    const char *p = d->path[0] ? d->path : d->base, *b = strrchr(p, '\\');
    sprintf(k, "D:%.36s", b ? b + 1 : p);
    return k;
}

int open_window(DOC *d)
{
    short x, y, w, h, slot = (short)d->slot, dx, dy, dw, dh;
    long want;
    d->wh = wind_create(win_kind(), desk.g_x, desk.g_y, desk.g_w, desk.g_h);
    if (d->wh < 0) {
        alert(1, 3, AL_NO_WINDOWS, AB_OK);
        doc_free(d);
        d->used = 0;
        return 0;
    }
    set_name(d);
    d->info[0] = 0;
    /* where the page is much wider than the screen (ST medium has narrow
     * pixels), start at the largest zoom that shows the page's width */
    if (d->zoom == 100 && page_w_px(d) + 2 * PGAP + 40 > desk.g_w * 5L / 4) {
        int k;
        for (k = NZOOM - 1; k > 0; k--)
            if (zooms[k] <= 100 && page_w_px(d) * zooms[k] / 100 + 2 * PGAP + 40 <= desk.g_w) break;
        d->zoom = zooms[k];
    }
    want = zx(d, page_w_px(d)) + 2 * PGAP + (pref_rulers ? RULW : 0);
    wind_calc(WC_BORDER, win_kind(), 0, 0, (short)(want < desk.g_w ? want : desk.g_w), 100, &dx, &dy, &dw, &dh);
    slot = (short)(slot % 8);
    x = (short)(desk.g_x + slot * 16);
    y = (short)(desk.g_y + slot * 16);
    w = dw;
    if (w > desk.g_w - slot * 16) w = (short)(desk.g_w - slot * 16);
    h = (short)(desk.g_h - slot * 16);
    /* a document opened again in this session goes where it was */
    if (winpos_get(doc_wkey(d), &x, &y, &w, &h)) {
        if (w > desk.g_w) w = desk.g_w;
        if (h > desk.g_h) h = desk.g_h;
        winpos_clamp(&x, &y, w, h);
    }
    wind_open(d->wh, x, y, w, h);
    doc_sync_ins(d);
    set_sliders(d);
    set_info(d);
    update_menu(d);
    update_winmenu();
    return 1;
}

void close_window(DOC *d)
{
    if (d->iconified) winpos_set(doc_wkey(d), d->ic_x, d->ic_y, d->ic_w, d->ic_h);
    else winpos_save(doc_wkey(d), d->wh);
    wind_close(d->wh);
    wind_delete(d->wh);
    d->wh = -1;
}

static void icon_rect(DOC *d, short kind, short *x, short *y, short *w, short *h)
{
    short dx, dy, slot = (short)d->slot;
    wind_calc(WC_BORDER, kind, 0, 0, 10 * wchar, 30 + hchar + 6, &dx, &dy, w, h);
    *x = desk.g_x + slot * (*w + 4);
    *y = desk.g_y + desk.g_h - *h;
}

void iconify(DOC *d, short *rect)
{
    short x, y, w, h;
    if (d->iconified) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    wind_get(d->wh, WF_CURRXYWH, &d->ic_x, &d->ic_y, &d->ic_w, &d->ic_h);
    if (native_iconify) {
        if (rect) { x = rect[0]; y = rect[1]; w = rect[2]; h = rect[3]; }
        else icon_rect(d, NAME, &x, &y, &w, &h);
        d->iconified = 1;
        wind_set(d->wh, WF_ICONIFY, x, y, w, h);
        set_name(d);
    } else {
        /* older AES: swap the window for a small one showing an icon */
        icon_rect(d, IKIND, &x, &y, &w, &h);
        close_window(d);
        d->wh = wind_create(IKIND, x, y, w, h);
        if (d->wh < 0) { d->iconified = 0; open_window(d); return; }
        d->iconified = 1;
        set_name(d);
        wind_open(d->wh, x, y, w, h);
    }
    update_winmenu();
}

void uniconify(DOC *d, short *rect)
{
    if (!d->iconified) return;
    if (native_iconify) {
        d->iconified = 0;
        if (rect) wind_set(d->wh, WF_UNICONIFY, rect[0], rect[1], rect[2], rect[3]);
        else wind_set(d->wh, WF_UNICONIFY, d->ic_x, d->ic_y, d->ic_w, d->ic_h);
    } else {
        close_window(d);
        d->iconified = 0;
        d->wh = wind_create(win_kind(), desk.g_x, desk.g_y, desk.g_w, desk.g_h);
        if (d->wh < 0) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
        wind_open(d->wh, d->ic_x, d->ic_y, d->ic_w, d->ic_h);
    }
    set_name(d);
    d->info[0] = 0;
    d->vsz = d->vpos = d->hsz = d->hpos = -1;
    clamp_scroll(d);
    set_sliders(d);
    set_info(d);
    update_menu(d);
    update_winmenu();
}

void send_to_back(DOC *d)
{
    int i;
    if (native_bottom) wind_set(d->wh, WF_BOTTOM, 0, 0, 0, 0);
    else
        for (i = 0; i < MAX_DOCS; i++)
            if (DOC_USED(i) && docs[i] != d) wind_set(docs[i]->wh, WF_TOP, 0, 0, 0, 0);
    update_menu(top_doc());
    update_winmenu();
}

/* how: 0 side by side, 1 stacked, 2 cascade */
void tile_windows(int how)
{
    DOC *w[MAX_DOCS];
    int n = 0, i;
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i) && !docs[i]->iconified) w[n++] = docs[i];
    if (!n) return;
    for (i = 0; i < n; i++) {
        short x, y, ww, hh;
        if (how == 0) {
            ww = (short)(desk.g_w / n); hh = desk.g_h;
            x = (short)(desk.g_x + i * ww); y = desk.g_y;
        } else if (how == 1) {
            ww = desk.g_w; hh = (short)(desk.g_h / n);
            x = desk.g_x; y = (short)(desk.g_y + i * hh);
        } else {
            ww = (short)(desk.g_w - (n - 1) * 20); hh = (short)(desk.g_h - (n - 1) * 20);
            x = (short)(desk.g_x + i * 20); y = (short)(desk.g_y + i * 20);
        }
        if (ww < 200) ww = 200;
        if (hh < 80) hh = 80;
        wind_set(w[i]->wh, WF_CURRXYWH, x, y, ww, hh);
        if (how == 2) wind_set(w[i]->wh, WF_TOP, 0, 0, 0, 0);
        win_resized(w[i]);
    }
    update_winmenu();
}

/* ================================================================= */
/*  Keyboard                                                          */
/* ================================================================= */

static void move_to(DOC *d, long pos, int extend)
{
    long oc = d->cursor, oa = d->anchor;
    if (pos < 0) pos = 0;
    if (pos > d->len) pos = d->len;
    if (extend) { if (d->anchor < 0) { d->anchor = d->cursor; d->colsel = d->colmode; } }
    else d->anchor = -1;
    d->cursor = pos;
    after_move(d, oc, oa);
    if (d->anchor < 0) d->colsel = 0;
}

/* a block selected: deleted (Backspace, Delete, typing over it) */
int block_delete(DOC *d)
{
    long la, lb, x0, x1;
    if (!d->colsel || d->anchor < 0) return 0;
    if (col_has_table(d)) { alert(1, 1, AL_COLSEL_TABLE, AB_OK); return 1; }
    if (!col_block(d, &la, &lb, &x0, &x1)) { d->colsel = 0; return 0; }
    col_delete(d);
    d->colsel = 0;
    after_edit(d, d->lines[la].start);
    return 1;
}

/* Key repeat: presses of the same cursor key already waiting are taken
 * together, and the window drawn once for all of them, so a held key
 * doesn't fall behind. A different key read on the way is kept for the
 * event loop (key_pending). */
static short pend_ks, pend_key;
static int   pend_on;

int key_pending(short *ks, short *key)
{
    if (!pend_on) return 0;
    pend_on = 0;
    *ks = pend_ks; *key = pend_key;
    return 1;
}

/* how many more of this key are waiting (at most max) */
static int key_repeats(short ks, short key, int max)
{
    short msg[8], mx, my, mb, ks2, k2, br, ev;
    int n = 0;
    while (n < max && !pend_on) {
        ev = evnt_multi(MU_KEYBD | MU_TIMER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        msg, 0L, &mx, &my, &mb, &ks2, &k2, &br);
        if (!(ev & MU_KEYBD)) break;
        if (k2 == key && (ks2 & 0x0F) == (ks & 0x0F)) n++;
        else { pend_on = 1; pend_ks = ks2; pend_key = k2; }
    }
    return n;
}

static void text_key2(DOC *d, short ks, short key)
{
    short scan = (key >> 8) & 0xff, asc = key & 0xff;
    int shift = (ks & 3) != 0, intbl = doc_in_table(d, d->cursor, NULL, NULL) != 0, blk = 0;
    long li, from, a, b;
    CELL c;

    if (d->selobj >= 0 && obj_key(d, ks, key)) return;
    li = doc_find_line(d, d->cursor);
    if (ks & 4) {                               /* Control: words, the document's ends */
        long p = d->cursor;
        d->want_x = -1;
        switch (scan) {
        case 0x4B:
            while (p > 0 && !st_isalpha(d->text[p - 1].ch) && !(d->text[p - 1].ch >= '0' && d->text[p - 1].ch <= '9')) p--;
            while (p > 0 && (st_isalpha(d->text[p - 1].ch) || (d->text[p - 1].ch >= '0' && d->text[p - 1].ch <= '9'))) p--;
            move_to(d, p, shift);
            break;
        case 0x4D:
            while (p < d->len && (st_isalpha(d->text[p].ch) || (d->text[p].ch >= '0' && d->text[p].ch <= '9'))) p++;
            while (p < d->len && !st_isalpha(d->text[p].ch) && !(d->text[p].ch >= '0' && d->text[p].ch <= '9')) p++;
            move_to(d, p, shift);
            break;
        case 0x47:
            move_to(d, shift ? d->len : 0, 0);
            break;
        }
        return;
    }
    switch (scan) {
    case 0x4B:                                                             /* left  */
    case 0x4D: {                                                           /* right */
        long n = 1 + key_repeats(ks, key, 200);
        d->want_x = -1;
        move_to(d, d->cursor + (scan == 0x4B ? -n : n), shift);
        return;
    }
    case 0x48:                                                             /* up    */
    case 0x50: {                                                           /* down  */
        long nl = li, wx;
        int n = 1 + key_repeats(ks, key, 100);
        if (d->want_x < 0) d->want_x = doc_x_of(d, li, d->cursor);
        wx = d->want_x;
        while (n-- > 0 && nl >= 0 && nl < d->nlines) nl = doc_line_vert(d, nl, wx, scan == 0x48 ? -1 : 1);
        if (nl < 0) { move_to(d, 0, shift); return; }
        if (nl >= d->nlines) { move_to(d, d->len, shift); return; }
        move_to(d, doc_pos_in_line(d, nl, wx), shift);
        d->want_x = wx;
        return;
    }
    case 0x47:                                                             /* Clr/Home */
        d->want_x = -1;
        if (shift) {
            LINE *ln = &d->lines[li];
            long e = ln->start + ln->len;
            if (e > ln->start && (ISEND(d->text[e - 1].ch) || li < d->nlines - 1)) e--;
            move_to(d, e, 0);
        } else move_to(d, d->lines[li].start, 0);
        return;
    case 0x0E:                                                             /* backspace */
        if (block_delete(d)) return;
        if (doc_sel(d, &a, &b)) { doc_delete_sel(d); after_edit(d, a); }
        else if (d->cursor > 0) {
            long len = d->len;
            doc_delete_range(d, d->cursor - 1, d->cursor);
            if (d->len < len) d->cursor--;
            else d->cursor--;                     /* a cell edge: just move */
            after_typing(d, d->cursor);
        }
        return;
    case 0x53:                                                             /* delete */
        if (block_delete(d)) return;
        if (doc_sel(d, &a, &b)) { doc_delete_sel(d); after_edit(d, a); }
        else if (d->cursor < d->len) { doc_delete_range(d, d->cursor, d->cursor + 1); after_typing(d, d->cursor); }
        return;
    case 0x01:                                                             /* Esc */
        if (d->anchor >= 0) { select_range(d, -1, d->cursor); d->colsel = 0; }
        return;
    }

    if (intbl && (scan == 0x0F || scan == 0x1C || scan == 0x72)) {         /* Tab, Return in a table */
        long p = doc_next_cell(d, d->cursor, shift && scan == 0x0F ? -1 : 1);
        if (p < 0 && !(shift && scan == 0x0F)) {
            /* past the last cell: Tab adds a row, Return leaves the table */
            if (scan == 0x0F) {
                if (tbl_row_insert(d, d->cursor, 1)) { after_edit(d, d->cursor); }
                return;
            }
            {
                long t0, t1;
                tbl_range(d, d->cursor, &t0, &t1);
                move_to(d, t1, 0);
            }
            return;
        }
        if (p >= 0) {
            long e = doc_cell_end(d, p);
            select_range(d, p, e);                 /* the next cell's contents */
            if (e == p) d->anchor = -1;
        }
        return;
    }
    if (scan == 0x1C || scan == 0x72) asc = '\n';                          /* return/enter */
    else if (scan == 0x0F) asc = '\t';
    else if (asc < 32 || asc == 127) return;

    if (d->colsel && d->anchor >= 0) {          /* typing over a block: it goes first */
        if (col_has_table(d)) { alert(1, 1, AL_COLSEL_TABLE, AB_OK); return; }
        col_delete(d);
        d->colsel = 0;
        blk = 1;
    }
    from = doc_sel(d, &a, &b) ? a : d->cursor;
    c = d->ins;
    c.ch = (unsigned char)asc;
    if (doc_type(d, &c, 1)) {
        if (asc == '\n' || blk || from != d->cursor - 1) after_edit(d, from);
        else after_typing(d, from);
    } else alert(1, 3, AL_NO_MEMORY, AB_OK);
}

/* ---- codes pane editing ---------------------------------------------- */

/* a key in the page: remember the layout first, so only the lines the
 * key changes are redrawn */
static void text_key(DOC *d, short ks, short key)
{
    keep_lines(d);
    text_key2(d, ks, key);
    old_n = -1;
}

/* ---- drawing only what changed in the codes pane ---------------------- */

/* the codes lines la..lb (lb < 0: to the bottom of the pane) again */
static void refresh_codes_lines(DOC *d, long la, long lb)
{
    GRECT tb, tp, cp, r;
    long y0, y1, t = d->cv.top;
    get_panes(d, &tb, &tp, &cp);
    if (!cp.g_h) return;
    if (lb >= 0 && la > lb) { long k = la; la = lb; lb = k; }
    if (la < t) la = t;
    y0 = la == t ? cp.g_y : cp.g_y + 2 + (la - t) * hchar;
    y1 = lb < 0 || lb >= d->cv.nl - 1 ? cp.g_y + cp.g_h : cp.g_y + 2 + (lb - t + 1) * hchar;
    if (y1 > cp.g_y + cp.g_h) y1 = cp.g_y + cp.g_h;
    if (y1 <= y0) return;
    r = cp; r.g_y = (short)y0; r.g_h = (short)(y1 - y0);
    redraw_area(d, &r);
}

/* the codes caret at pos, inverted (as draw_codes_pane draws it) */
static void codes_caret_xor(DOC *d, long pos)
{
    GRECT tb, tp, cp, c;
    CODES *cv = &d->cv;
    long li;
    get_panes(d, &tb, &tp, &cp);
    if (!cp.g_h || !cv->nl) return;
    li = codes_line_of(cv, pos);
    if (li < cv->top) return;
    c.g_x = (short)(cp.g_x + 4 + (pos - cv->ls[li]) * wchar);
    c.g_y = (short)(cp.g_y + 2 + (li - cv->top) * hchar);
    c.g_w = 2; c.g_h = (short)hchar;
    if (!rc_inter(&cp, &c)) return;
    xor_visible(d, &c);
}

/* the codes pane's first line was otop: move the lines still in view by
 * blitting and draw only the ones that came into view */
static void codes_scroll(DOC *d, long otop)
{
    GRECT tb, tp, cp, a, ex;
    MFDB scr;
    short pxy[8];
    long n = d->cv.top - otop, vis = codes_vis(d), an = n < 0 ? -n : n, dy;
    if (!n) return;
    get_panes(d, &tb, &tp, &cp);
    a = cp; a.g_y = (short)(cp.g_y + 2); a.g_h = (short)(vis * hchar);
    if (!cp.g_h || an >= vis || !unobscured(d, &a)) { refresh_codes(d); return; }
    dy = an * hchar;
    memset(&scr, 0, sizeof scr);
    pxy[0] = pxy[4] = a.g_x;
    pxy[2] = pxy[6] = (short)(a.g_x + a.g_w - 1);
    if (n > 0) {                    /* the lines move up */
        pxy[1] = (short)(a.g_y + dy); pxy[3] = (short)(a.g_y + a.g_h - 1);
        pxy[5] = a.g_y; pxy[7] = (short)(a.g_y + a.g_h - 1 - dy);
    } else {
        pxy[1] = a.g_y; pxy[3] = (short)(a.g_y + a.g_h - 1 - dy);
        pxy[5] = (short)(a.g_y + dy); pxy[7] = (short)(a.g_y + a.g_h - 1);
    }
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    vs_clip(vhandle, 0, pxy);
    vro_cpyfm(vhandle, S_ONLY, pxy, &scr, &scr);
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
    /* the lines that came into view, and the part line below the last */
    if (n > 0) refresh_codes_lines(d, d->cv.top + vis - an, -1);
    else {
        refresh_codes_lines(d, d->cv.top, d->cv.top + an - 1);
        ex = cp; ex.g_y = (short)(a.g_y + a.g_h); ex.g_h = (short)(cp.g_y + cp.g_h - ex.g_y);
        if (ex.g_h > 0) redraw_area(d, &ex);
    }
}

/* the codes caret or selection changed from (oc, oa) with the first line
 * otop: draw only what is different */
static void codes_moved(DOC *d, long oc, long oa, long otop)
{
    CODES *c = &d->cv;
    long lo = oc, hi = oc;
    int was_sel = oa >= 0 && oa != oc, is_sel = c->anchor >= 0 && c->anchor != c->cursor;
    if (c->top == otop && !was_sel && !is_sel) {
        if (oc != c->cursor && codes_focus(d)) { codes_caret_xor(d, oc); codes_caret_xor(d, c->cursor); }
        return;
    }
    if (oa >= 0 && oa == c->anchor) {
        lo = oc < c->cursor ? oc : c->cursor;
        hi = oc < c->cursor ? c->cursor : oc;
    } else {
#define SPAN(v) do { if ((v) >= 0) { if ((v) < lo) lo = (v); if ((v) > hi) hi = (v); } } while (0)
        SPAN(oa); SPAN(c->cursor); SPAN(c->anchor);
#undef SPAN
    }
    if (c->top != otop) { codes_scroll(d, otop); set_sliders(d); }
    refresh_codes_lines(d, codes_line_of(c, lo), codes_line_of(c, hi));
}

/* the codes text changed from at on: wrap it again and draw from the
 * line before (a word may have gone back up) to the end of the paragraph,
 * or to the bottom when the number of lines changed */
static void codes_edited_at(DOC *d, long at)
{
    CODES *c = &d->cv;
    long onl = c->nl, otop = c->top, la, p;
    if (at < 0) at = 0;
    la = codes_line_of(c, at) - 1;
    c->want_col = -1;
    codes_wrap(c, c->cols);
    ensure_codes_visible(d);
    if (c->top != otop) refresh_codes(d);
    else if (c->nl != onl) refresh_codes_lines(d, la, -1);
    else {
        for (p = at; p < c->len && c->t[p] != '\n'; p++) ;
        refresh_codes_lines(d, la, codes_line_of(c, p));
    }
    set_sliders(d);
    d->dirty = 1;
    if (d->dirty != d->shown_dirty) set_name(d);
}

static void codes_move(DOC *d, long pos, int extend)
{
    CODES *c = &d->cv;
    long oc = c->cursor, oa = c->anchor, otop = c->top;
    if (pos < 0) pos = 0;
    if (pos > c->len) pos = c->len;
    if (extend) { if (c->anchor < 0) c->anchor = c->cursor; }
    else c->anchor = -1;
    c->cursor = pos;
    ensure_codes_visible(d);
    codes_moved(d, oc, oa, otop);
    if (c->top != otop) set_sliders(d);
}

static int codes_del_sel(DOC *d)
{
    long a, b;
    if (!codes_sel(&d->cv, &a, &b)) { d->cv.anchor = -1; return 0; }
    codes_delete(&d->cv, a, b - a);
    d->cv.cursor = a;
    d->cv.anchor = -1;
    return 1;
}

static void codes_ins(DOC *d, const char *s, long n)
{
    long at;
    codes_del_sel(d);
    at = d->cv.cursor;
    if (!codes_insert(&d->cv, at, s, n)) { alert(1, 3, AL_NO_MEMORY, AB_OK); return; }
    d->cv.cursor += n;
    codes_edited_at(d, at);
}

/* a menu or toolbar command while the codes have the focus: insert the
 * code (around the selection for B/I/U) */
int codes_insert_code(DOC *d, const char *code)
{
    CODES *c = &d->cv;
    long a, b;
    if (!code[0]) return 0;
    if (codes_sel(c, &a, &b) && code[0] == '[' && code[2] == ']' && strchr("BIU", code[1])) {
        char close[5];
        sprintf(close, "[/%c]", code[1]);
        codes_insert(c, b, close, 4);
        codes_insert(c, a, code, 3);
        c->cursor = b + 7;
        c->anchor = -1;
        codes_edited_at(d, a);
        return 1;
    }
    codes_ins(d, code, (long)strlen(code));
    return 1;
}

void codes_clip(DOC *d, int cmd)
{
    CODES *c = &d->cv;
    long a, b;
    if (cmd == C_SELALL) { long oc = c->cursor, oa = c->anchor; c->anchor = 0; c->cursor = c->len; codes_moved(d, oc, oa, c->top); return; }
    if ((cmd == C_CUT || cmd == C_COPY) && codes_sel(c, &a, &b)) {
        char *n = (char *)malloc(b - a);
        if (n) { memcpy(n, c->t + a, b - a); free(cclip); cclip = n; cclen = b - a; }
        clip_put_text(c->t + a, b - a);           /* the GEM clipboard too */
        if (cmd == C_CUT) { codes_del_sel(d); codes_edited_at(d, c->cursor); }
    }
    if (cmd == C_PASTE) {
        long n;
        char *t = clip_get_text(&n);
        if (t) { if (n) codes_ins(d, t, n); free(t); }
        else if (cclip) codes_ins(d, cclip, cclen);
    }
}

static void codes_key(DOC *d, short ks, short key)
{
    CODES *c = &d->cv;
    short scan = (key >> 8) & 0xff, asc = key & 0xff;
    int shift = (ks & 3) != 0;
    long li, col, wc;
    char ch;

    switch (scan) {
    case 0x4B: codes_move(d, c->cursor - 1, shift); c->want_col = -1; return;
    case 0x4D: codes_move(d, c->cursor + 1, shift); c->want_col = -1; return;
    case 0x48:
    case 0x50:
        li = codes_line_of(c, c->cursor);
        col = c->cursor - c->ls[li];
        wc = c->want_col >= 0 ? c->want_col : col;
        li += scan == 0x48 ? -1 : 1;
        if (li < 0) codes_move(d, 0, shift);
        else if (li >= c->nl) codes_move(d, c->len, shift);
        else codes_move(d, codes_pos(c, li, wc), shift);
        c->want_col = wc;
        return;
    case 0x47:
        li = codes_line_of(c, c->cursor);
        codes_move(d, shift ? c->ls[li] + codes_line_len(c, li) : c->ls[li], 0);
        return;
    case 0x0E:
        if (!codes_del_sel(d)) {
            if (c->cursor <= 0) return;
            codes_delete(c, c->cursor - 1, 1);
            c->cursor--;
        }
        codes_edited_at(d, c->cursor);
        return;
    case 0x53:
        if (!codes_del_sel(d)) {
            if (c->cursor >= c->len) return;
            codes_delete(c, c->cursor, 1);
        }
        codes_edited_at(d, c->cursor);
        return;
    }
    if (scan == 0x1C || scan == 0x72) { codes_ins(d, "\n", 1); return; }
    if (scan == 0x0F) { codes_ins(d, "[Tab]", 5); return; }
    if (asc < 32 || asc == 127) return;
    ch = (char)asc;
    codes_ins(d, &ch, 1);
}

void win_key(DOC *d, short ks, short key)
{
    short scan = (key >> 8) & 0xff;
    if (d->iconified) {
        if (scan == 0x1C || scan == 0x72 || scan == 0x39) uniconify(d, NULL);
        return;
    }
    if (d->view == VIEW_PREVIEW) { preview_key(d, ks, key); return; }   /* the page keys; no typing */
    if (codes_focus(d)) codes_key(d, ks, key);
    else text_key(d, ks, key);
}

/* ================================================================= */
/*  Mouse                                                             */
/* ================================================================= */

static void set_focus(DOC *d, int f)
{
    if (d->focus == f) return;
    if (f == 0 && d->cv.dirty) apply_codes(d);
    if (f == 1 && d->codes_stale) {
        d->cv.cols = codes_cols_of(d);
        codes_build(d);
        d->codes_stale = 0;
        ensure_codes_visible(d);
    }
    d->focus = f;
    d->vsz = d->vpos = -1;
    refresh_text(d);
    refresh_codes(d);
    set_sliders(d);
    set_info(d);
}

static long hit_text(DOC *d, GRECT *tp, short mx, short my)
{
    long px, py;
    int p = view_to_page(d, tp, mx, my, &px, &py);
    return doc_hit(d, p, px, py);
}

static long hit_codes(DOC *d, const GRECT *cp, short mx, short my)
{
    long li = d->cv.top + (my - cp->g_y - 2) / hchar, col = (mx - cp->g_x - 4 + wchar / 2) / wchar;
    if (my < cp->g_y + 2) li = d->cv.top - 1;
    if (li < 0) return 0;
    if (li >= d->cv.nl) return d->cv.len;
    return codes_pos(&d->cv, li, col);
}

/* a press held for half a second without moving */
static int long_press(short mx, short my)
{
    short x, y, b, k;
    int t;
    for (t = 0; t < 25; t++) {
        graf_mkstate(&x, &y, &b, &k);
        if (!(b & 1)) return 0;
        if (x < mx - 3 || x > mx + 3 || y < my - 3 || y > my + 3) return 0;
        evnt_timer(20L);
    }
    return 1;
}

/* ---- charts ---------------------------------------------------------- */

/* the chart's menu: the kind of chart and where it goes */
void chart_popup(DOC *d, int t, short x, short y)
{
    static const int txt[10] = { S_CH_NONE, S_CH_PIE, S_CH_BAR, S_CH_PIE3D, S_CH_BAR3D, 0,
                                 S_CH_TOP, S_CH_BOTTOM, S_CH_LEFT, S_CH_RIGHT };
    const char *items[10];
    TBL *T;
    int i, r;
    if (t < 0 || t >= d->a.ntbl) return;
    T = &d->a.tbls[t];
    for (i = 0; i < 10; i++) items[i] = txt[i] ? L(txt[i]) : "-";
    r = popup2(items, 10, T->chart, T->chart ? 6 + T->cpos : -1, x, y);
    if (r < 0 || r == 5) return;
    if (r < 5) {
        if (T->chart == r) return;
        T->chart = (unsigned char)r;
        if (r && !T->cpos && !d->cbox[t].w) T->cpos = CP_BOTTOM;
    } else {
        if (T->cpos == r - 6 && T->chart) return;
        T->cpos = (unsigned char)(r - 6);
        if (!T->chart) T->chart = CH_BAR;
    }
    d->dirty = 1;
    doc_changed(d);
}

/* a click on a chart opens its menu */
static int chart_click(DOC *d, GRECT *tp, short mx, short my)
{
    long px, py;
    int p, t;
    if (!CHARTS) return 0;
    p = view_to_page(d, tp, mx, my, &px, &py);
    for (t = 0; t < d->a.ntbl; t++) {
        const CHBOX *cb = &d->cbox[t];
        if (!d->a.tbls[t].chart || cb->page != p) continue;
        if (px >= cb->x && px < cb->x + cb->w && py >= cb->y && py < cb->y + cb->h) {
            short x, y, b, k;
            do graf_mkstate(&x, &y, &b, &k); while (b & 1);
            chart_popup(d, t, mx, my);
            return 1;
        }
    }
    return 0;
}

/* after an edit in a table with a chart: draw the chart again */
static void refresh_chart_at(DOC *d, long pos)
{
    GRECT tb, tp, cp, r;
    long e;
    int t;
    const CHBOX *cb;
    if (!CHARTS || d->len <= 0) return;
    if (pos >= d->len) pos = d->len - 1;
    if (pos < 0) pos = 0;
    e = doc_para_end(d, pos);
    t = PARA_OF(d, &d->text[e < d->len ? e : d->len - 1])->tbl - 1;
    if (t < 0 || t >= d->a.ntbl || !d->a.tbls[t].chart) return;
    cb = &d->cbox[t];
    if (cb->page < 0 || cb->w <= 0) return;
    get_panes(d, &tb, &tp, &cp);
    if (!tp.g_h) return;
    r.g_x = (short)(scr_x(d, &tp, cb->x) - 1);
    r.g_y = (short)(scr_y(d, &tp, cb->page, cb->y) - 1);
    r.g_w = (short)(zx(d, cb->w) + 2);
    r.g_h = (short)(zx(d, cb->h) + 2);
    if (rc_inter(&tp, &r)) redraw_area(d, &r);
}

static void text_click(DOC *d, GRECT *tp, short mx, short my, short ks, short clicks)
{
    short mb;
    long oc, oa, pos, wa, wb;
    char word[WORDMAX];

    if (obj_click(d, tp, mx, my, ks, clicks)) return;
    if (chart_click(d, tp, mx, my)) return;
    pos = hit_text(d, tp, mx, my);
    oc = d->cursor; oa = d->anchor;
    /* holding the button on an underlined word offers corrections */
    if (pref_live && clicks < 2 && word_at(d, pos, &wa, &wb, word, WORDMAX) && word_misspelt(d, wa, wb) &&
        long_press(mx, my)) {
        d->anchor = wa; d->cursor = wb;
        after_move(d, oc, oa);
        live_popup(d, wa, wb, mx, my);
        return;
    }
    if (clicks >= 2) {                                  /* double click: word */
        d->colsel = 0;
        if (word_at(d, pos, &wa, &wb, word, WORDMAX)) { d->anchor = wa; d->cursor = wb; }
        after_move(d, oc, oa);
        return;
    }
    if (ks & 3) { if (d->anchor < 0) { d->anchor = d->cursor; d->colsel = d->colmode; } }
    else {
        /* Alternate held, or Column Select on: a block */
        if (d->colsel) { d->colsel = 0; if (oa >= 0) refresh_lines(d, doc_find_line(d, oa < oc ? oa : oc), doc_find_line(d, oa < oc ? oc : oa)); }
        d->anchor = pos;
        d->colsel = (ks & 8) || d->colmode;
    }
    d->cursor = pos;
    d->want_x = -1;
    after_move(d, oc, oa);

    wind_update(BEG_MCTRL);
    for (;;) {
        int scrolled = 0;
        long osy = d->scroll_y, lo, hi;
        graf_mkstate(&mx, &my, &mb, &ks);
        if (!(mb & 1)) break;
        if (my < tp->g_y && d->scroll_y > 0) { d->scroll_y -= hchar; scrolled = 1; }
        else if (my >= tp->g_y + tp->g_h) { d->scroll_y += hchar; scrolled = 1; }
        if (scrolled) clamp_scroll(d);
        pos = hit_text(d, tp, mx, my);
        if (pos != d->cursor || scrolled) {
            oc = d->cursor;
            d->cursor = pos;
            lo = oc < pos ? oc : pos; hi = oc < pos ? pos : oc;
            if (d->colsel && d->anchor >= 0) {     /* a block is as wide as the lines between */
                if (d->anchor < lo) lo = d->anchor;
                if (d->anchor > hi) hi = d->anchor;
            }
            if (scrolled) {
                scroll_view(d, d->scroll_x, osy);
                refresh_lines(d, doc_find_line(d, lo), doc_find_line(d, hi));
                set_sliders(d);
                evnt_timer(30L);
            }
            else refresh_lines(d, doc_find_line(d, lo), doc_find_line(d, hi));
        }
    }
    wind_update(END_MCTRL);
    if (d->anchor == d->cursor) { d->anchor = -1; d->colsel = 0; }
    if (d->anchor < 0) doc_sync_ins(d);
    update_menu(d);
    set_info(d);
    refresh_tb_if_changed(d);
}

static void codes_click(DOC *d, const GRECT *cp, short mx, short my, short ks)
{
    CODES *c = &d->cv;
    short mb;
    long pos = hit_codes(d, cp, mx, my), oc = c->cursor, oa = c->anchor;

    if (ks & 3) { if (c->anchor < 0) c->anchor = c->cursor; }
    else c->anchor = pos;
    c->cursor = pos;
    c->want_col = -1;
    codes_moved(d, oc, oa, c->top);
    wind_update(BEG_MCTRL);
    for (;;) {
        int scrolled = 0;
        long otop = c->top;
        graf_mkstate(&mx, &my, &mb, &ks);
        if (!(mb & 1)) break;
        if (my < cp->g_y && c->top > 0) { c->top--; scrolled = 1; }
        else if (my >= cp->g_y + cp->g_h) { c->top++; scrolled = 1; }
        if (scrolled) clamp_scroll(d);
        pos = hit_codes(d, cp, mx, my);
        if (pos != c->cursor || c->top != otop) {
            oc = c->cursor;
            c->cursor = pos;
            codes_moved(d, oc, c->anchor, otop);
            if (scrolled) evnt_timer(40L);
        }
    }
    wind_update(END_MCTRL);
    if (c->anchor == c->cursor) c->anchor = -1;
}

#ifndef EDLITE
/* dragging the bar between the two panes of a split window; dragged to
 * the top or the bottom it ends the split, the pane left showing goes on */
static void split_drag(DOC *d, const GRECT *dv)
{
    GRECT tb, hr, vr, tp, cp, op, sdv, top_p, bot_p;
    short x, ny, top, bot, mb, ks, mx, my;
    panes_ex(d, &tb, &hr, &vr, &tp, &cp, &op, &sdv);
    if (d->view == VIEW_CODES) tp = cp;
    else if (hr.g_h) { tp.g_h += (short)(tp.g_y - hr.g_y); tp.g_y = hr.g_y; }
    top_p = d->pane2 ? op : tp;
    bot_p = d->pane2 ? tp : op;
    top = top_p.g_y;
    bot = (short)(bot_p.g_y + bot_p.g_h);
    /* the bar follows the mouse as an outline until the button is let go */
    wind_update(BEG_UPDATE);
    wind_update(BEG_MCTRL);
    graf_mouse(FLAT_HAND, NULL);
    graf_mkstate(&mx, &my, &mb, &ks);
    {
        short off = (short)(my - dv->g_y), cur = dv->g_y, pxy[4] = { 0, 0, 0, 0 };
        vs_clip(vhandle, 0, pxy);
        vswr_mode(vhandle, MD_XOR);
        vsf_interior(vhandle, FIS_SOLID);
        vsf_color(vhandle, 1);
        graf_mouse(M_OFF, NULL);
        xor_box(dv->g_x, cur, dv->g_x + dv->g_w - 1, cur + dv->g_h - 1);
        graf_mouse(M_ON, NULL);
        while (mb & 1) {
            graf_mkstate(&mx, &my, &mb, &ks);
            ny = (short)(my - off);
            if (ny < top) ny = top;
            if (ny > bot - dv->g_h) ny = (short)(bot - dv->g_h);
            if (ny != cur) {
                graf_mouse(M_OFF, NULL);
                xor_box(dv->g_x, cur, dv->g_x + dv->g_w - 1, cur + dv->g_h - 1);
                cur = ny;
                xor_box(dv->g_x, cur, dv->g_x + dv->g_w - 1, cur + dv->g_h - 1);
                graf_mouse(M_ON, NULL);
            }
        }
        graf_mouse(M_OFF, NULL);
        xor_box(dv->g_x, cur, dv->g_x + dv->g_w - 1, cur + dv->g_h - 1);
        graf_mouse(M_ON, NULL);
        ny = cur;
        vswr_mode(vhandle, MD_REPLACE);
    }
    (void)x;
    graf_mouse(ARROW, NULL);
    wind_update(END_MCTRL);
    wind_update(END_UPDATE);
    if (ny < top + hchar * 2 || ny + SPLITH > bot - hchar * 2) {
        int keep_bottom = ny < top + hchar * 2;
        /* the pane that stays becomes the window's only one */
        if (keep_bottom != d->pane2) pane_swap(d);
        d->pane2 = 0;
        d->split2 = 0;
    } else if (bot - top - SPLITH > 0) {
        d->split_at = (int)((long)(ny - top) * 1000 / (bot - top - SPLITH));
        if (d->split_at < 30) d->split_at = 30;
        if (d->split_at > 970) d->split_at = 970;
    }
    d->vsz = d->vpos = d->hsz = d->hpos = -1;
    clamp_scroll(d);
    refresh_all(d);
    set_sliders(d);
    update_menu(d);
    refresh_tb_if_changed(d);
}
#endif

void win_click(DOC *d, short mx, short my, short ks, short clicks)
{
    GRECT tb, tp, cp, hr, vr, op, sdv;
    panes_ex(d, &tb, &hr, &vr, &tp, &cp, &op, &sdv);
    if (d->iconified) { uniconify(d, NULL); return; }
    if (in_rect(&tb, mx, my)) { toolbar_click(d, &tb, mx); return; }
    if (d->view == VIEW_PREVIEW) { if (in_rect(&tp, mx, my)) preview_area_click(d, &tp, mx, my); return; }
#ifndef EDLITE
    if (sdv.g_h && in_rect(&sdv, mx, my)) { split_drag(d, &sdv); return; }
#endif
    if (op.g_h && in_rect(&op, mx, my)) {
        /* the other pane: it becomes the one worked in */
        pane_swap(d);
        d->vsz = d->vpos = d->hsz = d->hpos = -1;
        set_sliders(d);
        refresh_text(d);                     /* the caret from one pane to the other */
        if (d->view == VIEW_CODES) refresh_codes(d);
        panes(d, &tb, &hr, &vr, &tp, &cp);
    }
    if (in_rect(&tb, mx, my)) toolbar_click(d, &tb, mx);
    else if (in_rect(&hr, mx, my) || in_rect(&vr, mx, my)) {
        /* a double click on a ruler: the indents, or the page layout */
        if (clicks >= 2) do_cmd(in_rect(&hr, mx, my) && !doc_in_table(d, d->cursor, NULL, NULL) ? C_INDENTS : C_PAGE);
    }
    else if (in_rect(&tp, mx, my)) {
        if (d->view == VIEW_SPLIT) set_focus(d, 0);
        text_click(d, &tp, mx, my, ks, clicks);
    } else if (in_rect(&cp, mx, my)) {
        set_focus(d, 1);
        codes_click(d, &cp, mx, my, ks);
    }
}

/* something dropped on the window at (mx, my): the cursor goes there, if
 * that is on the text */
void win_drop_point(DOC *d, short mx, short my)
{
    GRECT tb, tp, cp, hr, vr;
    long oc = d->cursor, oa = d->anchor;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (d->iconified || d->view == VIEW_CODES || !in_rect(&tp, mx, my)) return;
    d->anchor = -1;
    d->cursor = hit_text(d, &tp, mx, my);
    d->want_x = -1;
    after_move(d, oc, oa);
}

/* The right button on the text: a menu of what is most often wanted for
 * the line there (its paragraph style, a list) and the clipboard. A click
 * outside the selection puts the cursor there first. */
void win_rclick(DOC *d, short mx, short my)
{
#ifdef EDLITE
    static const short cmds[] = { C_HEAD1, C_HEAD2, C_HEAD3, C_BODY, 0, C_BULLETS, C_NUMBERS, 0, C_CUT, C_COPY, C_PASTE,
                                  0, C_COLSEL };
    static const short text[] = { M_HEAD1, M_HEAD2, M_HEAD3, M_BODY, 0, M_BULLETS, M_NUMBERS, 0, M_CUT, M_COPY, M_PASTE,
                                  0, M_COLSEL };
#else
    static const short cmds[] = { C_HEAD1, C_HEAD2, C_HEAD3, C_BODY, 0, C_BULLETS, C_NUMBERS, 0, C_CUT, C_COPY, C_PASTE,
                                  0, C_CUTSLOT, C_COPYSLOT, C_PASTESLOT, 0, C_INSBM, C_GOTOBM, C_COLSEL };
    static const short text[] = { M_HEAD1, M_HEAD2, M_HEAD3, M_BODY, 0, M_BULLETS, M_NUMBERS, 0, M_CUT, M_COPY, M_PASTE,
                                  0, M_CUTSLOT, M_COPYSLOT, M_PASTESLOT, 0, M_INSBM, M_GOTOBM, M_COLSEL };
#endif
#define NRC ((int)(sizeof cmds / sizeof cmds[0]))
    const char *items[NRC];
    static char colsel_item[48];
    GRECT tb, tp, cp, hr, vr;
    PARA *p;
    long pos, a, b;
    int i, cur, cur2 = -1, sel;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (d->iconified || !in_rect(&tp, mx, my) || d->view == VIEW_CODES) return;
    if (d->view == VIEW_SPLIT) set_focus(d, 0);
    pos = hit_text(d, &tp, mx, my);
    if (d->selobj >= 0) {                       /* on the selected picture: it stays selected */
        long px, py;
        int pg = view_to_page(d, &tp, mx, my, &px, &py);
        if (obj_at(d, pg, px, py) != d->selobj) { d->selobj = -1; refresh_text(d); }
    }
    if (d->selobj < 0 && (!doc_sel(d, &a, &b) || pos < a || pos > b)) {
        long oc = d->cursor, oa = d->anchor;
        d->anchor = -1;
        d->cursor = pos;
        d->want_x = -1;
        after_move(d, oc, oa);
    }
    p = doc_para_at(d, d->cursor);
    for (i = 0; i < NRC; i++) items[i] = cmds[i] ? L(text[i]) : "-";
    /* Column Select: ticked when on (a popup has only two marks) */
    sprintf(colsel_item, "%s%s", d->colmode ? "\10 " : "", L(M_COLSEL));
    items[NRC - 1] = colsel_item;
    cur = p->toc >= 1 && p->toc <= 3 ? p->toc - 1 : 3;
    if (p->list == LIST_BULLET) cur2 = 5;
    else if (p->list == LIST_NUMBER) cur2 = 6;
    sel = popup2(items, NRC, cur, cur2, mx, my);
    if (sel >= 0 && sel < NRC && cmds[sel]) do_cmd(cmds[sel]);
#undef NRC
}

/* ================================================================= */
/*  Window messages                                                   */
/* ================================================================= */

void win_msg(DOC *d, short *msg)
{
    GRECT r, tb, tp, cp;
    short x, y, w, h;
    long th, tw, vis, osx, osy;

    switch (msg[0]) {
    case WM_REDRAW:
        r.g_x = msg[4]; r.g_y = msg[5]; r.g_w = msg[6]; r.g_h = msg[7];
        redraw_area(d, &r);
        return;
    case WM_TOPPED:
        /* (its sheets aren't put back over it here: on TOS a window has to
         * be on top to be dragged; they come back over it when it moves) */
        wind_set(d->wh, WF_TOP, 0, 0, 0, 0);
        update_menu(d);
        update_winmenu();
        return;
    case WM_BOTTOMED:
        wind_set(d->wh, WF_BOTTOM, 0, 0, 0, 0);
        update_menu(top_doc());
        update_winmenu();
        return;
    case WM_ICONIFY:
        iconify(d, &msg[4]);
        return;
    case WM_ALLICONIFY: {
        int i;
        for (i = 0; i < MAX_DOCS; i++)
            if (DOC_USED(i) && !docs[i]->iconified) iconify(docs[i], docs[i] == d ? &msg[4] : NULL);
        return;
    }
    case WM_UNICONIFY:
        uniconify(d, NULL);
        return;
    case WM_CLOSED:
        close_doc(d);
        return;
    case WM_MOVED:
        if (d->iconified) { wind_set(d->wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]); return; }
        /* fall through */
    case WM_SIZED:
        w = msg[6] < 200 ? 200 : msg[6];
        h = msg[7] < 120 ? 120 : msg[7];
        wind_set(d->wh, WF_CURRXYWH, msg[4], msg[5], w, h);
        if (msg[0] == WM_SIZED) win_resized(d);
        sheets_place(d);                 /* its dialogs and file selector go with it */
        return;
    case WM_FULLED: {
        short cx, cy, cw, ch;
        if (d->iconified) return;
        wind_get(d->wh, WF_CURRXYWH, &cx, &cy, &cw, &ch);
        wind_get(d->wh, WF_FULLXYWH, &x, &y, &w, &h);
        if (cx == x && cy == y && cw == w && ch == h)
            wind_get(d->wh, WF_PREVXYWH, &x, &y, &w, &h);
        wind_set(d->wh, WF_CURRXYWH, x, y, w, h);
        win_resized(d);
        sheets_place(d);
        return;
    }
    }
    if (d->iconified) return;

    osx = d->scroll_x; osy = d->scroll_y;
    get_panes(d, &tb, &tp, &cp);
    if (d->view == VIEW_PREVIEW) { preview_scroll(d, &tp, msg); return; }
    if (codes_focus(d)) {
        CODES *c = &d->cv;
        long otop = c->top;
        vis = codes_vis(d);
        if (msg[0] == WM_ARROWED) {
            switch (msg[4]) {
            case WA_UPPAGE: c->top -= vis - 1; break;
            case WA_DNPAGE: c->top += vis - 1; break;
            case WA_UPLINE: c->top--; break;
            case WA_DNLINE: c->top++; break;
            }
        } else if (msg[0] == WM_VSLID)
            c->top = c->nl > vis ? (long)msg[4] * (c->nl - vis) / 1000L : 0;
        else return;
        clamp_scroll(d);
        codes_scroll(d, otop);
        set_sliders(d);
        return;
    }
    switch (msg[0]) {
    case WM_ARROWED:
        switch (msg[4]) {
        case WA_UPPAGE: d->scroll_y -= tp.g_h - hchar; break;
        case WA_DNPAGE: d->scroll_y += tp.g_h - hchar; break;
        case WA_UPLINE: d->scroll_y -= 2 * hchar; break;
        case WA_DNLINE: d->scroll_y += 2 * hchar; break;
        case WA_LFPAGE: d->scroll_x -= tp.g_w - wchar; break;
        case WA_RTPAGE: d->scroll_x += tp.g_w - wchar; break;
        case WA_LFLINE: d->scroll_x -= 2 * wchar; break;
        case WA_RTLINE: d->scroll_x += 2 * wchar; break;
        }
        break;
    case WM_VSLID:
        th = content_h(d);
        d->scroll_y = th > tp.g_h ? (long)msg[4] * (th - tp.g_h) / 1000L : 0;
        break;
    case WM_HSLID:
        tw = content_w(d);
        d->scroll_x = tw > tp.g_w ? (long)msg[4] * (tw - tp.g_w) / 1000L : 0;
        break;
    default:
        return;
    }
    clamp_scroll(d);
    scroll_view(d, osx, osy);
    set_sliders(d);
}

/* ================================================================= */
/*  The blinking caret                                                 */
/* ================================================================= */

/* The caret of the top document window goes on and off every half second,
 * drawn with XOR. Before anything else happens (a key, a click, a message,
 * idle work) blink_wake() puts it back, so the rest of the program always
 * finds it shown, as it did before it blinked. */
#define BLINK_MS 500

static clock_t blink_t;

/* the document whose caret blinks now, or NULL */
static DOC *blink_doc(void)
{
    short h, d1, d2, d3;
    DOC *d;
    long a, b;
#ifndef BLINKTEST
    if (demo_on) return NULL;
#endif
    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    d = doc_of(h);
    if (!d || d->iconified || d->selobj >= 0 || d->view == VIEW_PREVIEW) return NULL;
    if (codes_focus(d)) { if (codes_sel(&d->cv, &a, &b)) return NULL; }
    else if (doc_sel(d, &a, &b) || !(d->view == VIEW_DOC || d->focus == 0)) return NULL;
    return d;
}

static void blink_xor(DOC *d)
{
    if (codes_focus(d)) codes_caret_xor(d, d->cv.cursor);
    else caret_xor(d, d->cursor);
}

/* every hidden caret shown again; the blink starts over */
void blink_wake(void)
{
    int i;
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i) && docs[i]->caret_off) {
            docs[i]->caret_off = 0;
            if (docs[i]->wh >= 0 && !docs[i]->iconified) blink_xor(docs[i]);
        }
    blink_t = clock();
}

/* milliseconds to the next blink, or -1 if no caret blinks */
long blink_wait(void)
{
    long ms;
    if (!blink_doc()) return -1;
    ms = BLINK_MS - (long)(clock() - blink_t) * 1000L / CLOCKS_PER_SEC;
    return ms < 10 ? 10 : ms;
}

/* the timer: turn the caret over if it is time */
void blink_tick(void)
{
    DOC *d = blink_doc();
    if (!d) { blink_wake(); return; }
    if ((long)(clock() - blink_t) * 1000L / CLOCKS_PER_SEC < BLINK_MS - 20) return;
    d->caret_off = !d->caret_off;
    blink_xor(d);
    blink_t = clock();
}
