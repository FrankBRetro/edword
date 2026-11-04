/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * img.c - pictures: GEM IMG, PNG and IFF ILBM/PBM
 *
 * Every picture is kept as a 1-bit bitmap (1 = ink), made with error
 * diffusion, which every screen and the PDF export can show. When there
 * is room (img_rgb_max) a colour picture also keeps its colours, 3 bytes
 * a pixel, for screens with many colours, colour PDFs and the Mac.
 *
 * GEM IMG data is a series of scan line records:
 *   00 00 FF n          the next line is repeated n times
 *   00 00 n             n+1 bytes copied from the line above
 *   00 n  <pattern>     the pattern (patlen bytes) n times
 *   80 n  <n bytes>     n literal bytes
 *   b                   solid run: b&0x7F bytes of 0xFF (b&0x80) or 0x00
 * 1 to 8 planes are bit planes, one after the other in each line, with
 * the XIMG palette when there is one; 16, 24 and 32 planes are chunky
 * true colour: RGB 5-6-5 big-endian, R G B, and x R G B.
 *
 * PNG: all colour types and bit depths, interlaced or not; transparent
 * parts are put on white. IFF: ILBM with 1-8 planes, Extra Half-Brite,
 * HAM6/HAM8, 24-bit, and the chunky PBM of Deluxe Paint.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

long img_rgb_max = 0x7fffffffL;      /* the largest colour copy kept, bytes */

static unsigned rd16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
static unsigned long rd32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

void img_free(IMG *im)
{
    free(im->bits);
    free(im->rgb);
    im->bits = NULL;
    im->rgb = NULL;
}

static void set_name(IMG *im, const char *path)
{
    const char *s = strrchr(path, '\\');
    if (!s) s = strrchr(path, '/');
    if (!s) s = strrchr(path, ':');
    strncpy(im->name, s ? s + 1 : path, sizeof im->name - 1);
    im->name[sizeof im->name - 1] = 0;
}

/* ================================================================= */
/*  Rows of colour in, the 1-bit picture (and the colours) out        */
/* ================================================================= */

typedef struct {
    IMG   *im;
    long   w, h, y;
    short *e0, *e1;              /* error diffusion: this row and the next */
    int    colourful;            /* a pixel that isn't black or white      */
} SINK;

static int sink_begin(SINK *s, IMG *im, long w, long h, int colour)
{
    memset(s, 0, sizeof *s);
    if (w < 1 || h < 1 || w > 4000 || h > 4000) return 0;
    s->im = im; s->w = w; s->h = h;
    im->w = (short)w; im->h = (short)h; im->rb = (short)((w + 7) / 8);
    im->bits = (unsigned char *)calloc((size_t)im->rb, (size_t)h);
    s->e0 = (short *)calloc((size_t)w + 2, sizeof(short));
    s->e1 = (short *)calloc((size_t)w + 2, sizeof(short));
    if (!im->bits || !s->e0 || !s->e1) {
        free(s->e0); free(s->e1); img_free(im);
        return 0;
    }
    if (colour && w * h * 3 <= img_rgb_max) im->rgb = (unsigned char *)malloc((size_t)(w * h * 3));
    return 1;
}

/* row y, w pixels of R G B */
static void sink_row(SINK *s, const unsigned char *rgb)
{
    IMG *im = s->im;
    unsigned char *out = im->bits + s->y * im->rb;
    long x;
    short *t;
    if (s->y >= s->h) return;
    PROG(s->y, s->h);
    if (im->rgb) memcpy(im->rgb + s->y * s->w * 3, rgb, (size_t)(s->w * 3));
    memset(s->e1, 0, (size_t)(s->w + 2) * sizeof(short));
    for (x = 0; x < s->w; x++) {
        const unsigned char *c = rgb + x * 3;
        int lum = (c[0] * 30 + c[1] * 59 + c[2] * 11) / 100, v, o, err;
        if (!s->colourful && !((c[0] == 0 || c[0] == 255) && c[0] == c[1] && c[1] == c[2])) s->colourful = 1;
        v = lum + s->e0[x + 1] / 16;
        o = v < 128 ? 0 : 255;
        err = v - o;
        if (!o) out[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
        s->e0[x + 2] += (short)(err * 7);
        s->e1[x] += (short)(err * 3);
        s->e1[x + 1] += (short)(err * 5);
        s->e1[x + 2] += (short)err;
    }
    t = s->e0; s->e0 = s->e1; s->e1 = t;
    s->y++;
}

static int sink_end(SINK *s, int ok)
{
    free(s->e0); free(s->e1);
    s->e0 = s->e1 = NULL;
    if (!ok || s->y == 0) { img_free(s->im); return 0; }
    while (s->y < s->h) {                       /* a short file: the rest white */
        if (s->im->rgb) memset(s->im->rgb + s->y * s->w * 3, 255, (size_t)(s->w * 3));
        s->y++;
    }
    /* only black and white: the bitmap says it all */
    if (!s->colourful) { free(s->im->rgb); s->im->rgb = NULL; }
    return 1;
}

/* a picture made from rows of colour (for the front ends) */
int img_from_rgb(IMG *im, long w, long h, const unsigned char *rgb, long stride, int bpp)
{
    SINK s;
    unsigned char *row;
    long y, x;
    memset(im, 0, sizeof *im);
    if (!sink_begin(&s, im, w, h, 1)) return 0;
    row = (unsigned char *)malloc((size_t)w * 3);
    if (!row) return sink_end(&s, 0);
    for (y = 0; y < h; y++) {
        const unsigned char *p = rgb + y * stride;
        for (x = 0; x < w; x++) {
            const unsigned char *q = p + x * bpp;
            if (bpp == 4) {                 /* R G B A, on white */
                unsigned a = q[3];
                row[x * 3] = (unsigned char)((q[0] * a + 255 * (255 - a)) / 255);
                row[x * 3 + 1] = (unsigned char)((q[1] * a + 255 * (255 - a)) / 255);
                row[x * 3 + 2] = (unsigned char)((q[2] * a + 255 * (255 - a)) / 255);
            } else if (bpp == 1) row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = q[0];
            else { row[x * 3] = q[0]; row[x * 3 + 1] = q[1]; row[x * 3 + 2] = q[2]; }
        }
        sink_row(&s, row);
    }
    free(row);
    return sink_end(&s, 1);
}

/* ================================================================= */
/*  GEM IMG                                                           */
/* ================================================================= */

/* the VDI's first 16 colours, for pictures without a palette */
static const unsigned char vdi16[16][3] = {
    { 255, 255, 255 }, { 0, 0, 0 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 0, 255, 255 },
    { 255, 255, 0 }, { 255, 0, 255 }, { 192, 192, 192 }, { 128, 128, 128 }, { 128, 0, 0 }, { 0, 128, 0 },
    { 0, 0, 128 }, { 0, 128, 128 }, { 128, 128, 0 }, { 128, 0, 128 }
};

static int gem_img(const unsigned char *b, long n, IMG *im)
{
    unsigned hlen, nplanes, patlen, w, h, i;
    long bpb, bpl, p, y, x, k, rep;
    unsigned char *line = NULL, *prev = NULL, *rgb = NULL;
    unsigned char pal[256][3];
    SINK s;
    int ok = 1, chunky;

    if (n < 16) return 0;
    hlen = rd16(b + 2); nplanes = rd16(b + 4); patlen = rd16(b + 6);
    w = rd16(b + 12); h = rd16(b + 14);
    if (rd16(b) > 3 || hlen < 8 || (long)hlen * 2 > n || patlen < 1 || patlen > 16 || !w || !h) return 0;
    chunky = nplanes == 16 || nplanes == 24 || nplanes == 32;
    if (!chunky && (nplanes < 1 || nplanes > 8)) return 0;

    /* the palette: XIMG's, else the VDI colours or a grey ramp */
    for (i = 0; i < 256; i++) {
        unsigned nc = nplanes <= 8 ? 1u << nplanes : 0, g;
        if (nplanes == 1) g = i ? 0 : 255;
        else if (nplanes <= 4 && i < 16) { pal[i][0] = vdi16[i][0]; pal[i][1] = vdi16[i][1]; pal[i][2] = vdi16[i][2]; continue; }
        else g = nc > 1 ? 255 - (i * 255) / (nc - 1) : 255;
        pal[i][0] = pal[i][1] = pal[i][2] = (unsigned char)g;
    }
    if (!chunky && hlen >= 11 && !memcmp(b + 16, "XIMG", 4) && rd16(b + 20) == 0) {
        unsigned nc = 1u << nplanes;
        for (i = 0; i < nc && 22 + i * 6 + 6 <= hlen * 2u; i++) {
            const unsigned char *c = b + 22 + i * 6;
            int ch;
            for (ch = 0; ch < 3; ch++) {
                long v = (long)rd16(c + 2 * ch) * 255 / 1000;
                pal[i][ch] = (unsigned char)(v > 255 ? 255 : v);
            }
        }
    }

    bpb = ((long)w + 7) / 8;
    if (nplanes == 24) bpb = (bpb + 1) & ~1L;
    bpl = bpb * nplanes;
    line = (unsigned char *)calloc((size_t)bpl, 1);
    prev = (unsigned char *)calloc((size_t)bpl, 1);
    rgb = (unsigned char *)malloc((size_t)w * 3);
    if (!line || !prev || !rgb || !sink_begin(&s, im, w, h, nplanes > 1)) { free(line); free(prev); free(rgb); return 0; }

    p = (long)hlen * 2;
    y = 0;
    while (y < (long)h && p < n) {
        rep = 1;
        x = 0;
        if (p + 3 < n && b[p] == 0 && b[p + 1] == 0 && b[p + 2] == 0xFF) { rep = b[p + 3]; p += 4; if (!rep) rep = 1; }
        while (x < bpl && p < n) {
            unsigned c = b[p++];
            if (c == 0) {
                unsigned cnt;
                if (p >= n) break;
                cnt = b[p++];
                if (cnt == 0) {                         /* 00 00 n: from the line above */
                    unsigned m = p < n ? b[p++] + 1u : 1u;
                    for (k = 0; k < (long)m && x < bpl; k++, x++) line[x] = prev[x];
                    continue;
                }
                for (k = 0; k < (long)cnt; k++)
                    for (i = 0; i < patlen && x < bpl; i++) line[x++] = p + (long)i < n ? b[p + i] : 0;
                p += patlen;
            } else if (c == 0x80) {
                unsigned cnt = p < n ? b[p++] : 0;
                if (cnt == 0) cnt = 256;
                for (k = 0; k < (long)cnt && x < bpl && p < n; k++) line[x++] = b[p++];
            } else {
                unsigned cnt = c & 0x7F;
                for (k = 0; k < (long)cnt && x < bpl; k++) line[x++] = (c & 0x80) ? 0xFF : 0x00;
            }
        }
        /* the line in colour */
        for (k = 0; k < (long)w; k++) {
            unsigned char *o = rgb + k * 3;
            if (nplanes == 16) {
                unsigned v = rd16(line + k * 2);
                o[0] = (unsigned char)(((v >> 11) & 31) * 255 / 31);
                o[1] = (unsigned char)(((v >> 5) & 63) * 255 / 63);
                o[2] = (unsigned char)((v & 31) * 255 / 31);
            } else if (nplanes == 24) { o[0] = line[k * 3]; o[1] = line[k * 3 + 1]; o[2] = line[k * 3 + 2]; }
            else if (nplanes == 32) { o[0] = line[k * 4 + 1]; o[1] = line[k * 4 + 2]; o[2] = line[k * 4 + 3]; }
            else {
                unsigned idx = 0, bit = 0x80 >> (k & 7), pl;
                for (pl = 0; pl < nplanes; pl++) if (line[pl * bpb + (k >> 3)] & bit) idx |= 1u << pl;
                o[0] = pal[idx][0]; o[1] = pal[idx][1]; o[2] = pal[idx][2];
            }
        }
        for (k = 0; k < rep && y < (long)h; k++, y++) sink_row(&s, rgb);
        memcpy(prev, line, (size_t)bpl);
        memset(line, 0, (size_t)bpl);
    }
    free(line); free(prev); free(rgb);
    return sink_end(&s, ok);
}

/* ================================================================= */
/*  Inflate (RFC 1951), for PNG                                       */
/* ================================================================= */

typedef struct {
    const unsigned char *in; long inlen, inpos;
    unsigned long bitbuf; int bitcnt;
    unsigned char *out; long outlen, outpos;
} INF;

typedef struct { short count[16], symbol[288]; } HUFF;

static int inf_bits(INF *s, int need)
{
    unsigned long v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) return -1;
        v |= (unsigned long)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1UL << need) - 1));
}

static int inf_decode(INF *s, const HUFF *h)
{
    int code = 0, first = 0, index = 0, len, count;
    for (len = 1; len < 16; len++) {
        int b = inf_bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int inf_build(HUFF *h, const short *length, int n)
{
    short offs[16];
    int sym, len, left;
    for (len = 0; len < 16; len++) h->count[len] = 0;
    for (sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len < 16; len++) { left <<= 1; left -= h->count[len]; if (left < 0) return left; }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++) if (length[sym]) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int inf_codes(INF *s, const HUFF *lc, const HUFF *dc)
{
    for (;;) {
        int sym = inf_decode(s, lc);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->outpos >= s->outlen) return -2;
            s->out[s->outpos++] = (unsigned char)sym;
        } else if (sym == 256) return 0;
        else {
            long len, dist;
            int e;
            sym -= 257;
            if (sym >= 29) return -1;
            e = inf_bits(s, lext[sym]);
            if (e < 0) return -1;
            len = lbase[sym] + e;
            sym = inf_decode(s, dc);
            if (sym < 0 || sym >= 30) return -1;
            e = inf_bits(s, dext[sym]);
            if (e < 0) return -1;
            dist = dbase[sym] + e;
            if (dist > s->outpos || s->outpos + len > s->outlen) return -2;
            while (len--) { s->out[s->outpos] = s->out[s->outpos - dist]; s->outpos++; }
        }
    }
}

static int inflate_all(INF *s)
{
    static HUFF lc, dc;
    static short lens[320];
    int last, type, i;
    do {
        last = inf_bits(s, 1);
        type = inf_bits(s, 2);
        if (last < 0 || type < 0) return -1;
        if (type == 0) {
            unsigned len;
            s->bitbuf = 0; s->bitcnt = 0;
            if (s->inpos + 4 > s->inlen) return -1;
            len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
            s->inpos += 4;
            if (s->inpos + (long)len > s->inlen || s->outpos + (long)len > s->outlen) return -1;
            memcpy(s->out + s->outpos, s->in + s->inpos, len);
            s->inpos += len; s->outpos += len;
        } else if (type == 1) {
            for (i = 0; i < 144; i++) lens[i] = 8;
            for (; i < 256; i++) lens[i] = 9;
            for (; i < 280; i++) lens[i] = 7;
            for (; i < 288; i++) lens[i] = 8;
            inf_build(&lc, lens, 288);
            for (i = 0; i < 30; i++) lens[i] = 5;
            inf_build(&dc, lens, 30);
            if (inf_codes(s, &lc, &dc)) return -1;
        } else if (type == 2) {
            static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            int nlen = inf_bits(s, 5) + 257, ndist = inf_bits(s, 5) + 1, ncode = inf_bits(s, 4) + 4, idx;
            if (nlen > 286 || ndist > 30 || ncode < 4) return -1;
            for (i = 0; i < 19; i++) lens[order[i]] = 0;
            for (i = 0; i < ncode; i++) { int v = inf_bits(s, 3); if (v < 0) return -1; lens[order[i]] = (short)v; }
            if (inf_build(&lc, lens, 19) != 0) return -1;
            for (idx = 0; idx < nlen + ndist;) {
                int sym = inf_decode(s, &lc), len = 0, rep;
                if (sym < 0) return -1;
                if (sym < 16) { lens[idx++] = (short)sym; continue; }
                if (sym == 16) { if (!idx) return -1; len = lens[idx - 1]; rep = 3 + inf_bits(s, 2); }
                else if (sym == 17) rep = 3 + inf_bits(s, 3);
                else rep = 11 + inf_bits(s, 7);
                if (idx + rep > nlen + ndist) return -1;
                while (rep--) lens[idx++] = (short)len;
            }
            if (lens[256] == 0) return -1;
            if (inf_build(&lc, lens, nlen) < 0 && nlen - lc.count[0] != 1) return -1;
            if (inf_build(&dc, lens + nlen, ndist) < 0 && ndist - dc.count[0] != 1) return -1;
            if (inf_codes(s, &lc, &dc)) return -1;
        } else return -1;
    } while (!last);
    return 0;
}

/* ================================================================= */
/*  PNG                                                               */
/* ================================================================= */

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

/* undo the filter of one row (cur, of rb bytes; prior: the row above or NULL) */
static int unfilter(unsigned char *cur, const unsigned char *prior, long rb, int bpp, int type)
{
    long i;
    for (i = 0; i < rb; i++) {
        int a = i >= bpp ? cur[i - bpp] : 0, b = prior ? prior[i] : 0, c = (prior && i >= bpp) ? prior[i - bpp] : 0;
        switch (type) {
        case 0: break;
        case 1: cur[i] = (unsigned char)(cur[i] + a); break;
        case 2: cur[i] = (unsigned char)(cur[i] + b); break;
        case 3: cur[i] = (unsigned char)(cur[i] + ((a + b) >> 1)); break;
        case 4: cur[i] = (unsigned char)(cur[i] + paeth(a, b, c)); break;
        default: return 0;
        }
    }
    return 1;
}

typedef struct {
    int depth, ctype, chans, npal, trns_grey, trns[3], has_trns;
    unsigned char pal[256][3], alpha[256];
} PNGINFO;

/* one unfiltered row of w pixels into R G B (on white) */
static void png_pixels(const PNGINFO *P, const unsigned char *s, long w, unsigned char *o)
{
    long x;
    int d = P->depth;
    for (x = 0; x < w; x++, o += 3) {
        unsigned v[4] = { 0, 0, 0, 255 }, k, maxv = (1u << (d > 8 ? 8 : d)) - 1;
        if (d < 8) {
            long bitpos = x * d;
            v[0] = (s[bitpos >> 3] >> (8 - d - (bitpos & 7))) & maxv;
        } else
            for (k = 0; k < (unsigned)P->chans; k++)
                v[k] = d == 16 ? s[(x * P->chans + k) * 2] : s[x * P->chans + k];
        if (P->ctype == 3) {
            unsigned i = v[0];
            unsigned a = (int)i < P->npal ? P->alpha[i] : 255;
            unsigned char r = (int)i < P->npal ? P->pal[i][0] : 0, g = (int)i < P->npal ? P->pal[i][1] : 0,
                          b = (int)i < P->npal ? P->pal[i][2] : 0;
            o[0] = (unsigned char)((r * a + 255 * (255 - a)) / 255);
            o[1] = (unsigned char)((g * a + 255 * (255 - a)) / 255);
            o[2] = (unsigned char)((b * a + 255 * (255 - a)) / 255);
            continue;
        }
        if (P->ctype == 0 || P->ctype == 4) {
            unsigned g = d < 8 ? v[0] * 255 / maxv : v[0], a = P->ctype == 4 ? v[1] : 255;
            if (P->has_trns && P->ctype == 0) {
                unsigned raw = d < 8 ? v[0] : (d == 16 ? rd16(s + x * 2) : v[0]);
                if ((int)raw == P->trns_grey) a = 0;
            }
            g = (g * a + 255 * (255 - a)) / 255;
            o[0] = o[1] = o[2] = (unsigned char)g;
        } else {
            unsigned a = P->ctype == 6 ? v[3] : 255;
            if (P->has_trns && P->ctype == 2) {
                const unsigned char *q = d == 16 ? s + x * 6 : s + x * 3;
                unsigned r = d == 16 ? rd16(q) : q[0], g = d == 16 ? rd16(q + 2) : q[1], b = d == 16 ? rd16(q + 4) : q[2];
                if ((int)r == P->trns[0] && (int)g == P->trns[1] && (int)b == P->trns[2]) a = 0;
            }
            for (k = 0; k < 3; k++) o[k] = (unsigned char)((v[k] * a + 255 * (255 - a)) / 255);
        }
    }
}

static int png_img(const unsigned char *b, long n, IMG *im)
{
    PNGINFO P;
    long p = 8, w = 0, h = 0, zlen = 0, rawlen = 0, bpp, rb, y, i;
    int interlace = 0, ok = 0, pass;
    unsigned char *z = NULL, *raw = NULL, *rgb = NULL, *full = NULL;
    INF s;
    SINK sk;

    memset(&P, 0, sizeof P);
    for (i = 0; i < 256; i++) P.alpha[i] = 255;
    /* the chunks: IHDR, PLTE, tRNS and the IDAT data */
    for (i = 0; i < 2; i++) {
        long q = 8;
        while (q + 12 <= n) {
            long len = (long)rd32(b + q);
            const unsigned char *c = b + q + 8;
            if (len < 0 || q + 12 + len > n) break;
            if (i == 0) {
                if (!memcmp(b + q + 4, "IHDR", 4) && len >= 13) {
                    w = (long)rd32(c); h = (long)rd32(c + 4);
                    P.depth = c[8]; P.ctype = c[9]; interlace = c[12];
                } else if (!memcmp(b + q + 4, "PLTE", 4)) {
                    P.npal = (int)(len / 3 > 256 ? 256 : len / 3);
                    memcpy(P.pal, c, (size_t)P.npal * 3);
                } else if (!memcmp(b + q + 4, "tRNS", 4)) {
                    P.has_trns = 1;
                    if (P.ctype == 3) memcpy(P.alpha, c, (size_t)(len > 256 ? 256 : len));
                    else if (P.ctype == 0 && len >= 2) P.trns_grey = (int)rd16(c);
                    else if (P.ctype == 2 && len >= 6) { P.trns[0] = (int)rd16(c); P.trns[1] = (int)rd16(c + 2); P.trns[2] = (int)rd16(c + 4); }
                } else if (!memcmp(b + q + 4, "IDAT", 4)) zlen += len;
            } else if (!memcmp(b + q + 4, "IDAT", 4)) { memcpy(z + p, c, (size_t)len); p += len; }
            if (!memcmp(b + q + 4, "IEND", 4)) break;
            q += 12 + len;
        }
        if (i == 0) {
            if (w < 1 || h < 1 || w > 4000 || h > 4000 || !zlen || interlace > 1) return 0;
            if (P.ctype == 3 && !P.npal) return 0;
            z = (unsigned char *)malloc((size_t)zlen);
            if (!z) return 0;
            p = 0;
        }
    }
    P.chans = P.ctype == 0 ? 1 : P.ctype == 2 ? 3 : P.ctype == 3 ? 1 : P.ctype == 4 ? 2 : P.ctype == 6 ? 4 : 0;
    if (!P.chans || (P.depth != 1 && P.depth != 2 && P.depth != 4 && P.depth != 8 && P.depth != 16) ||
        (P.depth < 8 && P.chans > 1) || (P.depth == 16 && P.ctype == 3)) { free(z); return 0; }
    bpp = (P.chans * P.depth + 7) / 8;

    /* the size of the filtered data: every pass of an interlaced picture */
    {
        static const int xs[7] = { 0, 4, 0, 2, 0, 1, 0 }, ys[7] = { 0, 0, 4, 0, 2, 0, 1 },
                         dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
        int np = interlace ? 7 : 1;
        for (pass = 0; pass < np; pass++) {
            long pw = interlace ? (w - xs[pass] + dx[pass] - 1) / dx[pass] : w;
            long ph = interlace ? (h - ys[pass] + dy[pass] - 1) / dy[pass] : h;
            if (pw > 0 && ph > 0) rawlen += ph * (1 + (pw * P.chans * P.depth + 7) / 8);
        }
        raw = (unsigned char *)malloc((size_t)rawlen);
        rgb = (unsigned char *)malloc((size_t)w * 3);
        if (!raw || !rgb || zlen < 2) goto out;
        memset(&s, 0, sizeof s);
        s.in = z + 2; s.inlen = zlen - 2;            /* past the zlib header */
        s.out = raw; s.outlen = rawlen;
        if (inflate_all(&s) || s.outpos < rawlen) goto out;
        free(z); z = NULL;
        if (!sink_begin(&sk, im, w, h, 1)) goto out;
        if (!interlace) {
            rb = (w * P.chans * P.depth + 7) / 8;
            for (y = 0; y < h; y++) {
                unsigned char *row = raw + y * (rb + 1);
                if (!unfilter(row + 1, y ? row - rb : NULL, rb, (int)bpp, row[0])) break;
                png_pixels(&P, row + 1, w, rgb);
                sink_row(&sk, rgb);
            }
            ok = sink_end(&sk, y == h);
        } else {
            long at = 0;
            full = (unsigned char *)malloc((size_t)(w * h * 3));
            if (!full) { sink_end(&sk, 0); goto out; }
            memset(full, 255, (size_t)(w * h * 3));
            for (pass = 0; pass < 7; pass++) {
                long pw = (w - xs[pass] + dx[pass] - 1) / dx[pass], ph = (h - ys[pass] + dy[pass] - 1) / dy[pass], x;
                unsigned char *line = (unsigned char *)malloc((size_t)(pw > 0 ? pw : 1) * 3);
                if (!line) break;
                if (pw <= 0 || ph <= 0) { free(line); continue; }
                rb = (pw * P.chans * P.depth + 7) / 8;
                for (y = 0; y < ph; y++) {
                    unsigned char *row = raw + at + y * (rb + 1);
                    unfilter(row + 1, y ? row - rb : NULL, rb, (int)bpp, row[0]);
                    png_pixels(&P, row + 1, pw, line);
                    for (x = 0; x < pw; x++)
                        memcpy(full + ((ys[pass] + y * dy[pass]) * w + xs[pass] + x * dx[pass]) * 3, line + x * 3, 3);
                }
                at += ph * (rb + 1);
                free(line);
            }
            for (y = 0; y < h; y++) sink_row(&sk, full + y * w * 3);
            ok = sink_end(&sk, 1);
        }
    }
out:
    free(z); free(raw); free(rgb); free(full);
    return ok;
}

/* ================================================================= */
/*  IFF ILBM and PBM                                                  */
/* ================================================================= */

/* one row (of one plane), unpacked from the BODY; returns bytes used, -1 on error */
static long byterun(const unsigned char *s, long n, unsigned char *d, long rb, int comp)
{
    long i = 0, k = 0;
    if (!comp) { if (n < rb) return -1; memcpy(d, s, (size_t)rb); return rb; }
    while (k < rb) {
        int c;
        if (i >= n) return -1;
        c = (signed char)s[i++];
        if (c >= 0) {
            long m = c + 1;
            if (i + m > n) return -1;
            if (k + m > rb) m = rb - k;
            memcpy(d + k, s + i, (size_t)m);
            i += c + 1; k += m;
        } else if (c != -128) {
            long m = -c + 1;
            if (i >= n) return -1;
            if (k + m > rb) m = rb - k;
            memset(d + k, s[i++], (size_t)m);
            k += m;
        }
    }
    return i;
}

static int iff_img(const unsigned char *buf, long n, IMG *im)
{
    long p, body = -1, bodylen = 0, camg = 0;
    unsigned w = 0, h = 0, planes = 0, comp = 0, masking = 0, i;
    unsigned char cmap[256][3], *row = NULL, *rgb = NULL;
    int ncol = 0, ham, ehb, pbm = !memcmp(buf + 8, "PBM ", 4), ok = 0;
    SINK s;

    for (p = 12; p + 8 <= n;) {
        long sz = (long)rd32(buf + p + 4);
        const unsigned char *c = buf + p + 8;
        if (sz < 0 || p + 8 + sz > n) sz = n - p - 8;
        if (!memcmp(buf + p, "BMHD", 4) && sz >= 20) {
            w = rd16(c); h = rd16(c + 2); planes = c[8]; masking = c[9]; comp = c[10];
        } else if (!memcmp(buf + p, "CMAP", 4)) {
            ncol = (int)(sz / 3);
            if (ncol > 256) ncol = 256;
            memcpy(cmap, c, (size_t)ncol * 3);
        } else if (!memcmp(buf + p, "CAMG", 4) && sz >= 4) camg = (long)rd32(c);
        else if (!memcmp(buf + p, "BODY", 4)) { body = p + 8; bodylen = sz; }
        p += 8 + sz + (sz & 1);
    }
    if (!w || !h || !planes || body < 0 || comp > 1) return 0;
    if (pbm ? planes != 8 : (planes != 24 && planes > 8)) return 0;
    ham = !pbm && (camg & 0x800) != 0 && (planes == 6 || planes == 8);
    ehb = !pbm && (camg & 0x80) != 0 && planes == 6;
    if (!ncol) {                                     /* no colour map: a grey ramp */
        ncol = planes <= 8 ? 1 << planes : 0;
        for (i = 0; i < (unsigned)ncol; i++)
            cmap[i][0] = cmap[i][1] = cmap[i][2] = (unsigned char)(255 - i * 255 / (ncol - 1 ? ncol - 1 : 1));
    }
    if (ehb && ncol <= 32) {
        for (i = 0; i < 32; i++) { cmap[32 + i][0] = cmap[i][0] / 2; cmap[32 + i][1] = cmap[i][1] / 2; cmap[32 + i][2] = cmap[i][2] / 2; }
        ncol = 64;
    }
    {
        long rb = pbm ? (w + 1) & ~1L : ((long)w + 15) / 16 * 2, y, x, k, used;
        long nplanes = pbm ? 1 : planes + (masking == 1 ? 1 : 0);
        const unsigned char *src = buf + body;
        long left = bodylen;
        row = (unsigned char *)malloc((size_t)(rb * nplanes));
        rgb = (unsigned char *)malloc((size_t)w * 3);
        if (!row || !rgb || !sink_begin(&s, im, w, h, 1)) goto out;
        for (y = 0; y < (long)h; y++) {
            unsigned r = 0, g = 0, b = 0;
            for (k = 0; k < nplanes; k++) {
                used = byterun(src, left, row + k * rb, rb, (int)comp);
                if (used < 0) break;
                src += used; left -= used;
            }
            if (k < nplanes) break;
            for (x = 0; x < (long)w; x++) {
                unsigned v = 0, byte = (unsigned)(x >> 3), bit = 0x80 >> (x & 7);
                if (pbm) v = row[x];
                else for (k = 0; k < (long)planes; k++) if (row[k * rb + byte] & bit) v |= 1u << k;
                if (planes == 24) { r = v & 255; g = (v >> 8) & 255; b = (v >> 16) & 255; }
                else if (ham) {
                    int hb = planes == 8 ? 6 : 4, ctl = (int)(v >> hb), val = (int)(v & ((1 << hb) - 1));
                    int sc = planes == 8 ? 2 : 4;
                    unsigned e = (unsigned)(val << sc) | (unsigned)(val >> (hb - sc > 0 ? hb - sc : 0));
                    if (ctl == 0) { if (val < ncol) { r = cmap[val][0]; g = cmap[val][1]; b = cmap[val][2]; } }
                    else if (ctl == 1) b = e & 255;
                    else if (ctl == 2) r = e & 255;
                    else g = e & 255;
                } else if ((int)v < ncol) { r = cmap[v][0]; g = cmap[v][1]; b = cmap[v][2]; }
                else r = g = b = 0;
                rgb[x * 3] = (unsigned char)r; rgb[x * 3 + 1] = (unsigned char)g; rgb[x * 3 + 2] = (unsigned char)b;
            }
            sink_row(&s, rgb);
        }
        ok = sink_end(&s, y > 0);
    }
out:
    free(row); free(rgb);
    return ok;
}

/* ================================================================= */
/*  Any picture                                                       */
/* ================================================================= */

int img_load(const char *path, IMG *im)
{
    long n;
    unsigned char *b = load_file(path, &n);
    int ok = 0;
    memset(im, 0, sizeof *im);
    if (!b) return 0;
    if (n >= 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) ok = png_img(b, n, im);
    else if (n >= 12 && !memcmp(b, "FORM", 4) && (!memcmp(b + 8, "ILBM", 4) || !memcmp(b + 8, "PBM ", 4))) ok = iff_img(b, n, im);
    else ok = gem_img(b, n, im);
    free(b);
    if (ok) set_name(im, path);
    return ok;
}

/* is this a picture EdWord reads? (by its first bytes) */
int img_detect(const unsigned char *b, long n)
{
    if (n >= 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) return 1;
    if (n >= 12 && !memcmp(b, "FORM", 4) && (!memcmp(b + 8, "ILBM", 4) || !memcmp(b + 8, "PBM ", 4))) return 1;
    if (n >= 16 && rd16(b) <= 3 && rd16(b + 2) >= 8 && rd16(b + 6) >= 1 && rd16(b + 6) <= 16 && rd16(b + 12) && rd16(b + 14)) {
        unsigned pl = rd16(b + 4);
        return (pl >= 1 && pl <= 8) || pl == 16 || pl == 24 || pl == 32;
    }
    return 0;
}

/* ================================================================= */
/*  Writing GEM IMG (the clipboard's SCRAP.IMG)                       */
/* ================================================================= */

static void put16(FILE *f, unsigned v) { fputc((int)(v >> 8) & 255, f); fputc((int)v & 255, f); }

/* one line of bytes as IMG records: solid runs and literal runs */
static void img_line(FILE *f, const unsigned char *s, long n)
{
    long i = 0;
    while (i < n) {
        long r = i;
        if (s[i] == 0 || s[i] == 0xFF) {
            while (r < n && s[r] == s[i] && r - i < 127) r++;
            if (r - i >= 2 || r == n) { fputc((int)((s[i] ? 0x80 : 0) | (r - i)), f); i = r; continue; }
        }
        /* literal bytes up to the next run of solid bytes */
        r = i;
        while (r < n && r - i < 255) {
            if ((s[r] == 0 || s[r] == 0xFF) && r + 2 < n && s[r + 1] == s[r] && s[r + 2] == s[r]) break;
            r++;
        }
        if (r == i) r = i + 1;
        fputc(0x80, f);
        fputc((int)(r - i), f);
        fwrite(s + i, 1, (size_t)(r - i), f);
        i = r;
    }
}

/* the picture, in colour (24 planes) when it has its colours, else 1-bit */
int img_save(const IMG *im, const char *path)
{
    FILE *f = fopen(path, "wb");
    long y, bpl;
    unsigned char *line;
    int colour = im->rgb != NULL, err;
    if (!f) return 0;
    put16(f, 1); put16(f, 8); put16(f, colour ? 24 : 1); put16(f, 2);
    put16(f, 85); put16(f, 85);                   /* 0.085 mm pixels: 300 dpi */
    put16(f, (unsigned)im->w); put16(f, (unsigned)im->h);
    bpl = colour ? (((long)im->w + 7) / 8 + 1) / 2 * 2 * 24 : im->rb;
    line = (unsigned char *)calloc((size_t)bpl, 1);
    if (!line) { fclose(f); return 0; }
    for (y = 0; y < im->h; y++) {
        if (colour) {
            memset(line, 255, (size_t)bpl);
            memcpy(line, im->rgb + y * im->w * 3, (size_t)im->w * 3);
        } else memcpy(line, im->bits + y * im->rb, (size_t)im->rb);
        img_line(f, line, bpl);
    }
    free(line);
    err = ferror(f);
    fclose(f);
    return !err;
}

/* ================================================================= */
/*  Scaling                                                           */
/* ================================================================= */

/* a copy scaled to w x h (nearest pixel), 1-bit only */
int img_scale(const IMG *im, long w, long h, IMG *out)
{
    long x, y, rb;
    memset(out, 0, sizeof *out);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 4000 || h > 4000) return 0;
    rb = (w + 7) / 8;
    out->bits = (unsigned char *)calloc(rb * h, 1);
    if (!out->bits) return 0;
    out->w = (short)w; out->h = (short)h; out->rb = (short)rb;
    for (y = 0; y < h; y++) {
        const unsigned char *src = im->bits + (y * im->h / h) * im->rb;
        unsigned char *dst = out->bits + y * rb;
        for (x = 0; x < w; x++) {
            long sx = x * im->w / w;
            if (src[sx >> 3] & (0x80 >> (sx & 7))) dst[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
        }
    }
    return 1;
}

/* the colours scaled to w x h (averaging when smaller), malloc'ed RGB */
unsigned char *img_scale_rgb(const IMG *im, long w, long h)
{
    unsigned char *o;
    long x, y;
    if (!im->rgb || w < 1 || h < 1 || w > 4000 || h > 4000) return NULL;
    o = (unsigned char *)malloc((size_t)(w * h * 3));
    if (!o) return NULL;
    for (y = 0; y < h; y++) {
        long sy0 = y * im->h / h, sy1 = (y + 1) * im->h / h;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (x = 0; x < w; x++) {
            long sx0 = x * im->w / w, sx1 = (x + 1) * im->w / w, sx, sy, n = 0, r = 0, g = 0, b = 0;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            /* at most a 4 x 4 sample of the source */
            for (sy = sy0; sy < sy1; sy += (sy1 - sy0 + 3) / 4)
                for (sx = sx0; sx < sx1; sx += (sx1 - sx0 + 3) / 4) {
                    const unsigned char *p = im->rgb + (sy * im->w + sx) * 3;
                    r += p[0]; g += p[1]; b += p[2]; n++;
                }
            o[(y * w + x) * 3] = (unsigned char)(r / n);
            o[(y * w + x) * 3 + 1] = (unsigned char)(g / n);
            o[(y * w + x) * 3 + 2] = (unsigned char)(b / n);
        }
    }
    return o;
}
