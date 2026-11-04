/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * winrend.c - Windows fonts (TrueType through GDI), text metrics, drawing
 *             a page, the PDF export's character bitmaps and printing
 *             (after amiga/amirend.c)
 *
 * A point size becomes a font height in pixels through the screen's
 * resolution, so text is the size it will print at 100%. Every
 * character is drawn where the layout puts it (ExtTextOut with the
 * layout's advances), so lines break and justify the same in the view,
 * the print preview, the PDF and on paper.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winui.h"

/* pens for the text colours: black red green blue cyan magenta yellow grey */
int color_pen(int c)
{
    static const unsigned char p16[NCOLORS] = { P_BLACK, P_RED, P_GREEN, P_DBLUE, P_CYAN, P_MAGENTA, P_YELLOW, P_TGREY };
    if (!colour_on || c <= 0 || c >= NCOLORS) return P_BLACK;
    return p16[c];
}

/* ================================================================= */
/*  The fonts: the TrueType families Windows has                      */
/* ================================================================= */

static char famnames[MAX_FONTS * 2][LF_FACESIZE];
static int  nfam;

static int CALLBACK fam_cb(const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD type, LPARAM lp)
{
    int k;
    (void)tm; (void)lp;
    if (!(type & TRUETYPE_FONTTYPE) || lf->lfFaceName[0] == '@' || lf->lfCharSet == SYMBOL_CHARSET) return 1;
    for (k = 0; k < nfam; k++) if (!lstrcmpiA(famnames[k], lf->lfFaceName)) return 1;
    if (nfam < MAX_FONTS * 2) lstrcpynA(famnames[nfam++], lf->lfFaceName, LF_FACESIZE);
    return 1;
}

static int cmp_names(const void *a, const void *b) { return lstrcmpiA((const char *)a, (const char *)b); }

static int add_font(const char *name)
{
    int i;
    if (nfonts >= MAX_FONTS) return -1;
    for (i = 0; i < nfonts; i++) if (!lstrcmpiA(fonts[i].name, name)) return i;
    lstrcpynA(fonts[nfonts].name, name, sizeof fonts[0].name);
    fonts[nfonts].id = (short)nfonts;
    fonts[nfonts].outline = 1;
    return nfonts++;
}

static int have(const char *name)
{
    int k;
    for (k = 0; k < nfam; k++) if (!lstrcmpiA(famnames[k], name)) return 1;
    return 0;
}

void fonts_load(void)
{
    HDC dc = GetDC(NULL);
    int k;
    nfam = 0;
    EnumFontFamiliesA(dc, NULL, (FONTENUMPROCA)fam_cb, 0);
    ReleaseDC(NULL, dc);
    qsort(famnames, (size_t)nfam, sizeof famnames[0], cmp_names);
    /* font 0 plays the part of the Atari's system font: monospaced; then
     * the usual text fonts, then the rest */
    nfonts = 0;
    {   /* every Windows has these three; elsewhere (Wine), their likes */
        static const char *const mono[] = { "Courier New", "Liberation Mono", "DejaVu Sans Mono", NULL };
        static const char *const sans[] = { "Arial", "Liberation Sans", "DejaVu Sans", NULL };
        static const char *const serif[] = { "Times New Roman", "Liberation Serif", "DejaVu Serif", NULL };
        const char *const *set[3];
        int j;
        set[0] = mono; set[1] = sans; set[2] = serif;
        for (j = 0; j < 3; j++) {
            for (k = 0; set[j][k] && !have(set[j][k]); k++) ;
            if (set[j][k]) add_font(set[j][k]);
            else if (j < 2 && nfam > j) add_font(famnames[j]);
        }
    }
    for (k = 0; k < nfam; k++) add_font(famnames[k]);
}

/* ================================================================= */
/*  Fonts at a pixel height                                           */
/* ================================================================= */

#define NFCACHE 24
static struct { short font, ysize, attr; long stamp; HFONT hf; } fc[NFCACHE];
static long fstamp;

HFONT font_for(int font, int ysize, int attr)
{
    int i, slot = 0;
    attr &= A_BOLD | A_ITALIC;
    if (font < 0 || font >= nfonts) font = 0;
    if (ysize < 3) ysize = 3;
    if (ysize > 2000) ysize = 2000;
    for (i = 0; i < NFCACHE; i++)
        if (fc[i].hf && fc[i].font == font && fc[i].ysize == ysize && fc[i].attr == attr) { fc[i].stamp = ++fstamp; return fc[i].hf; }
    for (i = 0; i < NFCACHE; i++) {
        if (!fc[i].hf) { slot = i; break; }
        if (fc[i].stamp < fc[slot].stamp) slot = i;
    }
    if (fc[slot].hf) DeleteObject(fc[slot].hf);
    fc[slot].hf = CreateFontA(-ysize, 0, 0, 0, (attr & A_BOLD) ? FW_BOLD : FW_NORMAL, (attr & A_ITALIC) != 0, 0, 0,
                              ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, nfonts ? fonts[font].name : "Arial");
    fc[slot].font = (short)font;
    fc[slot].ysize = (short)ysize;
    fc[slot].attr = (short)attr;
    fc[slot].stamp = ++fstamp;
    return fc[slot].hf;
}

void fonts_unload(void)
{
    int i;
    for (i = 0; i < NFCACHE; i++) if (fc[i].hf) { DeleteObject(fc[i].hf); fc[i].hf = NULL; }
}

static int ypx(long size) { long v = pt2px_y(size); return (int)(v < 3 ? 3 : v); }

/* ---- metrics ----------------------------------------------------------- */

#define NCACHE 24
static struct { short font, size, attr; long stamp; METRICS m; } mc[NCACHE];
static long mstamp;

const METRICS *get_metrics(int font, int size, int attr)
{
    int i, slot = 0;
    HDC dc;
    HFONT of;
    INT w[256];
    TEXTMETRICA tm;
    attr &= A_BOLD | A_ITALIC;
    if (font >= nfonts) font = 0;
    for (i = 0; i < NCACHE; i++)
        if (mc[i].size == size && mc[i].font == font && mc[i].attr == attr) {
            mc[i].stamp = ++mstamp;
            return &mc[i].m;
        }
    for (i = 0; i < NCACHE; i++) {
        if (mc[i].size == 0) { slot = i; break; }
        if (mc[i].stamp < mc[slot].stamp) slot = i;
    }
    dc = GetDC(NULL);
    of = (HFONT)SelectObject(dc, font_for(font, ypx(size), attr));
    memset(w, 0, sizeof w);
    GetCharWidthA(dc, 0, 255, w);
    GetTextMetricsA(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    for (i = 0; i < 256; i++) mc[slot].m.w[i] = (short)(i < 32 ? 0 : w[lat_ch((unsigned char)i)]);
    mc[slot].m.asc = (short)tm.tmAscent;
    mc[slot].m.desc = (short)tm.tmDescent;
    mc[slot].font = (short)font; mc[slot].size = (short)size; mc[slot].attr = (short)attr;
    mc[slot].stamp = ++mstamp;
    return &mc[slot].m;
}

/* ================================================================= */
/*  Drawing a page                                                    */
/* ================================================================= */

typedef struct { long pn, pd, sn, sd; } SCALE;
#define SP(v, sc) ((v) * (sc)->pn / (sc)->pd)

/* n characters (Windows-1252) with their left edges at xs[0..n-1] and
 * the run's end at xs[n] */
static void run_at(const long *xs, const char *s, int n, long base, int font, int size_px, int attr, int pen)
{
    INT dx[132];
    HFONT of;
    int i;
    if (!gdc || n <= 0) return;
    if (n > 131) n = 131;
    for (i = 0; i < n; i++) dx[i] = (INT)(xs[i + 1] - xs[i]);
    of = (HFONT)SelectObject(gdc, font_for(font, size_px, attr));
    SetTextColor(gdc, PEN(pen));
    SetBkMode(gdc, TRANSPARENT);
    SetTextAlign(gdc, TA_BASELINE | TA_LEFT);
    ExtTextOutA(gdc, (int)xs[0], (int)base, 0, NULL, s, (UINT)n, dx);
    SelectObject(gdc, of);
}

/* text in the ST character set at a pixel height */
void text_at(long x, long base, const char *st, int n, int font, int size_px, int attr, int pen)
{
    char buf[200];
    HFONT of;
    if (!gdc || n <= 0) return;
    if (n > 199) n = 199;
    lat_n(buf, st, n);
    of = (HFONT)SelectObject(gdc, font_for(font, size_px, attr));
    SetTextColor(gdc, PEN(pen));
    SetBkMode(gdc, TRANSPARENT);
    SetTextAlign(gdc, TA_BASELINE | TA_LEFT);
    TextOutA(gdc, (int)x, (int)base, buf, n);
    SelectObject(gdc, of);
}


static void greek(long x1, long x2, long base, int pen, long xh)
{
    if (x2 <= x1) x2 = x1 + 1;
    fill_rect(x1, base - xh, x2 - 1, base - 1, pen);
}

/* the spelling of [a,b) is not in the dictionaries */
int word_misspelt(DOC *d, long a, long b)
{
    char w[WORDMAX];
    long i;
    if (b - a < 2 || b - a >= WORDMAX) return 0;
    if (!spell_main_loaded() && !spell_nuser) return 0;
    for (i = a; i < b; i++) w[i - a] = (char)d->text[i].ch;
    w[b - a] = 0;
    return !spell_check(w);
}

static int wch(unsigned char c) { return st_isalpha(c) || (c >= '0' && c <= '9'); }

/* a wavy line under misspelt words of line li */
static void live_marks(DOC *d, long li, long ox, long base, const SCALE *sc)
{
    LWALK w;
    long ws = -1, wx = 0;
    int pen = npens >= 8 ? P_RED : P_BLACK;
    lw_init(&w, d, li);
    for (;;) {
        int at_end = w.k >= w.end || ISEND(d->text[w.k].ch);
        unsigned char c = at_end ? ' ' : d->text[w.k].ch;
        int in = wch(c) || (c == '\'' && ws >= 0 && w.k + 1 < w.end && wch(d->text[w.k + 1].ch));
        if (in && ws < 0) { ws = w.k; wx = w.x; }
        if (!in && ws >= 0) {
            if (!(d->cursor == w.k && d->anchor < 0) && word_misspelt(d, ws, w.k)) {
                long x1 = ox + SP(wx, sc), x2 = ox + SP(w.x, sc), y = base + SP(2, sc) + 1, x;
                int up = 0;
                long px0 = x1, py0 = y + 1;
                for (x = x1 + 2; x <= x2; x += 2) {
                    long ny = y + (up ? 1 : 0);
                    line(px0, py0, x, ny, pen);
                    px0 = x; py0 = ny;
                    up = !up;
                }
            }
            ws = -1;
        }
        if (at_end) break;
        lw_step(&w);
    }
}

/* Draw line li: ox is the window x of the line's own left edge, base its
 * baseline. pen >= 0 forces a pen. Words are drawn one at a time at their
 * layout positions; zoomed, each character goes where the layout says.
 * Underlines are drawn as lines so they run unbroken under spaces. */
static void draw_line_ex(DOC *d, long li, long ox, long base, const SCALE *sc, int pen)
{
    LWALK w;
    LINE *ln = &d->lines[li];
    char buf[132];
    long xs[133];

    /* list marker */
    if (ln->mark && ln->len) {
        CELL st = d->text[ln->start];
        int p = pen >= 0 ? pen : color_pen(st.color);
        long mx = ox + SP(ln->markx - ln->x, sc);
        if (ln->mark < 0) {
            const METRICS *m = get_metrics(st.font, st.size, st.attr);
            long r = SP(m->asc / 6 + 1, sc);
            if (r < 1) r = 1;
            disc(mx + r + 1, base - SP(m->asc, sc) / 3, r, p);
        } else {
            long sz = st.size * sc->sn / sc->sd;
            mx = ox + SP(doc_marker_x(d, li) - ln->x, sc);
            doc_marker(d, li, buf);
            if (sz >= 4) text_at(mx, base, buf, (int)strlen(buf), st.font, ypx(st.size) * sc->sn / sc->sd, st.attr, p);
        }
    }
    lw_init(&w, d, li);
    while (w.k < w.end) {
        CELL *c = &d->text[w.k], st;
        long sx, x;
        int n = 0;
        if (ISEND(c->ch)) break;
        if (c->ch == '\t' && w.k - ln->start == ln->tabk) {          /* dot leader */
            long x1 = ox + SP(w.x, sc), x2 = ox + SP(w.x + ln->tabw, sc) - SP(4, sc);
            int p = pen >= 0 ? pen : color_pen(c->color);
            long step = SP(6, sc) > 2 ? SP(6, sc) : 3, xx;
            for (xx = ((x1 - ox) / step + 1) * step + ox; xx < x2; xx += step) fill_rect(xx, base - 1, xx, base - 1, p);
        }
        if (c->ch == ' ' || c->ch == '\t') { lw_step(&w); continue; }
        st = *c;
        sx = w.x;
        while (w.k < w.end && n < 128) {
            c = &d->text[w.k];
            if (ISEND(c->ch) || c->ch == ' ' || c->ch == '\t' || c->font != st.font ||
                c->size != st.size || ((c->attr ^ st.attr) & (A_BOLD | A_ITALIC)) || c->color != st.color)
                break;
            xs[n] = w.x;
            buf[n++] = (char)lat_ch(c->ch);
            lw_step(&w);
        }
        buf[n] = 0;
        x = ox + SP(sx, sc);
        if (x > -2000 && x < 16000) {
            int p = pen >= 0 ? pen : color_pen(st.color);
            long sz = st.size * sc->sn / sc->sd, py = (long)ypx(st.size) * sc->sn / sc->sd;
            if (sz < 4 || py < 4) {
                long xh = py / 3;
                greek(x, ox + SP(w.x, sc), base, p, xh < 1 ? 1 : xh);
            } else {
                /* each character where the layout puts it */
                long i;
                for (i = 0; i < n; i++) xs[i] = ox + SP(xs[i], sc);
                xs[n] = ox + SP(w.x, sc);
                run_at(xs, buf, n, base, st.font, (int)py, st.attr, p);
            }
        }
    }

    lw_init(&w, d, li);
    while (w.k < w.end) {
        CELL *c = &d->text[w.k];
        long ux, t, i, uy;
        int col;
        if (ISEND(c->ch)) break;
        if (!(c->attr & A_UNDER)) { lw_step(&w); continue; }
        ux = w.x;
        col = c->color;
        uy = get_metrics(c->font, c->size, c->attr)->desc / 2 + 1;
        t = pt2px_y(c->size) / 16 + 1;
        while (w.k < w.end && !ISEND(d->text[w.k].ch) && (d->text[w.k].attr & A_UNDER) &&
               d->text[w.k].color == col)
            lw_step(&w);
        t = SP(t, sc);
        if (t < 1) t = 1;
        for (i = 0; i < t; i++)
            line(ox + SP(ux, sc), base + SP(uy, sc) + i, ox + SP(w.x, sc) - 1, base + SP(uy, sc) + i,
                 pen >= 0 ? pen : color_pen(col));
    }
}

/* the header and footer of a page; (ox,oy) is the page's top left corner */
static void draw_hf(DOC *d, int page, int pen, const SCALE *sc, long ox, long oy)
{
    char buf[120];
    int i, k, f = default_font();
    const METRICS *m = get_metrics(f, HF(d).size, 0);
    PGFMT g;
    long tw, mx, mt, mb, ph = page_h_px(d), lh = m->asc + m->desc;
    long w, x, base, sz = HF(d).size * sc->sn / sc->sd, py = (long)ypx(HF(d).size) * sc->sn / sc->sd;

    page_fmt(d, page, &g);
    tw = text_w_px(d, page); mx = pt2px_x(g.ml); mt = pt2px_y(g.mt); mb = pt2px_y(g.mb);
    for (i = 0; i < 2; i++) {
        if (!hf_visible(d, i, page)) continue;
        hf_expand(d, i, page, doc_npages(d), pref_date, buf, sizeof buf);
        for (w = 0, k = 0; buf[k]; k++) w += m->w[(unsigned char)buf[k]];
        x = mx + (HF(d).just[i] == J_CENTRE ? (tw - w) / 2 : HF(d).just[i] == J_RIGHT ? tw - w : 0);
        if (i == 0) base = mt > lh ? (mt - lh) / 2 + m->asc : m->asc;
        else base = ph - (mb > lh ? (mb + lh) / 2 - m->asc : lh) + m->asc;
        if (sz < 4 || py < 4) greek(ox + SP(x, sc), ox + SP(x + w, sc), oy + SP(base, sc), pen, py / 3 + 1);
        else if (sc->pn == sc->pd) text_at(ox + x, oy + base, buf, (int)strlen(buf), f, (int)py, 0, pen);
        else {
            /* each character where it goes */
            long cx = x;
            for (k = 0; buf[k]; k++) {
                text_at(ox + SP(cx, sc), oy + SP(base, sc), &buf[k], 1, f, (int)py, 0, pen);
                cx += m->w[(unsigned char)buf[k]];
            }
        }
    }
}

/* the grid of a table row */
static void draw_row(DOC *d, TROW *t, const SCALE *sc, long ox, long oy, int pen, int dotted)
{
    const TBL *T = &d->a.tbls[t->tbl];
    long x = ox + SP(t->x, sc), y0 = oy + SP(t->y, sc), y1 = oy + SP(t->y + t->h, sc), k = 0;
    int i;
    for (i = 0; i < T->ncols; i++) k += pt2px_x(T->w[i]);
    hline(x, x + SP(k, sc), y0, pen, dotted);
    hline(x, x + SP(k, sc), y1, pen, dotted);
    if (dotted) vline_dotted(x, y0, y1, pen); else line(x, y0, x, y1, pen);
    for (i = 0, k = 0; i < T->ncols; i++) {
        k += pt2px_x(T->w[i]);
        if (dotted) vline_dotted(x + SP(k, sc), y0, y1, pen);
        else line(x + SP(k, sc), y0, x + SP(k, sc), y1, pen);
    }
}

/* a table's chart: colours on a colour screen, else black and grey */
static void draw_chart(DOC *d, int t, const SCALE *sc, long ox, long oy, int fg, int dark)
{
    static CHOP op[CH_MAXOPS];
    const CHBOX *cb = &d->cbox[t];
    const METRICS *m = get_metrics(default_font(), CHART_PT, 0);
    long bx = ox + SP(cb->x, sc), by = oy + SP(cb->y, sc), py = ypx(CHART_PT) * sc->sn / sc->sd;
    int n = chart_ops(d, t, cb->w, cb->h, op, CH_MAXOPS), i, k, colour = colour_on && npens >= 8 && !dark;
    long xy[2 * (CH_MAXPTS + 1)];
    for (i = 0; i < n; i++) {
        const CHOP *o = &op[i];
        for (k = 0; k < o->n; k++) { xy[2 * k] = bx + SP(o->xy[2 * k], sc); xy[2 * k + 1] = by + SP(o->xy[2 * k + 1], sc); }
        if (o->op == CO_POLY && o->n >= 3) {
            if (colour) poly_fill(xy, o->n, color_pen(chart_color(o->series)), 0);
            else if (o->series % 3 == 0) poly_fill(xy, o->n, fg, 0);
            else if (o->series % 3 == 1) poly_fill(xy, o->n, fg, 1);
            if (o->shade == 2) poly_fill(xy, o->n, fg, 1);
            xy[2 * o->n] = xy[0]; xy[2 * o->n + 1] = xy[1];
            poly_line(xy, o->n + 1, fg, 1);
        } else if (o->op == CO_LINE) poly_line(xy, 2, fg, 1);
        else if (o->op == CO_TEXT && py >= 4) {
            long w = 0;
            for (k = 0; o->text[k]; k++) w += m->w[(unsigned char)o->text[k]];
            if (o->align == 1) w /= 2; else if (o->align == 0) w = 0;
            text_at(bx + SP(o->xy[0] - w, sc), by + SP(o->xy[1], sc), o->text, (int)strlen(o->text), default_font(), (int)py, 0, fg);
        }
    }
}

/* Draw a page with its top left corner at window (ox, oy), at n/den of
 * its size. pen >= 0 forces a pen (the dark display modes); live: the
 * editing view (spelling marks, heading marks, dotted table grids). */
void draw_page(DOC *d, int page, long ox, long oy, long n, long den, const GRECT *clip, int pen, int live)
{
    SCALE sc;
    long li, a, e, r;
    int i, dark = pen >= 0;
    int fg = dark ? pen : P_BLACK;
    sc.pn = sc.sn = n; sc.pd = sc.sd = den;
    if (page < 0 || page >= d->npages || !gdc) return;
    for (i = 0; i < d->a.nobj; i++)
        if (d->a.objs[i].page == page) draw_obj(d, &d->a.objs[i], ox, oy, n, den);
    for (r = 0; r < d->nrows; r++) {
        TROW *t = &d->rows[r];
        if (t->page != page || t->tbl < 0 || t->tbl >= d->a.ntbl) continue;
        if (d->a.tbls[t->tbl].border) draw_row(d, t, &sc, ox, oy, fg, 0);
        else if (live) draw_row(d, t, &sc, ox, oy, fg, 1);   /* seen while editing, never printed */
    }
    for (i = 0; i < d->a.ntbl; i++)
        if (d->a.tbls[i].chart && d->cbox[i].page == page && d->cbox[i].w > 0) draw_chart(d, i, &sc, ox, oy, fg, dark);
    edlog(" page %d: objects and rows drawn", page);
    a = d->pgline[page];
    e = d->pgline[page + 1];
    for (li = a; li < e; li++) {
        LINE *ln = &d->lines[li];
        long y = oy + SP(ln->py, &sc), lh = SP(ln->asc + ln->desc, &sc);
        if (clip && (y > clip->g_y + clip->g_h || y + lh + 4 < clip->g_y)) continue;
        draw_line_ex(d, li, ox + SP(ln->x, &sc), oy + SP(ln->py + ln->asc, &sc), &sc, pen);
        if (live && pref_live) live_marks(d, li, ox + SP(ln->x, &sc), oy + SP(ln->py + ln->asc, &sc), &sc);
        if (live && (ln->flags & LF_FIRST) && ln->len) {
            const PARA *p = PARA_OF(d, &d->text[ln->start]);
            if (p->toc) {                      /* a heading the contents will list */
                char t[4];
                t[0] = 'H'; t[1] = (char)('0' + p->toc); t[2] = 0;
                fix_text(ox + 2, oy + SP(ln->py, &sc), t, 2, npens >= 8 ? P_GREEN : fg);
            }
        }
    }
    edlog(" page %d: lines drawn", page);
    draw_hf(d, page, fg, &sc, ox, oy);
    edlog(" page %d: header and footer drawn", page);
}

void draw_preview_page(DOC *d, int page, long ox, long oy, long n, long den)
{
    draw_page(d, page, ox, oy, n, den, NULL, -1, 0);
}


/* ================================================================= */
/*  PDF and printing: character bitmaps from the fonts               */
/* ================================================================= */

typedef struct {
    short font, size, attr, scale, ch;
    short w, h, ox, oy;
    unsigned char *bits;
} GLYPH;

#define NGLYPH 512
static GLYPH gcache[NGLYPH];
static int   ngc;

static void glyph_cache_free(void)
{
    int i;
    for (i = 0; i < ngc; i++) free(gcache[i].bits);
    ngc = 0;
}

/* character ch (ST) drawn by GDI into a 1-bit bitmap, then cut to the ink */
static int gdi_glyph(GLYPH *g, int font, int ysize, int attr, unsigned char ch)
{
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bm;
    void *pix;
    struct { BITMAPINFOHEADER h; RGBQUAD c[2]; } bi;
    HFONT of;
    TEXTMETRICA tm;
    INT adv = 0;
    long cw2, ch2, rb, r, x, x0, x1, y0, y1;
    unsigned char *src, c = lat_ch(ch);
    char s[1];
    if (!dc) return 0;
    of = (HFONT)SelectObject(dc, font_for(font, ysize, attr));
    GetTextMetricsA(dc, &tm);
    GetCharWidthA(dc, c, c, &adv);
    cw2 = adv + tm.tmHeight + 8;                     /* room for italic overhang */
    ch2 = tm.tmHeight + 4;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = cw2;
    bi.h.biHeight = -ch2;                            /* top-down */
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 1;
    bi.c[1].rgbRed = bi.c[1].rgbGreen = bi.c[1].rgbBlue = 255;
    bm = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &pix, NULL, 0);
    if (!bm || !pix) { SelectObject(dc, of); DeleteDC(dc); return 0; }
    SelectObject(dc, bm);
    rb = ((cw2 + 31) / 32) * 4;
    memset(pix, 0, (size_t)(rb * ch2));
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_BASELINE | TA_LEFT);
    s[0] = (char)c;
    GdiFlush();
    TextOutA(dc, (int)(tm.tmHeight / 2 + 4), (int)(2 + tm.tmAscent), s, 1);
    GdiFlush();
    src = (unsigned char *)pix;
    /* the ink's box */
    x0 = cw2; x1 = -1; y0 = ch2; y1 = -1;
    for (r = 0; r < ch2; r++)
        for (x = 0; x < cw2; x++)
            if (src[r * rb + (x >> 3)] & (0x80 >> (x & 7))) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (r < y0) y0 = r;
                if (r > y1) y1 = r;
            }
    g->bits = NULL;
    if (x1 < 0) { g->w = g->h = 0; }
    else {
        long nrb;
        g->w = (short)(x1 - x0 + 1);
        g->h = (short)(y1 - y0 + 1);
        g->ox = (short)(x0 - (tm.tmHeight / 2 + 4));
        g->oy = (short)(y0 - (2 + tm.tmAscent));
        nrb = (g->w + 7) / 8;
        g->bits = (unsigned char *)calloc((size_t)nrb, (size_t)g->h);
        if (g->bits)
            for (r = 0; r < g->h; r++)
                for (x = 0; x < g->w; x++)
                    if (src[(y0 + r) * rb + ((x0 + x) >> 3)] & (0x80 >> ((x0 + x) & 7)))
                        g->bits[r * nrb + (x >> 3)] |= (unsigned char)(0x80 >> (x & 7));
    }
    SelectObject(dc, of);
    DeleteObject(bm);
    DeleteDC(dc);
    return x1 < 0 || g->bits != NULL;
}

static GLYPH *get_glyph(int font, int size, int attr, int ch, int scale)
{
    int i;
    GLYPH *g;
    attr &= A_BOLD | A_ITALIC;
    if (font >= nfonts) font = 0;
    for (i = 0; i < ngc; i++) {
        g = &gcache[i];
        if (g->ch == ch && g->font == font && g->size == size && g->attr == attr && g->scale == scale) return g;
    }
    if (ngc >= NGLYPH) glyph_cache_free();
    g = &gcache[ngc];
    memset(g, 0, sizeof *g);
    g->font = (short)font; g->size = (short)size; g->attr = (short)attr;
    g->scale = (short)scale; g->ch = (short)ch;
    if (!gdi_glyph(g, font, ypx(size) * scale, attr, (unsigned char)ch)) return NULL;
    ngc++;
    return g;
}

static GLYPHBM gbm;

static const GLYPHBM *pdf_glyph(void *ctx, int font, int size, int attr, int ch, int scale)
{
    GLYPH *g = get_glyph(font, size, attr, ch, scale);
    (void)ctx;
    if (!g) return NULL;
    gbm.w = g->w; gbm.h = g->h; gbm.ox = g->ox; gbm.oy = g->oy; gbm.bits = g->bits;
    return &gbm;
}

typedef struct { int scale; void (*gauge)(long done, long total); } RCTX;

static int render_cb(void *ctx, DOC *d, int page, int color, long y0, long h, long w,
                     unsigned char *bits, long rb)
{
    RCTX *r = (RCTX *)ctx;
    BAND b;
    b.bits = bits; b.rb = rb; b.w = w; b.y0 = y0; b.h = h;
    page_render(d, page, color, &b, r->scale, pdf_glyph, NULL, pref_date);
    return 1;
}

static void progress_cb(void *ctx, int done, int total)
{
    RCTX *r = (RCTX *)ctx;
    if (r->gauge) r->gauge(done, total);
}

int export_pdf(DOC *d, const char *path, void (*gauge)(long done, long total))
{
    RCTX r;
    PDFOUT o;
    int ok;
    memset(&r, 0, sizeof r);
    /* a page pixel is a screen pixel (about 96 dpi): at least 3x for print */
    r.scale = pdf_scale < 1 ? 3 : pdf_scale + 2;
    r.gauge = gauge;
    memset(&o, 0, sizeof o);
    o.scale = r.scale;
    o.max_band = 64;
    o.colour = colour_on;
    o.render = render_cb;
    o.progress = progress_cb;
    o.ctx = &r;
    doc_layout(d);
    busy(1);
    ok = pdf_export(d, path, &o);
    busy(0);
    glyph_cache_free();
    rs_end();
    return ok;
}

/* ---- printing: bitmap bands at the printer's resolution ------------------- */

static HDC pdc;
static char pname[200];

/* the printer the system uses unless told otherwise */
static HDC default_printer(char *name, int n)
{
    PRINTDLGA pd;
    HDC dc;
    memset(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.Flags = PD_RETURNDEFAULT | PD_RETURNDC;
    if (!PrintDlgA(&pd)) return NULL;
    dc = pd.hDC;
    if (name) {
        name[0] = 0;
        if (pd.hDevNames) {
            DEVNAMES *dn = (DEVNAMES *)GlobalLock(pd.hDevNames);
            if (dn) { lstrcpynA(name, (const char *)dn + dn->wDeviceOffset, n); GlobalUnlock(pd.hDevNames); }
        }
    }
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    return dc;
}

int printer_name(char *out)
{
    HDC dc = default_printer(pname, sizeof pname);
    if (!dc) return 0;
    sprintf(out, "%.60s (%d dpi)", pname, GetDeviceCaps(dc, LOGPIXELSX));
    DeleteDC(dc);
    return 1;
}

/* the Windows Print dialog: printer, pages and copies; the printer's
 * device context is kept for print_doc */
static HDC chosen;

int print_choose(DOC *d, int *from, int *to, int *copies)
{
    PRINTDLGA pd;
    int np = doc_npages(d), ok;
    memset(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = frame;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_HIDEPRINTTOFILE;
    pd.nMinPage = 1;
    pd.nMaxPage = (WORD)(np > 0 ? np : 1);
    pd.nFromPage = 1;
    pd.nToPage = pd.nMaxPage;
    pd.nCopies = 1;
    block_input(1);
    ok = PrintDlgA(&pd);
    block_input(0);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    if (!ok || !pd.hDC) return 0;
    if (chosen) DeleteDC(chosen);
    chosen = pd.hDC;
    if (pd.Flags & PD_PAGENUMS) { *from = pd.nFromPage; *to = pd.nToPage; }
    else { *from = 1; *to = np; }
    *copies = pd.nCopies > 0 ? pd.nCopies : 1;
    return 1;
}

typedef struct {
    PRTOUT o;
    int offx, offy, page, npg, done;
} PCTX;

static int prt_render(void *ctx, DOC *d, int page, int color, long y0, long h, long w,
                      unsigned char *bits, long rb)
{
    PCTX *c = (PCTX *)ctx;
    BAND b;
    b.bits = bits; b.rb = rb; b.w = w; b.y0 = y0; b.h = h;
    page_render(d, page, color, &b, c->o.scale, pdf_glyph, NULL, pref_date);
    return 1;
}

static int prt_out(void *ctx, long y0, long h, long w, const unsigned char *bits, long rb)
{
    PCTX *c = (PCTX *)ctx;
    struct { BITMAPINFOHEADER h; RGBQUAD c[2]; } bi;
    long drb = (w + 31) / 32 * 4, r;
    unsigned char *buf = (unsigned char *)malloc((size_t)(drb * h));
    if (!buf) return 0;
    for (r = 0; r < h; r++) {
        memcpy(buf + (h - 1 - r) * drb, bits + r * rb, (size_t)(rb < drb ? rb : drb));
        if (drb > rb) memset(buf + (h - 1 - r) * drb + rb, 0, (size_t)(drb - rb));
    }
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = w;
    bi.h.biHeight = h;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 1;
    bi.c[0].rgbRed = bi.c[0].rgbGreen = bi.c[0].rgbBlue = 255;      /* paper */
    StretchDIBits(pdc, -c->offx, (int)(y0 - c->offy), (int)w, (int)h, 0, 0, (int)w, (int)h, buf,
                  (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    free(buf);
    return 1;
}

static void prt_progress(void *ctx, int done, int total)
{
    PCTX *c = (PCTX *)ctx;
    gauge_set((long)c->done * total + done, (long)c->npg * total);
}

int print_doc(DOC *d, const unsigned char *sel, int copies)
{
    static PCTX c;
    DOCINFOA di;
    int p, k, ok = 1, np;
    char title[120];
    memset(&c, 0, sizeof c);
    pdc = chosen ? chosen : default_printer(NULL, 0);
    chosen = NULL;
    if (!pdc) return 0;
    c.o.pixw = 25400L / GetDeviceCaps(pdc, LOGPIXELSX);
    c.o.pixh = 25400L / GetDeviceCaps(pdc, LOGPIXELSY);
    c.offx = GetDeviceCaps(pdc, PHYSICALOFFSETX);
    c.offy = GetDeviceCaps(pdc, PHYSICALOFFSETY);
    c.o.w = 0;
    c.o.h = 0;
    c.o.band = 128;
    c.o.render = prt_render;
    c.o.out = prt_out;
    c.o.progress = prt_progress;
    c.o.ctx = &c;
    np = doc_npages(d);
    for (p = 0, k = 0; p < np; p++) if (!sel || sel[p]) k++;
    if (!k) { DeleteDC(pdc); pdc = NULL; return 0; }
    c.npg = k * copies;
    memset(&di, 0, sizeof di);
    di.cbSize = sizeof di;
    lstrcpynA(title, lat(d->base), sizeof title);
    di.lpszDocName = title;
    if (StartDocA(pdc, &di) <= 0) { DeleteDC(pdc); pdc = NULL; return 0; }
    gauge_open(L(S_PRINTING));
    doc_layout(d);
    busy(1);
    for (k = 0; k < copies && ok; k++)
        for (p = 0; p < np && ok; p++) {
            if (sel && !sel[p]) continue;
            if (StartPage(pdc) <= 0) { ok = 0; break; }
            ok = print_page(d, p, &c.o);
            if (EndPage(pdc) <= 0) ok = 0;
            c.done++;
        }
    if (ok) EndDoc(pdc); else AbortDoc(pdc);
    busy(0);
    gauge_close();
    DeleteDC(pdc);
    pdc = NULL;
    glyph_cache_free();
    rs_end();
    return ok;
}
