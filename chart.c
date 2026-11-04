/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * chart.c - pie, bar, 3D pie and 3D bar charts of a table's numbers
 *
 * The numbers come from the table itself, each time it is drawn: the rows
 * are the categories and the columns of numbers the series. The first row
 * names the series if it holds words (or years), and the first column
 * names the categories if it holds words. A pie shows the first series.
 *
 * chart_ops() makes a short list of filled polygons, lines and labels in
 * the chart's own pixels; each front end and the PDF export draw that
 * list in their own way. Fills name a series: a colour, or in black and
 * white a pattern. Polygons are outlined in black.
 *
 * Everything is whole numbers (values in hundredths, angles in 4096ths of
 * a turn): the Amiga build has no floating point library.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

/* ---- the colours and patterns of the series --------------------------- */

static const unsigned char ser_col[7] = { 3, 1, 2, 6, 5, 4, 7 };    /* blue red green yellow magenta cyan grey */
static const unsigned char ser_pat[7] = { FL_SOLID, FL_GREY, FP_DIAG1, FP_HLINE, FP_DIAG2, FP_CROSS, FP_DOTS };

int chart_color(int series) { return ser_col[series % 7]; }
int chart_pattern(int series) { return ser_pat[series % 7]; }

/* ---- whole-number maths -------------------------------------------------- */

#define TURN 4096L
static const short qsin[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270,
    6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585,
    11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364,
    16379, 16384 };

/* sine of a (4096ths of a turn), times 16384 */
static long isin(long a)
{
    long q, i, f;
    int neg = 0;
    a %= TURN;
    if (a < 0) a += TURN;
    if (a >= TURN / 2) { a -= TURN / 2; neg = 1; }
    if (a > TURN / 4) a = TURN / 2 - a;
    q = a * 64;                                   /* 1024ths of a quarter -> 64 steps */
    i = q / 1024; f = q % 1024;
    q = qsin[i] + (i < 64 ? (qsin[i + 1] - qsin[i]) * f / 1024 : 0);
    return neg ? -q : q;
}
static long icos(long a) { return isin(a + TURN / 4); }

/* a * b / c without overflow, for b small (a pixel size) */
static long muldiv(long a, long b, long c)
{
    while ((a > 1000000L || a < -1000000L || b > 1000000L) && c > 1) { a /= 2; c /= 2; }
    if (c == 0) return 0;
    return a * b / c;
}

/* ---- numbers --------------------------------------------------------- */

/* a cell's text as a number in hundredths: 1234, 1,234.5, 3,5 (decimal
 * comma), 12%, -4, (4), $5, \234 5 (pounds); 0 if it isn't one */
int chart_number(const char *s, long *v)
{
    char t[40];
    int n = 0, neg = 0, digits = 0, i, dots = 0, commas = 0, lastsep = -1, decs = 0;
    long r = 0;
    char dec = '.';
    while (*s == ' ') s++;
    if (*s == '(') { neg = 1; s++; }
    for (; *s && n < 38; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '-' && !n && !digits) { neg = !neg; continue; }
        if (c == '+' && !n) continue;
        if ((c >= '0' && c <= '9') || c == '.' || c == ',') { t[n++] = (char)c; if (c <= '9' && c >= '0') digits++; continue; }
        if (c == ' ' || c == '$' || c == 0x9C || c == 0x9D || c == '%' || c == ')' || c == 0xA0 || c == '\'') continue;
        return 0;
    }
    t[n] = 0;
    if (!digits) return 0;
    for (i = 0; i < n; i++) {
        if (t[i] == '.') { dots++; lastsep = i; }
        if (t[i] == ',') { commas++; lastsep = i; }
    }
    /* which of . and , is the decimal point */
    if (dots && commas) dec = t[lastsep];
    else if (commas == 1 && !dots) dec = (n - lastsep - 1 == 3) ? '.' : ',';   /* 1,234 or 3,5 */
    else if (dots == 1) dec = '.';
    else if (commas > 1) dec = '.';
    else if (dots > 1) dec = ',';
    for (i = 0; i < n; i++) {
        if (t[i] == dec) { i++; break; }
        if (t[i] >= '0' && t[i] <= '9' && r < 20000000L) r = r * 10 + (t[i] - '0');
    }
    r *= 100;
    for (; i < n; i++)
        if (t[i] >= '0' && t[i] <= '9') {
            if (decs == 0) r += 10 * (t[i] - '0');
            else if (decs == 1) r += t[i] - '0';
            decs++;
        }
    *v = neg ? -r : r;
    return 1;
}

/* the cells of table tbl's first run of rows */
static int table_cells(DOC *d, int tbl, char cell[][CH_MAXCOL][CH_TXT], int *ncol)
{
    long k = 0, s;
    int nrow = 0, found = 0;
    *ncol = 0;
    while (k < d->len && nrow < CH_MAXROW + 1) {
        long e = doc_para_end(d, k);
        const PARA *P = PARA_OF(d, &d->text[e < d->len ? e : d->len - 1]);
        if (P->tbl == tbl + 1) {
            int c = 0, o = 0;
            found = 1;
            for (s = k; s <= e && c < CH_MAXCOL; s++) {
                unsigned char ch = s < d->len ? d->text[s].ch : '\n';
                if (s == e || ch == CH_CELL) {
                    while (o > 0 && cell[nrow][c][o - 1] == ' ') o--;
                    cell[nrow][c][o] = 0;
                    c++; o = 0;
                } else if (o < CH_TXT - 1 && (ch >= ' ' || ch == '\t') && !(o == 0 && ch == ' ')) cell[nrow][c][o++] = (char)ch;
            }
            for (; c < CH_MAXCOL; c++) cell[nrow][c][0] = 0;
            if (c > *ncol) *ncol = d->a.tbls[tbl].ncols < CH_MAXCOL ? d->a.tbls[tbl].ncols : CH_MAXCOL;
            nrow++;
        } else if (found) break;
        k = e + 1;
    }
    return nrow;
}

int chart_data(DOC *d, int tbl, CHDATA *cd)
{
    static char cell[CH_MAXROW + 1][CH_MAXCOL][CH_TXT];
    int nrow, ncol, r, c, numcol[CH_MAXCOL], header = 0, labels = 0, start;
    long v;
    memset(cd, 0, sizeof *cd);
    if (tbl < 0 || tbl >= d->a.ntbl) return 0;
    nrow = table_cells(d, tbl, cell, &ncol);
    if (nrow < 1 || ncol < 1) return 0;
    /* the columns of numbers: most of their cells below the first row are numbers */
    for (c = 0; c < ncol; c++) {
        int num = 0, any = 0;
        for (r = nrow > 1 ? 1 : 0; r < nrow; r++)
            if (cell[r][c][0]) { any++; if (chart_number(cell[r][c], &v)) num++; }
        numcol[c] = any && num * 2 > any;
    }
    labels = ncol > 1 && !numcol[0];      /* a first column of words names the rows */
    /* a first row of words (where the numbers are) is a header */
    for (c = 0; c < ncol && nrow > 1; c++)
        if (numcol[c] && cell[0][c][0] && !chart_number(cell[0][c], &v)) header = 1;
    /* ... and so is one of years over the numbers, or one with an empty corner */
    if (!header && nrow > 1 && labels) {
        int years = 0, cols = 0;
        for (c = 1; c < ncol; c++)
            if (numcol[c]) {
                cols++;
                if (chart_number(cell[0][c], &v) && v >= 100000L && v <= 299900L && v % 100 == 0 && !strchr(cell[0][c], ',') &&
                    !strchr(cell[0][c], '.'))
                    years++;
            }
        if ((cols && years == cols) || !cell[0][0][0]) header = 1;
    }
    start = header ? 1 : 0;
    for (c = 0; c < ncol && cd->nser < CH_MAXSER; c++) {
        if (!numcol[c] || (labels && c == 0)) continue;
        if (header) strcpy(cd->ser[cd->nser], cell[0][c]);
        else sprintf(cd->ser[cd->nser], "%d", cd->nser + 1);
        for (r = start; r < nrow && r - start < CH_MAXCAT; r++) {
            if (!chart_number(cell[r][c], &v)) v = 0;
            cd->v[r - start][cd->nser] = v;
        }
        cd->nser++;
    }
    cd->ncat = nrow - start > CH_MAXCAT ? CH_MAXCAT : nrow - start;
    for (r = 0; r < cd->ncat; r++) {
        if (labels) strcpy(cd->cat[r], cell[r + start][0]);
        else sprintf(cd->cat[r], "%d", r + 1);
    }
    return cd->nser > 0 && cd->ncat > 0;
}

/* ---- the display list ------------------------------------------------- */

typedef struct {
    CHOP *op;
    int   n, max;
    const METRICS *m;
} OPS;

static CHOP *new_op(OPS *o, int type)
{
    CHOP *p;
    if (o->n >= o->max) return NULL;
    p = &o->op[o->n++];
    memset(p, 0, sizeof *p);
    p->op = (unsigned char)type;
    return p;
}

static void poly(OPS *o, const long *xy, int n, int series, int shade)
{
    CHOP *p;
    int i;
    if (n < 3) return;
    if (n > CH_MAXPTS) n = CH_MAXPTS;
    p = new_op(o, CO_POLY);
    if (!p) return;
    p->series = (unsigned char)series;
    p->shade = (unsigned char)shade;
    p->n = (short)n;
    for (i = 0; i < 2 * n; i++) p->xy[i] = (short)xy[i];
}

static void rect(OPS *o, long x0, long y0, long x1, long y1, int series, int shade)
{
    long q[8];
    q[0] = x0; q[1] = y0; q[2] = x1; q[3] = y0; q[4] = x1; q[5] = y1; q[6] = x0; q[7] = y1;
    poly(o, q, 4, series, shade);
}

static void line(OPS *o, long x0, long y0, long x1, long y1)
{
    CHOP *p = new_op(o, CO_LINE);
    if (!p) return;
    p->n = 2;
    p->xy[0] = (short)x0; p->xy[1] = (short)y0; p->xy[2] = (short)x1; p->xy[3] = (short)y1;
}

static long text_w(OPS *o, const char *s)
{
    long w = 0;
    for (; *s; s++) w += o->m->w[(unsigned char)*s];
    return w;
}

/* a label with its baseline at y; shortened to fit maxw */
static void text(OPS *o, long x, long y, int align, const char *s, long maxw)
{
    CHOP *p = new_op(o, CO_TEXT);
    int n;
    if (!p) return;
    strncpy(p->text, s, sizeof p->text - 1);
    n = (int)strlen(p->text);
    while (maxw > 0 && n > 1 && text_w(o, p->text) > maxw) p->text[--n] = 0;
    p->align = (unsigned char)align;
    p->n = 1;
    p->xy[0] = (short)x; p->xy[1] = (short)y;
}

/* v (hundredths) with as many decimals as the step needs */
static void fmt_num(long v, long step, char *out)
{
    long a = v < 0 ? -v : v, sc = 1;
    const char *suf = "";
    int dec;
    if (a >= 100000000L && step >= 10000000L) { sc = 100000000L; suf = "M"; }
    else if (a >= 1000000L && step >= 100000L) { sc = 100000L; suf = "k"; }
    if (sc > 1) {
        long w = (a + sc / 20) / (sc / 10);              /* tenths of k or M */
        if (step >= sc) sprintf(out, "%s%ld%s", v < 0 ? "-" : "", (w + 5) / 10, suf);
        else sprintf(out, "%s%ld.%ld%s", v < 0 ? "-" : "", w / 10, w % 10, suf);
        return;
    }
    dec = step >= 100 ? 0 : step >= 10 ? 1 : 2;
    if (dec == 0) sprintf(out, "%s%ld", v < 0 && (a + 50) / 100 ? "-" : "", (a + 50) / 100);
    else if (dec == 1) sprintf(out, "%s%ld.%ld", v < 0 ? "-" : "", (a + 5) / 100, ((a + 5) / 10) % 10);
    else sprintf(out, "%s%ld.%02ld", v < 0 ? "-" : "", a / 100, a % 100);
}

/* the legend down the right: swatch and name; returns its width */
static long legend(OPS *o, long w, long h, char names[][CH_TXT], int n, const long *pct)
{
    long fh = o->m->asc + o->m->desc, lw = 0, tw, x, y, step = fh + 3;
    int i;
    char buf[CH_TXT + 8];
    for (i = 0; i < n; i++) {
        if (pct) sprintf(buf, "%.20s %ld%%", names[i], pct[i]); else strcpy(buf, names[i]);
        tw = text_w(o, buf);
        if (tw > lw) lw = tw;
    }
    if (lw > w * 2 / 5) lw = w * 2 / 5;
    x = w - lw - fh - 6;
    y = (h - n * step) / 2;
    if (y < 2) y = 2;
    for (i = 0; i < n && y + fh <= h; i++, y += step) {
        if (pct) sprintf(buf, "%.20s %ld%%", names[i], pct[i]); else strcpy(buf, names[i]);
        rect(o, x, y + 1, x + fh - 2, y + fh - 1, i, 0);
        text(o, x + fh + 2, y + o->m->asc, 0, buf, lw + 2);
    }
    return lw + fh + 10;
}

static void pie(OPS *o, const CHDATA *cd, long w, long h, int d3)
{
    long tot = 0, a0, cx, cy, rx, ry, dep, pct[CH_MAXCAT], lw;
    long xy[2 * CH_MAXPTS];
    char names[CH_MAXCAT][CH_TXT];
    int i, k, n = 0, pass;
    for (i = 0; i < cd->ncat; i++) if (cd->v[i][0] > 0) tot += cd->v[i][0];
    if (tot <= 0) return;
    for (i = 0; i < cd->ncat; i++) {
        strcpy(names[i], cd->cat[i]);
        pct[i] = cd->v[i][0] > 0 ? muldiv(cd->v[i][0], 100, tot) : 0;
    }
    lw = legend(o, w, h, names, cd->ncat, pct);
    cx = (w - lw) / 2;
    rx = (w - lw < h ? w - lw : h) / 2 - 6;
    if (rx < 10) rx = 10;
    ry = d3 ? rx * 55 / 100 : rx;
    dep = d3 ? rx / 4 : 0;
    cy = h / 2 - dep / 2;
    /* 3D: first the sides you can see (the front half), then the tops */
    for (pass = d3 ? 0 : 1; pass < 2; pass++) {
        a0 = 0;
        for (i = 0; i < cd->ncat; i++) {
            long v = cd->v[i][0], a1, lo, hi, steps;
            if (v <= 0) continue;
            a1 = a0 + muldiv(v, TURN, tot);
            if (i == cd->ncat - 1 || a1 > TURN) a1 = a1 > TURN - 8 ? TURN : a1;
            if (pass == 0) {
                /* the part of the slice between a quarter and three quarters of a turn */
                lo = a0 > TURN / 4 ? a0 : TURN / 4;
                hi = a1 < TURN * 3 / 4 ? a1 : TURN * 3 / 4;
                if (hi > lo) {
                    steps = (hi - lo) * 48 / TURN + 1;
                    if (steps > CH_MAXPTS / 2 - 1) steps = CH_MAXPTS / 2 - 1;
                    n = 0;
                    for (k = 0; k <= steps; k++) {
                        long a = lo + (hi - lo) * k / steps;
                        xy[2 * n] = cx + rx * isin(a) / 16384; xy[2 * n + 1] = cy - ry * icos(a) / 16384; n++;
                    }
                    for (k = (int)steps; k >= 0; k--) {
                        long a = lo + (hi - lo) * k / steps;
                        xy[2 * n] = cx + rx * isin(a) / 16384; xy[2 * n + 1] = cy - ry * icos(a) / 16384 + dep; n++;
                    }
                    poly(o, xy, n, i, 2);
                }
            } else {
                steps = (a1 - a0) * 48 / TURN + 1;
                if (steps > CH_MAXPTS - 2) steps = CH_MAXPTS - 2;
                n = 0;
                if (a1 - a0 < TURN) { xy[0] = cx; xy[1] = cy; n = 1; }
                for (k = 0; k <= steps; k++) {
                    long a = a0 + (a1 - a0) * k / steps;
                    xy[2 * n] = cx + rx * isin(a) / 16384; xy[2 * n + 1] = cy - ry * icos(a) / 16384; n++;
                }
                poly(o, xy, n, i, 0);
            }
            a0 = a1;
        }
    }
}

static long ydev(long v, long lo, long hi, long py0, long py1)
{
    return py1 - muldiv(v - lo, py1 - py0, hi - lo);
}

static void bars(OPS *o, const CHDATA *cd, long w, long h, int d3)
{
    long vmin = 0, vmax = 0, step, mag, lo, hi, px0, px1, py0, py1, gw, bw, dep, y0, lw = 0, r;
    long fh = o->m->asc + o->m->desc, axw = 0;
    char buf[24];
    int i, s, k, nt;
    for (i = 0; i < cd->ncat; i++)
        for (s = 0; s < cd->nser; s++) {
            if (cd->v[i][s] < vmin) vmin = cd->v[i][s];
            if (cd->v[i][s] > vmax) vmax = cd->v[i][s];
        }
    if (vmax == vmin) vmax = vmin + 100;
    /* a round step: 1, 2 or 5 times a power of ten, about 5 ticks */
    step = (vmax - vmin) / 5;
    if (step < 1) step = 1;
    mag = 1;
    while (mag <= step / 10) mag *= 10;
    r = step * 10 / mag;                          /* 10 .. 99 */
    step = r < 15 ? mag : r < 35 ? 2 * mag : r < 75 ? 5 * mag : 10 * mag;
    lo = vmin >= 0 ? 0 : -((-vmin + step - 1) / step) * step;
    hi = vmax <= 0 ? 0 : ((vmax + step - 1) / step) * step;
    nt = (int)((hi - lo) / step);
    for (k = 0; k <= nt; k++) {
        long tw;
        fmt_num(lo + k * step, step, buf);
        tw = text_w(o, buf);
        if (tw > axw) axw = tw;
    }
    if (cd->nser > 1) lw = legend(o, w, h, (char (*)[CH_TXT])cd->ser, cd->nser, NULL);
    dep = d3 ? (h / 18 < 10 ? h / 18 : 10) : 0;
    px0 = axw + 6;
    px1 = w - lw - 4 - dep;
    py0 = fh / 2 + 2 + dep;
    py1 = h - fh - 4;
    if (px1 - px0 < 20 || py1 - py0 < 20) return;
    /* the value axis with its ticks */
    for (k = 0; k <= nt; k++) {
        long v = lo + k * step, y = ydev(v, lo, hi, py0, py1);
        fmt_num(v, step, buf);
        text(o, px0 - 4, y + o->m->asc / 2, 2, buf, 0);
        line(o, px0 - 2, y, px0, y);
        if (k && d3) line(o, px0, y, px0 + dep, y - dep);
    }
    line(o, px0, py0 - dep, px0, py1);
    y0 = ydev(0, lo, hi, py0, py1);                /* the zero line */
    gw = (px1 - px0) / cd->ncat;
    bw = gw * 8 / 10 / cd->nser;
    if (bw < 1) bw = 1;
    if (d3 && dep > bw * 6 / 10) dep = bw * 6 / 10;
    for (i = 0; i < cd->ncat; i++) {
        long gx = px0 + i * gw + gw / 10;
        for (s = 0; s < cd->nser; s++) {
            long x0 = gx + s * bw, x1 = x0 + bw - (cd->nser > 1 ? 1 : 0);
            long y = ydev(cd->v[i][s], lo, hi, py0, py1), ya = y < y0 ? y : y0, yb = y < y0 ? y0 : y;
            if (yb - ya < 1) yb = ya + 1;
            rect(o, x0, ya, x1, yb, s, 0);
            if (d3) {
                long q[8];
                q[0] = x0; q[1] = ya; q[2] = x0 + dep; q[3] = ya - dep; q[4] = x1 + dep; q[5] = ya - dep; q[6] = x1; q[7] = ya;
                poly(o, q, 4, s, 1);                            /* top */
                q[0] = x1; q[1] = ya; q[2] = x1 + dep; q[3] = ya - dep; q[4] = x1 + dep; q[5] = yb - dep; q[6] = x1; q[7] = yb;
                poly(o, q, 4, s, 2);                            /* side */
            }
        }
        text(o, gx + gw * 4 / 10, h - 2 - o->m->desc, 1, cd->cat[i], gw);
    }
    line(o, px0, y0, px1 + (d3 ? dep : 0), y0);
}

int chart_ops(DOC *d, int tbl, long w, long h, CHOP *op, int max)
{
    static CHDATA cd;
    OPS o;
    const TBL *T;
    if (tbl < 0 || tbl >= d->a.ntbl) return 0;
    T = &d->a.tbls[tbl];
    o.op = op; o.n = 0; o.max = max;
    o.m = get_metrics(default_font(), CHART_PT, 0);
    /* the frame */
    line(&o, 0, 0, w - 1, 0); line(&o, w - 1, 0, w - 1, h - 1);
    line(&o, w - 1, h - 1, 0, h - 1); line(&o, 0, h - 1, 0, 0);
    if (!chart_data(d, tbl, &cd)) return o.n;
    if (T->chart == CH_PIE || T->chart == CH_PIE3D) pie(&o, &cd, w, h, T->chart == CH_PIE3D);
    else bars(&o, &cd, w, h, T->chart == CH_BAR3D);
    return o.n;
}

/* the chart's size in page pixels: the table's own, or one that suits
 * where it goes (tw: the table's width, room: the space beside it) */
void chart_size(const TBL *T, long tw, long colw, long *w, long *h)
{
    long cw = T->cw ? pt2px_x(T->cw) : 0, ch = T->ch ? pt2px_y(T->ch) : 0;
    if (!cw) {
        if (T->cpos == CP_LEFT || T->cpos == CP_RIGHT) cw = colw - tw - pt2px_x(12);
        else cw = tw;
        if (cw > pt2px_x(300)) cw = pt2px_x(300);
        if (cw < pt2px_x(240) && T->cpos != CP_LEFT && T->cpos != CP_RIGHT) cw = pt2px_x(240);
    }
    if (cw > colw) cw = colw;
    if (!ch) ch = cw * 3 / 5;
    if (ch < pt2px_y(90)) ch = pt2px_y(90);
    if (ch > pt2px_y(260)) ch = pt2px_y(260);
    *w = cw; *h = ch;
}
