/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amispd.c - Speedo outline fonts (the .SPD fonts of SpeedoGDOS and XGDOS)
 *
 * The fonts come from the Speedo drawer next to EdWord (the eight
 * Bitstream fonts XGDOS has are there) and from SPEEDO: when it is
 * assigned. They are listed with the Amiga fonts in FONTS:, and drawn
 * with XGDOS's own Speedo decoder and scan converter (spd/, GPL 2 or MIT).
 *
 * A font is read into memory the first time it is used. Characters are
 * made at the size wanted (pixels per em across and down, so text keeps
 * its shape on screens with tall pixels), hinted to the pixel grid, and
 * kept in a cache in fast memory. A run of text is put together there,
 * a byte at a time, and then drawn in one go with BltTemplate through a
 * band of chip memory: the blitter draws only from chip memory.
 *
 * Bold is drawn thicker by a pixel or more, italic slanted 12 degrees.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiui.h"
#include "spd/spd.h"
#include "spd/raster.h"
#include "spd/charmap.h"

#define MAXSPD 48
typedef struct {
    char   path[200];
    char   name[34];
    long   fsize;
    u8    *data;                 /* the whole file, NULL until used */
    SpdFont f;
    u16   *idmap;
    int    bad;                  /* not readable or not a Speedo font */
    s16    ymin, ymax;
    int    qidx;                 /* '?' for characters the font hasn't */
} SFONT;
static SFONT sf[MAXSPD];
static int nsf;

/* ---- finding the fonts ---------------------------------------------------- */

static int cmp_sf(const void *a, const void *b) { return strcmp(((const SFONT *)a)->name, ((const SFONT *)b)->name); }

static void scan_dir(const char *dir)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR ow = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;                /* no "insert volume" requester */
    lock = Lock((CONST_STRPTR)dir, ACCESS_READ);
    me->pr_WindowPtr = ow;
    if (!lock) return;
    fib = (struct FileInfoBlock *)AllocMem(sizeof *fib, MEMF_CLEAR);
    if (fib && Examine(lock, fib)) {
        while (ExNext(lock, fib) && nsf < MAXSPD) {
            const char *n = (const char *)fib->fib_FileName;
            int l = (int)strlen(n), k;
            u8 hdr[SPD_HDR_MIN];
            SpdInfo info;
            FILE *f;
            SFONT *s = &sf[nsf];
            if (fib->fib_DirEntryType >= 0 || l < 5) continue;
            if (!(n[l - 4] == '.' && (n[l - 3] | 32) == 's' && (n[l - 2] | 32) == 'p' && (n[l - 1] | 32) == 'd')) continue;
            memset(s, 0, sizeof *s);
            strcpy(s->path, dir);
            k = (int)strlen(s->path);
            if (k && s->path[k - 1] != ':' && s->path[k - 1] != '/') strcat(s->path, "/");
            strncat(s->path, n, sizeof s->path - strlen(s->path) - 1);
            f = fopen(s->path, "rb");
            if (!f) continue;
            k = (int)fread(hdr, 1, sizeof hdr, f);
            fclose(f);
            if (k < SPD_HDR_MIN || !spd_probe(hdr, k, &info)) continue;
            /* the same font twice (both drawers): the first one */
            strncpy(s->name, info.name[0] ? info.name : info.sname, 33);
            s->name[33] = 0;
            for (k = 0; k < nsf; k++) if (!strcmp(sf[k].name, s->name)) break;
            if (k < nsf) continue;
            s->fsize = (long)fib->fib_Size;
            nsf++;
        }
    }
    if (fib) FreeMem(fib, sizeof *fib);
    UnLock(lock);
}

/* the fonts, by name; add(name, k) for each */
void spd_fonts_scan(void (*add)(const char *name, int k))
{
    char d[300];
    int i;
    nsf = 0;
    sprintf(d, "%sSpeedo", progdir);
    scan_dir(d);
    scan_dir("SPEEDO:");
    qsort(sf, (size_t)nsf, sizeof sf[0], cmp_sf);
    for (i = 0; i < nsf; i++) add(sf[i].name, i);
}

/* the font read into memory (fast memory where there is some) */
static SFONT *use(int k)
{
    SFONT *s;
    FILE *f;
    long n;
    if (k < 0 || k >= nsf) return NULL;
    s = &sf[k];
    if (s->data) return s;
    if (s->bad) return NULL;
    s->data = (u8 *)malloc((size_t)s->fsize);
    f = s->data ? fopen(s->path, "rb") : NULL;
    if (!f) { free(s->data); s->data = NULL; s->bad = 1; return NULL; }
    n = (long)fread(s->data, 1, (size_t)s->fsize, f);
    fclose(f);
    if (n <= 0 || !spd_open(&s->f, s->data, n)) { free(s->data); s->data = NULL; s->bad = 1; return NULL; }
    s->idmap = (u16 *)malloc((size_t)s->f.nchars * 4);
    if (s->idmap) spd_build_idmap(&s->f, s->idmap);
    s->ymin = s->f.ymin;
    s->ymax = s->f.ymax;
    s->qidx = spd_index_of_id(&s->f, '?');
    edlog("speedo: %s read, %d characters", s->name, s->f.nchars);
    return s;
}

/* the glyph of ST character c, the font's '?' if it hasn't one */
static int glyph_of(SFONT *s, unsigned char c)
{
    u16 id = xg_atari_to_bcid[c];
    int i = id ? spd_index_of_id(&s->f, id) : -1;
    return i >= 0 ? i : s->qidx;
}

/* pixels per em (x, y) to 16.16 pixels per outline unit */
static fix per_oru(SFONT *s, long ppem) { return (fix)(((long)ppem << 16) / s->f.orupm); }

static int bold_px(long ppy) { return ppy >= 40 ? (int)(ppy / 20) : 1; }

/* ---- metrics -------------------------------------------------------------- */

void spdf_metrics(int k, long ppx, long ppy, int attr, short *w, short *asc, short *desc)
{
    SFONT *s = use(k);
    int c, b = (attr & A_BOLD) ? bold_px(ppy) : 0;
    if (!s) {
        for (c = 0; c < 256; c++) w[c] = (short)(c < 32 ? 0 : ppx / 2);
        *asc = (short)(ppy * 3 / 4); *desc = (short)(ppy / 4);
        return;
    }
    for (c = 0; c < 256; c++) {
        long ow;
        if (c < 32) { w[c] = 0; continue; }
        ow = spd_char_width(&s->f, glyph_of(s, (unsigned char)c));
        if (ow < 0) ow = s->f.orupm / 2;
        w[c] = (short)((ow * ppx + s->f.orupm / 2) / s->f.orupm + b);
    }
    *asc = (short)(((long)s->ymax * ppy + s->f.orupm - 1) / s->f.orupm);
    *desc = (short)(((long)-s->ymin * ppy + s->f.orupm - 1) / s->f.orupm);
}

/* ---- the glyph cache ------------------------------------------------------ */

typedef struct GL {
    struct GL *next;
    short  font, idx, italic;
    long   ppx, ppy;
    short  left, top, w, h, rb;  /* pixels from the origin, y down          */
    long   lru, bytes;
    u8     bits[1];
} GL;
#define NBUCKET 256
#define BUDGET  (384L * 1024)    /* bytes of glyphs kept                     */
static GL  *bucket[NBUCKET];
static long used, tick;
static long pin = 0x7fffffffL;  /* glyphs used after this tick stay (a run in the making) */
static u8  *scratch;             /* the scan converter's memory              */
static long scratchsize;

static unsigned hashk(int f, int idx, long ppx, long ppy, int it)
{
    return (unsigned)(f * 31 + idx * 7 + ppx * 3 + ppy * 5 + it) & (NBUCKET - 1);
}

static int evict_one(void)
{
    GL **pp, **best = NULL;
    int b;
    for (b = 0; b < NBUCKET; b++)
        for (pp = &bucket[b]; *pp; pp = &(*pp)->next)
            if ((*pp)->lru <= pin && (!best || (*pp)->lru < (*best)->lru)) best = pp;
    if (!best) return 0;
    {
        GL *g = *best;
        *best = g->next;
        used -= g->bytes;
        free(g);
    }
    return 1;
}

static GL *glyph_aa(int k, int idx, long ppx, long ppy, int italic, unsigned h, Raster *r, int nseg,
                    int x0, int y0, int x1, int y1);

static int scratch_for(long nseg)
{
    long need = nseg * raster_seg_bytes() + 64;
    if (need <= scratchsize) return 1;
    free(scratch);
    scratch = (u8 *)malloc((size_t)need);
    scratchsize = scratch ? need : 0;
    return scratch != NULL;
}

/* a glyph made (or found) at ppx x ppy pixels per em; italic: bit 0
 * slanted, bit 1 (GL_AA) smoothed: made four times the size each way and
 * kept as a byte a pixel, 0 to 255 covered */
#define GL_AA 2
#define AA 4
static GL *glyph(int k, int idx, long ppx, long ppy, int italic)
{
    unsigned h = hashk(k, idx, ppx, ppy, italic);
    GL *g;
    SFONT *s = use(k);
    SpdXform xf;
    Raster r;
    int x0, y0, x1, y1, w, ht, rb, nseg;
    long need;
    if (!s || idx < 0) return NULL;
    for (g = bucket[h]; g; g = g->next)
        if (g->font == k && g->idx == idx && g->ppx == ppx && g->ppy == ppy && g->italic == italic) {
            g->lru = ++tick;
            return g;
        }
    memset(&xf, 0, sizeof xf);
    {
        int m = (italic & GL_AA) ? AA : 1;
        xf.sx = per_oru(s, ppx * m);
        xf.sy = per_oru(s, ppy * m);
        xf.hint = !(italic & GL_AA) && ppy <= 200;
    }
    xf.skew = (italic & 1) ? 13933 : 0;            /* tan 12 degrees, 16.16 */
    if (!scratch_for(1024)) return NULL;
    /* the scratch memory: segments (16 bytes each), then the fill tables */
    for (;;) {
        nseg = (int)((scratchsize - 64) / raster_seg_bytes());
        raster_init(&r, (s32 *)scratch, nseg, 0, 0);
        r.dropout = ppy <= 40 && !(italic & GL_AA);
        if (!spd_char_outline(&s->f, idx, &xf, &r.sink)) return NULL;
        if (!r.overflow) break;
        if (!scratch_for(r.need + 16)) return NULL;
    }
    if (!raster_bounds(&r, &x0, &y0, &x1, &y1)) { x0 = y0 = x1 = y1 = 0; }
    if (italic & GL_AA) return glyph_aa(k, idx, ppx, ppy, italic, h, &r, nseg, x0, y0, x1, y1);
    w = x1 - x0; ht = y1 - y0;
    if (w > 2000 || ht > 2000) return NULL;
    rb = (w + 7) >> 3;
    need = (long)sizeof(GL) + (long)rb * ht;
    while (used + need > BUDGET && evict_one()) ;
    g = (GL *)malloc((size_t)need);
    while (!g && evict_one()) g = (GL *)malloc((size_t)need);
    if (!g) return NULL;
    memset(g->bits, 0, (size_t)rb * ht + 1);
    if (ht && w) {
        long segb = ((long)nseg * 16 + 3) & ~3L;
        if (!raster_fill(&r, g->bits, rb, x0, y0, w, ht, scratch + segb, scratchsize - segb)) { free(g); return NULL; }
    }
    g->font = (short)k; g->idx = (short)idx; g->italic = (short)italic;
    g->ppx = ppx; g->ppy = ppy;
    g->left = (short)x0; g->top = (short)y0; g->w = (short)w; g->h = (short)ht; g->rb = (short)rb;
    g->bytes = need;
    g->lru = ++tick;
    g->next = bucket[h];
    bucket[h] = g;
    used += need;
    return g;
}

/* a smoothed glyph from the outline drawn AA times the size in r: each
 * pixel's coverage, counted from its AA x AA dots */
static GL *glyph_aa(int k, int idx, long ppx, long ppy, int italic, unsigned h, Raster *r, int nseg,
                    int x0, int y0, int x1, int y1)
{
    static u8 *big;
    static long bigsize;
    GL *g;
    long bw = x1 - x0, bh = y1 - y0, brb = (bw + 7) >> 3, need, i, j;
    int fx0 = x0 >= 0 ? x0 / AA : -((-x0 + AA - 1) / AA), fy0 = y0 >= 0 ? y0 / AA : -((-y0 + AA - 1) / AA);
    int fx1 = x1 >= 0 ? (x1 + AA - 1) / AA : -((-x1) / AA), fy1 = y1 >= 0 ? (y1 + AA - 1) / AA : -((-y1) / AA);
    int w = fx1 - fx0, ht = fy1 - fy0;
    if (bw > 4000 || bh > 4000) return NULL;
    need = (long)sizeof(GL) + (long)w * ht;
    while (used + need > BUDGET && evict_one()) ;
    g = (GL *)malloc((size_t)need);
    while (!g && evict_one()) g = (GL *)malloc((size_t)need);
    if (!g) return NULL;
    memset(g->bits, 0, (size_t)w * ht + 1);
    if (bw > 0 && bh > 0) {
        long segb = ((long)nseg * 16 + 3) & ~3L;
        if (brb * bh > bigsize) {
            free(big);
            bigsize = brb * bh;
            big = (u8 *)malloc((size_t)bigsize);
            if (!big) { bigsize = 0; free(g); return NULL; }
        }
        memset(big, 0, (size_t)(brb * bh));
        if (!raster_fill(r, big, (int)brb, x0, y0, (int)bw, (int)bh, scratch + segb, scratchsize - segb)) { free(g); return NULL; }
        /* count the dots of each pixel */
        {
            static u16 *acc;
            static long accn;
            if (w > accn) { free(acc); accn = w + 16; acc = (u16 *)malloc((size_t)accn * 2); if (!acc) { accn = 0; free(g); return NULL; } }
            for (j = 0; j < ht; j++) {
                long by;
                memset(acc, 0, (size_t)w * 2);
                for (by = (fy0 + j) * AA - y0; by < (fy0 + j + 1) * AA - y0; by++) {
                    const u8 *row;
                    if (by < 0 || by >= bh) continue;
                    row = big + by * brb;
                    for (i = 0; i < bw; i++)
                        if (row[i >> 3] & (0x80 >> (i & 7))) acc[(i + x0 - (long)fx0 * AA) / AA]++;
                }
                for (i = 0; i < w; i++) g->bits[j * w + i] = (u8)(acc[i] >= AA * AA ? 255 : acc[i] * 255 / (AA * AA));
            }
        }
    }
    g->font = (short)k; g->idx = (short)idx; g->italic = (short)italic;
    g->ppx = ppx; g->ppy = ppy;
    g->left = (short)fx0; g->top = (short)fy0; g->w = (short)w; g->h = (short)ht; g->rb = (short)w;
    g->bytes = need;
    g->lru = ++tick;
    g->next = bucket[h];
    bucket[h] = g;
    used += need;
    return g;
}

/* ---- drawing a run of text -------------------------------------------------- */

static u8   *run;                /* the run, put together in fast memory    */
static long  runsize;
static UBYTE *chip;              /* ... and a band of it for the blitter     */
static long  chipsize;

/* OR a glyph's bits into the run at (x, y), b more times a pixel to the right */
static void or_glyph(const GL *g, u8 *dst, long drb, long x, long y, int b)
{
    long r, k;
    int t;
    for (t = 0; t <= b; t++) {
        long xx = x + t, sh = xx & 7, bx = xx >> 3;
        for (r = 0; r < g->h; r++) {
            const u8 *s = g->bits + r * g->rb;
            u8 *d = dst + (y + r) * drb + bx;
            if (!sh) for (k = 0; k < g->rb; k++) d[k] |= s[k];
            else for (k = 0; k < g->rb; k++) {
                d[k] |= (u8)(s[k] >> sh);
                d[k + 1] |= (u8)(s[k] << (8 - sh));
            }
        }
    }
}

static short cw(long v) { return (short)(v < -16000 ? -16000 : v > 16000 ? 16000 : v); }

/* ST characters st[0..n) with their origins at window x xs[i] on baseline
 * base, ppx x ppy pixels per em, in pen */
void spdf_draw(int k, long ppx, long ppy, int attr, const unsigned char *st, const long *xs, int n, long base, int pen)
{
    static GL *gs[256];
    SFONT *s = use(k);
    long x0 = 0x7fffffffL, x1 = -0x7fffffffL, y0 = 0x7fffffffL, y1 = -0x7fffffffL, w, h, rb, i, cx;
    int b = (attr & A_BOLD) ? bold_px(ppy) : 0, it = (attr & A_ITALIC) != 0;
    if (!s || !grp || n <= 0) return;
    if (n > 256) n = 256;
    if (aa_text() && ppy <= 240) it |= GL_AA;    /* bigger is clean as it is */
    pin = tick;                                    /* the run's glyphs are kept while it's made */
    for (i = 0; i < n; i++) {
        GL *g = st[i] > 32 ? glyph(k, glyph_of(s, st[i]), ppx, ppy, it) : NULL;
        gs[i] = g;
        if (!g || !g->w || !g->h) continue;
        if (xs[i] + g->left < x0) x0 = xs[i] + g->left;
        if (xs[i] + g->left + g->w + b > x1) x1 = xs[i] + g->left + g->w + b;
        if (base + g->top < y0) y0 = base + g->top;
        if (base + g->top + g->h > y1) y1 = base + g->top + g->h;
    }
    if (x1 <= x0 || y1 <= y0) { pin = 0x7fffffffL; return; }
    w = x1 - x0;
    h = y1 - y0;
    if (it & GL_AA) {
        /* smoothed: each pixel's coverage, the most of the glyphs over it,
         * blended with what is on the screen there */
        long need = w * h + 1, r, c;
        int t;
        if (need > runsize) {
            free(run);
            runsize = need + 512;
            run = (u8 *)malloc((size_t)runsize);
            if (!run) { runsize = 0; pin = 0x7fffffffL; return; }
        }
        memset(run, 0, (size_t)need);
        for (i = 0; i < n; i++) {
            GL *g = gs[i];
            if (!g || !g->w || !g->h) continue;
            for (t = 0; t <= b; t++)
                for (r = 0; r < g->h; r++) {
                    const u8 *src = g->bits + r * g->w;
                    u8 *d = run + (base + g->top - y0 + r) * w + (xs[i] + g->left - x0 + t);
                    for (c = 0; c < g->w; c++) if (src[c] > d[c]) d[c] = src[c];
                }
        }
        pin = 0x7fffffffL;
        aa_put(run, w, h, x0, y0, pen);
        return;
    }
    rb = ((w + 15) >> 4) << 1;
    if (rb * h + 2 > runsize) {
        free(run);
        runsize = rb * h + 2 + 512;
        run = (u8 *)malloc((size_t)runsize);
        if (!run) { runsize = 0; pin = 0x7fffffffL; return; }
    }
    memset(run, 0, (size_t)(rb * h + 2));
    for (i = 0; i < n; i++) {
        GL *g = gs[i];
        if (g && g->w && g->h) or_glyph(g, run, rb, xs[i] + g->left - x0, base + g->top - y0, b);
    }
    pin = 0x7fffffffL;
    /* to the window through chip memory, a band of rows at a time */
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
    {
        long band = 4096 / rb, r;
        if (band < 1) band = 1;
        if (band > h) band = h;
        if (rb * band > chipsize) {
            if (chip) FreeMem(chip, (ULONG)chipsize);
            chipsize = rb * band;
            if (chipsize < 4096) chipsize = 4096;
            chip = (UBYTE *)AllocMem((ULONG)chipsize, MEMF_CHIP);
            if (!chip) { chipsize = 0; return; }
        }
        for (r = 0; r < h; r += band) {
            long nr = h - r < band ? h - r : band;
            CopyMem(run + r * rb, chip, (ULONG)(rb * nr));
            /* the blitter's widest is 1008 pixels: wider runs in pieces */
            for (cx = 0; cx < w; cx += 992) {
                long cw_ = w - cx < 992 ? w - cx : 992;
                BltTemplate((PLANEPTR)(chip + (cx >> 4) * 2), 0, (WORD)rb, grp, cw(x0 + cx), cw(y0 + r), (WORD)cw_, (WORD)nr);
            }
        }
    }
}

/* ---- glyphs for the PDF and the printer ------------------------------------- */

/* ST character c as a bitmap (rows of (w + 7) / 8 bytes, malloc'd), its
 * top left corner (ox, oy) from the origin; 0 if it can't be made */
int spdf_bitmap(int k, long ppx, long ppy, int attr, unsigned char c,
                short *w, short *h, short *ox, short *oy, unsigned char **bits)
{
    SFONT *s = use(k);
    GL *g;
    int b = (attr & A_BOLD) ? bold_px(ppy) : 0;
    long rb, r;
    *bits = NULL; *w = *h = *ox = *oy = 0;
    if (!s) return 0;
    if (c <= 32) return 1;
    g = glyph(k, glyph_of(s, c), ppx, ppy, (attr & A_ITALIC) != 0);
    if (!g) return 0;
    if (!g->w || !g->h) return 1;
    *w = (short)(g->w + b); *h = g->h; *ox = g->left; *oy = g->top;
    rb = (*w + 7) / 8;
    *bits = (unsigned char *)calloc((size_t)(rb * g->h + 1), 1);
    if (!*bits) return 0;
    if (b) or_glyph(g, *bits, rb, 0, 0, b);
    else for (r = 0; r < g->h; r++) memcpy(*bits + r * rb, g->bits + r * g->rb, (size_t)g->rb);
    return 1;
}

void spdf_free(void)
{
    int i;
    pin = 0x7fffffffL;
    while (evict_one()) ;
    free(scratch); scratch = NULL; scratchsize = 0;
    free(run); run = NULL; runsize = 0;
    if (chip) FreeMem(chip, (ULONG)chipsize);
    chip = NULL; chipsize = 0;
    for (i = 0; i < nsf; i++) {
        free(sf[i].data); sf[i].data = NULL;
        free(sf[i].idmap); sf[i].idmap = NULL;
    }
}
