/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * edit.c - selections, typing, character styles, the internal clipboard,
 *          and table editing
 *
 * A table row is a paragraph whose format has tbl set; its cells are
 * separated by CH_CELL and it ends with a paragraph break. Editing keeps
 * that structure: deleting a selection only removes separators and row
 * ends when whole rows are selected, and text typed or pasted into a
 * cell has its line breaks turned into spaces.
 */
#include <stdlib.h>
#include <string.h>
#include "wp.h"

/* the internal clipboard: cells with their own paragraph formats and
 * tables, like a small document */
static DOC  *clip;

static const PARA *para_of_cell(DOC *d, long k)
{
    return &d->a.paras[d->text[k].para < d->a.npara ? d->text[k].para : 0];
}

/* the table number + 1 of the paragraph ending at or after k, or 0 */
static int row_tbl(DOC *d, long k)
{
    long e;
    if (d->len == 0 || k < 0) return 0;
    e = doc_para_end(d, k);
    if (e >= d->len) e = d->len - 1;
    return para_of_cell(d, e)->tbl;
}

int doc_sel(DOC *d, long *a, long *b)
{
    if (d->anchor < 0 || d->anchor == d->cursor) return 0;
    if (d->anchor < d->cursor) { *a = d->anchor; *b = d->cursor; }
    else { *a = d->cursor; *b = d->anchor; }
    return 1;
}

/* is cell k a separator that must stay because its row stays? */
static int keep_cell(DOC *d, long k, long a, long b)
{
    unsigned char ch = d->text[k].ch;
    long rs, re;
    if (ch != CH_CELL && !ISBRK(ch)) return 0;
    if (ISBRK(ch) && !para_of_cell(d, k)->tbl) {
        /* a paragraph break just before a table that stays */
        return k + 1 >= b && k + 1 < d->len && row_tbl(d, k + 1) && b <= k + 1;
    }
    if (!para_of_cell(d, k)->tbl && ch == CH_CELL) return 0;
    rs = doc_para_start(d, k);
    re = doc_para_end(d, k);
    return !(rs >= a && re < b);        /* the whole row is selected */
}

/* delete [a,b), keeping the table structure the range doesn't cover */
int doc_delete_range(DOC *d, long a, long b)
{
    long k, run_end;
    if (a < 0) a = 0;
    if (b > d->len) b = d->len;
    if (b <= a) return 0;
    run_end = b;
    for (k = b - 1; k >= a; k--) {
        if (keep_cell(d, k, a, b)) {
            if (run_end > k + 1) doc_delete(d, k + 1, run_end - k - 1);
            run_end = k;
        }
    }
    if (run_end > a) doc_delete(d, a, run_end - a);
    return 1;
}

int doc_delete_sel(DOC *d)
{
    long a, b;
    if (!doc_sel(d, &a, &b)) { d->anchor = -1; return 0; }
    doc_delete_range(d, a, b);
    d->cursor = a;
    d->anchor = -1;
    return 1;
}

int doc_type(DOC *d, const CELL *c, long n)
{
    CELL buf[256], *t = NULL;
    const CELL *src = c;
    long i;
    doc_delete_sel(d);
    if (row_tbl(d, d->cursor) && d->cursor < d->len) {
        /* inside a table cell: no breaks or separators */
        int need = 0;
        for (i = 0; i < n; i++) if (ISEND(c[i].ch)) need = 1;
        if (need) {
            t = n <= 256 ? buf : (CELL *)malloc(n * sizeof(CELL));
            if (!t) return 0;
            for (i = 0; i < n; i++) {
                t[i] = c[i];
                if (ISEND(t[i].ch)) t[i].ch = ' ';
            }
            src = t;
        }
    }
    i = doc_insert(d, d->cursor, src, n);
    if (t && t != buf) free(t);
    if (!i) return 0;
    {
        /* what follows the last inserted break joins the paragraph it was
         * typed into, and takes its format (a single typed break too) */
        long k, last = -1, p = d->cursor, e;
        int idx;
        for (k = 0; k < n && n > 1; k++) if (ISBRK(d->text[p + k].ch)) last = k;
        e = doc_para_end(d, p + n);
        if (e < d->len) idx = d->text[e].para;
        else if (p > 0 && last < 0 && ISBRK(d->text[p - 1].ch)) idx = d->ins.para;
        else idx = d->text[d->len - 1].para;
        if (e >= d->len && p + n >= d->len) idx = d->ins.para;
        for (k = last + 1; k < n; k++) d->text[p + k].para = (unsigned char)idx;
        if (e >= d->len) for (k = p + n; k < d->len; k++) d->text[k].para = (unsigned char)idx;
        doc_touch(d, p, e < d->len ? e + 1 : d->len);
    }
    d->cursor += n;
    d->anchor = -1;
    return 1;
}

/* the paragraph formats and tables of src's cells, as indices of d's */
static void remap(DOC *d, DOC *src, CELL *c, long n)
{
    unsigned char map[256], tmap[MAX_TBLS + 1];
    long k;
    int i;
    for (i = 0; i < 256; i++) map[i] = 255;
    memset(tmap, 0, sizeof tmap);
    for (k = 0; k < n; k++) {
        int pi = c[k].para < src->a.npara ? c[k].para : 0;
        if (map[pi] == 255) {
            PARA p = src->a.paras[pi];
            if (p.tbl && p.tbl <= src->a.ntbl) {
                if (!tmap[p.tbl]) {
                    const TBL *t = &src->a.tbls[p.tbl - 1];
                    tmap[p.tbl] = (unsigned char)doc_tbl_new(d, t->ncols, t->w, t->border);
                }
                p.tbl = tmap[p.tbl];
            } else p.tbl = 0;
            map[pi] = (unsigned char)para_find(d, &p);
        }
        c[k].para = map[pi];
    }
}

/* insert the cells [a,b) of another document at the caret */
int doc_insert_doc(DOC *d, DOC *src, long a, long b)
{
    CELL *c;
    int ok;
    if (b <= a) return 0;
    c = (CELL *)malloc((b - a) * sizeof(CELL));
    if (!c) return 0;
    memcpy(c, src->text + a, (b - a) * sizeof(CELL));
    remap(d, src, c, b - a);
    ok = doc_type(d, c, b - a);
    free(c);
    return ok;
}

/* type a string (ST charset) in the current insertion style */
int doc_type_str(DOC *d, const char *s)
{
    CELL buf[128];
    long n = 0;
    while (*s && n < 128) {
        buf[n] = d->ins;
        buf[n].ch = (unsigned char)*s++;
        n++;
    }
    return doc_type(d, buf, n);
}

/* Apply a character style to [a,b). For AP_TOGGLE returns the new state. */
int doc_apply(DOC *d, long a, long b, int what, int val)
{
    long k;
    int all = 1;
    if (what == AP_TOGGLE)
        for (k = a; k < b; k++)
            if (!ISEND(d->text[k].ch) && !(d->text[k].attr & val)) { all = 0; break; }
    for (k = a; k < b; k++) {
        CELL *c = &d->text[k];
        switch (what) {
        case AP_FONT:   c->font = (unsigned char)val; break;
        case AP_SIZE:   c->size = (unsigned char)val; break;
        case AP_PLAIN:  c->attr = 0; break;
        case AP_TOGGLE: if (all) c->attr &= ~val; else c->attr |= val; break;
        case AP_COLOR:  c->color = (unsigned char)val; break;
        case AP_ATTRS:  c->attr = (unsigned char)val; break;
        }
    }
    if (b > a) doc_touch(d, a, b);
    d->dirty = 1;
    return !all;
}

/* typed text takes the style of the character before the caret, and
 * the format of the paragraph it is in */
void doc_sync_ins(DOC *d)
{
    long p = d->cursor;
    CELL *c = NULL;
    if (p > d->len) p = d->len;
    if (p > 0 && !ISEND(d->text[p - 1].ch)) c = &d->text[p - 1];
    else if (p < d->len && !ISEND(d->text[p].ch)) c = &d->text[p];
    else if (p < d->len) c = &d->text[p];
    else if (p > 0) c = &d->text[p - 1];
    if (c) {
        d->ins = *c;
        d->ins.ch = 0;
    }
    if (d->len == 0) { d->ins.para = (unsigned char)para_find(d, &d->insp); return; }
    if (p >= d->len && ISBRK(d->text[d->len - 1].ch)) {
        /* a new paragraph after the last break: not a table row */
        PARA q = *para_of_cell(d, d->len - 1);
        if (q.tbl) { q.tbl = 0; q.left = 0; q.first = 0; }
        q.tocent = 0;
        d->ins.para = (unsigned char)para_find(d, &q);
    } else {
        long e = doc_para_end(d, p);
        if (e >= d->len) e = d->len - 1;
        d->ins.para = d->text[e].para;
    }
}

void doc_copy(DOC *d)
{
    long a, b;
    if (!doc_sel(d, &a, &b)) return;
    if (!clip) clip = (DOC *)malloc(sizeof(DOC));
    else doc_free(clip);
    if (!clip) return;
    doc_init(clip);
    clip->a = d->a;
    if (!doc_insert(clip, 0, d->text + a, b - a)) doc_free(clip);
}

/* a document (a block's lines, say) as the internal clipboard */
void doc_copy_doc(const DOC *t)
{
    if (!clip) clip = (DOC *)malloc(sizeof(DOC));
    else doc_free(clip);
    if (!clip) return;
    doc_init(clip);
    clip->a = t->a;
    if (!doc_insert(clip, 0, t->text, t->len)) doc_free(clip);
}

DOC *doc_clip(void) { return clip && clip->len ? clip : NULL; }

int doc_paste(DOC *d)
{
    if (!clip || !clip->len) return 0;
    return doc_insert_doc(d, clip, 0, clip->len);
}

void doc_clip_free(void)
{
    if (clip) { doc_free(clip); free(clip); clip = NULL; }
}

/* ---- column (block) selection --------------------------------------------- */

/* The block runs from the anchor's line to the caret's, between their x
 * positions. In each line it takes the characters that start inside
 * that, not the line's end. */
int col_block(DOC *d, long *la, long *lb, long *x0, long *x1)
{
    long l1, l2, xa, xb;
    if (d->anchor < 0 || d->anchor == d->cursor || !d->nlines) return 0;
    l1 = doc_find_line(d, d->anchor);
    l2 = doc_find_line(d, d->cursor);
    xa = doc_x_of(d, l1, d->anchor);
    xb = doc_x_of(d, l2, d->cursor);
    if (l1 > l2) { long t = l1; l1 = l2; l2 = t; }
    if (xa > xb) { long t = xa; xa = xb; xb = t; }
    *la = l1; *lb = l2; *x0 = xa; *x1 = xb;
    return xb > xa || l2 > l1;
}

/* the cells [a, b) of line li inside the block's x0..x1 */
void col_span(DOC *d, long li, long x0, long x1, long *a, long *b)
{
    LINE *ln = &d->lines[li];
    long e = ln->start + ln->len;
    while (e > ln->start && ISEND(d->text[e - 1].ch)) e--;
    *a = doc_pos_in_line(d, li, x0);
    *b = doc_pos_in_line(d, li, x1);
    if (*a > e) *a = e;
    if (*b > e) *b = e;
    if (*a < ln->start) *a = ln->start;
    if (*b < *a) *b = *a;
}

/* a table in the block: blocks of tables aren't done */
int col_has_table(DOC *d)
{
    long la, lb, x0, x1, li;
    if (!col_block(d, &la, &lb, &x0, &x1)) return 0;
    for (li = la; li <= lb; li++) if (d->lines[li].flags & LF_CELL) return 1;
    return 0;
}

/* the block as a document: each line's part, then a Return */
int col_copy(DOC *d, DOC *t)
{
    long la, lb, x0, x1, li, a, b;
    CELL nl;
    doc_init(t);
    t->a = d->a;
    t->a.nobj = 0; t->a.novr = 0; t->a.ntbl = 0;
    if (!col_block(d, &la, &lb, &x0, &x1)) return 0;
    for (li = la; li <= lb; li++) {
        col_span(d, li, x0, x1, &a, &b);
        if (b > a && !doc_insert(t, t->len, d->text + a, b - a)) { doc_free(t); return 0; }
        nl = b > a ? d->text[b - 1] : d->text[d->lines[li].start < d->len ? d->lines[li].start : 0];
        if (!d->len) nl = d->ins;
        nl.ch = '\n';
        if (li < lb && !doc_insert(t, t->len, &nl, 1)) { doc_free(t); return 0; }
    }
    {   /* no tables in the copy */
        long k;
        for (k = 0; k < t->len; k++) t->a.paras[t->text[k].para < t->a.npara ? t->text[k].para : 0].tbl = 0;
    }
    return 1;
}

/* delete the block (from the bottom up, so the lines above don't move);
 * the caret goes to its top left corner */
int col_delete(DOC *d)
{
    long la, lb, x0, x1, li, a, b, top = -1;
    long *sp;
    int n = 0, i;
    if (!col_block(d, &la, &lb, &x0, &x1)) return 0;
    sp = (long *)malloc((lb - la + 1) * 2 * sizeof(long));
    if (!sp) return 0;
    for (li = la; li <= lb; li++) {
        col_span(d, li, x0, x1, &a, &b);
        sp[2 * n] = a; sp[2 * n + 1] = b; n++;
        if (top < 0) top = a;
    }
    for (i = n - 1; i >= 0; i--) if (sp[2 * i + 1] > sp[2 * i]) doc_delete(d, sp[2 * i], sp[2 * i + 1] - sp[2 * i]);
    free(sp);
    d->cursor = top < 0 ? d->cursor : top;
    d->anchor = -1;
    return 1;
}

/* paste src as a block: its first line at the caret, each next line at
 * the same x in the line below (new lines at the end of the document) */
int col_paste(DOC *d, DOC *src)
{
    long k = 0, li, x, pos = d->cursor, e;
    CELL nl;
    if (!src->len || !d->nlines) return 0;
    li = doc_find_line(d, pos);
    x = doc_x_of(d, li, pos);
    while (k < src->len) {
        CELL *c;
        long n;
        for (e = k; e < src->len && !ISBRK(src->text[e].ch); e++) ;
        n = e - k;
        if (n > 0) {
            c = (CELL *)malloc(n * sizeof(CELL));
            if (!c) return 0;
            memcpy(c, src->text + k, n * sizeof(CELL));
            for (e = 0; e < n; e++) { c[e].para = d->text[pos < d->len ? pos : (d->len ? d->len - 1 : 0)].para; if (ISEND(c[e].ch)) c[e].ch = ' '; }
            if (!d->len) for (e = 0; e < n; e++) c[e].para = 0;
            if (!doc_insert(d, pos, c, n)) { free(c); return 0; }
            free(c);
        }
        k += n + 1;
        if (k >= src->len) { d->cursor = pos + n; break; }
        /* the next line: lay out what was inserted, then the line below */
        doc_layout(d);
        li = doc_find_line(d, pos + n);
        while (li + 1 < d->nlines && d->lines[li + 1].start <= pos + n) li++;
        if (li + 1 < d->nlines) {
            li++;
            pos = doc_pos_in_line(d, li, x);
        } else {                            /* past the end: a new paragraph */
            nl = d->len ? d->text[d->len - 1] : d->ins;
            nl.ch = '\n';
            if (!doc_insert(d, d->len, &nl, 1)) return 0;
            pos = d->len;
        }
    }
    d->anchor = -1;
    return 1;
}

/* ---- tables --------------------------------------------------------------- */

int doc_in_table(DOC *d, long pos, long *row_start, int *cell)
{
    long rs, k;
    int t, n = 0;
    if (pos > d->len) pos = d->len;
    if (pos >= d->len && (d->len == 0 || ISBRK(d->text[d->len - 1].ch))) return 0;
    t = row_tbl(d, pos);
    if (!t) return 0;
    rs = doc_para_start(d, pos);
    for (k = rs; k < pos; k++) if (d->text[k].ch == CH_CELL) n++;
    if (row_start) *row_start = rs;
    if (cell) *cell = n;
    return t;
}

long doc_cell_start(DOC *d, long pos)
{
    if (pos > d->len) pos = d->len;
    while (pos > 0 && !ISEND(d->text[pos - 1].ch)) pos--;
    return pos;
}

/* the separator or break ending the cell at pos (or len) */
long doc_cell_end(DOC *d, long pos)
{
    while (pos < d->len && !ISEND(d->text[pos].ch)) pos++;
    return pos;
}

/* the start of the next (dir > 0) or previous cell, or -1 */
long doc_next_cell(DOC *d, long pos, int dir)
{
    long e, s;
    int t = row_tbl(d, pos);
    if (!t) return -1;
    if (dir > 0) {
        e = doc_cell_end(d, pos);
        if (e >= d->len) return -1;
        if (ISBRK(d->text[e].ch) && (e + 1 >= d->len || row_tbl(d, e + 1) != t)) return -1;
        return e + 1;
    }
    s = doc_cell_start(d, pos);
    if (s == 0) return -1;
    if (ISBRK(d->text[s - 1].ch) && row_tbl(d, s - 1) != t) return -1;
    return doc_cell_start(d, s - 1);
}

/* the rows of the table at pos: [a, b), b just after the last row's break */
int tbl_range(DOC *d, long pos, long *a, long *b)
{
    int t = doc_in_table(d, pos, NULL, NULL);
    long s, e;
    if (!t) return 0;
    s = doc_para_start(d, pos);
    while (s > 0 && row_tbl(d, s - 1) == t) s = doc_para_start(d, s - 1);
    e = doc_para_end(d, pos);
    while (e + 1 < d->len && row_tbl(d, e + 1) == t) e = doc_para_end(d, e + 1);
    *a = s;
    *b = e < d->len ? e + 1 : d->len;
    return t;
}

int tbl_rows(DOC *d, long pos)
{
    long a, b, k;
    int n = 0;
    if (!tbl_range(d, pos, &a, &b)) return 0;
    for (k = a; k < b; k++) if (ISBRK(d->text[k].ch)) n++;
    return n;
}

static int fill_row(CELL *buf, int ncols, const CELL *st, int para)
{
    int i, n = 0;
    for (i = 0; i < ncols - 1; i++) { buf[n] = *st; buf[n].ch = CH_CELL; buf[n].para = (unsigned char)para; n++; }
    buf[n] = *st; buf[n].ch = '\n'; buf[n].para = (unsigned char)para; n++;
    return n;
}

/* a new table at pos with equal column widths across the text column */
int tbl_insert(DOC *d, long pos, int rows, int cols, int border)
{
    short w[MAX_COLS];
    PARA p;
    CELL st = d->ins, buf[MAX_COLS + 1], br;
    PGFMT f;
    long tw, total;
    int i, t, para, normal;

    if (rows < 1) rows = 1;
    if (rows > 200) rows = 200;
    if (cols < 1) cols = 1;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (doc_in_table(d, pos, NULL, NULL)) return 0;
    page_fmt(d, 0, &f);
    tw = (long)PS(d).w - f.ml - f.mr;
    if (f.cols > 1) tw = (tw - (long)f.colgap * (f.cols - 1)) / f.cols;
    for (i = 0; i < cols; i++) w[i] = (short)(tw / cols);
    t = doc_tbl_new(d, cols, w, border);
    if (!t) return 0;
    p = *doc_para_at(d, pos);
    normal = para_find(d, &p);
    p.tbl = (unsigned char)t;
    p.list = 0; p.toc = 0; p.tocent = 0; p.left = 0; p.first = 0;
    para = para_find(d, &p);
    st.ch = ' ';
    br = st;
    br.ch = '\n';
    br.para = (unsigned char)normal;

    /* the table starts a paragraph of its own */
    if (pos > 0 && !ISBRK(d->text[pos - 1].ch)) {
        if (!doc_insert(d, pos, &br, 1)) return 0;
        pos++;
    }
    total = 0;
    for (i = 0; i < rows; i++) {
        int n = fill_row(buf, cols, &st, para);
        if (!doc_insert(d, pos + total, buf, n)) return 0;
        total += n;
    }
    /* and is followed by an ordinary paragraph */
    if (pos + total >= d->len) doc_insert(d, pos + total, &br, 1);
    d->cursor = pos;
    d->anchor = -1;
    d->dirty = 1;
    return 1;
}

int tbl_row_insert(DOC *d, long pos, int below)
{
    long rs, re;
    int t, cell, n;
    CELL buf[MAX_COLS + 1], st;
    t = doc_in_table(d, pos, &rs, &cell);
    if (!t) return 0;
    re = doc_para_end(d, pos);
    st = d->text[rs];
    st.attr &= ~A_UNDER;
    n = fill_row(buf, d->a.tbls[t - 1].ncols, &st, d->text[re].para);
    if (below) rs = re + 1;
    if (!doc_insert(d, rs, buf, n)) return 0;
    d->cursor = rs;
    d->anchor = -1;
    return 1;
}

int tbl_delete(DOC *d, long pos)
{
    long a, b;
    if (!tbl_range(d, pos, &a, &b)) return 0;
    doc_delete(d, a, b - a);
    d->cursor = a;
    d->anchor = -1;
    return 1;
}

int tbl_row_delete(DOC *d, long pos)
{
    long rs, re;
    if (!doc_in_table(d, pos, &rs, NULL)) return 0;
    if (tbl_rows(d, pos) <= 1) return tbl_delete(d, pos);
    re = doc_para_end(d, pos);
    doc_delete(d, rs, re - rs + 1);
    d->cursor = rs < d->len ? rs : d->len;
    d->anchor = -1;
    return 1;
}

/* the start and end (separator) of cell c in the row starting at rs */
static void cell_span(DOC *d, long rs, int c, long *s, long *e)
{
    long k = rs;
    int i;
    for (i = 0; i < c; i++) {
        k = doc_cell_end(d, k);
        if (k >= d->len || ISBRK(d->text[k].ch)) break;
        k++;
    }
    *s = k;
    *e = doc_cell_end(d, k);
}

int tbl_col_insert(DOC *d, long pos, int right)
{
    long a, b, rs, s, e;
    int t, cell, i;
    TBL *T;
    CELL sep;
    t = doc_in_table(d, pos, NULL, &cell);
    if (!t) return 0;
    T = &d->a.tbls[t - 1];
    if (T->ncols >= MAX_COLS) return 0;
    tbl_range(d, pos, &a, &b);
    /* rows from the last to the first, so positions stay valid */
    rs = doc_para_start(d, b - 1);
    for (;;) {
        cell_span(d, rs, cell, &s, &e);
        sep = d->text[e < d->len ? e : d->len - 1];
        sep.ch = CH_CELL;
        if (right) {
            if (e < d->len && d->text[e].ch == CH_CELL) doc_insert(d, e + 1, &sep, 1);
            else doc_insert(d, e, &sep, 1);
        } else doc_insert(d, s, &sep, 1);
        if (rs <= a) break;
        rs = doc_para_start(d, rs - 1);
    }
    /* the new column takes half of the current one */
    for (i = T->ncols; i > cell + (right ? 1 : 0); i--) T->w[i] = T->w[i - 1];
    T->w[cell] = (short)(T->w[cell] / 2 > 24 ? T->w[cell] / 2 : 24);
    T->w[right ? cell + 1 : cell] = T->w[cell];
    T->ncols++;
    d->dirty = 1;
    return 1;
}

int tbl_col_delete(DOC *d, long pos)
{
    long a, b, rs, s, e;
    int t, cell, i;
    TBL *T;
    t = doc_in_table(d, pos, NULL, &cell);
    if (!t) return 0;
    T = &d->a.tbls[t - 1];
    if (T->ncols <= 1) return tbl_delete(d, pos);
    tbl_range(d, pos, &a, &b);
    rs = doc_para_start(d, b - 1);
    for (;;) {
        cell_span(d, rs, cell, &s, &e);
        if (e < d->len && d->text[e].ch == CH_CELL) doc_delete(d, s, e - s + 1);
        else if (s > rs) doc_delete(d, s - 1, e - s + 1);     /* last cell: with the separator before it */
        if (rs <= a) break;
        rs = doc_para_start(d, rs - 1);
    }
    for (i = cell; i < T->ncols - 1; i++) T->w[i] = T->w[i + 1];
    T->ncols--;
    d->cursor = a;
    d->anchor = -1;
    d->dirty = 1;
    return 1;
}

/* set the size of the table at pos: rows and columns are added or
 * removed at the end; w (may be NULL) gives the column widths */
int tbl_resize(DOC *d, long pos, int rows, int cols, const short *w, int border)
{
    long a, b, s, e;
    int t, i, n;
    TBL *T;
    t = doc_in_table(d, pos, NULL, NULL);
    if (!t) return 0;
    T = &d->a.tbls[t - 1];
    if (cols < 1) cols = 1;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows < 1) rows = 1;
    if (rows > 200) rows = 200;
    while (T->ncols < cols) {                   /* columns at the right */
        tbl_range(d, pos, &a, &b);
        cell_span(d, a, T->ncols - 1, &s, &e);
        if (!tbl_col_insert(d, s, 1)) break;
        pos = a;
    }
    while (T->ncols > cols) {
        tbl_range(d, pos, &a, &b);
        cell_span(d, a, T->ncols - 1, &s, &e);
        if (!tbl_col_delete(d, s)) break;
        pos = a;
    }
    n = tbl_rows(d, pos);
    while (n < rows) {
        tbl_range(d, pos, &a, &b);
        if (!tbl_row_insert(d, b - 1, 1)) break;
        n++;
        pos = a;
    }
    while (n > rows && n > 1) {
        tbl_range(d, pos, &a, &b);
        tbl_row_delete(d, b - 1);
        n--;
        pos = a;
    }
    if (w) for (i = 0; i < T->ncols; i++) if (w[i] >= 12) T->w[i] = w[i];
    T->border = (short)(border != 0);
    tbl_range(d, pos, &a, &b);
    d->cursor = a;
    d->anchor = -1;
    d->dirty = 1;
    return 1;
}
