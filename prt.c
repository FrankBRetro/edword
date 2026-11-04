/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * prt.c - printing pages as bitmaps at the printer's resolution
 *
 * The platform layer opens the printer and says how big its pixels are.
 * Each page is drawn into bands by page_render() at a whole multiple of
 * the screen resolution, at least as fine as the printer's, and then
 * resampled to the printer's pixels, so the printed page is the right
 * size whatever the two resolutions are. The platform layer sends each
 * finished band (1 bit a pixel, 1 = ink, MSB first) to the printer.
 */
#include <stdlib.h>
#include <string.h>
#include "wp.h"

int print_page(DOC *d, int page, PRTOUT *o)
{
    long sw, sh, pw, ph, srb, prb, band, y, sband;
    long *xmap;
    unsigned char *src, *dst;
    int scale, ok = 1;

    if (o->pixw <= 0 || o->pixh <= 0) return 0;
    /* the smallest whole scale whose pixels are no bigger than the printer's */
    scale = (int)((dev_pixw + o->pixw - 1) / o->pixw);
    if ((dev_pixh + o->pixh - 1) / o->pixh > scale) scale = (int)((dev_pixh + o->pixh - 1) / o->pixh);
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
    srb = ((sw + 15) / 16) * 2;
    prb = ((pw + 15) / 16) * 2;
    /* source rows a band of printer rows needs, with a row to spare */
    sband = band * o->pixh * scale / dev_pixh + 2;
    src = (unsigned char *)malloc(srb * sband);
    dst = (unsigned char *)malloc(prb * band);
    xmap = (long *)malloc(pw * sizeof(long));
    if (!src || !dst || !xmap) { free(src); free(dst); free(xmap); return 0; }
    for (y = 0; y < pw; y++) {                     /* printer column -> source column */
        long sx = y * o->pixw * scale / dev_pixw;
        xmap[y] = sx < sw ? sx : -1;
    }
    for (y = 0; y < ph && ok; y += band) {
        long rows = ph - y < band ? ph - y : band, r;
        long sy0 = y * o->pixh * scale / dev_pixh;
        long sy1 = (y + rows) * o->pixh * scale / dev_pixh + 1;
        if (sy1 > sh) sy1 = sh;
        if (sy1 - sy0 > sband) sy1 = sy0 + sband;
        memset(src, 0, srb * sband);
        memset(dst, 0, prb * band);
        if (sy1 > sy0 && !o->render(o->ctx, d, page, -1, sy0, sy1 - sy0, sw, src, srb)) { ok = 0; break; }
        for (r = 0; r < rows; r++) {
            long sy = (y + r) * o->pixh * scale / dev_pixh - sy0, x;
            const unsigned char *s;
            unsigned char *t = dst + r * prb;
            if (sy < 0 || sy >= sy1 - sy0) continue;
            s = src + sy * srb;
            for (x = 0; x < pw; x++) {
                long sx = xmap[x];
                if (sx >= 0 && (s[sx >> 3] & (0x80 >> (sx & 7)))) t[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
            }
        }
        if (!o->out(o->ctx, y, rows, pw, dst, prb)) ok = 0;
        if (o->progress) o->progress(o->ctx, (int)(y + rows), (int)ph);
    }
    free(src); free(dst); free(xmap);
    return ok;
}
