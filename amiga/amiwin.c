/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amiwin.c - document windows: toolbar, page view, codes pane, drawing,
 *            scrolling, keyboard and mouse editing, views, zoom, tiling,
 *            iconify (after gemwin.c)
 *
 * The document view shows the pages one below the other, at the zoom
 * set for the window. Layout positions are screen pixels at 100%; zx()
 * scales them to the zoom. Window coordinates include the borders, as
 * Intuition gives them.
 */
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <intuition/icclass.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiui.h"

/* the window of each document and its scrollers */
enum { GID_VPROP = 1, GID_HPROP, GID_UP, GID_DOWN, GID_LEFT, GID_RIGHT };
typedef struct {
    struct Window *w;
    struct Gadget *g[7];                    /* by GID; g[0] unused            */
    struct Image  *img[5];                  /* up down left right size        */
    long  vf, hf;                           /* scroller scale factors         */
    char  title[200];
    short iw;                               /* iconified window               */
} DWIN;
static DWIN dws[MAX_DOCS];
static long tb_state[MAX_DOCS], rul_state[MAX_DOCS];
static void draw_guides(DOC *d, int page, long x, long y);
static long ruler_key(DOC *d);
static char *cclip;
static long  cclen;
static short cwid, chgt;                     /* codes pane character cell     */

#define DW(d) (&dws[(d)->slot])

struct Window *doc_win(DOC *d) { return d && d->used && d->wh >= 0 ? DW(d)->w : NULL; }

static void draw_begin(DOC *d) { gfx_begin(DW(d)->w); }

/* ---- display modes for the editing panes ------------------------------- */

int ed_bg(void) { return pref_display == DISP_NORMAL ? P_WHITE : P_BLACK; }

int ed_fg(void)
{
    switch (pref_display) {
    case DISP_DARK:  return P_WHITE;
    case DISP_GREEN: return npens >= 16 ? P_PHOSPHOR : P_WHITE;
    case DISP_AMBER: return npens >= 16 ? P_AMBER : P_WHITE;
    }
    return P_BLACK;
}

/* ================================================================= */
/*  Geometry                                                          */
/* ================================================================= */

void work_area(DOC *d, GRECT *g)
{
    struct Window *w = DW(d)->w;
    if (!w) { g->g_x = g->g_y = g->g_w = g->g_h = 0; return; }
    g->g_x = w->BorderLeft;
    g->g_y = w->BorderTop;
    g->g_w = (short)(w->Width - w->BorderLeft - w->BorderRight);
    g->g_h = (short)(w->Height - w->BorderTop - w->BorderBottom);
#ifdef AMI13
    {
        short mr, mb;                   /* scroll bars inside narrow 1.3 borders */
        c13_scroll_margins(w, &mr, &mb);
        g->g_w = (short)(g->g_w - mr);
        g->g_h = (short)(g->g_h - mb);
    }
#endif
}

/* the rulers: along the top and down the left of the page pane */
#define RULH (hchar + 7)
#define RULW (3 * wchar + 4)
static int rulers_on(DOC *d) { return pref_rulers && d->view != VIEW_CODES && !d->iconified; }

static void panes(DOC *d, GRECT *tb, GRECT *hr, GRECT *vr, GRECT *tp, GRECT *cp)
{
    GRECT wa;
    short y, h;
    work_area(d, &wa);
    *tb = *tp = *cp = *hr = *vr = wa;
    tb->g_h = cp->g_h = hr->g_h = vr->g_h = 0;
    if (d->iconified) return;
    y = wa.g_y; h = wa.g_h;
    if (d->toolbar) { tb->g_h = TBH; y += TBH; h -= TBH; }
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
    if (rulers_on(d) && tp->g_h > 2 * RULH && tp->g_w > 3 * RULW) {
        hr->g_y = tp->g_y; hr->g_h = RULH; hr->g_x = tp->g_x; hr->g_w = tp->g_w;
        vr->g_y = tp->g_y + RULH; vr->g_h = tp->g_h - RULH; vr->g_x = tp->g_x; vr->g_w = RULW;
        tp->g_x += RULW; tp->g_w -= RULW;
        tp->g_y += RULH; tp->g_h -= RULH;
    }
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

static long scr_x(DOC *d, GRECT *tp, long px) { return tp->g_x - d->scroll_x + view_page_left(d, tp) + zx(d, px); }
static long scr_y(DOC *d, GRECT *tp, int page, long py) { return tp->g_y - d->scroll_y + view_page_top(d, page) + zx(d, py); }

static long codes_vis(DOC *d)
{
    GRECT tb, tp, cp;
    long v;
    get_panes(d, &tb, &tp, &cp);
    v = (cp.g_h - 4) / chgt;
    return v < 1 ? 1 : v;
}

static long codes_cols_of(DOC *d)
{
    GRECT wa;
    work_area(d, &wa);
    return (wa.g_w - 8) / cwid;
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
    if (d->selobj >= 0) return;
    if (doc_sel(d, &a, &b)) {
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
                xor_rect(scr_x(d, tp, x1), y1, scr_x(d, tp, x2) - 1, y1 + zx(d, ln->asc + ln->desc) - 1);
        }
    } else if (d->view == VIEW_DOC || d->focus == 0) {
        long li2 = doc_find_line(d, d->cursor);
        LINE *ln = &d->lines[li2];
        long x = scr_x(d, tp, doc_x_of(d, li2, d->cursor)), y = scr_y(d, tp, ln->page, ln->py);
        xor_rect(x, y, x + (d->zoom >= 150 ? 1 : 0), y + zx(d, ln->asc + ln->desc) - 1);
    }
}

/* fill the part of a with the desk colour, leaving out the column
 * [px, px + pw) between rows y0 and y1 (where a page is): nothing is
 * ever painted under a page, so typing doesn't flash */
static void desk_around(const GRECT *a, long px, long pw, long y0, long y1, int pen)
{
    GRECT r;
    long ax2 = a->g_x + a->g_w, ay2 = a->g_y + a->g_h;
    if (y1 <= a->g_y || y0 >= ay2 || px >= ax2 || px + pw <= a->g_x) { fill_g(a, pen); return; }
    if (y0 > a->g_y) { r = *a; r.g_h = (short)(y0 - a->g_y); fill_g(&r, pen); }
    if (y1 < ay2) { r = *a; r.g_y = (short)y1; r.g_h = (short)(ay2 - y1); fill_g(&r, pen); }
    if (y0 < a->g_y) y0 = a->g_y;
    if (y1 > ay2) y1 = ay2;
    if (px > a->g_x) { r.g_x = a->g_x; r.g_w = (short)(px - a->g_x); r.g_y = (short)y0; r.g_h = (short)(y1 - y0); fill_g(&r, pen); }
    if (px + pw < ax2) { r.g_x = (short)(px + pw); r.g_w = (short)(ax2 - px - pw); r.g_y = (short)y0; r.g_h = (short)(y1 - y0); fill_g(&r, pen); }
}

static void draw_text_pane(DOC *d, const GRECT *tpc, GRECT *clip)
{
    GRECT r = *clip, tp = *tpc, pg;
    int p, np = doc_npages(d), dark = pref_display != DISP_NORMAL;
    long ph = page_h_view(d), pw = zx(d, page_w_px(d));
    int deskpen = dark ? P_BLACK : P_DESK;

    if (!rc_inter(&tp, &r)) return;
    clip_set(&r);
    {
        long y0 = scr_y(d, &tp, 0, 0) - 1, y1 = scr_y(d, &tp, np - 1, 0) + ph + 1, x = scr_x(d, &tp, 0) - 1;
        desk_around(&r, x, pw + 2, y0, y1, deskpen);
        for (p = 0; p + 1 < np; p++) {
            GRECT g;
            g.g_x = (short)x; g.g_w = (short)(pw + 2);
            g.g_y = (short)(scr_y(d, &tp, p, 0) + ph + 1);
            g.g_h = (short)(scr_y(d, &tp, p + 1, 0) - 1 - g.g_y);
            if (g.g_h > 0 && rc_inter(&r, &g)) fill_g(&g, deskpen);
        }
    }
    for (p = 0; p < np; p++) {
        long y = scr_y(d, &tp, p, 0), x = scr_x(d, &tp, 0);
        if (y > r.g_y + r.g_h) break;
        if (y + ph + 4 < r.g_y) continue;
        if (!dark) {                                         /* shadow: right and bottom edges */
            fill_rect(x + pw + 1, y + 3, x + pw + 3, y + ph + 3, P_SHADOW);
            fill_rect(x + 3, y + ph + 1, x + pw + 3, y + ph + 3, P_SHADOW);
        }
        pg.g_x = (short)x; pg.g_y = (short)y; pg.g_w = (short)pw; pg.g_h = (short)ph;
        fill_g(&pg, ed_bg());
        if (!dark) frame(pg.g_x - 1, pg.g_y - 1, pg.g_x + pg.g_w, pg.g_y + pg.g_h, P_BLACK);
        {
            GRECT c = pg;
            if (rc_inter(&r, &c)) {
                clip_set(&c);
                if (pref_guides) draw_guides(d, p, x, y);
                draw_page(d, p, x, y, d->zoom, 100, &c, dark ? ed_fg() : -1, 1);
                clip_set(&r);
            }
        }
    }
    draw_selection(d, &tp, &r);
    if (d->selobj >= 0) obj_draw_handles(d, &tp);
    clip_off();
}

/* one wrapped line of the codes text; codes in another colour */
static void draw_codes_line(CODES *c, long li, long x, long top, int cols)
{
    long s = c->ls[li], n = codes_line_len(c, li), i = 0;
    char buf[256];
    int fg = ed_fg(), pen = pref_display == DISP_NORMAL ? P_DBLUE : P_TGREY;
    if (npens < 16) pen = fg;
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
        lat_n(buf, c->t + s + i, j - i);
        fix_text(x + i * cwid, top, buf, j - i, code ? pen : fg);
        if (code && pen == fg) fix_text(x + i * cwid + 1, top, buf, j - i, fg);   /* bold instead */
        i = j;
    }
}

static void draw_codes_pane(DOC *d, const GRECT *cp, GRECT *clip)
{
    GRECT r = *clip;
    CODES *c = &d->cv;
    long li, a, b, x0 = cp->g_x + 4, y0 = cp->g_y + 2;
    int cols = (int)((cp->g_w - 8) / cwid);

    if (!rc_inter(cp, &r)) return;
    clip_set(&r);
    fill_g(&r, ed_bg());
    for (li = c->top; li < c->nl; li++) {
        long y = y0 + (li - c->top) * chgt;
        if (y >= r.g_y + r.g_h) break;
        if (y + chgt <= r.g_y) continue;
        draw_codes_line(c, li, x0, y, cols);
    }
    if (codes_sel(c, &a, &b)) {
        long la = codes_line_of(c, a), lb = codes_line_of(c, b);
        for (li = la; li <= lb; li++) {
            long c1 = li == la ? a - c->ls[li] : 0;
            long c2 = li == lb ? b - c->ls[li] : codes_line_len(c, li) + 1;
            long y = y0 + (li - c->top) * chgt;
            if (li < c->top || y > r.g_y + r.g_h) continue;
            if (c2 > c1) xor_rect(x0 + c1 * cwid, y, x0 + c2 * cwid - 1, y + chgt - 1);
        }
    } else if (codes_focus(d)) {
        long li2 = codes_line_of(c, c->cursor);
        long x = x0 + (c->cursor - c->ls[li2]) * cwid, y = y0 + (li2 - c->top) * chgt;
        if (li2 >= c->top) xor_rect(x, y, x + 1, y + chgt - 1);
    }
    clip_off();
}

/* ---- toolbar ------------------------------------------------------- */

typedef struct { short icon, w, cmd; } TBI;    /* w in pixels; icon < 0: gap */
#define TB_FONT  (-1)
#define TB_SIZE  (-2)
#define TB_COLOR (-3)
#define TB_ZOOM  (-4)
#define BTNW 22
#define POPW 30
static const TBI tbi[] = {
    { IC_UNDO, BTNW, C_UNDO }, { IC_REDO, BTNW, C_REDO }, { -1, 6, 0 },
    { IC_BOLD, BTNW, C_BOLD }, { IC_ITALIC, BTNW, C_ITALIC }, { IC_UNDER, BTNW, C_UNDER }, { -1, 6, 0 },
    { IC_LEFT, BTNW, C_JLEFT }, { IC_CENTRE, BTNW, C_JCENTRE }, { IC_RIGHT, BTNW, C_JRIGHT },
    { IC_JUSTIFY, BTNW, C_JFULL }, { -1, 6, 0 },
    { IC_BULLETS, BTNW, C_BULLETS }, { IC_NUMBERS, BTNW, C_NUMBERS }, { IC_TABLE, BTNW, C_TBLINS }, { IC_PGBRK, BTNW, C_PGBRK }, { -1, 6, 0 },
    { IC_FONT, POPW, TB_FONT }, { IC_SIZE, POPW, TB_SIZE }, { IC_COLOUR, POPW, TB_COLOR }, { -1, 6, 0 },
    { IC_ZOOM, POPW, TB_ZOOM }, { IC_SPELL, BTNW, C_SPELL }, { IC_CODES, BTNW, C_SPLIT }, { -1, 6, 0 },
    { IC_RULER, BTNW, C_RULERS }, { IC_GUIDES, BTNW, C_GUIDES }, { -1, 0, 0 }
};

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
    case C_SPLIT:   return d->view != VIEW_DOC;
    case C_RULERS:  return pref_rulers;
    case C_GUIDES:  return pref_guides;
    }
    return 0;
}

static int tb_disabled(DOC *d, int cmd)
{
    if (cmd == C_UNDO) return !undo_can(d) && d->ch_lo < 0;
    if (cmd == C_REDO) return !redo_can(d);
    if (cmd == TB_COLOR) return !colour_on;
    return 0;
}

static long tb_key(DOC *d)
{
    PARA *p = doc_para_at(d, d->cursor);
    return ((long)d->ins.attr << 8) ^ ((long)p->just << 2) ^ ((long)p->list << 12) ^ d->view ^ ((long)colour_on << 30) ^
           ((long)undo_can(d) << 16) ^ ((long)redo_can(d) << 17) ^ ((long)d->zoom << 18) ^ ((long)(d->ch_lo >= 0) << 29) ^
           ((long)pref_rulers << 26) ^ ((long)pref_guides << 27);
}

static void draw_toolbar(DOC *d, const GRECT *tb, GRECT *clip)
{
    GRECT r = *clip;
    long x = tb->g_x + 4, by = tb->g_y + 1, bh = tb->g_h - 3;
    int i, ih = icon_h();

    if (!rc_inter(tb, &r)) return;
    clip_set(&r);
    fill_g(&r, P_GREY);
    line(tb->g_x, tb->g_y + tb->g_h - 1, tb->g_x + tb->g_w - 1, tb->g_y + tb->g_h - 1, P_BLACK);
    for (i = 0; tbi[i].w; i++) {
        long bw = tbi[i].w - 2;
        if (tbi[i].icon >= 0) {
            GRECT b;
            int act = tb_active(d, tbi[i].cmd), dis = tb_disabled(d, tbi[i].cmd);
            b.g_x = (short)x; b.g_y = (short)by; b.g_w = (short)bw; b.g_h = (short)bh;
            if (act) fill_rect(x + 1, by + 1, x + bw - 2, by + bh - 2, P_LIGHT);
            bevel(&b, act);
            draw_tbicon(tbi[i].icon, x + 3, by + (bh - ih) / 2, P_BLACK);
            if (tbi[i].cmd < 0) {                     /* popup arrow */
                long ax = x + bw - 9, ay = by + bh / 2 - 1, k;
                for (k = 0; k < 3; k++) line(ax + k, ay + k, ax + 6 - k, ay + k, P_BLACK);
            }
            if (dis) fill_pat(x + 1, by + 1, x + bw - 2, by + bh - 2, P_GREY, 1);
        }
        x += tbi[i].w;
    }
    clip_off();
    tb_state[d->slot] = tb_key(d);
}

static void toolbar_click(DOC *d, const GRECT *tb, short mx)
{
    static const char *items[MAX_FONTS + 2];
    static char sz[NSIZES + 1][16], zm[NZOOM][8];
    long x = tb->g_x + 4;
    int i, k, n, sel;
    short px, py;
    struct Window *w = DW(d)->w;

    for (i = 0; tbi[i].w; i++) {
        if (tbi[i].icon >= 0 && mx >= x && mx < x + tbi[i].w - 2) break;
        x += tbi[i].w;
    }
    if (!tbi[i].w) return;
    px = (short)(x + w->LeftEdge); py = (short)(tb->g_y + tb->g_h + w->TopEdge);
    switch (tbi[i].cmd) {
    case TB_FONT: {
        /* as many as a popup can hold; the rest are in the font selector */
        int nshow = nfonts, rows = (scr_h - py) / (hchar + 1) - 1;
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
        sel = popup(items, NSIZES + 1, n, px, py);
        if (sel >= 0 && sel < NSIZES) apply_style(d, AP_SIZE, sizes[sel]);
        else if (sel == NSIZES) do_cmd(C_SIZEOTHER);
        break;
    case TB_COLOR:
        if (!colour_on) { alert(1, 1, AL_COLOUR_OFF, AB_OK); break; }
        for (k = 0; k < NCOLORS; k++) items[k] = L(S_COL_BLACK + k);
        sel = popup(items, NCOLORS, d->ins.color, px, py);
        if (sel >= 0) apply_style(d, AP_COLOR, sel);
        break;
    case TB_ZOOM:
        n = -1;
        for (k = 0; k < NZOOM; k++) { sprintf(zm[k], "%d%%", zooms[k]); items[k] = zm[k]; if (zooms[k] == d->zoom) n = k; }
        sel = popup(items, NZOOM, n, px, py);
        if (sel >= 0) set_zoom(d, zooms[sel]);
        break;
    default:
        if (!tb_disabled(d, tbi[i].cmd)) do_cmd(tbi[i].cmd);
    }
    update_menu(top_doc());
}

/* ---- iconified window ------------------------------------------------ */

static void draw_icon(DOC *d, const GRECT *wa)
{
    long cx = wa->g_x + wa->g_w / 2, y = wa->g_y + 3;
    char s[12];
    long k;
    fill_g(wa, P_GREY);
    fill_rect(cx - 10, y, cx + 10, y + 26, P_WHITE);
    line(cx - 10, y, cx + 4, y, P_BLACK);                     /* a page with a folded corner */
    line(cx + 4, y, cx + 10, y + 6, P_BLACK);
    line(cx + 10, y + 6, cx + 10, y + 26, P_BLACK);
    line(cx + 10, y + 26, cx - 10, y + 26, P_BLACK);
    line(cx - 10, y + 26, cx - 10, y, P_BLACK);
    for (k = y + 10; k < y + 24; k += 4) line(cx - 7, k, cx + 7, k, P_BLACK);
    sprintf(s, "%.10s", lat(d->base));
    sys_text(cx - sys_width(s) / 2, y + 29, s, P_BLACK);
}

/* ---- rulers and guides ------------------------------------------------ */

/* mm from the page edge to page pixels at 100% */
static long mm_x(long mm) { return pt2px_x(mm * 720L / 254); }
static long mm_y(long mm) { return pt2px_y(mm * 720L / 254); }

static long label_step(DOC *d, long (*mm)(long), long room)
{
    static const long st[] = { 10, 20, 50, 100 };
    int i;
    for (i = 0; i < 3; i++) if (zx(d, mm(st[i])) >= room) break;
    return st[i];
}

/* a small triangle, tip at x,y, pointing down (up = 0) or up */
static void tri(long x, long y, int up)
{
    long p[6], s = hchar >= 12 ? 4 : 3;
    p[0] = x; p[1] = y;
    p[2] = x - s; p[3] = up ? y + s : y - s;
    p[4] = x + s; p[5] = p[3];
    poly_fill(p, 3, P_BLACK, 0);
}

static void dotted_v(long x, long y1, long y2, int pen)
{
    if (!grp) return;
    SetDrPt(grp, 0xAAAA);
    line(x, y1, x, y2, pen);
    SetDrPt(grp, 0xFFFF);
}

static void draw_hruler(DOC *d, const GRECT *hr, const GRECT *tp, GRECT *clip)
{
    GRECT r = *clip;
    LINE *ln = &d->lines[doc_find_line(d, d->cursor)];
    PARA *p = doc_para_at(d, d->cursor);
    PGFMT f;
    long x0 = scr_x(d, (GRECT *)tp, 0), pw = zx(d, page_w_px(d)), mm, wmm, step, base = hr->g_y + hr->g_h - 1;
    long colx, colw, ml, mr;
    char s[8];

    if (!rc_inter(hr, &r)) return;
    clip_set(&r);
    fill_g(hr, P_WHITE);
    page_fmt(d, ln->page, &f);
    if (x0 > hr->g_x) fill_rect(hr->g_x, hr->g_y, x0 - 1, base, P_DESK);
    if (x0 + pw < hr->g_x + hr->g_w) fill_rect(x0 + pw, hr->g_y, hr->g_x + hr->g_w - 1, base, P_DESK);
    ml = zx(d, pt2px_x(f.ml)); mr = zx(d, pt2px_x(f.mr));
    fill_rect(x0, hr->g_y + 1, x0 + ml - 1, base - 1, npens >= 16 ? P_LIGHT : P_GREY);
    fill_rect(x0 + pw - mr, hr->g_y + 1, x0 + pw - 1, base - 1, npens >= 16 ? P_LIGHT : P_GREY);
    wmm = (long)PS(d).w * 254 / 720;
    step = label_step(d, mm_x, 3 * wchar);
    for (mm = 0; mm <= wmm; mm++) {
        long x = x0 + zx(d, mm_x(mm)), h;
        if (x < r.g_x - 2 * wchar || x > r.g_x + r.g_w + 2 * wchar) continue;
        if (mm % 10 == 0) h = hchar >= 12 ? 6 : 4;
        else if (mm % 5 == 0) h = hchar >= 12 ? 4 : 3;
        else if (zx(d, mm_x(1)) >= 3) h = 2;
        else continue;
        line(x, base - h, x, base, P_BLACK);
        if (mm % step == 0 && mm > 0 && mm < wmm) {
            sprintf(s, "%ld", mm / 10);
            sys_text(x - sys_width(s) / 2, hr->g_y + 1, s, P_BLACK);
        }
    }
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
                fill_rect(x, hr->g_y + 2, x + 1, base, P_BLACK);
                if (c < T->ncols) cx += pt2px_x(T->w[c]);
            }
        }
    } else {
        tri(x0 + zx(d, colx + pt2px_x(p->left + p->first)), hr->g_y + 1 + (hchar >= 12 ? 4 : 3), 0);
        tri(x0 + zx(d, colx + pt2px_x(p->left)), base - (hchar >= 12 ? 4 : 3), 1);
        tri(x0 + zx(d, colx + colw), base - (hchar >= 12 ? 4 : 3), 1);
    }
    line(hr->g_x, base, hr->g_x + hr->g_w - 1, base, P_BLACK);
    clip_off();
    rul_state[d->slot] = ruler_key(d);
}

static void draw_vruler(DOC *d, const GRECT *vr, const GRECT *tp, GRECT *clip)
{
    GRECT r = *clip;
    int pg, np = doc_npages(d);
    long ph = page_h_view(d), hmm = (long)PS(d).h * 254 / 720, step, right = vr->g_x + vr->g_w - 1;
    char s[8];

    if (!rc_inter(vr, &r)) return;
    clip_set(&r);
    fill_g(vr, P_DESK);
    step = label_step(d, mm_y, hchar + 2);
    for (pg = 0; pg < np; pg++) {
        long y0 = scr_y(d, (GRECT *)tp, pg, 0), mm, mt, mb;
        PGFMT f;
        if (y0 > r.g_y + r.g_h) break;
        if (y0 + ph < r.g_y) continue;
        page_fmt(d, pg, &f);
        fill_rect(vr->g_x, y0, right, y0 + ph - 1, P_WHITE);
        mt = zx(d, pt2px_y(f.mt)); mb = zx(d, pt2px_y(f.mb));
        fill_rect(vr->g_x + 1, y0, right - 1, y0 + mt - 1, npens >= 16 ? P_LIGHT : P_GREY);
        fill_rect(vr->g_x + 1, y0 + ph - mb, right - 1, y0 + ph - 1, npens >= 16 ? P_LIGHT : P_GREY);
        for (mm = 0; mm <= hmm; mm++) {
            long y = y0 + zx(d, mm_y(mm)), w;
            if (y < r.g_y - hchar || y > r.g_y + r.g_h + hchar) continue;
            if (mm % 10 == 0) w = vr->g_w / 3;
            else if (mm % 5 == 0) w = vr->g_w / 5;
            else if (zx(d, mm_y(1)) >= 3) w = 2;
            else continue;
            line(right - w, y, right, y, P_BLACK);
            if (mm % step == 0 && mm > 0 && mm < hmm) {
                sprintf(s, "%ld", mm / 10);
                sys_text(vr->g_x + 2, y - hchar / 2, s, P_BLACK);
            }
        }
    }
    line(right, vr->g_y, right, vr->g_y + vr->g_h - 1, P_BLACK);
    clip_off();
}

static void draw_corner(const GRECT *hr, const GRECT *vr, GRECT *clip)
{
    GRECT c = *clip, k;
    k.g_x = vr->g_x; k.g_y = hr->g_y; k.g_w = vr->g_w; k.g_h = hr->g_h;
    if (!rc_inter(&k, &c)) return;
    clip_set(&c);
    fill_g(&k, P_DESK);
    clip_off();
}

/* dotted lines where the margins and column gaps of a page are */
static void draw_guides(DOC *d, int page, long x, long y)
{
    PGFMT f;
    long l, t, rt, b, colw;
    int c, pen = pref_display != DISP_NORMAL ? ed_fg() : npens >= 16 ? P_DBLUE : P_BLACK;
    page_fmt(d, page, &f);
    l = x + zx(d, pt2px_x(f.ml)); rt = x + zx(d, pt2px_x(PS(d).w - f.mr));
    t = y + zx(d, pt2px_y(f.mt)); b = y + zx(d, pt2px_y(PS(d).h - f.mb));
    hline(l, rt, t, pen, 1);
    hline(l, rt, b, pen, 1);
    dotted_v(l, t, b, pen);
    dotted_v(rt, t, b, pen);
    if (f.cols > 1) {
        colw = (pt2px_x(PS(d).w - f.ml - f.mr) - pt2px_x(f.colgap) * (f.cols - 1)) / f.cols;
        for (c = 1; c < f.cols; c++) {
            long cx = pt2px_x(f.ml) + c * colw + (c - 1) * pt2px_x(f.colgap);
            dotted_v(x + zx(d, cx), t, b, pen);
            dotted_v(x + zx(d, cx + pt2px_x(f.colgap)), t, b, pen);
        }
    }
}

static void draw_window(DOC *d, GRECT *clip)
{
    GRECT tb, tp, cp, dv, hr, vr;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (d->iconified) { GRECT wa; work_area(d, &wa); clip_set(&wa); draw_icon(d, &wa); clip_off(); return; }
    if (tb.g_h) draw_toolbar(d, &tb, clip);
    if (hr.g_h) { draw_hruler(d, &hr, &tp, clip); draw_vruler(d, &vr, &tp, clip); draw_corner(&hr, &vr, clip); }
    if (tp.g_h) draw_text_pane(d, &tp, clip);
    if (hr.g_h && d->view == VIEW_SPLIT) { tp.g_y = hr.g_y; tp.g_h += hr.g_h; tp.g_x = vr.g_x; tp.g_w += vr.g_w; }
    if (d->view == VIEW_SPLIT) {
        GRECT r;
        dv = tp;
        dv.g_y = tp.g_y + tp.g_h;
        dv.g_h = DIVH;
        r = *clip;
        if (rc_inter(&dv, &r)) {
            fill_g(&r, P_GREY);
            line(dv.g_x, dv.g_y, dv.g_x + dv.g_w - 1, dv.g_y, P_WHITE);
            line(dv.g_x, dv.g_y + DIVH - 1, dv.g_x + dv.g_w - 1, dv.g_y + DIVH - 1, P_BLACK);
        }
    }
    if (cp.g_h) draw_codes_pane(d, &cp, clip);
}

void redraw_area(DOC *d, GRECT *area)
{
    GRECT a = *area, wa;
    if (!DW(d)->w) return;
    work_area(d, &wa);
    if (!rc_inter(&wa, &a)) return;
    edlog("redraw %d,%d %dx%d", a.g_x, a.g_y, a.g_w, a.g_h);
    draw_begin(d);
    draw_window(d, &a);
    gfx_end();
    edlog("redraw done");
}

void refresh_all(DOC *d)
{
    GRECT wa;
    if (!DW(d)->w) return;
    work_area(d, &wa);
    redraw_area(d, &wa);
}

void refresh_text(DOC *d)
{
    GRECT tb, hr, vr, tp, cp;
    if (!DW(d)->w) return;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (hr.g_h) { tp.g_h += (short)(tp.g_y - hr.g_y); tp.g_y = hr.g_y; tp.g_w += (short)(tp.g_x - vr.g_x); tp.g_x = vr.g_x; }
    if (tp.g_h) redraw_area(d, &tp);
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

static void refresh_ruler_if_changed(DOC *d)
{
    GRECT tb, hr, vr, tp, cp;
    if (!rulers_on(d) || !DW(d)->w || rul_state[d->slot] == ruler_key(d)) return;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (hr.g_h) redraw_area(d, &hr);
}
void refresh_codes(DOC *d)   { GRECT tb, tp, cp; if (!DW(d)->w) return; get_panes(d, &tb, &tp, &cp); if (cp.g_h) redraw_area(d, &cp); }
void refresh_toolbar(DOC *d) { GRECT tb, tp, cp; if (!DW(d)->w) return; get_panes(d, &tb, &tp, &cp); if (tb.g_h) redraw_area(d, &tb); }

static void refresh_tb_if_changed(DOC *d)
{
    if (d->toolbar && tb_state[d->slot] != tb_key(d)) refresh_toolbar(d);
}

/* redraw from line li to the bottom of the pane; from the top of its page
 * when the page has columns, tables or pictures (they move together) */
static void refresh_from(DOC *d, long li)
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

static void refresh_lines(DOC *d, long la, long lb)
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
        long h = ln->lh > ln->asc + ln->desc ? ln->lh : ln->asc + ln->desc;
        if (y < sy) sy = y;
        if (y + zx(d, h) > ey) ey = y + zx(d, h);
    }
    sy -= 1; ey += 2;
    if (sy < tp.g_y) sy = tp.g_y;
    if (ey > tp.g_y + tp.g_h) ey = tp.g_y + tp.g_h;
    if (ey <= sy) return;
    r.g_x = tp.g_x; r.g_w = tp.g_w; r.g_y = (short)sy; r.g_h = (short)(ey - sy);
    redraw_area(d, &r);
}

/* ================================================================= */
/*  Window furniture: the scrollers and the title                      */
/* ================================================================= */

static void set_prop(DOC *d, int gid, long top, long total, long vis)
{
    DWIN *w = DW(d);
    long f = 1;
    if (!w->g[gid]) return;
    if (total < vis) total = vis;
    while (total / f > 30000) f *= 2;
    if (gid == GID_VPROP) w->vf = f; else w->hf = f;
#ifdef AMI13
    c13_set_prop(w->g[gid], w->w, total / f, vis / f, top / f);
#else
    SetGadgetAttrs(w->g[gid], w->w, NULL, PGA_Total, (ULONG)(total / f), PGA_Visible, (ULONG)(vis / f),
                   PGA_Top, (ULONG)(top / f), TAG_END);
#endif
}

void set_sliders(DOC *d)
{
    GRECT tb, tp, cp;
    if (d->iconified || !DW(d)->w) return;
    get_panes(d, &tb, &tp, &cp);
    if (codes_focus(d)) set_prop(d, GID_VPROP, d->cv.top, d->cv.nl, codes_vis(d));
    else set_prop(d, GID_VPROP, d->scroll_y, content_h(d), tp.g_h);
    set_prop(d, GID_HPROP, d->scroll_x, codes_focus(d) ? 0 : content_w(d), tp.g_w);
}

static void set_title(DOC *d)
{
    DWIN *w = DW(d);
    if (!w->w) return;
    if (d->iconified) sprintf(w->title, "%.12s", lat(d->base));
    else sprintf(w->title, "%s%s  %s", d->dirty ? "* " : "", lat(d->base), d->info);
    SetWindowTitles(w->w, (UBYTE *)w->title, (UBYTE *)-1);
}

void set_name(DOC *d)
{
    d->shown_dirty = d->dirty;
    set_title(d);
}

void set_info(DOC *d)
{
    static const int jn[4] = { S_J_LEFT, S_J_CENTRE, S_J_RIGHT, S_J_FULL };
    char s[140], extra[40];
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
    sprintf(s, "- %.18s %dpt %s%s%s  %s%s  %s %d/%d  %d%%%s", fonts[c->font < nfonts ? c->font : 0].name, c->size,
            c->attr & A_BOLD ? "B" : "", c->attr & A_ITALIC ? "I" : "", c->attr & A_UNDER ? "U" : "",
            L(jn[p->just & 3]), extra, L(S_I_PAGE), ln->page + 1, doc_npages(d), d->zoom,
            d->view == VIEW_CODES ? L(S_I_QUICK) : d->view == VIEW_SPLIT ? (d->focus ? L(S_I_CODES) : L(S_I_TEXT)) : "");
    if (strcmp(s, d->info)) {
        strcpy(d->info, s);
        set_title(d);
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

/* Previous page, Next page, Go to page */
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

void ensure_caret(DOC *d) { ensure_visible(d); }

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
static int refresh_changed(DOC *d)
{
    GRECT tb, tp, cp, r;
    long a = 0, eo, en, shift = d->len - old_len, sy = 0x7fffffffL, ey = -0x7fffffffL;
    if (old_n < 1 || !d->nlines) return 0;
    get_panes(d, &tb, &tp, &cp);
    if (!tp.g_h) return 1;
    while (a < old_n && a < d->nlines && same_line(&old_lines[a], &d->lines[a], 0)) a++;
    eo = old_n - 1; en = d->nlines - 1;
    while (eo >= a && en >= a && same_line(&old_lines[eo], &d->lines[en], shift)) { eo--; en--; }
    /* the caret's old line, if the caret jumped (Tab adding a table row) */
    if (old_cur >= 0 && old_cur < old_n && (old_cur < a || old_cur > eo))
        lines_span(d, &tp, old_lines, old_cur, old_cur, &sy, &ey);
    if (a > eo && a > en && sy > ey) return 1;
    if (a <= eo) lines_span(d, &tp, old_lines, a, eo, &sy, &ey);
    if (a <= en) lines_span(d, &tp, d->lines, a, en, &sy, &ey);
    if (sy < tp.g_y) sy = tp.g_y;
    if (ey > tp.g_y + tp.g_h) ey = tp.g_y + tp.g_h;
    if (ey <= sy) return 1;
    r.g_x = tp.g_x; r.g_w = tp.g_w; r.g_y = (short)sy; r.g_h = (short)(ey - sy);
    redraw_area(d, &r);
    return 1;
}

/* after an edit in a table with a chart: draw the chart again */
static void refresh_chart_at(DOC *d, long pos)
{
    GRECT tb, tp, cp, r;
    long e;
    int t;
    const CHBOX *cb;
    if (d->len <= 0) return;
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

static void edited(DOC *d, long from, int typing)
{
    long li;
    int oldpages = d->npages;
    edlog("edit: start");
    undo_commit(d, typing);
    edlog("edit: undo done");
    doc_layout(d);
    edlog("edit: layout done");
    d->want_x = -1;
    d->selobj = -1;
    if (d->view != VIEW_DOC) d->codes_stale = 1;
    if (ensure_visible(d) || d->npages != oldpages) refresh_text(d);
    else if (!refresh_changed(d)) {
        li = doc_find_line(d, from);
        refresh_from(d, li > 0 ? li - 1 : 0);
    }
    refresh_chart_at(d, from);
    edlog("edit: drawn");
    old_n = -1;
    if (d->dirty != d->shown_dirty) set_name(d);
    set_sliders(d);
    set_info(d);
    refresh_tb_if_changed(d);
    refresh_ruler_if_changed(d);
    preview_changed(d);
    edlog("edit: done");
}

void after_edit(DOC *d, long from)   { edited(d, from, 0); update_menu(d); }
void after_typing(DOC *d, long from) { edited(d, from, 1); }

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
}

/* the caret at pos, inverted: drawing it twice takes it away again, so a
 * move is two small boxes, not a redraw */
static void caret_xor(DOC *d, long pos)
{
    GRECT tb, tp, cp;
    long li, x, y;
    LINE *ln;
    get_panes(d, &tb, &tp, &cp);
    if (!tp.g_h || !d->nlines || !DW(d)->w) return;
    li = doc_find_line(d, pos);
    ln = &d->lines[li];
    x = scr_x(d, &tp, doc_x_of(d, li, pos));
    y = scr_y(d, &tp, ln->page, ln->py);
    draw_begin(d);
    clip_set(&tp);
    xor_rect(x, y, x + (d->zoom >= 150 ? 1 : 0), y + zx(d, ln->asc + ln->desc) - 1);
    gfx_end();
}

static void scroll_text(DOC *d, long osx, long osy);

void after_move(DOC *d, long oc, long oa)
{
    long lo = oc, hi = oc;
    long osx = d->scroll_x, osy = d->scroll_y;
    int caret_shown = d->view == VIEW_DOC || d->focus == 0;
#define SPAN(v) do { if ((v) >= 0) { if ((v) < lo) lo = (v); if ((v) > hi) hi = (v); } } while (0)
    SPAN(oa); SPAN(d->cursor); SPAN(d->anchor);
#undef SPAN
    if (d->selobj < 0 && oa < 0 && d->anchor < 0 && caret_shown && !ensure_visible(d)) {
        /* only the caret moved: take it away and put it back */
        if (oc != d->cursor) { caret_xor(d, oc); caret_xor(d, d->cursor); }
        goto done;
    }
    d->scroll_x = osx; d->scroll_y = osy;
    if (d->selobj < 0 && oa >= 0 && oa == d->anchor) {
        /* a selection grew or shrank: only between the old end and the new */
        lo = oc < d->cursor ? oc : d->cursor;
        hi = oc < d->cursor ? d->cursor : oc;
    }
    if (d->selobj >= 0) { d->selobj = -1; refresh_text(d); }
    else if (ensure_visible(d)) {
        /* move the view by scrolling what is drawn, then the caret's lines */
        long nsy = d->scroll_y;
        scroll_text(d, osx, osy);
        if (nsy != osy) {
            GRECT tb, hr, vr, tp, cp;
            panes(d, &tb, &hr, &vr, &tp, &cp);
            if (vr.g_w) redraw_area(d, &vr);
        }
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
    d->anchor = a;
    d->cursor = b;
    after_move(d, oc, oa);
}

/* ---- views ------------------------------------------------------------- */

void apply_codes(DOC *d)
{
    if (!d->cv.dirty) return;
    busy(1);
    codes_apply(d);
    undo_commit(d, 0);
    doc_layout(d);
    busy(0);
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
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    if (d->view == VIEW_DOC) {
        d->cv.cols = codes_cols_of(d);
        codes_build(d);
        d->codes_stale = 0;
    }
    d->view = view;
    d->focus = view == VIEW_CODES ? 1 : 0;
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

void set_zoom(DOC *d, int zoom)
{
    if (zoom < zooms[0]) zoom = zooms[0];
    if (zoom > zooms[NZOOM - 1]) zoom = zooms[NZOOM - 1];
    if (zoom == d->zoom) return;
    d->zoom = zoom;
    if (d->view == VIEW_CODES) set_view(d, VIEW_DOC);
    clamp_scroll(d);
    ensure_visible(d);
    refresh_all(d);
    set_sliders(d);
    set_info(d);
    update_menu(d);
}

void win_resized(DOC *d)
{
    if (d->view != VIEW_DOC) {
        long cols = codes_cols_of(d);
        if (cols != d->cv.cols) codes_wrap(&d->cv, cols);
    }
    clamp_scroll(d);
    refresh_all(d);
    set_sliders(d);
}

/* ---- window creation, iconify, tiling ------------------------------------ */

#ifdef AMI13
/* Workbench 1.3: classic gadgets, placed once the window is open */
static void free_scrollers(DWIN *w)
{
    int i;
    for (i = 1; i <= 6; i++) if (w->g[i]) { c13_free_scroller(w->g[i]); w->g[i] = NULL; }
}

static struct Gadget *make_scrollers(DWIN *w)
{
    static const int gid[6] = { GID_UP, GID_DOWN, GID_LEFT, GID_RIGHT, GID_VPROP, GID_HPROP };
    int i;
    for (i = 0; i < 6; i++) {
        w->g[gid[i]] = c13_scroller(i, gid[i]);
        if (!w->g[gid[i]]) return NULL;
    }
    return w->g[GID_UP];
}

static void add_scrollers(DWIN *w)
{
    struct Gadget *g[6];
    g[SB13_UP] = w->g[GID_UP]; g[SB13_DOWN] = w->g[GID_DOWN]; g[SB13_LEFT] = w->g[GID_LEFT];
    g[SB13_RIGHT] = w->g[GID_RIGHT]; g[SB13_VPROP] = w->g[GID_VPROP]; g[SB13_HPROP] = w->g[GID_HPROP];
    c13_place_scrollers(w->w, g);
}
#else
static struct Image *sysimage(ULONG which)
{
    ULONG size = small_icons ? SYSISIZE_MEDRES : SYSISIZE_HIRES;
    return (struct Image *)NewObject(NULL, (UBYTE *)SYSICLASS, SYSIA_DrawInfo, (ULONG)dri, SYSIA_Which, which,
                                     SYSIA_Size, size, TAG_END);
}

static void free_scrollers(DWIN *w)
{
    int i;
    for (i = 1; i <= 6; i++) if (w->g[i]) { DisposeObject(w->g[i]); w->g[i] = NULL; }
    for (i = 0; i < 5; i++) if (w->img[i]) { DisposeObject(w->img[i]); w->img[i] = NULL; }
}

/* the right and bottom border scrollers, in the 2.0 style */
static struct Gadget *make_scrollers(DWIN *w)
{
    struct Gadget *prev = NULL, *first = NULL;
    struct Image **im = w->img;
    long top = scr->WBorTop + scr->Font->ta_YSize + 1, sw, sh;
    im[0] = sysimage(UPIMAGE); im[1] = sysimage(DOWNIMAGE);
    im[2] = sysimage(LEFTIMAGE); im[3] = sysimage(RIGHTIMAGE); im[4] = sysimage(SIZEIMAGE);
    if (!im[0] || !im[1] || !im[2] || !im[3] || !im[4]) return NULL;
    sw = im[4]->Width; sh = im[4]->Height;
#define BTN(gid, img, rr, rb, border) \
    w->g[gid] = (struct Gadget *)NewObject(NULL, (UBYTE *)BUTTONGCLASS, GA_Image, (ULONG)(img), \
        GA_RelRight, (ULONG)(rr), GA_RelBottom, (ULONG)(rb), border, TRUE, GA_ID, gid, \
        GA_Immediate, TRUE, GA_RelVerify, TRUE, ICA_TARGET, ICTARGET_IDCMP, \
        prev ? GA_Previous : TAG_IGNORE, (ULONG)prev, TAG_END); \
    if (!w->g[gid]) return NULL; \
    if (!first) first = w->g[gid]; \
    prev = w->g[gid];
    BTN(GID_UP, im[0], -(im[0]->Width - 1), -(sh + im[1]->Height + im[0]->Height - 1), GA_RightBorder)
    BTN(GID_DOWN, im[1], -(im[1]->Width - 1), -(sh + im[1]->Height - 1), GA_RightBorder)
    BTN(GID_LEFT, im[2], -(sw + im[3]->Width + im[2]->Width - 1), -(im[2]->Height - 1), GA_BottomBorder)
    BTN(GID_RIGHT, im[3], -(sw + im[3]->Width - 1), -(im[3]->Height - 1), GA_BottomBorder)
#undef BTN
    w->g[GID_VPROP] = (struct Gadget *)NewObject(NULL, (UBYTE *)PROPGCLASS,
        GA_RelRight, (ULONG)-(sw - 5), GA_Top, (ULONG)(top + 1), GA_Width, (ULONG)(sw - 8),
        GA_RelHeight, (ULONG)-(top + sh + im[0]->Height + im[1]->Height + 2), GA_RightBorder, TRUE,
        GA_ID, GID_VPROP, PGA_Freedom, FREEVERT, PGA_NewLook, TRUE, PGA_Borderless, TRUE,
        PGA_Total, 1, PGA_Visible, 1, PGA_Top, 0, ICA_TARGET, ICTARGET_IDCMP, GA_Previous, (ULONG)prev, TAG_END);
    if (!w->g[GID_VPROP]) return NULL;
    prev = w->g[GID_VPROP];
    w->g[GID_HPROP] = (struct Gadget *)NewObject(NULL, (UBYTE *)PROPGCLASS,
        GA_Left, (ULONG)(scr->WBorLeft), GA_RelBottom, (ULONG)-(sh - 3), GA_Height, (ULONG)(sh - 4),
        GA_RelWidth, (ULONG)-(scr->WBorLeft + sw + im[2]->Width + im[3]->Width + 2), GA_BottomBorder, TRUE,
        GA_ID, GID_HPROP, PGA_Freedom, FREEHORIZ, PGA_NewLook, TRUE, PGA_Borderless, TRUE,
        PGA_Total, 1, PGA_Visible, 1, PGA_Top, 0, ICA_TARGET, ICTARGET_IDCMP, GA_Previous, (ULONG)prev, TAG_END);
    if (!w->g[GID_HPROP]) return NULL;
    return first;
}

#endif

#ifdef AMI13
#define AMI13_GADGETS(gl) NULL
#else
#define AMI13_GADGETS(gl) (gl)
#endif

#define DOC_IDCMP (IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS | \
                   IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MENUPICK | IDCMP_IDCMPUPDATE | \
                   IDCMP_ACTIVEWINDOW | IDCMP_GADGETUP | IDCMP_CHANGEWINDOW)

static struct Window *make_window(DOC *d, long x, long y, long w, long h)
{
    DWIN *dw = DW(d);
    struct Gadget *gl;
    static WORD zoom[MAX_DOCS][4];
    int slot = d->slot;
    gl = make_scrollers(dw);
    if (!gl) { free_scrollers(dw); return NULL; }
    zoom[slot][0] = 0; zoom[slot][1] = desk.g_y; zoom[slot][2] = scr_w; zoom[slot][3] = desk.g_h;
    sprintf(dw->title, "%s", lat(d->base));
    dw->w = open_ewin((struct TagItem *)(ULONG[]){
        WA_Left, (ULONG)x, WA_Top, (ULONG)y, WA_Width, (ULONG)w, WA_Height, (ULONG)h,
        WA_Title, (ULONG)dw->title, WA_ScreenTitle, (ULONG)"EdWord " ED_VERSION,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_SizeBRight, TRUE, WA_SizeBBottom, TRUE, WA_SmartRefresh, TRUE, WA_Activate, TRUE,
        WA_MinWidth, 200, WA_MinHeight, 120, WA_MaxWidth, (ULONG)~0, WA_MaxHeight, (ULONG)~0,
        WA_Zoom, (ULONG)zoom[slot], WA_Gadgets, (ULONG)AMI13_GADGETS(gl), WA_NewLookMenus, TRUE, TAG_END }, DOC_IDCMP);
    if (!dw->w) { free_scrollers(dw); return NULL; }
#ifdef AMI13
    add_scrollers(dw);
#endif
    return dw->w;
}

int open_window(DOC *d)
{
    long x, y, w, h, slot = d->slot, want;
    if (!cwid) { cwid = fixfont->tf_XSize; chgt = fixfont->tf_YSize + 1; }
    /* where the page is much wider than the screen, start at the largest
     * zoom that shows the page's width */
    if (d->zoom == 100 && page_w_px(d) + 2 * PGAP + 40 > desk.g_w * 5L / 4) {
        int k;
        for (k = NZOOM - 1; k > 0; k--)
            if (zooms[k] <= 100 && page_w_px(d) * zooms[k] / 100 + 2 * PGAP + 40 <= desk.g_w) break;
        d->zoom = zooms[k];
    }
    want = zx(d, page_w_px(d)) + 2 * PGAP + 30;
    x = desk.g_x + slot * 16;
    y = desk.g_y + slot * 12;
    w = want < desk.g_w - x ? want : desk.g_w - x;
    h = desk.g_h - slot * 12;
    d->wh = (short)slot;
    if (!make_window(d, x, y, w, h)) {
        d->wh = -1;
        alert(1, 3, AL_NO_WINDOWS, AB_OK);
        doc_free(d);
        d->used = 0;
        return 0;
    }
    d->info[0] = 0;
    doc_sync_ins(d);
    set_name(d);
    set_sliders(d);
    set_info(d);
    refresh_all(d);
    update_menu(d);
    return 1;
}

void close_window(DOC *d)
{
    DWIN *dw = DW(d);
    if (dw->w) {
        close_ewin(dw->w);
        dw->w = NULL;
    }
    free_scrollers(dw);
    d->wh = -1;
}

/* iconify: the window becomes a small one showing an icon */
void iconify(DOC *d)
{
    DWIN *dw = DW(d);
    long w, h, slot = d->slot;
    if (d->iconified || !dw->w) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    d->ic_x = dw->w->LeftEdge; d->ic_y = dw->w->TopEdge;
    d->ic_w = dw->w->Width; d->ic_h = dw->w->Height;
    close_ewin(dw->w);
    dw->w = NULL;
    free_scrollers(dw);
    d->iconified = 1;
    w = 12 * wchar + 8;
    h = scr->WBorTop + scr->Font->ta_YSize + 1 + 30 + hchar + 6;
    sprintf(dw->title, "%.12s", lat(d->base));
    dw->w = open_ewin((struct TagItem *)(ULONG[]){
        WA_Left, (ULONG)(desk.g_x + slot * (w + 4)), WA_Top, (ULONG)(scr_h - h), WA_Width, (ULONG)w, WA_Height, (ULONG)h,
        WA_Title, (ULONG)dw->title, WA_DragBar, TRUE, WA_DepthGadget, FALSE, WA_SmartRefresh, TRUE,
        WA_Activate, TRUE, TAG_END },
        IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MENUPICK | IDCMP_REFRESHWINDOW);
    if (!dw->w) { d->iconified = 0; open_window(d); return; }
    refresh_all(d);
    update_menu(top_doc());
}

void uniconify(DOC *d)
{
    DWIN *dw = DW(d);
    if (!d->iconified) return;
    if (dw->w) close_ewin(dw->w);
    dw->w = NULL;
    d->iconified = 0;
    if (!make_window(d, d->ic_x, d->ic_y, d->ic_w, d->ic_h)) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    d->info[0] = 0;
    clamp_scroll(d);
    set_name(d);
    set_sliders(d);
    set_info(d);
    refresh_all(d);
    update_menu(d);
}

void send_to_back(DOC *d)
{
    if (DW(d)->w) WindowToBack(DW(d)->w);
    update_menu(top_doc());
}

/* how: 0 side by side, 1 stacked, 2 cascade */
void tile_windows(int how)
{
    DOC *w[MAX_DOCS];
    int n = 0, i;
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i) && !docs[i]->iconified && DW(docs[i])->w) w[n++] = docs[i];
    if (!n) return;
    for (i = 0; i < n; i++) {
        struct Window *win = DW(w[i])->w;
        long x, y, ww, hh;
        if (how == 0) {
            ww = desk.g_w / n; hh = desk.g_h;
            x = desk.g_x + i * ww; y = desk.g_y;
        } else if (how == 1) {
            ww = desk.g_w; hh = desk.g_h / n;
            x = desk.g_x; y = desk.g_y + i * hh;
        } else {
            ww = desk.g_w - (n - 1) * 20; hh = desk.g_h - (n - 1) * 16;
            x = desk.g_x + i * 20; y = desk.g_y + i * 16;
        }
        if (ww < 200) ww = 200;
        if (hh < 120) hh = 120;
        ChangeWindowBox(win, x, y, ww, hh);
        if (how == 2) WindowToFront(win);
    }
    update_menu(top_doc());
}

/* ================================================================= */
/*  Keyboard                                                          */
/* ================================================================= */

static void move_to(DOC *d, long pos, int extend)
{
    long oc = d->cursor, oa = d->anchor;
    if (pos < 0) pos = 0;
    if (pos > d->len) pos = d->len;
    if (extend) { if (d->anchor < 0) d->anchor = d->cursor; }
    else d->anchor = -1;
    d->cursor = pos;
    after_move(d, oc, oa);
}

static void line_end(DOC *d, long li, int extend)
{
    LINE *ln = &d->lines[li];
    long e = ln->start + ln->len;
    if (e > ln->start && (ISEND(d->text[e - 1].ch) || li < d->nlines - 1)) e--;
    move_to(d, e, extend);
}

/* the chart of table t: its kind and place */
void chart_menu(DOC *d, int t)
{
    TBL *T;
    int kind, pos;
    if (t < 0 || t >= d->a.ntbl) return;
    T = &d->a.tbls[t];
    kind = T->chart;
    pos = T->chart ? T->cpos : CP_BOTTOM;
    if (!dlg_chart(&kind, &pos) || (kind == T->chart && pos == T->cpos)) return;
    T->chart = (unsigned char)kind;
    T->cpos = (unsigned char)pos;
    d->dirty = 1;
    doc_changed(d);
}

static void text_key2(DOC *d, int key, int qual)
{
    int shift = (qual & KQ_SHIFT) != 0, alt = (qual & KQ_ALT) != 0, intbl = doc_in_table(d, d->cursor, NULL, NULL) != 0;
    long li, from, a, b;
    CELL c;
    unsigned char ch;

    if (d->selobj >= 0 && obj_key(d, key, qual)) return;
    li = doc_find_line(d, d->cursor);
    /* The cursor keys as the Amiga style guide has them: Shift goes to
     * the edge (the line's ends, a windowful up or down), Alt by words
     * (and pages of the document), Control to the document's ends.
     * Shift with Alt and a plain cursor key extends the selection. */
    {
        int ext = shift && alt, ctrl = (qual & KQ_CTRL) != 0;
        if (key == K_LEFT || key == K_RIGHT) {
            long p = d->cursor;
            d->want_x = -1;
            if (ctrl || (shift && !alt)) {                         /* start or end of the line */
                if (key == K_LEFT) move_to(d, d->lines[li].start, 0);
                else line_end(d, li, 0);
                return;
            }
            if (alt && !shift) {                                    /* a word */
                if (key == K_LEFT) {
                    while (p > 0 && !st_isalpha(d->text[p - 1].ch) && !(d->text[p - 1].ch >= '0' && d->text[p - 1].ch <= '9')) p--;
                    while (p > 0 && (st_isalpha(d->text[p - 1].ch) || (d->text[p - 1].ch >= '0' && d->text[p - 1].ch <= '9'))) p--;
                } else {
                    while (p < d->len && (st_isalpha(d->text[p].ch) || (d->text[p].ch >= '0' && d->text[p].ch <= '9'))) p++;
                    while (p < d->len && !st_isalpha(d->text[p].ch) && !(d->text[p].ch >= '0' && d->text[p].ch <= '9')) p++;
                }
                move_to(d, p, 0);
                return;
            }
            move_to(d, d->cursor + (key == K_LEFT ? -1 : 1), ext);
            return;
        }
        if (key == K_UP || key == K_DOWN) {
            long nl, wx;
            if (ctrl) { d->want_x = -1; move_to(d, key == K_UP ? 0 : d->len, 0); return; }
            if (alt && !shift) {                                    /* the previous or next page */
                goto_page(d, d->lines[li].page + (key == K_UP ? -1 : 1));
                return;
            }
            if (d->want_x < 0) d->want_x = doc_x_of(d, li, d->cursor);
            wx = d->want_x;
            if (shift && !alt) {                                    /* a windowful */
                GRECT tb, tp, cp;
                long y, px;
                int p;
                get_panes(d, &tb, &tp, &cp);
                y = view_page_top(d, d->lines[li].page) + zx(d, d->lines[li].py) +
                    (key == K_UP ? -tp.g_h : tp.g_h) * 9 / 10;
                d->scroll_y += (key == K_UP ? -tp.g_h : tp.g_h) * 9 / 10;
                clamp_scroll(d);
                p = view_to_page(d, &tp, tp.g_x + 1, (short)(y - d->scroll_y + tp.g_y), &px, &y);
                nl = doc_find_line(d, doc_hit(d, p, wx, y));
                move_to(d, doc_pos_in_line(d, nl, wx), 0);
                d->want_x = wx;
                refresh_text(d);
                set_sliders(d);
                return;
            }
            nl = doc_line_vert(d, li, wx, key == K_UP ? -1 : 1);
            if (nl < 0) { move_to(d, 0, ext); return; }
            if (nl >= d->nlines) { move_to(d, d->len, ext); return; }
            move_to(d, doc_pos_in_line(d, nl, wx), ext);
            d->want_x = wx;
            return;
        }
    }
    switch (key) {
    case K_BS:
        if (doc_sel(d, &a, &b)) { doc_delete_sel(d); after_edit(d, a); }
        else if (d->cursor > 0) {
            doc_delete_range(d, d->cursor - 1, d->cursor);
            d->cursor--;
            after_typing(d, d->cursor);
        }
        return;
    case K_DEL:
        if (doc_sel(d, &a, &b)) { doc_delete_sel(d); after_edit(d, a); }
        else if (d->cursor < d->len) { doc_delete_range(d, d->cursor, d->cursor + 1); after_typing(d, d->cursor); }
        return;
    case K_ESC:
        if (d->anchor >= 0) select_range(d, -1, d->cursor);
        return;
    }

    if (intbl && (key == K_TAB || key == K_RETURN || key == K_ENTER)) {
        long p = doc_next_cell(d, d->cursor, shift && key == K_TAB ? -1 : 1);
        if (p < 0 && !(shift && key == K_TAB)) {
            if (key == K_TAB) {
                if (tbl_row_insert(d, d->cursor, 1)) after_edit(d, d->cursor);
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
            select_range(d, p, e);
            if (e == p) d->anchor = -1;
        }
        return;
    }
    if (key == K_RETURN || key == K_ENTER) ch = '\n';
    else if (key == K_TAB) ch = '\t';
    else if (key >= 32 && key < 256 && key != 127 && (qual & KQ_AMIGA) == 0) ch = st_ch((unsigned char)key);
    else return;

    from = doc_sel(d, &a, &b) ? a : d->cursor;
    c = d->ins;
    c.ch = ch;
    if (doc_type(d, &c, 1)) {
        if (ch == '\n' || from != d->cursor - 1) after_edit(d, from);
        else after_typing(d, from);
    } else alert(1, 3, AL_NO_MEMORY, AB_OK);
}

/* a key in the page: remember the layout first, so only the lines the
 * key changes are redrawn */
static void text_key(DOC *d, int key, int qual)
{
    keep_lines(d);
    text_key2(d, key, qual);
    old_n = -1;
}

/* ---- codes pane editing ---------------------------------------------- */

/* ---- drawing only what changed in the codes pane ---------------------- */

/* the codes lines la..lb (lb < 0: to the bottom of the pane) again */
static void refresh_codes_lines(DOC *d, long la, long lb)
{
    GRECT tb, tp, cp, r;
    long y0, y1, t = d->cv.top;
    if (!DW(d)->w) return;
    get_panes(d, &tb, &tp, &cp);
    if (!cp.g_h) return;
    if (lb >= 0 && la > lb) { long k = la; la = lb; lb = k; }
    if (la < t) la = t;
    y0 = la == t ? cp.g_y : cp.g_y + 2 + (la - t) * chgt;
    y1 = lb < 0 || lb >= d->cv.nl - 1 ? cp.g_y + cp.g_h : cp.g_y + 2 + (lb - t + 1) * chgt;
    if (y1 > cp.g_y + cp.g_h) y1 = cp.g_y + cp.g_h;
    if (y1 <= y0) return;
    r = cp; r.g_y = (short)y0; r.g_h = (short)(y1 - y0);
    redraw_area(d, &r);
}

/* the codes caret at pos, inverted (as draw_codes_pane draws it) */
static void codes_caret_xor(DOC *d, long pos)
{
    GRECT tb, tp, cp;
    CODES *cv = &d->cv;
    long li, x, y;
    if (!DW(d)->w) return;
    get_panes(d, &tb, &tp, &cp);
    if (!cp.g_h || !cv->nl) return;
    li = codes_line_of(cv, pos);
    if (li < cv->top) return;
    x = cp.g_x + 4 + (pos - cv->ls[li]) * cwid;
    y = cp.g_y + 2 + (li - cv->top) * chgt;
    draw_begin(d);
    clip_set(&cp);
    xor_rect(x, y, x + 1, y + chgt - 1);
    clip_off();
    gfx_end();
}

/* the codes pane's first line was otop: move the lines still in view and
 * draw only the ones that came into view */
static void codes_scroll(DOC *d, long otop)
{
    GRECT tb, tp, cp, ex;
    struct Window *w = DW(d)->w;
    long n = d->cv.top - otop, vis = codes_vis(d), an = n < 0 ? -n : n, ay, ah;
    if (!n || !w) return;
    get_panes(d, &tb, &tp, &cp);
    if (!cp.g_h || an >= vis || !(w->Flags & WFLG_WINDOWACTIVE)) { refresh_codes(d); return; }
    ay = cp.g_y + 2; ah = vis * chgt;
    ScrollRaster(w->RPort, 0, n * chgt, cp.g_x, ay, cp.g_x + cp.g_w - 1, ay + ah - 1);
    if (n > 0) refresh_codes_lines(d, d->cv.top + vis - an, -1);
    else {
        refresh_codes_lines(d, d->cv.top, d->cv.top + an - 1);
        ex = cp; ex.g_y = (short)(ay + ah); ex.g_h = (short)(cp.g_y + cp.g_h - ex.g_y);
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
 * line before to the end of the paragraph, or to the bottom when the
 * number of lines changed */
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
        clip_put_text(c->t + a, b - a);
        if (cmd == C_CUT) { codes_del_sel(d); codes_edited_at(d, c->cursor); }
    }
    if (cmd == C_PASTE) {
        long n;
        char *t = clip_get_text(&n);
        if (t) { if (n) codes_ins(d, t, n); free(t); }
        else if (cclip) codes_ins(d, cclip, cclen);
    }
}

static void codes_key(DOC *d, int key, int qual)
{
    CODES *c = &d->cv;
    /* as in the page: Shift to the edges, Control to the ends, Shift
     * with Alt extends the selection */
    int edge = (qual & KQ_SHIFT) && !(qual & KQ_ALT), shift = (qual & KQ_SHIFT) && (qual & KQ_ALT);
    long li, col, wc;
    char ch;

    switch (key) {
    case K_LEFT:
        li = codes_line_of(c, c->cursor);
        codes_move(d, (edge || (qual & KQ_CTRL)) ? c->ls[li] : c->cursor - 1, shift); c->want_col = -1; return;
    case K_RIGHT:
        li = codes_line_of(c, c->cursor);
        codes_move(d, (edge || (qual & KQ_CTRL)) ? c->ls[li] + codes_line_len(c, li) : c->cursor + 1, shift); c->want_col = -1; return;
    case K_UP:
    case K_DOWN:
        if (qual & KQ_CTRL) { codes_move(d, key == K_UP ? 0 : c->len, 0); return; }
        li = codes_line_of(c, c->cursor);
        col = c->cursor - c->ls[li];
        wc = c->want_col >= 0 ? c->want_col : col;
        li += (key == K_UP ? -1 : 1) * (edge ? codes_vis(d) - 1 : 1);
        if (li < 0) codes_move(d, 0, shift);
        else if (li >= c->nl) codes_move(d, c->len, shift);
        else codes_move(d, codes_pos(c, li, wc), shift);
        c->want_col = wc;
        return;
    case K_BS:
        if (!codes_del_sel(d)) {
            if (c->cursor <= 0) return;
            codes_delete(c, c->cursor - 1, 1);
            c->cursor--;
        }
        codes_edited_at(d, c->cursor);
        return;
    case K_DEL:
        if (!codes_del_sel(d)) {
            if (c->cursor >= c->len) return;
            codes_delete(c, c->cursor, 1);
        }
        codes_edited_at(d, c->cursor);
        return;
    case K_ESC:
        if (c->anchor >= 0) { long oa = c->anchor; c->anchor = -1; codes_moved(d, c->cursor, oa, c->top); }
        return;
    }
    if (key == K_RETURN || key == K_ENTER) { codes_ins(d, "\n", 1); return; }
    if (key == K_TAB) { codes_ins(d, "[Tab]", 5); return; }
    if (key < 32 || key >= 256 || key == 127 || (qual & KQ_AMIGA)) return;
    ch = (char)st_ch((unsigned char)key);
    codes_ins(d, &ch, 1);
}

void win_key(DOC *d, int key, int qual)
{
    if (d->iconified) {
        if (key == K_RETURN || key == K_ENTER || key == ' ') uniconify(d);
        return;
    }
    if (codes_focus(d)) codes_key(d, key, qual);
    else text_key(d, key, qual);
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
    long li = d->cv.top + (my - cp->g_y - 2) / chgt, col = (mx - cp->g_x - 4 + cwid / 2) / cwid;
    if (my < cp->g_y + 2) li = d->cv.top - 1;
    if (li < 0) return 0;
    if (li >= d->cv.nl) return d->cv.len;
    return codes_pos(&d->cv, li, col);
}

static void text_click(DOC *d, GRECT *tp, short mx, short my, int qual, int clicks)
{
    long oc, oa, pos, wa, wb;
    char word[WORDMAX];
    struct Window *w = DW(d)->w;

    if (obj_click(d, tp, mx, my, qual, clicks)) return;
    {   /* a click on a chart: its kind and place */
        long px, py;
        int p = view_to_page(d, tp, mx, my, &px, &py), t;
        for (t = 0; t < d->a.ntbl; t++) {
            const CHBOX *cb = &d->cbox[t];
            if (d->a.tbls[t].chart && cb->page == p && px >= cb->x && px < cb->x + cb->w && py >= cb->y && py < cb->y + cb->h) {
                chart_menu(d, t);
                return;
            }
        }
    }
    pos = hit_text(d, tp, mx, my);
    oc = d->cursor; oa = d->anchor;
    /* holding the button on an underlined word offers corrections */
    if (pref_live && clicks < 2 && word_at(d, pos, &wa, &wb, word, WORDMAX) && word_misspelt(d, wa, wb) &&
        long_press(w, mx, my)) {
        d->anchor = wa; d->cursor = wb;
        after_move(d, oc, oa);
        track_end();
        live_popup(d, wa, wb, (short)(mx + w->LeftEdge), (short)(my + w->TopEdge));
        return;
    }
    if (clicks >= 2) {                                  /* double click: word */
        if (word_at(d, pos, &wa, &wb, word, WORDMAX)) { d->anchor = wa; d->cursor = wb; }
        after_move(d, oc, oa);
        return;
    }
    if (qual & KQ_SHIFT) { if (d->anchor < 0) d->anchor = d->cursor; }
    else d->anchor = pos;
    d->cursor = pos;
    d->want_x = -1;
    after_move(d, oc, oa);

    for (;;) {
        int scrolled = 0, q;
        if (!track_next(&mx, &my, &q)) break;
        if (my < tp->g_y && d->scroll_y > 0) { d->scroll_y -= 2 * hchar; scrolled = 1; }
        else if (my >= tp->g_y + tp->g_h) { d->scroll_y += 2 * hchar; scrolled = 1; }
        if (scrolled) clamp_scroll(d);
        pos = hit_text(d, tp, mx, my);
        if (pos != d->cursor || scrolled) {
            oc = d->cursor;
            d->cursor = pos;
            if (scrolled) { refresh_text(d); set_sliders(d); }
            else refresh_lines(d, doc_find_line(d, oc < pos ? oc : pos), doc_find_line(d, oc < pos ? pos : oc));
        }
    }
    if (d->anchor == d->cursor) d->anchor = -1;
    if (d->anchor < 0) doc_sync_ins(d);
    update_menu(d);
    set_info(d);
    refresh_tb_if_changed(d);
    refresh_ruler_if_changed(d);
    fontsel_sync();
}

static void codes_click(DOC *d, const GRECT *cp, short mx, short my, int qual)
{
    CODES *c = &d->cv;
    long pos = hit_codes(d, cp, mx, my), oc = c->cursor, oa = c->anchor;
    int q;

    if (qual & KQ_SHIFT) { if (c->anchor < 0) c->anchor = c->cursor; }
    else c->anchor = pos;
    c->cursor = pos;
    c->want_col = -1;
    codes_moved(d, oc, oa, c->top);
    for (;;) {
        long otop = c->top;
        if (!track_next(&mx, &my, &q)) break;
        if (my < cp->g_y && c->top > 0) c->top--;
        else if (my >= cp->g_y + cp->g_h) c->top++;
        if (c->top != otop) clamp_scroll(d);
        pos = hit_codes(d, cp, mx, my);
        if (pos != c->cursor || c->top != otop) {
            oc = c->cursor;
            c->cursor = pos;
            codes_moved(d, oc, c->anchor, otop);
        }
    }
    if (c->anchor == c->cursor) c->anchor = -1;
}

static void win_click(DOC *d, short mx, short my, int qual, int clicks)
{
    GRECT tb, tp, cp, hr, vr;
    panes(d, &tb, &hr, &vr, &tp, &cp);
    if (d->iconified) { uniconify(d); return; }
    track_begin(DW(d)->w);
    if (in_rect(&tb, mx, my)) { track_end(); toolbar_click(d, &tb, mx); }
    else if (in_rect(&hr, mx, my) || in_rect(&vr, mx, my)) {
        /* a double click on a ruler: the indents, or the page layout */
        track_end();
        if (clicks >= 2) do_cmd(in_rect(&hr, mx, my) && !doc_in_table(d, d->cursor, NULL, NULL) ? C_INDENTS : C_PAGE);
    }
    else if (in_rect(&tp, mx, my)) {
        if (d->view == VIEW_SPLIT) set_focus(d, 0);
        text_click(d, &tp, mx, my, qual, clicks);
    } else if (in_rect(&cp, mx, my)) {
        set_focus(d, 1);
        codes_click(d, &cp, mx, my, qual);
    }
    track_end();
}

/* ================================================================= */
/*  Window messages                                                   */
/* ================================================================= */

/* scroll the text pane by dy, moving what is already drawn */
static void scroll_text(DOC *d, long osx, long osy)
{
    GRECT tb, tp, cp, r;
    long dy = d->scroll_y - osy, dx = d->scroll_x - osx;
    struct Window *w = DW(d)->w;
    get_panes(d, &tb, &tp, &cp);
    if (!dx && !dy) return;
    if (dx || dy >= tp.g_h || -dy >= tp.g_h || !tp.g_h || !(w->Flags & WFLG_WINDOWACTIVE)) { refresh_text(d); return; }
    ScrollRaster(w->RPort, 0, dy, tp.g_x, tp.g_y, tp.g_x + tp.g_w - 1, tp.g_y + tp.g_h - 1);
    r = tp;
    if (dy > 0) { r.g_y = (short)(tp.g_y + tp.g_h - dy); r.g_h = (short)dy; }
    else r.g_h = (short)-dy;
    redraw_area(d, &r);
}

void win_msg(DOC *d, struct IntuiMessage *m)
{
    GRECT tb, tp, cp;
    struct Window *w = DW(d)->w;
    long th, tw, vis, osx = d->scroll_x, osy = d->scroll_y;
    int clicks;

    switch (m->Class) {
    case IDCMP_REFRESHWINDOW:
        GT_BeginRefresh(w);
        GT_EndRefresh(w, TRUE);
        refresh_all(d);
        return;
    case IDCMP_NEWSIZE:
        if (!d->iconified) win_resized(d);
        else refresh_all(d);
        return;
    case IDCMP_CHANGEWINDOW:
        return;
    case IDCMP_ACTIVEWINDOW:
        update_menu(d);
        fontsel_sync();
        return;
    case IDCMP_CLOSEWINDOW:
        close_doc(d);
        return;
    case IDCMP_MOUSEBUTTONS:
        if (m->Code != SELECTDOWN) return;
        clicks = double_click(m->Seconds, m->Micros) ? 2 : 1;
        {
            int q = ((m->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? KQ_SHIFT : 0) |
                    ((m->Qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? KQ_ALT : 0);
            win_click(d, m->MouseX, m->MouseY, q, clicks);
        }
        return;
    }
    if (d->iconified || m->Class != IDCMP_IDCMPUPDATE) return;

    get_panes(d, &tb, &tp, &cp);
    if (codes_focus(d)) {
        CODES *c = &d->cv;
        long otop = c->top;
        vis = codes_vis(d);
        switch (upd_id) {
        case GID_UP: c->top--; break;
        case GID_DOWN: c->top++; break;
        case GID_VPROP: if (upd_top >= 0) c->top = upd_top * DW(d)->vf; break;
        default: return;
        }
        (void)vis;
        clamp_scroll(d);
        codes_scroll(d, otop);
        if (upd_id != GID_VPROP) set_sliders(d);
        return;
    }
    switch (upd_id) {
    case GID_UP:    d->scroll_y -= 2 * hchar; break;
    case GID_DOWN:  d->scroll_y += 2 * hchar; break;
    case GID_LEFT:  d->scroll_x -= 4 * wchar; break;
    case GID_RIGHT: d->scroll_x += 4 * wchar; break;
    case GID_VPROP:
        th = content_h(d);
        if (upd_top >= 0) d->scroll_y = upd_top * DW(d)->vf;
        (void)th;
        break;
    case GID_HPROP:
        tw = content_w(d);
        if (upd_top >= 0) d->scroll_x = upd_top * DW(d)->hf;
        (void)tw;
        break;
    default:
        return;
    }
    clamp_scroll(d);
    scroll_text(d, osx, osy);
    if (upd_id != GID_VPROP && upd_id != GID_HPROP) set_sliders(d);
}
