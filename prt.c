/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * prt.c - printing pages as bitmaps at the printer's resolution
 *
 * The platform layer opens the printer and says how big its pixels are.
 * Each page is drawn into bands by page_render() at a whole multiple of
 * the screen resolution, about as fine as the printer's, and then
 * resampled to the printer's pixels, so the printed page is the right
 * size whatever the two resolutions are. The platform layer sends each
 * finished band (1 bit a pixel, 1 = ink, MSB first) to the printer, once
 * for black and white or once for each colour the page uses.
 *
 * The resampling works on runs of ink, not on single pixels: white space
 * is skipped a long word at a time and every run of ink becomes one span
 * of printer pixels, so a page of text costs a few thousand operations a
 * row instead of one per printer pixel. A source pixel always leaves at
 * least one printer pixel of ink, so hairlines don't vanish when the page
 * is drawn finer than the printer prints.
 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

/* 32 bits on the ST (long) and on a PC (int) */
#if UINT_MAX == 0xFFFFFFFFUL
typedef unsigned int U32;
#else
typedef unsigned long U32;
#endif

/* set printer pixels x0 .. x1-1 of a row */
static void set_span(unsigned char *t, long x0, long x1)
{
    long b0 = x0 >> 3, b1 = (x1 - 1) >> 3;
    unsigned char m0 = (unsigned char)(0xFF >> (x0 & 7));
    unsigned char m1 = (unsigned char)(0xFF << (7 - ((x1 - 1) & 7)));
    if (b0 == b1) { t[b0] |= (unsigned char)(m0 & m1); return; }
    t[b0] |= m0;
    if (b1 > b0 + 1) memset(t + b0 + 1, 0xFF, (size_t)(b1 - b0 - 1));
    t[b1] |= m1;
}

/* leading ones and zeros of a byte */
static unsigned char lead1[256], lead0[256];

static void lead_init(void)
{
    int v, k;
    if (lead0[0]) return;
    for (v = 0; v < 256; v++) {
        for (k = 0; k < 8 && (v & (0x80 >> k)); k++) ;
        lead1[v] = (unsigned char)k;
        for (k = 0; k < 8 && !(v & (0x80 >> k)); k++) ;
        lead0[v] = (unsigned char)k;
    }
}

/* the printer pixels of the source run a .. e-1 */
static int run_out(unsigned char *t, const long *dlo, long a, long e, long pw)
{
    long x0 = dlo[a], x1 = dlo[e];
    if (x1 <= dlo[e - 1]) x1 = dlo[e - 1] + 1;      /* the last pixel leaves ink too */
    if (x1 > pw) x1 = pw;
    if (x0 >= x1) return 0;
    set_span(t, x0, x1);
    return 1;
}

/* one source row to printer pixels, OR-ed into t; 0 if it has no ink.
 * dlo[sx] is the first printer column of source column sx (sw+1 of them).
 * Runs are found a byte at a time with the leading-bits tables. */
static int expand_row(const unsigned char *s, long sw, const long *dlo, long pw, unsigned char *t)
{
    long nb = (sw + 7) >> 3, i = 0, a = -1;
    int any = 0;
    while (i < nb) {
        unsigned v;
        int p;
        if (a < 0) {                                /* white: skip it a long at a time */
            if ((i & 1) && !s[i]) i++;              /* longs at even addresses only (68000) */
            if (!(i & 1)) while (i + 4 <= nb && *(const U32 *)(s + i) == 0) i += 4;
            while (i < nb && !s[i]) i++;
            if (i >= nb) break;
        }
        v = s[i];
        if (v == 0xFF) { if (a < 0) a = i << 3; i++; continue; }
        p = 0;
        while (p < 8) {
            unsigned w = (v << p) & 0xFF;
            int n;
            if (a < 0) {                            /* in white: to the next ink */
                n = lead0[w];
                if (p + n >= 8) break;
                p += n;
                a = (i << 3) + p;
            } else {                                /* in ink: to its end */
                n = lead1[w];
                if (p + n >= 8) break;
                p += n;
                {
                    long e = (i << 3) + p;
                    if (e > sw) e = sw;
                    any |= run_out(t, dlo, a, e, pw);
                }
                a = -1;
            }
        }
        i++;
    }
    if (a >= 0 && a < sw) any |= run_out(t, dlo, a, sw, pw);
    return any;
}

/* The fast way, when no source pixel is wider than a printer pixel (the
 * usual case): for each source byte column, the printer pixels of each of
 * its pixels as a mask on the 4 printer bytes from base[i], OR-ed together
 * a nibble at a time. Same pixels as expand_row(). */
typedef struct {
    long *base;                     /* first printer byte of source byte i  */
    U32 (*nib)[2][16];    /* masks for the high and low nibble   */
    long nb;
} BYTEMAP;

static int bytemap_make(BYTEMAP *m, long sw, const long *dlo, long pw)
{
    long i;
    int k, v;
    m->nb = (sw + 7) >> 3;
    m->base = (long *)malloc(m->nb * sizeof(long));
    m->nib = (U32 (*)[2][16])malloc(m->nb * sizeof *m->nib);
    if (!m->base || !m->nib) { free(m->base); free(m->nib); m->base = NULL; m->nib = NULL; return 0; }
    for (i = 0; i < m->nb; i++) {
        U32 bit[8];
        long bb = dlo[i << 3] >> 3;
        m->base[i] = bb;
        for (k = 0; k < 8; k++) {
            long sx = (i << 3) + k, x0, x1, x;
            bit[k] = 0;
            if (sx >= sw) continue;
            x0 = dlo[sx]; x1 = dlo[sx + 1];
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > pw) x1 = pw;
            for (x = x0; x < x1; x++) {
                long off = x - (bb << 3);
                if (off < 0 || off >= 32) { free(m->base); free(m->nib); m->base = NULL; m->nib = NULL; return 0; }
                bit[k] |= (U32)0x80000000UL >> off;
            }
        }
        for (v = 0; v < 16; v++) {
            U32 hi = 0, lo = 0;
            for (k = 0; k < 4; k++) if (v & (8 >> k)) { hi |= bit[k]; lo |= bit[k + 4]; }
            m->nib[i][0][v] = hi;
            m->nib[i][1][v] = lo;
        }
    }
    return 1;
}

static void bytemap_free(BYTEMAP *m) { free(m->base); free(m->nib); }

/* a source row through the byte map, OR-ed into the printer row t (prb bytes) */
static int bytemap_row(const BYTEMAP *m, const unsigned char *s, unsigned char *t, long prb)
{
    long i = 0, nb = m->nb;
    int any = 0;
    while (i < nb) {
        unsigned v;
        U32 w;
        long b;
        if ((i & 1) && !s[i]) i++;
        if (!(i & 1)) while (i + 4 <= nb && *(const U32 *)(s + i) == 0) i += 4;
        if (i >= nb) break;
        v = s[i];
        if (!v) { i++; continue; }
        w = m->nib[i][0][v >> 4] | m->nib[i][1][v & 15];
        b = m->base[i];
        t[b] |= (unsigned char)(w >> 24);
        if (b + 1 < prb) t[b + 1] |= (unsigned char)(w >> 16);
        if (b + 2 < prb) t[b + 2] |= (unsigned char)(w >> 8);
        if (b + 3 < prb) t[b + 3] |= (unsigned char)w;
        any = 1;
        i++;
    }
    return any;
}

static void or_row(unsigned char *t, const unsigned char *s, long n)
{
    long i = 0;
    while (i + 4 <= n) { *(U32 *)(t + i) |= *(const U32 *)(s + i); i += 4; }
    while (i < n) { t[i] |= s[i]; i++; }
}

int print_page(DOC *d, int page, PRTOUT *o)
{
    long sw, sh, pw, ph, srb, prb, band, y, sband, sx;
    long *dlo;
    unsigned char *src, *dst, *tmp;
    int scale, ok = 1, pass, npass = 0, col[NCOLORS + 1], use_map;
    BYTEMAP map;
    unsigned used;

    if (o->pixw <= 0 || o->pixh <= 0) return 0;
    lead_init();
    /* the smallest whole scale whose pixels are at most 12% bigger than the
     * printer's (it costs the square of the scale; the difference doesn't
     * show at printer resolutions) */
    scale = (int)((dev_pixw * 100 + o->pixw * 112 - 1) / (o->pixw * 112));
    if ((dev_pixh * 100 + o->pixh * 112 - 1) / (o->pixh * 112) > scale)
        scale = (int)((dev_pixh * 100 + o->pixh * 112 - 1) / (o->pixh * 112));
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    o->scale = scale;
    sw = pt2px_x(PS(d).w) * scale;
    sh = pt2px_y(PS(d).h) * scale;
    /* the page in printer pixels, cut to what the printer can print */
    pw = (long)PS(d).w * 25400L / 72L / o->pixw;
    ph = (long)PS(d).h * 25400L / 72L / o->pixh;
    if (o->w > 0 && pw > o->w) pw = o->w;
    if (o->h > 0 && ph > o->h) ph = o->h;
    band = o->band > 0 ? o->band : 64;
    srb = ((sw + 31) / 32) * 4;                     /* whole longs: the white is skipped by longs */
    prb = ((pw + 31) / 32) * 4;
    /* source rows a band of printer rows needs, with rows to spare */
    sband = band * o->pixh * scale / dev_pixh + 4;
    /* the colours to print: one pass each, or all in black at once */
    used = o->colour ? page_colours(d, page) : 1u;
    if (o->colour && (used & ~1u)) {
        for (pass = 0; pass < NCOLORS; pass++) if (used & (1u << pass)) col[npass++] = pass;
    } else col[npass++] = -1;
    src = (unsigned char *)malloc(srb * sband);
    dst = (unsigned char *)malloc(prb * band);
    tmp = (unsigned char *)malloc(prb);
    dlo = (long *)malloc((sw + 1) * sizeof(long));
    if (!src || !dst || !tmp || !dlo) { free(src); free(dst); free(tmp); free(dlo); return 0; }
    /* source column -> its first printer column */
    for (sx = 0; sx <= sw; sx++) dlo[sx] = sx * dev_pixw / ((long)scale * o->pixw);
    use_map = bytemap_make(&map, sw, dlo, pw);
    for (y = 0; y < ph && ok; y += band) {
        long rows = ph - y < band ? ph - y : band;
        /* the source rows whose printer rows fall in this band */
        long sy0 = y * o->pixh * scale / dev_pixh - 1;
        long sy1 = (y + rows) * o->pixh * scale / dev_pixh + 2;
        if (sy0 < 0) sy0 = 0;
        if (sy1 > sh) sy1 = sh;
        if (sy1 - sy0 > sband) sy1 = sy0 + sband;
        for (pass = 0; pass < npass && ok; pass++) {
            long sy;
            memset(dst, 0, prb * rows);
            if (sy1 > sy0) {
                memset(src, 0, srb * (sy1 - sy0));
                if (!o->render(o->ctx, d, page, col[pass], sy0, sy1 - sy0, sw, src, srb)) { ok = 0; break; }
            }
            for (sy = sy0; sy < sy1; sy++) {
                /* printer rows r0 .. r1-1 of this source row, at least one */
                long r0 = sy * dev_pixh / ((long)scale * o->pixh) - y;
                long r1 = (sy + 1) * dev_pixh / ((long)scale * o->pixh) - y, r;
                if (r1 <= r0) r1 = r0 + 1;
                if (r0 < 0) r0 = 0;
                if (r1 > rows) r1 = rows;
                if (r0 >= r1) continue;
                if (r1 == r0 + 1) {                 /* straight into its printer row */
                    if (use_map) bytemap_row(&map, src + (sy - sy0) * srb, dst + r0 * prb, prb);
                    else expand_row(src + (sy - sy0) * srb, sw, dlo, pw, dst + r0 * prb);
                    continue;
                }
                memset(tmp, 0, prb);
                if (!(use_map ? bytemap_row(&map, src + (sy - sy0) * srb, tmp, prb)
                              : expand_row(src + (sy - sy0) * srb, sw, dlo, pw, tmp))) continue;
                for (r = r0; r < r1; r++) or_row(dst + r * prb, tmp, prb);
            }
            if (!o->out(o->ctx, col[pass], y, rows, pw, dst, prb)) ok = 0;
        }
        if (o->progress) o->progress(o->ctx, (int)(y + rows), (int)ph);
    }
    if (use_map) bytemap_free(&map);
    free(src); free(dst); free(tmp); free(dlo);
    return ok;
}
