/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * pagedraw.c - one page of a document into a 1-bit band at k times screen
 *              resolution, for the PDF export
 *
 * The platform layer supplies the character bitmaps; everything else is
 * drawn here: pictures and drawings, table grids, the text with its
 * underlines, list bullets and numbers, dot leaders, header and footer.
 * Only things of one colour are drawn at a time (color < 0: everything).
 */
#include <stdio.h>
#include <string.h>
#include "wp.h"

static void glyph_at(BAND *b, const GLYPHBM *g, long px, long base)
{
    long r, i, grb, dx0, db, lim;
    int sh;
    if (!g || !g->bits) return;
    grb = (g->w + 7) / 8;
    dx0 = px + g->ox;
    db = dx0 >> 3;                                  /* a byte at a time, shifted into place */
    sh = (int)(dx0 & 7);
    lim = (b->w + 7) >> 3;
    for (r = 0; r < g->h; r++) {
        long y = base + g->oy + r;
        const unsigned char *src = g->bits + r * grb;
        unsigned char *dst;
        if (y < b->y0 || y >= b->y0 + b->h) continue;
        dst = b->bits + (y - b->y0) * b->rb;
        for (i = 0; i < grb; i++) {
            unsigned v = src[i], t = 0;
            long k = db + i;
            if (!v) continue;
            if (i == grb - 1 && (g->w & 7)) v &= 0xFF00u >> (g->w & 7);
            if (k >= 0 && k < lim) dst[k] |= (unsigned char)(v >> sh);
            if (sh && k + 1 >= 0 && k + 1 < lim) { t = (v << (8 - sh)) & 0xFF; dst[k + 1] |= (unsigned char)t; }
        }
        /* nothing past the band's right edge */
        if ((b->w & 7) && lim > 0) dst[lim - 1] &= (unsigned char)(0xFF00u >> (b->w & 7));
    }
}

/* a string in one style; returns the advance */
static long text_at(BAND *b, GLYPHFN glyph, void *ctx, const CELL *st, const char *s, long x, long base, int sc)
{
    const METRICS *m = get_metrics(st->font, st->size, st->attr);
    long adv = 0;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch > ' ') glyph_at(b, glyph(ctx, st->font, st->size, st->attr, ch, sc), x + adv * sc, base);
        adv += m->w[ch];
    }
    return adv;
}

static void dots(BAND *b, long x0, long x1, long base, int sc)
{
    long step = 6 * sc, x = (x0 / step + 1) * step, t = sc > 1 ? sc : 1;
    for (; x + t <= x1 - 2 * sc; x += step) rs_rect(b, x, base - t, x + t, base, FL_SOLID);
}

static void draw_line(DOC *d, long li, int color, BAND *b, int sc, GLYPHFN glyph, void *ctx)
{
    LINE *ln = &d->lines[li];
    LWALK w;
    long base = (long)(ln->py + ln->asc) * sc, lx = (long)ln->x * sc;

    /* list marker */
    if (ln->mark && ln->len) {
        CELL st = d->text[ln->start];
        if (color < 0 || st.color == color) {
            const METRICS *m = get_metrics(st.font, st.size, st.attr);
            if (ln->mark < 0) {
                long r = m->asc / 6 + 1, cx = ((long)ln->markx + r + 1) * sc, cy = base - (m->asc * sc) / 3;
                rs_begin();
                rs_ellipse_path(cx * 16, cy * 16, r * sc * 16, r * sc * 16);
                rs_fill(b, FL_SOLID);
            } else {
                char num[12];
                st.attr &= ~A_UNDER;
                doc_marker(d, li, num);
                text_at(b, glyph, ctx, &st, num, doc_marker_x(d, li) * sc, base, sc);
            }
        }
    }
    /* characters */
    lw_init(&w, d, li);
    while (w.k < w.end) {
        CELL *c = &d->text[w.k];
        if (ISEND(c->ch)) break;
        if (color < 0 || c->color == color) {
            if (c->ch > ' ')
                glyph_at(b, glyph(ctx, c->font, c->size, c->attr, c->ch, sc), lx + w.x * sc, base);
            else if (c->ch == '\t' && w.k - ln->start == ln->tabk)
                dots(b, lx + w.x * sc, lx + (w.x + ln->tabw) * sc, base, sc);
        }
        lw_step(&w);
    }
    /* underlines, unbroken under spaces */
    lw_init(&w, d, li);
    while (w.k < w.end) {
        CELL *c = &d->text[w.k];
        long ux, uy, t;
        int col;
        if (ISEND(c->ch)) break;
        if (!(c->attr & A_UNDER) || (color >= 0 && c->color != color)) { lw_step(&w); continue; }
        ux = w.x;
        col = c->color;
        uy = get_metrics(c->font, c->size, c->attr)->desc / 2 + 1;
        t = pt2px_y(c->size) / 16 + 1;
        while (w.k < w.end && !ISEND(d->text[w.k].ch) && (d->text[w.k].attr & A_UNDER) && d->text[w.k].color == col)
            lw_step(&w);
        rs_rect(b, lx + ux * sc, base + uy * sc, lx + w.x * sc, base + (uy + t) * sc, FL_SOLID);
    }
}

static void draw_rows(DOC *d, int page, BAND *b, int sc)
{
    long r;
    for (r = 0; r < d->nrows; r++) {
        TROW *t = &d->rows[r];
        const TBL *T;
        long x, y0, y1, k;
        int i;
        if (t->page != page || t->tbl < 0 || t->tbl >= d->a.ntbl) continue;
        T = &d->a.tbls[t->tbl];
        if (!T->border) continue;
        y0 = (long)t->y * sc; y1 = (long)(t->y + t->h) * sc;
        x = (long)t->x * sc;
        k = x;
        for (i = 0; i < T->ncols; i++) k += pt2px_x(T->w[i]) * sc;
        rs_rect(b, x, y0, k + sc, y0 + sc, FL_SOLID);           /* top    */
        rs_rect(b, x, y1, k + sc, y1 + sc, FL_SOLID);           /* bottom */
        rs_rect(b, x, y0, x + sc, y1 + sc, FL_SOLID);
        for (i = 0; i < T->ncols; i++) {
            x += pt2px_x(T->w[i]) * sc;
            rs_rect(b, x, y0, x + sc, y1 + sc, FL_SOLID);
        }
    }
}

static void draw_hf(DOC *d, int page, BAND *b, int sc, GLYPHFN glyph, void *ctx, int datefmt)
{
    char buf[120];
    int i, k;
    CELL st;
    const METRICS *m;
    PGFMT f;
    long tw, mx, mt, mb, ph, lh, w, x, base;
    memset(&st, 0, sizeof st);
    st.font = (unsigned char)default_font();
    st.size = HF(d).size;
    m = get_metrics(st.font, st.size, 0);
    page_fmt(d, page, &f);
    tw = text_w_px(d, page); mx = pt2px_x(f.ml); mt = pt2px_y(f.mt); mb = pt2px_y(f.mb);
    ph = page_h_px(d); lh = m->asc + m->desc;
    for (i = 0; i < 2; i++) {
        if (!hf_visible(d, i, page)) continue;
        hf_expand(d, i, page, doc_npages(d), datefmt, buf, sizeof buf);
        for (w = 0, k = 0; buf[k]; k++) w += m->w[(unsigned char)buf[k]];
        x = mx + (HF(d).just[i] == J_CENTRE ? (tw - w) / 2 : HF(d).just[i] == J_RIGHT ? tw - w : 0);
        if (i == 0) base = mt > lh ? (mt - lh) / 2 + m->asc : m->asc;
        else base = ph - (mb > lh ? (mb + lh) / 2 - m->asc : lh) + m->asc;
        text_at(b, glyph, ctx, &st, buf, x * sc, base * sc, sc);
    }
}

/* the tables' charts on this page */
static void draw_charts(DOC *d, int page, int color, BAND *b, int sc, GLYPHFN glyph, void *ctx)
{
    static CHOP op[CH_MAXOPS];
    int t, n, i, k;
    for (t = 0; t < d->a.ntbl; t++) {
        const CHBOX *cb = &d->cbox[t];
        long ox, oy;
        if (!d->a.tbls[t].chart || cb->page != page || cb->w <= 0) continue;
        ox = (long)cb->x * sc; oy = (long)cb->y * sc;
        if (oy > b->y0 + b->h || oy + (long)cb->h * sc < b->y0) continue;
        n = chart_ops(d, t, cb->w, cb->h, op, CH_MAXOPS);
        for (i = 0; i < n; i++) {
            const CHOP *o = &op[i];
            long xy[2 * CH_MAXPTS];
            for (k = 0; k < 2 * o->n; k++) xy[k] = ((k & 1 ? oy : ox) + (long)o->xy[k] * sc) * 16 + 8;
            if (o->op == CO_POLY) {
                int fill = -1;
                if (color < 0) fill = chart_pattern(o->series);
                else if (chart_color(o->series) == color) fill = FL_SOLID;
                if (fill >= 0) { rs_begin(); rs_poly(xy, o->n); rs_fill(b, fill); }
                if (color <= 0) {
                    if (o->shade == 2 && color == 0) { rs_begin(); rs_poly(xy, o->n); rs_fill(b, FP_DOTS); }
                    rs_begin();
                    for (k = 0; k < o->n; k++) {
                        int j = (k + 1) % o->n;
                        rs_thick(xy[2 * k], xy[2 * k + 1], xy[2 * j], xy[2 * j + 1], 16L * (sc > 1 ? sc / 2 + 1 : 1));
                    }
                    rs_fill(b, FL_SOLID);
                }
            } else if (o->op == CO_LINE && color <= 0) {
                rs_begin();
                rs_thick(xy[0], xy[1], xy[2], xy[3], 16L * (sc > 1 ? sc / 2 + 1 : 1));
                rs_fill(b, FL_SOLID);
            } else if (o->op == CO_TEXT && color <= 0) {
                CELL st;
                const METRICS *m;
                long w = 0, x;
                memset(&st, 0, sizeof st);
                st.font = (unsigned char)default_font();
                st.size = CHART_PT;
                m = get_metrics(st.font, st.size, 0);
                for (k = 0; o->text[k]; k++) w += m->w[(unsigned char)o->text[k]];
                x = o->xy[0] - (o->align == 1 ? w / 2 : o->align == 2 ? w : 0);
                text_at(b, glyph, ctx, &st, o->text, ox + x * sc, oy + (long)o->xy[1] * sc, sc);
            }
        }
    }
}

int page_render(DOC *d, int page, int color, BAND *b, int sc, GLYPHFN glyph, void *ctx, int datefmt)
{
    long li, a, e;
    int i;
    /* pictures and drawings first: text that isn't wrapped around them
     * prints on top */
    for (i = 0; i < d->a.nobj; i++)
        if (d->a.objs[i].page == page) rs_obj(b, d, &d->a.objs[i], color, sc, 1);
    if (color <= 0) draw_rows(d, page, b, sc);
    draw_charts(d, page, color, b, sc, glyph, ctx);
    if (page < d->npages) {
        a = d->pgline[page];
        e = d->pgline[page + 1];
        for (li = a; li < e; li++) {
            LINE *ln = &d->lines[li];
            long top = (long)ln->py * sc, bot = (long)(ln->py + ln->asc + ln->desc + 8) * sc;
            if (bot < b->y0 || top - 8 * sc >= b->y0 + b->h) continue;
            draw_line(d, li, color, b, sc, glyph, ctx);
        }
    }
    if (color <= 0) draw_hf(d, page, b, sc, glyph, ctx, datefmt);
    return 1;
}
