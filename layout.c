/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * layout.c - breaking the text into lines on pages: columns, indents,
 *            line spacing, lists, tables, dot leaders, and text flowing
 *            around pictures and drawings. Also hit testing and the
 *            line walker that gives every character its x position.
 *
 * All positions are screen pixels at 100%, measured from the top left
 * corner of the page.
 */
#include <stdlib.h>
#include <string.h>
#include "wp.h"

#define SKEY(c) (((long)(c)->font << 16) | ((long)(c)->size << 8) | \
                 ((c)->attr & (A_BOLD | A_ITALIC)))
#define CELLPAD pt2px_x(3)      /* space inside a table cell            */
#define MINSPAN pt2px_x(32)     /* narrowest gap text may flow into     */
#define OBJGAP  pt2px_x(6)      /* space kept around a picture          */

/* ---- growing the line, row and page arrays ----------------------------- */

static LINE *new_line(DOC *d)
{
    if (d->nlines >= d->lcap) {
        long c = d->lcap ? d->lcap * 2 : 64;
        LINE *n = (LINE *)realloc(d->lines, c * sizeof(LINE));
        if (!n) return NULL;
        d->lines = n; d->lcap = c;
    }
    memset(&d->lines[d->nlines], 0, sizeof(LINE));
    d->lines[d->nlines].tabk = -1;
    return &d->lines[d->nlines++];
}

static TROW *new_row(DOC *d)
{
    if (d->nrows >= d->rcap) {
        long c = d->rcap ? d->rcap * 2 : 16;
        TROW *n = (TROW *)realloc(d->rows, c * sizeof(TROW));
        if (!n) return NULL;
        d->rows = n; d->rcap = c;
    }
    return &d->rows[d->nrows++];
}

/* ---- layout state ------------------------------------------------------ */

typedef struct {
    DOC  *d;
    int   page, col, ncol;
    PGFMT f;
    long  pw, ph, top, bottom;  /* page size; text area top and bottom  */
    long  colx, colw;           /* this column                          */
    long  y;                    /* next line's top                      */
    int   wc;                   /* wrap column                          */
    int   listno, listleft;     /* numbering of the current list        */
} LS;

static void set_col(LS *s)
{
    long ml = pt2px_x(s->f.ml), tw = s->pw - ml - pt2px_x(s->f.mr), gap = pt2px_x(s->f.colgap), cw;
    if (tw < pt2px_x(40)) tw = pt2px_x(40);
    s->ncol = s->f.cols < 1 ? 1 : s->f.cols;
    cw = (tw - gap * (s->ncol - 1)) / s->ncol;
    if (cw < 60) { s->ncol = 1; cw = tw; gap = 0; }
    if (s->col >= s->ncol) s->col = s->ncol - 1;
    s->colx = ml + s->col * (cw + gap);
    s->colw = cw;
}

static void ls_page(LS *s, int page)
{
    s->page = page;
    page_fmt(s->d, page, &s->f);
    s->top = pt2px_y(s->f.mt);
    s->bottom = s->ph - pt2px_y(s->f.mb);
    if (s->bottom < s->top + pt2px_y(40)) s->bottom = s->top + pt2px_y(40);
    s->col = 0;
    set_col(s);
    s->y = s->top;
}

static void next_col(LS *s)
{
    if (s->col + 1 < s->ncol) {
        s->col++;
        set_col(s);
        s->y = s->top;
    } else ls_page(s, s->page + 1);
}

/* ---- flowing around objects ---------------------------------------------- */

/* the widest free span of [L,R) between rows y0 and y1 of the page; if
 * there is none, *ny is where to try again */
static int free_span(LS *s, long y0, long y1, long L, long R, long *sx, long *sw, long *ny)
{
    DOC *d = s->d;
    long iv[2 * MAX_OBJS * MAX_SHAPES + 4];
    int n = 0, i, k;
    long best = -1, bx = L, x, low = 0x7fffffffL;

    for (i = 0; i < d->a.nobj; i++) {
        OBJ *o = &d->a.objs[i];
        long oy0, oy1, ox0, ox1;
        if (o->page != s->page || o->wrap == WR_NONE || o->type == OB_NONE) continue;
        oy0 = tw2px_y(o->y) - OBJGAP;
        oy1 = tw2px_y(o->y + o->h) + OBJGAP;
        if (oy1 <= y0 || oy0 >= y1) continue;
        if (o->type == OB_DRAW && o->wrap == WR_SHAPE) {
            /* around each shape: its width within this band */
            long by0 = px2pt_y(y0) - o->y / TWIPS - 4, by1 = px2pt_y(y1) - o->y / TWIPS + 4;
            for (k = 0; k < o->nsh; k++) {
                long sx0, sx1;
                shape_extent(&o->sh[k], by0, by1, &sx0, &sx1);
                if (sx1 <= sx0) continue;
                iv[n++] = tw2px_x(o->x + sx0 * TWIPS) - OBJGAP;
                iv[n++] = tw2px_x(o->x + sx1 * TWIPS) + OBJGAP;
                if (oy1 < low) low = oy1;
            }
        } else {
            ox0 = tw2px_x(o->x) - OBJGAP;
            ox1 = tw2px_x(o->x + o->w) + OBJGAP;
            iv[n++] = ox0; iv[n++] = ox1;
            if (oy1 < low) low = oy1;
        }
    }
    if (!n) { *sx = L; *sw = R - L; return 1; }

    /* sort the intervals by their start (few of them) */
    for (i = 2; i < n; i += 2)
        for (k = i; k > 0 && iv[k - 2] > iv[k]; k -= 2) {
            long t0 = iv[k], t1 = iv[k + 1];
            iv[k] = iv[k - 2]; iv[k + 1] = iv[k - 1];
            iv[k - 2] = t0; iv[k - 1] = t1;
        }
    x = L;
    for (i = 0; i <= n; i += 2) {
        long e = i < n ? iv[i] : R, w;
        if (e > R) e = R;
        w = e - x;
        if (w > best) { best = w; bx = x; }
        if (i < n && iv[i + 1] > x) x = iv[i + 1];
        if (x >= R) break;
    }
    if (best >= MINSPAN || best >= R - L) { *sx = bx; *sw = best; return 1; }
    *ny = low > y0 ? low : y0 + 4;
    return 0;
}

/* the bottom of the lowest wrapped object overlapping a rectangle, or -1 */
static long obj_below(LS *s, long y0, long y1, long L, long R)
{
    DOC *d = s->d;
    long low = -1;
    int i;
    for (i = 0; i < d->a.nobj; i++) {
        OBJ *o = &d->a.objs[i];
        long oy0, oy1, ox0, ox1;
        if (o->page != s->page || o->wrap == WR_NONE || o->type == OB_NONE) continue;
        oy0 = tw2px_y(o->y) - OBJGAP; oy1 = tw2px_y(o->y + o->h) + OBJGAP;
        ox0 = tw2px_x(o->x) - OBJGAP; ox1 = tw2px_x(o->x + o->w) + OBJGAP;
        if (oy1 <= y0 || oy0 >= y1 || ox1 <= L || ox0 >= R) continue;
        if (oy1 > low) low = oy1;
    }
    return low;
}

/* ---- one line ------------------------------------------------------------- */

static void line_metrics(DOC *d, long s, long e, short *asc, short *desc)
{
    const METRICS *m;
    long k, key = -1;
    *asc = *desc = 0;
    if (s >= e) {
        const CELL *c = s < d->len ? &d->text[s] : s > 0 ? &d->text[s - 1] : &d->ins;
        m = get_metrics(c->font, c->size, c->attr);
        *asc = m->asc; *desc = m->desc;
        return;
    }
    for (k = s; k < e; k++) {
        CELL *c = &d->text[k];
        if (SKEY(c) != key) {
            key = SKEY(c);
            m = get_metrics(c->font, c->size, c->attr);
            if (m->asc > *asc) *asc = m->asc;
            if (m->desc > *desc) *desc = m->desc;
        }
    }
}

static long first_height(DOC *d, long i)
{
    short a, ds;
    line_metrics(d, i, i, &a, &ds);
    return a + ds;
}

/* Greedy word wrap into width W (and the wrap column). Spaces and tabs
 * may hang past the edge; a word longer than a line is broken anywhere.
 * tx0 is the line's offset in its column, for the tab stops. */
/* how far a tab at x moves: to the hanging indent if the line has one
 * ahead, else to the next tab stop */
static long tab_adv(long x, long tx0, long hang, long tab)
{
    if (hang > 0 && x < hang) return hang - x;
    return tab - (x + tx0) % tab;
}

static long fit(DOC *d, long i, long W, int wc, long tx0, long hang)
{
    long x = 0, j = i, brk = -1, key = -1, n = 0, tab = tab_px();
    const METRICS *m = NULL;
    while (j < d->len) {
        CELL *c = &d->text[j];
        long w;
        if (ISEND(c->ch)) { j++; break; }
        if (c->ch == '\t') { x += tab_adv(x, tx0, hang, tab); j++; n++; brk = j; continue; }
        if (SKEY(c) != key) { key = SKEY(c); m = get_metrics(c->font, c->size, c->attr); }
        w = m->w[c->ch];
        if (c->ch == ' ') { x += w; j++; n++; brk = j; continue; }
        if ((x + w > W || (wc > 0 && n >= wc)) && j > i) {
            if (brk > i) j = brk;
            break;
        }
        x += w; j++; n++;
    }
    return j;
}

/* centre/right offset, the space stretch for full justification, and
 * the width of a dot-leader tab */
static void align_line(DOC *d, LINE *ln, const PARA *P)
{
    long k, s = ln->start, e = ln->start + ln->len, x = 0, key = -1, tab = tab_px();
    long cw = 0, lastns = -1, lastab = -1, nsp = 0, wrap = ln->w;
    const METRICS *m = NULL;
    int just = P->just, last = (ln->flags & LF_LAST) != 0;

    ln->xoff = ln->sp_first = ln->nsp = ln->sp_add = ln->sp_rem = 0;
    ln->tabk = -1; ln->tabw = 0;
    if (ln->len == 0) return;
    for (k = s; k < e; k++) {
        CELL *c = &d->text[k];
        if (ISEND(c->ch)) break;
        if (c->ch == '\t') { x += tab_adv(x, ln->tx0, ln->hang, tab); lastab = k; lastns = k; cw = x; continue; }
        if (SKEY(c) != key) { key = SKEY(c); m = get_metrics(c->font, c->size, c->attr); }
        x += m->w[c->ch];
        if (c->ch != ' ') { lastns = k; cw = x; }
    }
    if (P->tocent == TOCE_ENTRY && lastab >= 0) {
        /* the last tab stretches to put the page number at the right */
        long before = 0, after = 0;
        key = -1;
        for (k = s; k < lastab; k++) {
            CELL *c = &d->text[k];
            if (c->ch == '\t') { before += tab_adv(before, ln->tx0, ln->hang, tab); continue; }
            if (SKEY(c) != key) { key = SKEY(c); m = get_metrics(c->font, c->size, c->attr); }
            before += m->w[c->ch];
        }
        key = -1;
        for (k = lastab + 1; k < e && !ISEND(d->text[k].ch); k++) {
            CELL *c = &d->text[k];
            if (SKEY(c) != key) { key = SKEY(c); m = get_metrics(c->font, c->size, c->attr); }
            after += m->w[c->ch];
        }
        if (wrap - before - after > pt2px_x(8)) {
            ln->tabk = (short)(lastab - s);
            ln->tabw = (short)(wrap - before - after);
        }
        return;
    }
    if (just == J_LEFT || lastns < 0 || cw >= wrap) return;
    if (just == J_CENTRE) ln->xoff = (short)((wrap - cw) / 2);
    else if (just == J_RIGHT) ln->xoff = (short)(wrap - cw);
    else if (just == J_FULL && !last) {
        ln->sp_first = (short)((lastab >= 0 ? lastab + 1 : s) - s);
        for (k = s + ln->sp_first; k < lastns; k++)
            if (d->text[k].ch == ' ') nsp++;
        if (nsp > 0) {
            ln->nsp = (short)nsp;
            ln->sp_add = (short)((wrap - cw) / nsp);
            ln->sp_rem = (short)((wrap - cw) % nsp);
        }
    }
}

static long spaced(long h, const PARA *P)
{
    return P->spacing == LS_DOUBLE ? 2 * h : P->spacing == LS_ONEHALF ? h + h / 2 : h;
}

static LINE *add_line(LS *s, long i, long j, long x, long w, long tx0, long y, const PARA *P)
{
    DOC *d = s->d;
    LINE *ln = new_line(d);
    if (!ln) return NULL;
    ln->start = i;
    ln->len = j - i;
    ln->page = (short)s->page;
    ln->col = (short)s->col;
    ln->x = (short)x;
    ln->w = (short)w;
    ln->tx0 = (short)tx0;
    ln->py = (short)y;
    line_metrics(d, i, j, &ln->asc, &ln->desc);
    ln->lh = (short)spaced(ln->asc + ln->desc, P);
    return ln;
}

/* ---- paragraphs ----------------------------------------------------------- */

static void list_indent(const PARA *P, long *left, long *first)
{
    *left = pt2px_x(P->left);
    *first = pt2px_x(P->first);
    if (P->list && P->left == 0 && P->first == 0) {   /* a list's default indent */
        *left = pt2px_x(24);
        *first = -pt2px_x(18);
    }
}

/* lay out the paragraph [i, e] (e: the cell that ends it, or len); with
 * brk >= 0, its line brk starts a new column or page */
static void lay_para1(LS *s, long i, long e, const PARA *P, long brk)
{
    DOC *d = s->d;
    long left, first, nl = 0;
    int firstline = 1;

    list_indent(P, &left, &first);
    if (P->list == LIST_NUMBER) {
        if (s->listno > 0 && s->listleft == P->left) s->listno++;
        else s->listno = 1;
        s->listleft = P->left;
    } else s->listno = 0;

    do {
        long ind = firstline && !P->list ? left + first : left, est, j, sx = 0, sw = 0, ny, hang = 0;
        int tries = 0, skips = 0;
        LINE *ln;
        if (ind < 0) ind = 0;
        if (ind > s->colw - pt2px_x(24)) ind = s->colw - pt2px_x(24);
        if (ind < 0) ind = 0;
        if (nl++ == brk && brk > 0) next_col(s);
        est = first_height(d, i);
        for (;;) {
            long L = s->colx + ind, R = s->colx + s->colw;
            short a, ds;
            if (!free_span(s, s->y, s->y + est, L, R, &sx, &sw, &ny)) {
                s->y = ny;
                if (s->y + est > s->bottom) next_col(s);
                if (++tries > 400) { sx = L; sw = R - L; break; }
                continue;
            }
            hang = firstline && !P->list && first < 0 ? s->colx + left - sx : 0;
            j = fit(d, i, sw, s->wc, sx - s->colx, hang);
            /* a word that doesn't fit beside a picture goes below it
             * rather than being broken */
            if (sw < R - L && j > i && j < d->len && skips < 400 && !ISEND(d->text[j - 1].ch) &&
                d->text[j - 1].ch != ' ' && d->text[j - 1].ch != '\t' && d->text[j].ch != ' ' && !ISEND(d->text[j].ch)) {
                s->y += pt2px_y(4) > 0 ? pt2px_y(4) : 1;
                if (s->y + est > s->bottom) next_col(s);
                skips++;
                continue;
            }
            line_metrics(d, i, j, &a, &ds);
            if (s->y > s->top && s->y + a + ds > s->bottom) { next_col(s); continue; }
            if (a + ds > est && tries < 3) { est = a + ds; tries++; continue; }
            break;
        }
        hang = firstline && !P->list && first < 0 ? s->colx + left - sx : 0;
        j = fit(d, i, sw, s->wc, sx - s->colx, hang);
        ln = add_line(s, i, j, sx, sw, sx - s->colx, s->y, P);
        if (!ln) return;
        ln->hang = (short)(hang > 0 ? hang : 0);
        if (firstline) {
            ln->flags |= LF_FIRST;
            if (P->list) {
                long mx = left + first;
                if (mx < 0) mx = 0;
                ln->mark = (short)(P->list == LIST_BULLET ? -1 : s->listno);
                ln->markx = (short)(s->colx + mx);
            }
        }
        if (j > e || j >= d->len) ln->flags |= LF_LAST;
        align_line(d, ln, P);
        s->y += ln->lh;
        i = j;
        firstline = 0;
    } while (i <= e && i < d->len);
}


/* How paragraphs may break across columns and pages:
 * KEEP_ANY anywhere; KEEP_WIDOWS never one line alone at the foot or the
 * head of a column (no orphans or widows); KEEP_TOGETHER as KEEP_WIDOWS,
 * and a paragraph up to half a column high moves whole to the next column
 * rather than break. Headings always stay with the paragraph after them
 * (unless KEEP_ANY). */
int lay_keep = KEEP_TOGETHER;

static int same_col(const LINE *a, const LINE *b) { return a->page == b->page && a->col == b->col; }

static void lay_para(LS *s, long i, long e, const PARA *P)
{
    DOC *d = s->d;
    LS s0 = *s;
    long n0 = d->nlines, nl, k, first_seg, last_seg, h = 0;
    int at_top = s0.y <= s0.top;
    lay_para1(s, i, e, P, -1);
    if (lay_keep == KEEP_ANY || at_top) return;
    nl = d->nlines - n0;
    if (nl < 2) return;
    for (first_seg = 1; first_seg < nl && same_col(&d->lines[n0], &d->lines[n0 + first_seg]); first_seg++) ;
    if (first_seg == nl) return;                        /* all in one column */
    for (k = 0; k < nl; k++) h += d->lines[n0 + k].lh;
    if (first_seg < 2 || (lay_keep == KEEP_TOGETHER && h <= (s0.bottom - s0.top) / 2)) {
        /* an orphan, or a short paragraph: the whole paragraph moves on */
        *s = s0;
        d->nlines = n0;
        next_col(s);
        lay_para1(s, i, e, P, -1);
        return;
    }
    for (last_seg = nl - 1; last_seg > 0 && same_col(&d->lines[n0 + last_seg - 1], &d->lines[n0 + nl - 1]); last_seg--) ;
    if (nl - last_seg < 2 && last_seg >= 3 &&
        (last_seg < 2 || same_col(&d->lines[n0 + last_seg - 3], &d->lines[n0 + last_seg - 1]))) {
        /* a widow: one more line goes with it */
        *s = s0;
        d->nlines = n0;
        lay_para1(s, i, e, P, last_seg - 1);
    }
}

/* lay out one table row [rs, e] */
/* ---- a table's chart -------------------------------------------------- */

#define CHGAP pt2px_y(8)

static long tbl_width(const TBL *T)
{
    long tw = 0;
    int i;
    for (i = 0; i < T->ncols; i++) tw += pt2px_x(T->w[i]);
    return tw;
}

/* before the table's first row: the chart's size and where it goes; a
 * chart above takes its room now */
static void chart_begin(LS *s, int t, const PARA *P)
{
    DOC *d = s->d;
    const TBL *T = &d->a.tbls[t];
    CHBOX *b = &d->cbox[t];
    long tw = tbl_width(T), x0 = s->colx + pt2px_x(P->left), avail = s->colw - pt2px_x(P->left), w, h;
    TBL t2 = *T;
    b->page = -1;
    b->pos = T->cpos;
    if ((b->pos == CP_LEFT || b->pos == CP_RIGHT) && avail - tw - CHGAP < pt2px_x(100))
        b->pos = CP_BOTTOM;                 /* no room beside it: below */
    t2.cpos = (unsigned char)b->pos;
    chart_size(&t2, tw, avail, &w, &h);
    if ((b->pos == CP_LEFT || b->pos == CP_RIGHT) && w > avail - tw - CHGAP) w = avail - tw - CHGAP;
    b->w = (short)w;
    b->h = (short)h;
    b->x = (short)x0;
    if (b->pos == CP_TOP || b->pos == CP_LEFT || b->pos == CP_RIGHT)
        if (s->y + h + CHGAP > s->bottom && s->y > s->top) next_col(s);
    if (b->pos == CP_TOP) {
        b->page = (short)s->page;
        b->y = (short)s->y;
        s->y += h + CHGAP;
    }
}

/* after the first row: a chart beside the table starts level with it */
static void chart_first_row(LS *s, int t)
{
    DOC *d = s->d;
    CHBOX *b = &d->cbox[t];
    const TROW *r;
    if ((b->pos != CP_LEFT && b->pos != CP_RIGHT) || !d->nrows) return;
    r = &d->rows[d->nrows - 1];
    b->page = r->page;
    b->y = r->y;
    if (b->pos == CP_LEFT) b->x = (short)(r->x - b->w - CHGAP);
    else b->x = (short)(r->x + tbl_width(&d->a.tbls[t]) + CHGAP);
}

/* after the last row: a chart below; text goes on below a chart beside */
static void chart_end(LS *s, int t)
{
    DOC *d = s->d;
    CHBOX *b = &d->cbox[t];
    if (b->pos == CP_BOTTOM) {
        if (s->y + CHGAP + b->h > s->bottom && s->y > s->top) next_col(s);
        else s->y += CHGAP;
        b->page = (short)s->page;
        b->y = (short)s->y;
        s->y += b->h + CHGAP;
    } else if ((b->pos == CP_LEFT || b->pos == CP_RIGHT) && b->page == s->page && s->y < b->y + b->h + CHGAP)
        s->y = b->y + b->h + CHGAP;
}

static void lay_row(LS *s, long rs, long e, const PARA *P)
{
    DOC *d = s->d;
    const TBL *T = &d->a.tbls[P->tbl - 1];
    long saved = d->nlines, x0 = s->colx + pt2px_x(P->left);
    int tries = 0;
    if (T->chart && d->cbox[P->tbl - 1].pos == CP_LEFT) x0 += d->cbox[P->tbl - 1].w + CHGAP;

    for (;;) {
        long y0 = s->y, rowh = 0, k = rs, cx = x0, tw = 0, below;
        int ci;
        TROW *r;
        /* a row doesn't flow around pictures: it moves below them */
        for (ci = 0; ci < T->ncols; ci++) tw += pt2px_x(T->w[ci]);
        below = obj_below(s, y0, y0 + first_height(d, rs) + 2 * CELLPAD, x0, x0 + tw);
        if (below > y0 && tries < 4) {
            s->y = below;
            if (s->y + first_height(d, rs) > s->bottom) next_col(s);
            tries++;
            continue;
        }
        for (ci = 0; ci < T->ncols; ci++) {
            long cw = pt2px_x(T->w[ci]), inner = cw - 2 * CELLPAD, cy = y0 + CELLPAD, i = k, h;
            int lastcol = ci == T->ncols - 1;
            if (inner < pt2px_x(8)) inner = pt2px_x(8);
            if (k > e || (k >= d->len && ci > 0)) break;
            do {
                long j = fit(d, i, inner, 0, 0, 0);
                LINE *ln;
                /* the last column takes any surplus cells too */
                ln = add_line(s, i, j, cx + CELLPAD, inner, 0, cy, P);
                if (!ln) return;
                ln->flags |= LF_CELL;
                ln->cell = (unsigned char)ci;
                if (j >= d->len || ISBRK(d->text[j - 1].ch) || (!lastcol && d->text[j - 1].ch == CH_CELL))
                    ln->flags |= LF_LAST;
                align_line(d, ln, P);
                cy += ln->lh;
                i = j;
                if (j >= d->len || ISBRK(d->text[j - 1].ch)) break;
                if (!lastcol && d->text[j - 1].ch == CH_CELL) break;
            } while (i < d->len);
            h = cy - y0 + CELLPAD;
            if (h > rowh) rowh = h;
            k = i;
            cx += cw;
        }
        if (rowh < pt2px_y(8)) rowh = pt2px_y(8);
        if (y0 > s->top && y0 + rowh > s->bottom && tries < 2) {
            d->nlines = saved;
            next_col(s);
            tries++;
            continue;
        }
        r = new_row(d);
        if (r) {
            r->start = rs;
            r->page = (short)s->page;
            r->y = (short)y0;
            r->h = (short)rowh;
            r->x = (short)x0;
            r->tbl = (short)(P->tbl - 1);
        }
        s->y = y0 + rowh;
        return;
    }
}

/* ---- the whole document ------------------------------------------------------ */

static void page_index(DOC *d)
{
    long li;
    int np = 1, p, i;
    if (d->nlines) np = d->lines[d->nlines - 1].page + 1;
    for (i = 0; i < d->a.nobj; i++)
        if (d->a.objs[i].page + 1 > np) np = d->a.objs[i].page + 1;
    if (np + 1 > d->pgcap) {
        long *n = (long *)realloc(d->pgline, (np + 9) * sizeof(long));
        if (!n) { d->npages = 0; return; }
        d->pgline = n;
        d->pgcap = np + 9;
    }
    d->npages = np;
    li = 0;
    for (p = 0; p <= np; p++) {
        while (li < d->nlines && d->lines[li].page < p) li++;
        d->pgline[p] = li;
    }
}

/* lay out the paragraphs from i on; with an old layout's tail (incremental
 * layout), stop where the new lines meet the old ones again */
typedef struct {
    LINE *tail; long ntail;     /* old lines from the restart point          */
    TROW *rows; long nrows;     /* old rows from the restart point           */
    long shift, hi;             /* text growth; end of the changed range     */
} TAIL;

static int converge(LS *s, long i, TAIL *t)
{
    DOC *d = s->d;
    long io = i - t->shift, lo = 0, hi = t->ntail, k;
    const LINE *o;
    const PARA *P;
    long e;
    if (!t->tail || i < t->hi || i >= d->len || io < 0) return 0;
    while (lo < hi) {                       /* the first old line at or after io */
        long mid = (lo + hi) / 2;
        if (t->tail[mid].start < io) lo = mid + 1; else hi = mid;
    }
    if (lo >= t->ntail) return 0;
    o = &t->tail[lo];
    if (o->start != io || !(o->flags & LF_FIRST) || (o->flags & LF_CELL) ||
        o->page != s->page || o->col != s->col || o->py != s->y) return 0;
    e = doc_para_end(d, i);
    P = &d->a.paras[d->text[e < d->len ? e : d->len - 1].para < d->a.npara ? d->text[e < d->len ? e : d->len - 1].para : 0];
    if (P->list == LIST_NUMBER || P->tbl) return 0;
    /* the rest of the old layout is still right, a little further on */
    for (k = lo; k < t->ntail; k++) {
        LINE *ln = new_line(d);
        if (!ln) return 0;
        *ln = t->tail[k];
        ln->start += t->shift;
    }
    for (k = 0; k < t->nrows; k++)
        if (t->rows[k].start >= io) {
            TROW *r = new_row(d);
            if (!r) return 0;
            *r = t->rows[k];
            r->start += t->shift;
        }
    return 1;
}

static void lay_from(LS *s, long i, TAIL *t)
{
    DOC *d = s->d;
    while (i < d->len) {
        long e, last;
        const PARA *P;
        unsigned char brk;
        if (t && converge(s, i, t)) return;
        if (!t) PROG(i, d->len);
        e = doc_para_end(d, i);
        last = e < d->len ? e : d->len - 1;
        P = &d->a.paras[d->text[last].para < d->a.npara ? d->text[last].para : 0];
        brk = e < d->len ? d->text[e].ch : 0;
        if (P->tbl && P->tbl <= d->a.ntbl && d->a.tbls[P->tbl - 1].ncols > 0) {
            int t = P->tbl - 1, first = 1, lastrow = 1;
            if (d->a.tbls[t].chart) {
                if (i > 0) first = PARA_OF(d, &d->text[i - 1])->tbl != P->tbl;
                if (e + 1 < d->len) {
                    long e2 = doc_para_end(d, e + 1);
                    lastrow = PARA_OF(d, &d->text[e2 < d->len ? e2 : d->len - 1])->tbl != P->tbl;
                }
                if (first) chart_begin(s, t, P);
            }
            lay_row(s, i, e, P);
            if (d->a.tbls[t].chart) {
                if (first) chart_first_row(s, t);
                if (lastrow) chart_end(s, t);
            }
        }
        else {
            LS s0 = *s;
            long n0 = d->nlines;
            lay_para(s, i, e, P);
            /* a heading stays with the first lines of what follows it */
            if (lay_keep != KEEP_ANY && P->toc && brk != CH_PAGE && brk != CH_COLBRK && e + 1 < d->len && s0.y > s0.top &&
                d->nlines > n0 && d->lines[n0].page == s->page && d->lines[n0].col == s->col &&
                s->y + 2 * first_height(d, e + 1) > s->bottom) {
                *s = s0;
                d->nlines = n0;
                next_col(s);
                lay_para(s, i, e, P);
            }
        }
        if (P->list != LIST_NUMBER) s->listno = 0;
        i = e + 1;
        if (brk == CH_PAGE) ls_page(s, s->page + 1);
        else if (brk == CH_COLBRK) next_col(s);
    }
    /* after a final paragraph break there is an empty line for the caret */
    if (ISBRK(d->text[d->len - 1].ch)) {
        const PARA *P = &d->a.paras[d->text[d->len - 1].para < d->a.npara ? d->text[d->len - 1].para : 0];
        PARA q = *P;
        long sx, sw, ny, est = first_height(d, d->len);
        LINE *ln;
        if (q.tbl) { q.tbl = 0; q.left = 0; }
        if (s->y + est > s->bottom) next_col(s);
        if (!free_span(s, s->y, s->y + est, s->colx, s->colx + s->colw, &sx, &sw, &ny)) { sx = s->colx; sw = s->colw; }
        ln = add_line(s, d->len, d->len, sx, sw, sx - s->colx, s->y, &q);
        if (ln) {
            ln->flags = LF_FIRST | LF_LAST;
            align_line(d, ln, &q);
        }
    }
}

static void ls_start(LS *s, DOC *d)
{
    memset(s, 0, sizeof *s);
    s->d = d;
    s->pw = page_w_px(d);
    s->ph = page_h_px(d);
    s->wc = PS(d).wrapcol;
    ls_page(s, 0);
}

static void layout_all(DOC *d)
{
    LS s;
    int i;
    doc_norm_para(d);
    ls_start(&s, d);
    d->nlines = 0;
    d->nrows = 0;
    for (i = 0; i < MAX_TBLS; i++) d->cbox[i].page = -1;
    if (d->len == 0) {
        LINE *ln = add_line(&s, 0, 0, s.colx, s.colw, 0, s.y, &d->insp);
        if (ln) {
            ln->flags = LF_FIRST | LF_LAST;
            if (d->insp.list) { ln->mark = (short)(d->insp.list == LIST_BULLET ? -1 : 1); ln->markx = (short)s.colx; }
            align_line(d, ln, &d->insp);
        }
        return;
    }
    lay_from(&s, 0, NULL);
}

/* the paragraph formats of [a, b]'s paragraphs, as doc_norm_para() */
static void norm_range(DOC *d, long a, long b)
{
    long k, e;
    unsigned char cur;
    if (d->len == 0) return;
    a = doc_para_start(d, a);
    e = doc_para_end(d, b);
    if (e >= d->len) e = d->len - 1;
    cur = d->text[e].para;
    for (k = e; k >= a; k--) {
        if (ISBRK(d->text[k].ch)) cur = d->text[k].para;
        if (d->text[k].para != cur) { d->text[k].para = cur; doc_touch(d, k, k + 1); }
    }
}

/* lay out again only from just before the change, until the new lines
 * meet the old ones; 0 if that can't be done */
static int layout_part(DOC *d)
{
    long lo, hi, p, li0, n, keep;
    LINE *L;
    LS s;
    TAIL t;
    int ok;

    if (d->lay_lo > d->len) d->lay_lo = d->len;
    norm_range(d, d->lay_lo, d->lay_hi > d->lay_lo ? d->lay_hi : d->lay_lo);
    lo = d->lay_lo;
    hi = d->lay_hi > lo ? d->lay_hi : lo;
    if (d->lines[d->nlines - 1].start != d->len - d->lay_shift &&
        d->lines[d->nlines - 1].start + d->lines[d->nlines - 1].len != d->len - d->lay_shift) return 0;
    /* restart at the start of the paragraph before the changed one, and
     * not inside a table */
    p = doc_para_start(d, lo);
    if (p > 0) p = doc_para_start(d, p - 1);
    for (;;) {
        long a = 0, b = d->nlines;
        while (a < b) { long m = (a + b) / 2; if (d->lines[m].start < p) a = m + 1; else b = m; }
        li0 = a;
        if (li0 >= d->nlines) return 0;
        L = &d->lines[li0];
        if (L->start == p && (L->flags & LF_FIRST) && !(L->flags & LF_CELL)) {
            /* a paragraph at the top of a column may have been moved there
             * to keep it whole (or with its heading): start before it */
            PGFMT f;
            page_fmt(d, L->page, &f);
            if (lay_keep == KEEP_ANY || p == 0 || L->py > pt2px_y(f.mt)) break;
        }
        if (p == 0) return 0;
        p = doc_para_start(d, p - 1);
    }
    ls_start(&s, d);
    if (L->page != 0) ls_page(&s, L->page);
    if (L->col) { s.col = L->col; set_col(&s); }
    s.y = L->py;
    if (p > 0) {                                /* numbering carries on from the paragraph before */
        long ps = doc_para_start(d, p - 1), f = li0 - 1;
        const PARA *Q = PARA_OF(d, &d->text[p - 1]);
        while (f > 0 && d->lines[f].start > ps) f--;
        if (Q->list == LIST_NUMBER && f >= 0 && d->lines[f].start == ps && d->lines[f].mark > 0) {
            s.listno = d->lines[f].mark;
            s.listleft = Q->left;
        }
    }
    /* keep the old lines and rows from there on, to meet them again */
    n = d->nlines - li0;
    t.tail = (LINE *)malloc((n ? n : 1) * sizeof(LINE));
    if (!t.tail) return 0;
    if (n) memcpy(t.tail, d->lines + li0, n * sizeof(LINE));
    t.ntail = n;
    for (keep = 0; keep < d->nrows && d->rows[keep].start < p; keep++) ;
    t.nrows = d->nrows - keep;
    t.rows = (TROW *)malloc((t.nrows ? t.nrows : 1) * sizeof(TROW));
    if (!t.rows) { free(t.tail); return 0; }
    if (t.nrows) memcpy(t.rows, d->rows + keep, t.nrows * sizeof(TROW));
    t.shift = d->lay_shift;
    t.hi = hi;
    d->nlines = li0;
    d->nrows = keep;
    lay_from(&s, p, &t);
    ok = d->nlines > 0;
    free(t.tail);
    free(t.rows);
    return ok;
}

/* what a layout depends on besides the text, compared with the last one */
static int aux_same(DOC *d)
{
    const DOCAUX *a = &d->a, *b = d->lay_aux;
    if (!b) return 0;
    if (memcmp(&a->ps, &b->ps, sizeof a->ps) || a->npara != b->npara || a->ntbl != b->ntbl ||
        a->novr != b->novr || a->nobj != b->nobj) return 0;
    if (memcmp(a->paras, b->paras, a->npara * sizeof(PARA))) return 0;
    if (a->ntbl && memcmp(a->tbls, b->tbls, a->ntbl * sizeof(TBL))) return 0;
    if (a->novr && memcmp(a->ovr, b->ovr, a->novr * sizeof(PGOVR))) return 0;
    if (a->nobj && memcmp(a->objs, b->objs, a->nobj * sizeof(OBJ))) return 0;
    return 1;
}

static void lay_done(DOC *d)
{
    DOCAUX *b;
    page_index(d);
    if (!d->lay_aux) d->lay_aux = (DOCAUX *)malloc(sizeof(DOCAUX));
    b = d->lay_aux;
    if (b) {
        b->ps = d->a.ps;
        b->npara = d->a.npara; b->ntbl = d->a.ntbl; b->novr = d->a.novr; b->nobj = d->a.nobj;
        memcpy(b->paras, d->a.paras, d->a.npara * sizeof(PARA));
        if (d->a.ntbl) memcpy(b->tbls, d->a.tbls, d->a.ntbl * sizeof(TBL));
        if (d->a.novr) memcpy(b->ovr, d->a.ovr, d->a.novr * sizeof(PGOVR));
        if (d->a.nobj) memcpy(b->objs, d->a.objs, d->a.nobj * sizeof(OBJ));
    }
    d->lay_lo = d->lay_hi = -1;
    d->lay_shift = 0;
    d->lay_full = 0;
}

/* Lay the document out. After an edit only the changed part is laid out
 * again, from the paragraph before it until the lines are the same as
 * before; so typing costs the same in a long document as in a short one. */
void doc_layout(DOC *d)
{
    if (d->len > 0 && !d->lay_full && d->nlines > 0 && d->npages > 0 && aux_same(d)) {
        if (d->lay_lo < 0) { page_index(d); return; }     /* nothing changed */
        if (layout_part(d)) { lay_done(d); return; }
    }
    layout_all(d);
    lay_done(d);
}

int doc_npages(DOC *d)
{
    return d->npages > 0 ? d->npages : 1;
}

long doc_find_line(DOC *d, long pos)
{
    long lo = 0, hi = d->nlines - 1;
    while (lo < hi) {
        long mid = (lo + hi + 1) / 2;
        if (d->lines[mid].start <= pos) lo = mid; else hi = mid - 1;
    }
    /* an empty cell's line starts where the next one does: prefer it */
    return lo < 0 ? 0 : lo;
}

/* ---- the line walker: every x position comes from here ----------------- */

void lw_init(LWALK *w, DOC *d, long li)
{
    w->d = d;
    w->ln = &d->lines[li];
    w->k = w->ln->start;
    w->end = w->ln->start + w->ln->len;
    w->x = w->ln->xoff;
    w->tab = tab_px();
    w->key = -1;
    w->gi = 0;
    w->m = NULL;
}

/* advance width of the cell at w->k */
long lw_adv(LWALK *w)
{
    CELL *c = &w->d->text[w->k];
    long a;
    if (ISEND(c->ch)) return 0;
    if (c->ch == '\t') {
        if (w->k - w->ln->start == w->ln->tabk) return w->ln->tabw;
        return tab_adv(w->x, w->ln->tx0, w->ln->hang, w->tab);
    }
    w->m = get_metrics(c->font, c->size, c->attr);
    a = w->m->w[c->ch];
    if (c->ch == ' ' && w->gi < w->ln->nsp && w->k - w->ln->start >= w->ln->sp_first)
        a += w->ln->sp_add + (w->gi < w->ln->sp_rem ? 1 : 0);
    return a;
}

void lw_step(LWALK *w)
{
    CELL *c = &w->d->text[w->k];
    w->x += lw_adv(w);
    if (c->ch == ' ' && w->k - w->ln->start >= w->ln->sp_first) w->gi++;
    w->k++;
}

/* x position on the page of the caret before pos */
long doc_x_of(DOC *d, long li, long pos)
{
    LWALK w;
    lw_init(&w, d, li);
    while (w.k < pos && w.k < w.end && !ISEND(d->text[w.k].ch)) lw_step(&w);
    return d->lines[li].x + w.x;
}

/* the caret position nearest page x xt on line li */
long doc_pos_in_line(DOC *d, long li, long xt)
{
    LWALK w;
    LINE *ln = &d->lines[li];
    xt -= ln->x;
    lw_init(&w, d, li);
    while (w.k < w.end) {
        long a;
        if (ISEND(d->text[w.k].ch)) return w.k;
        a = lw_adv(&w);
        if (xt < w.x + a / 2) return w.k;
        lw_step(&w);
    }
    /* past the end of a wrapped line: stay on this line */
    if (li < d->nlines - 1 && ln->len > 0 && d->lines[li + 1].start == w.end) return w.end - 1;
    return w.end;
}

/* the text position nearest point (x, y) of a page */
long doc_hit(DOC *d, int page, long x, long y)
{
    long li, best = -1, bs = 0x7fffffffL, a, b;
    if (!d->nlines) return 0;
    if (page < 0) page = 0;
    if (page >= d->npages) page = d->npages - 1;
    a = d->pgline[page];
    b = d->pgline[page + 1];
    if (a >= b) {           /* a page without text: the end of the text before it */
        while (a > 0 && a >= d->nlines) a--;
        if (a < d->nlines && d->lines[a].page > page) return d->lines[a].start;
        return d->len;
    }
    for (li = a; li < b; li++) {
        LINE *ln = &d->lines[li];
        long top = ln->py, bot = ln->py + (ln->lh > ln->asc + ln->desc ? ln->lh : ln->asc + ln->desc);
        long dy = y < top ? top - y : y >= bot ? y - bot + 1 : 0;
        long dx = x < ln->x ? ln->x - x : x >= ln->x + ln->w ? x - (ln->x + ln->w) + 1 : 0;
        long sc = dy * 4 + dx;
        if (sc < bs) { bs = sc; best = li; }
    }
    return doc_pos_in_line(d, best, x);
}

/* the line above (dir < 0) or below (dir > 0) line li, at page x */
long doc_line_vert(DOC *d, long li, long x, int dir)
{
    LINE *ln = &d->lines[li];
    long k, best = -1, bd = 0x7fffffffL;
    long a = d->pgline[ln->page], b = d->pgline[ln->page + 1];
    for (k = a; k < b; k++) {
        LINE *l2 = &d->lines[k];
        long dist;
        if (k == li) continue;
        if (x < l2->x - pt2px_x(4) || x > l2->x + l2->w + pt2px_x(4)) continue;
        if (dir > 0 && l2->py <= ln->py + ln->lh / 2) continue;
        if (dir < 0 && l2->py + l2->lh / 2 >= ln->py) continue;
        dist = dir > 0 ? l2->py - ln->py : ln->py - l2->py;
        if (dist < bd) { bd = dist; best = k; }
    }
    if (best >= 0) return best;
    k = li + dir;
    if (k < 0) return -1;
    if (k >= d->nlines) return d->nlines;
    return k;
}

/* the text of a numbered list marker ("" for a bullet) */
/* where a list marker is drawn (page pixels): numbers end a space
 * before the text, so long numbers never run into it */
long doc_marker_x(DOC *d, long li)
{
    LINE *ln = &d->lines[li];
    char buf[12];
    const METRICS *m;
    long w = 0, x, i;
    CELL *c;
    if (ln->mark >= 0 && ln->len > 0) {
        c = &d->text[ln->start];
        m = get_metrics(c->font, c->size, c->attr);
        doc_marker(d, li, buf);
        for (i = 0; buf[i]; i++) w += m->w[(unsigned char)buf[i]];
        x = ln->x - w - m->w[' '];
        if (x < ln->x - ln->tx0) x = ln->x - ln->tx0;
        return x;
    }
    return ln->markx;
}

void doc_marker(DOC *d, long li, char *buf)
{
    LINE *ln = &d->lines[li];
    int n = ln->mark, i = 0;
    char t[8];
    (void)d;
    buf[0] = 0;
    if (n <= 0) return;
    do { t[i++] = (char)('0' + n % 10); n /= 10; } while (n && i < 6);
    n = 0;
    while (i > 0) buf[n++] = t[--i];
    buf[n++] = '.';
    buf[n] = 0;
}
