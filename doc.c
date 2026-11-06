/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * doc.c - document storage, paragraph formats, tables, page formats,
 *         pictures and drawings
 */
#include <stdlib.h>
#include <string.h>
#include "wp.h"

short   dev_pixw = 372, dev_pixh = 372;
FONTDEF fonts[MAX_FONTS];
short   nfonts;

const char *paper_name[NPAPER] = { "A4", "Letter", "A5", "Legal" };
static const short paper_wh[NPAPER][2] = {
    { 595, 842 }, { 612, 792 }, { 420, 595 }, { 612, 1008 }
};

/* ---- page set-up and units ---------------------------------------- */

void page_set_paper(PAGESET *p, int paper, int landscape)
{
    if (paper < 0 || paper >= NPAPER) paper = P_A4;
    p->paper = (short)paper;
    p->landscape = (short)(landscape ? 1 : 0);
    p->w = paper_wh[paper][landscape ? 1 : 0];
    p->h = paper_wh[paper][landscape ? 0 : 1];
}

void page_default(PAGESET *p)
{
    page_set_paper(p, P_A4, 0);
    p->ml = p->mr = p->mt = p->mb = 56;     /* 2 cm */
    p->wrapcol = 0;
    p->cols = 1;
    p->colgap = 18;                          /* about 6 mm */
}

long pt2px_x(long pt) { return pt * 25400L / (72L * dev_pixw); }

/* Objects are placed in twips, so every screen pixel at every zoom has its
 * own position: pixels from twips round down, twips from pixels round up,
 * and a twip being much smaller than a pixel, a pixel comes back as itself. */
static long tw2px(long tw, long num, long den, long pix)
{
    long long v = (long long)tw * num * 25400;
    long long q = (long long)den * 1440 * pix;
    return (long)(v >= 0 ? v / q : -((-v + q - 1) / q));
}
static long px2tw(long px, long num, long den, long pix)
{
    long long v = (long long)px * 1440 * pix * den;
    long long q = (long long)num * 25400;
    return (long)(v >= 0 ? (v + q - 1) / q : -((-v) / q));
}
long tw2px_x(long tw) { return tw2px(tw, 1, 1, dev_pixw); }
long tw2px_y(long tw) { return tw2px(tw, 1, 1, dev_pixh); }
long tw2px_xs(long tw, long num, long den) { return tw2px(tw, num, den, dev_pixw); }
long tw2px_ys(long tw, long num, long den) { return tw2px(tw, num, den, dev_pixh); }
long px2tw_xs(long px, long num, long den) { return px2tw(px, num, den, dev_pixw); }
long px2tw_ys(long px, long num, long den) { return px2tw(px, num, den, dev_pixh); }
long pt2px_y(long pt) { return pt * 25400L / (72L * dev_pixh); }
long px2pt_x(long px) { return (px * 72L * dev_pixw + 12700L) / 25400L; }
long px2pt_y(long px) { return (px * 72L * dev_pixh + 12700L) / 25400L; }

long tab_px(void)
{
    long t = pt2px_x(36);
    return t < 8 ? 8 : t;
}

int default_font(void) { return nfonts > 1 ? 1 : 0; }
int default_size(void) { return nfonts > 1 ? 12 : 10; }

/* the margins and columns a page uses: the document's, unless a page
 * format covers it (one for this page alone wins over one that runs on) */
void page_fmt(DOC *d, int page, PGFMT *f)
{
    const PAGESET *ps = &PS(d);
    const PGOVR *run = NULL, *one = NULL, *o;
    int i;
    for (i = 0; i < d->a.novr; i++) {
        o = &d->a.ovr[i];
        if (o->page > page) continue;
        if (o->following) { if (!run || o->page >= run->page) run = o; }
        else if (o->page == page) one = o;
    }
    o = one ? one : run;
    if (o) {
        f->ml = o->ml; f->mr = o->mr; f->mt = o->mt; f->mb = o->mb;
        f->cols = o->cols; f->colgap = o->colgap;
    } else {
        f->ml = ps->ml; f->mr = ps->mr; f->mt = ps->mt; f->mb = ps->mb;
        f->cols = ps->cols; f->colgap = ps->colgap;
    }
    if (f->cols < 1) f->cols = 1;
    if (f->cols > 4) f->cols = 4;
    /* keep at least 3 cm of text area */
    if (f->ml + f->mr > ps->w - 85) f->ml = f->mr = (short)((ps->w - 85) / 2);
    if (f->mt + f->mb > ps->h - 85) f->mt = f->mb = (short)((ps->h - 85) / 2);
}

/* set the format for one page, or for it and the pages after it */
int page_ovr_set(DOC *d, int page, int following, const PGFMT *f)
{
    int i, k = -1;
    if (following) {
        /* a format that runs on replaces any that start at or after it */
        for (i = 0; i < d->a.novr; ) {
            if (d->a.ovr[i].page >= page) {
                memmove(&d->a.ovr[i], &d->a.ovr[i + 1], (d->a.novr - i - 1) * sizeof(PGOVR));
                d->a.novr--;
            } else i++;
        }
    } else
        for (i = 0; i < d->a.novr; i++)
            if (d->a.ovr[i].page == page && !d->a.ovr[i].following) k = i;
    if (k < 0) {
        if (d->a.novr >= MAX_PGOVR) return 0;
        k = d->a.novr++;
    }
    d->a.ovr[k].page = (short)page;
    d->a.ovr[k].following = (short)(following != 0);
    d->a.ovr[k].ml = f->ml; d->a.ovr[k].mr = f->mr;
    d->a.ovr[k].mt = f->mt; d->a.ovr[k].mb = f->mb;
    d->a.ovr[k].cols = f->cols; d->a.ovr[k].colgap = f->colgap;
    return 1;
}

void page_ovr_clear(DOC *d) { d->a.novr = 0; }

long page_w_px(DOC *d) { return pt2px_x(PS(d).w); }
long page_h_px(DOC *d) { return pt2px_y(PS(d).h); }

long text_w_px(DOC *d, int page)
{
    PGFMT f;
    long w;
    page_fmt(d, page, &f);
    w = (long)PS(d).w - f.ml - f.mr;
    return pt2px_x(w < 72 ? 72 : w);
}

/* ---- storage ------------------------------------------------------- */

void para_default(PARA *p)
{
    memset(p, 0, sizeof *p);
}

void doc_init(DOC *d)
{
    memset(d, 0, sizeof *d);
    d->anchor = -1;
    d->want_x = -1;
    d->wh = -1;
    d->selobj = -1;
    d->zoom = 100;
    d->ch_lo = d->ch_hi = -1;
    d->lay_lo = d->lay_hi = -1;
    d->lay_full = 1;
    d->vsz = d->vpos = d->hsz = d->hpos = -1;
    d->ins.font = (unsigned char)default_font();
    d->ins.size = (unsigned char)default_size();
    d->ins.para = 0;
    para_default(&d->a.paras[0]);
    para_default(&d->insp);
    d->a.npara = 1;
    d->cv.anchor = -1;
    d->cv.want_col = -1;
    d->cv.cols = 60;
    page_default(&PS(d));
    HF(d).just[0] = HF(d).just[1] = J_CENTRE;
    HF(d).size = 10;
    HF(d).first = 1;
}

void doc_free(DOC *d)
{
    int i;
    free(d->text);
    free(d->lines);
    free(d->rows);
    free(d->pgline);
    codes_free(&d->cv);
    for (i = 0; i < d->nimg; i++) img_free(&d->imgs[i]);
    free(d->fpts);
    d->fpts = NULL;
    d->nfpts = d->capfpts = 0;
    undo_free(d);
    free(d->lay_aux);
    d->lay_aux = NULL;
    d->lay_full = 1;
    d->lay_lo = d->lay_hi = -1;
    d->text = NULL; d->lines = NULL; d->rows = NULL; d->pgline = NULL;
    d->len = d->cap = d->nlines = d->lcap = d->nrows = d->rcap = 0;
    d->npages = d->pgcap = 0;
    d->nimg = 0;
}

static int grow_text(DOC *d, long need)
{
    CELL *n; long c;
    if (need <= d->cap) return 1;
    c = d->cap ? d->cap : 256;
    while (c < need) c *= 2;
    n = (CELL *)realloc(d->text, c * sizeof(CELL));
    if (!n) return 0;
    d->text = n; d->cap = c;
    return 1;
}

/* the range changed since the last layout */
static void lay_touch(DOC *d, long a, long b)
{
    if (d->lay_lo < 0) { d->lay_lo = a; d->lay_hi = b; return; }
    if (a < d->lay_lo) d->lay_lo = a;
    if (b > d->lay_hi) d->lay_hi = b;
}

/* the range changed since the last undo step, in current positions */
void doc_touch(DOC *d, long a, long b)
{
    if (d->ch_lo < 0 || a < d->ch_lo) d->ch_lo = a;
    if (b > d->ch_hi) d->ch_hi = b;
    lay_touch(d, a, b);
}

void doc_touch_all(DOC *d)
{
    d->lay_full = 1;
    d->ch_lo = 0;
    d->ch_hi = d->len;
    d->u.full = 1;              /* compare everything at the next step */
}

int doc_insert(DOC *d, long pos, const CELL *c, long n)
{
    if (n <= 0) return 1;
    if (pos < 0) pos = 0;
    if (pos > d->len) pos = d->len;
    if (!grow_text(d, d->len + n)) return 0;
    memmove(d->text + pos + n, d->text + pos, (d->len - pos) * sizeof(CELL));
    memcpy(d->text + pos, c, n * sizeof(CELL));
    d->len += n;
    if (d->ch_lo >= 0) {
        if (d->ch_hi > pos) d->ch_hi += n;
        if (d->ch_lo > pos) d->ch_lo += n;
    }
    if (d->lay_lo >= 0) {
        if (d->lay_hi > pos) d->lay_hi += n;
        if (d->lay_lo > pos) d->lay_lo += n;
    }
    d->lay_shift += n;
    doc_touch(d, pos, pos + n);
    d->dirty = 1;
    return 1;
}

void doc_delete(DOC *d, long pos, long n)
{
    if (pos < 0) { n += pos; pos = 0; }
    if (pos + n > d->len) n = d->len - pos;
    if (n <= 0) return;
    memmove(d->text + pos, d->text + pos + n, (d->len - pos - n) * sizeof(CELL));
    d->len -= n;
    if (d->ch_lo >= 0) {
        if (d->ch_hi > pos + n) d->ch_hi -= n; else if (d->ch_hi > pos) d->ch_hi = pos;
        if (d->ch_lo > pos + n) d->ch_lo -= n; else if (d->ch_lo > pos) d->ch_lo = pos;
    }
    if (d->lay_lo >= 0) {
        if (d->lay_hi > pos + n) d->lay_hi -= n; else if (d->lay_hi > pos) d->lay_hi = pos;
        if (d->lay_lo > pos + n) d->lay_lo -= n; else if (d->lay_lo > pos) d->lay_lo = pos;
    }
    d->lay_shift -= n;
    doc_touch(d, pos, pos);
    d->dirty = 1;
}

/* ---- paragraph formats ------------------------------------------------ */

/* drop formats no cell uses (keeps 0); renumbers the cells */
void doc_compact_paras(DOC *d)
{
    unsigned char map[256], used[256];
    PARA np[MAX_PARAS];
    long k;
    int i, n = 1;
    memset(used, 0, sizeof used);
    used[0] = 1;
    for (k = 0; k < d->len; k++) used[d->text[k].para] = 1;
    used[d->ins.para] = 1;
    np[0] = d->a.paras[0];
    map[0] = 0;
    for (i = 1; i < d->a.npara; i++)
        if (used[i]) { np[n] = d->a.paras[i]; map[i] = (unsigned char)n; n++; }
        else map[i] = 0;
    for (k = 0; k < d->len; k++) d->text[k].para = map[d->text[k].para < d->a.npara ? d->text[k].para : 0];
    d->ins.para = map[d->ins.para < d->a.npara ? d->ins.para : 0];
    memcpy(d->a.paras, np, n * sizeof(PARA));
    d->a.npara = (short)n;
    doc_touch_all(d);
}

/* the index of a paragraph format, adding it if it is new */
int para_find(DOC *d, const PARA *p)
{
    int i;
    for (i = 0; i < d->a.npara; i++)
        if (!memcmp(&d->a.paras[i], p, sizeof(PARA))) return i;
    if (d->a.npara >= MAX_PARAS) {
        doc_compact_paras(d);
        for (i = 0; i < d->a.npara; i++)
            if (!memcmp(&d->a.paras[i], p, sizeof(PARA))) return i;
        if (d->a.npara >= MAX_PARAS) return 0;
    }
    d->a.paras[d->a.npara] = *p;
    return d->a.npara++;
}

/* every cell takes the format of the cell that ends its paragraph */
void doc_norm_para(DOC *d)
{
    long k;
    unsigned char cur;
    if (d->len == 0) return;
    cur = d->text[d->len - 1].para;
    for (k = d->len - 1; k >= 0; k--) {
        if (ISBRK(d->text[k].ch)) cur = d->text[k].para;
        if (d->text[k].para != cur) { d->text[k].para = cur; doc_touch(d, k, k + 1); }
    }
}

long doc_para_start(DOC *d, long pos)
{
    if (pos > d->len) pos = d->len;
    while (pos > 0 && !ISBRK(d->text[pos - 1].ch)) pos--;
    return pos;
}

/* index of the cell that ends the paragraph (or len) */
long doc_para_end(DOC *d, long pos)
{
    if (pos < 0) pos = 0;                 /* an empty document: no text to look at */
    while (pos < d->len && !ISBRK(d->text[pos].ch)) pos++;
    return pos;
}

/* the format of the paragraph holding pos */
PARA *doc_para_at(DOC *d, long pos)
{
    long e;
    if (d->len == 0) return &d->insp;
    if (pos < 0) pos = 0;
    e = doc_para_end(d, pos);
    if (e >= d->len) e = d->len - 1;
    return &d->a.paras[d->text[e].para < d->a.npara ? d->text[e].para : 0];
}

static void set_field(PARA *p, int field, int val)
{
    switch (field) {
    case PF_JUST:    p->just = (unsigned char)val; break;
    case PF_SPACING: p->spacing = (unsigned char)val; break;
    case PF_LIST:    p->list = (unsigned char)val; break;
    case PF_TOC:     p->toc = (unsigned char)val; break;
    case PF_TOCENT:  p->tocent = (unsigned char)val; break;
    case PF_TBL:     p->tbl = (unsigned char)val; break;
    case PF_LEFT:    p->left = (short)val; break;
    case PF_FIRST:   p->first = (short)val; break;
    }
}

/* change one field of every paragraph that [a,b) touches */
int doc_para_set(DOC *d, long a, long b, int field, int val)
{
    long s, e, k;
    if (d->len == 0) {
        set_field(&d->insp, field, val);
        d->ins.para = (unsigned char)para_find(d, &d->insp);
        return 1;
    }
    if (b < a) { k = a; a = b; b = k; }
    s = doc_para_start(d, a);
    while (s < d->len || s == a) {
        PARA p;
        int idx;
        e = doc_para_end(d, s);
        if (e >= d->len) e = d->len - 1;
        if (e < s) break;
        p = d->a.paras[d->text[e].para < d->a.npara ? d->text[e].para : 0];
        set_field(&p, field, val);
        idx = para_find(d, &p);
        for (k = s; k <= e; k++) d->text[k].para = (unsigned char)idx;
        doc_touch(d, s, e + 1);
        if (e + 1 >= b || e + 1 >= d->len) {
            if (d->cursor >= s && d->cursor <= e + 1) d->ins.para = (unsigned char)idx;
            break;
        }
        s = e + 1;
    }
    d->dirty = 1;
    return 1;
}

/* ---- tables ----------------------------------------------------------- */

static void compact_tbls(DOC *d)
{
    unsigned char used[MAX_TBLS + 1], map[MAX_TBLS + 1];
    int i, n = 0;
    memset(used, 0, sizeof used);
    for (i = 0; i < d->a.npara; i++) if (d->a.paras[i].tbl) used[d->a.paras[i].tbl] = 1;
    map[0] = 0;
    for (i = 1; i <= d->a.ntbl; i++)
        if (used[i]) { d->a.tbls[n] = d->a.tbls[i - 1]; map[i] = (unsigned char)(n + 1); n++; }
        else map[i] = 0;
    for (i = 0; i < d->a.npara; i++) d->a.paras[i].tbl = map[d->a.paras[i].tbl <= d->a.ntbl ? d->a.paras[i].tbl : 0];
    d->a.ntbl = (short)n;
}

/* add a table definition; returns its number + 1 (the PARA.tbl value) */
int doc_tbl_new(DOC *d, int ncols, const short *w, int border)
{
    TBL *t;
    int i;
    if (ncols < 1) ncols = 1;
    if (ncols > MAX_COLS) ncols = MAX_COLS;
    if (d->a.ntbl >= MAX_TBLS) compact_tbls(d);
    if (d->a.ntbl >= MAX_TBLS) return 0;
    t = &d->a.tbls[d->a.ntbl];
    memset(t, 0, sizeof *t);
    t->ncols = (short)ncols;
    t->border = (short)(border != 0);
    for (i = 0; i < ncols; i++) t->w[i] = (short)(w && w[i] > 8 ? w[i] : 72);
    return ++d->a.ntbl;
}

/* ---- pictures and drawings ---------------------------------------------- */

/* a freehand stroke's n points (x, y pairs, 0..FREE_ONE across its box)
 * into the document's pool; returns where they start, or -1 */
int free_add(DOC *d, const short *xy, int n)
{
    long at = d->nfpts;
    if (n < 2 || n > MAX_FREEPTS) return -1;
    if (d->nfpts + n > d->capfpts) {
        long nc = d->capfpts ? d->capfpts * 2 : 1024;
        short *np;
        while (nc < d->nfpts + n) nc *= 2;
        np = (short *)realloc(d->fpts, (size_t)nc * 2 * sizeof(short));
        if (!np) return -1;
        d->fpts = np;
        d->capfpts = nc;
    }
    memcpy(d->fpts + 2 * at, xy, (size_t)n * 2 * sizeof(short));
    d->nfpts += n;
    return (int)at;
}

const short *free_pts(const DOC *d, const SHAPE *s)
{
    if (s->type != SH_FREE || s->np < 2 || s->p0 < 0 || s->p0 + s->np > d->nfpts) return NULL;
    return d->fpts + 2 * s->p0;
}

int doc_obj_add(DOC *d, const OBJ *o)
{
    if (d->a.nobj >= MAX_OBJS) return -1;
    d->a.objs[d->a.nobj] = *o;
    d->dirty = 1;
    return d->a.nobj++;
}

void doc_obj_delete(DOC *d, int i)
{
    if (i < 0 || i >= d->a.nobj) return;
    memmove(&d->a.objs[i], &d->a.objs[i + 1], (d->a.nobj - i - 1) * sizeof(OBJ));
    d->a.nobj--;
    d->dirty = 1;
}

/* pictures are kept until the document closes (undo may bring a
 * deleted one back), so the list only grows */
int doc_img_add(DOC *d, const IMG *im)
{
    if (d->nimg >= MAX_IMGS) return -1;
    d->imgs[d->nimg] = *im;
    return d->nimg++;
}
