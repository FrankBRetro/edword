/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amirend.c - Amiga fonts, text metrics, drawing a page with
 *             graphics.library, and the PDF export's character bitmaps
 *             (after gemrend.c)
 *
 * Fonts come from diskfont.library, which scales the bitmap fonts in
 * FONTS: (and outline fonts, with Workbench 2.04's Intellifont) to any
 * size. Without diskfont.library EdWord loads the bitmap fonts itself
 * and scales them the same way.
 *
 * A point size becomes a font height in pixels through the screen's
 * pixel size, so text is the size it will print at 100%. The document
 * view, the print preview and the PDF draw characters at their laid-out
 * positions, so lines break and justify the same everywhere.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/doshunks.h>
#include <graphics/gfxbase.h>
#include <diskfont/diskfont.h>
#include <graphics/gfxmacros.h>
#include <devices/printer.h>
#include <devices/prtbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiui.h"

extern struct Library *DiskfontBase;
extern struct GfxBase *GfxBase;

static char fontfile[MAX_FONTS][32];        /* "times.font" */

/* VDI-like pens for the text colours: black red green blue cyan magenta yellow grey */
int color_pen(int c)
{
    static const unsigned char p16[NCOLORS] = { P_BLACK, P_RED, P_GREEN, P_DBLUE, P_CYAN, P_MAGENTA, P_YELLOW, P_TGREY };
    static const unsigned char p8[NCOLORS] = { P_BLACK, P_RED, P_GREEN, P_DBLUE, P_CYAN, P_RED, P_BLACK, P_BLACK };
    if (!colour_on || c <= 0 || c >= NCOLORS) return P_BLACK;
    if (npens >= 16) return p16[c];
    if (npens >= 8) return p8[c];
    return P_BLACK;
}

/* ================================================================= */
/*  Fonts without diskfont.library: loaded and scaled here             */
/* ================================================================= */

typedef struct ownfont {
    struct TextFont *tf;
    BPTR   seg;                 /* loaded from disk, or 0: made by scaling   */
    ULONG  datasize;            /* scaled: the chip memory of the bitmap      */
    UBYTE *chipcopy; ULONG chipsize;
    char   name[32];
    struct ownfont *next;
} OWNFONT;
static OWNFONT *own;

/* a .font contents file: the sizes and where they are */
static int font_sizes(const char *file, UWORD *sizes, char names[][40], int max)
{
    char path[80];
    BPTR fh;
    UWORD hdr[2];
    int n = 0, i;
    sprintf(path, "FONTS:%s", file);
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return 0;
    if (Read(fh, hdr, 4) == 4 && (hdr[0] == 0x0f00 || hdr[0] == 0x0f02)) {
        for (i = 0; i < hdr[1] && n < max; i++) {
            struct { char name[256]; UWORD ysize; UBYTE style, flags; } fc;
            if (Read(fh, &fc, sizeof fc) != sizeof fc) break;
            fc.name[255] = 0;
            strncpy(names[n], fc.name, 39);
            names[n][39] = 0;
            sizes[n++] = fc.ysize;
        }
    }
    Close(fh);
    return n;
}

static struct TextFont *load_font_file(const char *name, const char *fontname)
{
    char path[80];
    BPTR seg;
    struct DiskFontHeader *dfh;
    struct TextFont *tf;
    OWNFONT *o;
    sprintf(path, "FONTS:%s", name);
    seg = LoadSeg((CONST_STRPTR)path);
    if (!seg) return NULL;
    dfh = (struct DiskFontHeader *)((UBYTE *)BADDR(seg) + 8);
    if (dfh->dfh_FileID != 0x0f80) { UnLoadSeg(seg); return NULL; }
    tf = &dfh->dfh_TF;
    o = (OWNFONT *)malloc(sizeof *o);
    if (!o) { UnLoadSeg(seg); return NULL; }
    memset(o, 0, sizeof *o);
    o->tf = tf;
    o->seg = seg;
    /* the name is the .font file's, as diskfont.library does it */
    strncpy(o->name, fontname, 31);
    tf->tf_Message.mn_Node.ln_Name = o->name;
    tf->tf_Accessors = 0;
    /* the blitter draws from chip memory only */
    if (!(TypeOfMem(tf->tf_CharData) & MEMF_CHIP)) {
        o->chipsize = (ULONG)tf->tf_Modulo * tf->tf_YSize;
        o->chipcopy = (UBYTE *)AllocMem(o->chipsize, MEMF_CHIP);
        if (o->chipcopy) { CopyMem(tf->tf_CharData, o->chipcopy, o->chipsize); tf->tf_CharData = o->chipcopy; }
    }
    AddFont(tf);
    o->next = own;
    own = o;
    return tf;
}

/* a copy of a font scaled to height ny, as diskfont.library would make */
static struct TextFont *scale_font(struct TextFont *s, int ny, const char *name_as)
{
    int oy = s->tf_YSize, n = s->tf_HiChar - s->tf_LoChar + 2, i, x, y;
    long xn, xd;
    ULONG *sl = (ULONG *)s->tf_CharLoc, *nl;
    WORD *ss = (WORD *)s->tf_CharSpace, *sk = (WORD *)s->tf_CharKern, *ns = NULL, *nk = NULL;
    long tw = 0, mod, off;
    struct TextFont *t;
    UBYTE *data, *sd = (UBYTE *)s->tf_CharData;
    OWNFONT *o;
    char *name;
    ULONG msize;

    /* widths follow the pixel shapes: the font's (tall dots for hires,
     * wide dots for lores interlaced, else square) and the screen's */
    xn = (long)ny * dev_pixh; xd = (long)oy * dev_pixw;
    if (s->tf_Flags & FPF_TALLDOT) xd *= 2;
    else if (s->tf_Flags & FPF_WIDEDOT) xn *= 2;
#define SX(v) (((long)(v) * xn + (xd / 2)) / xd)
    for (i = 0; i < n; i++) tw += SX(sl[i] & 0xFFFF);
    mod = (tw + 15) / 16 * 2;
    if (mod < 2) mod = 2;
    msize = sizeof(struct TextFont) + n * 4 + (ss ? n * 2 : 0) + (sk ? n * 2 : 0) + 40;
    t = (struct TextFont *)AllocMem(msize, MEMF_CLEAR);
    if (!t) return NULL;
    data = (UBYTE *)AllocMem((ULONG)(mod * ny), MEMF_CHIP | MEMF_CLEAR);
    if (!data) { FreeMem(t, msize); return NULL; }
    nl = (ULONG *)(t + 1);
    if (ss) ns = (WORD *)(nl + n);
    if (sk) nk = (WORD *)((UBYTE *)(nl + n) + (ss ? n * 2 : 0));
    name = (char *)((UBYTE *)(nl + n) + (ss ? n * 2 : 0) + (sk ? n * 2 : 0));
    strncpy(name, name_as, 39);
    off = 0;
    for (i = 0; i < n; i++) {
        long so = sl[i] >> 16, sw = sl[i] & 0xFFFF, nw = SX(sw);
        nl[i] = ((ULONG)off << 16) | (ULONG)nw;
        for (y = 0; y < ny; y++) {
            long sy = (long)y * oy / ny;
            for (x = 0; x < nw; x++) {
                long sx = so + (long)x * sw / nw, dx = off + x;
                if (sd[sy * s->tf_Modulo + (sx >> 3)] & (0x80 >> (sx & 7)))
                    data[y * mod + (dx >> 3)] |= (UBYTE)(0x80 >> (dx & 7));
            }
        }
        if (ns) ns[i] = (WORD)SX(ss[i]);
        if (nk) nk[i] = (WORD)(sk[i] >= 0 ? SX(sk[i]) : -SX(-sk[i]));
        off += nw;
    }
    t->tf_Message.mn_Node.ln_Name = name;
    t->tf_Message.mn_Node.ln_Type = NT_FONT;
    t->tf_YSize = (UWORD)ny;
    t->tf_Style = s->tf_Style;
    t->tf_Flags = (UBYTE)(s->tf_Flags & ~(FPF_DESIGNED | FPF_ROMFONT | FPF_DISKFONT | FPF_TALLDOT | FPF_WIDEDOT));
    t->tf_XSize = (UWORD)SX(s->tf_XSize);
#undef SX
    t->tf_Baseline = (UWORD)((s->tf_Baseline * ny + oy / 2) / oy);
    if (t->tf_Baseline >= ny) t->tf_Baseline = (UWORD)(ny - 1);
    t->tf_BoldSmear = s->tf_BoldSmear;
    t->tf_LoChar = s->tf_LoChar;
    t->tf_HiChar = s->tf_HiChar;
    t->tf_CharData = data;
    t->tf_Modulo = (UWORD)mod;
    t->tf_CharLoc = nl;
    t->tf_CharSpace = ns;
    t->tf_CharKern = nk;
    o = (OWNFONT *)malloc(sizeof *o);
    if (!o) { FreeMem(data, (ULONG)(mod * ny)); FreeMem(t, msize); return NULL; }
    memset(o, 0, sizeof *o);
    o->tf = t;
    o->datasize = (ULONG)(mod * ny) | 0;
    o->chipsize = msize;              /* the TextFont block, for freeing */
    AddFont(t);
    o->next = own;
    own = o;
    return t;
}

static void own_fonts_free(void)
{
    while (own) {
        OWNFONT *o = own;
        own = o->next;
        Forbid();
        if (o->tf->tf_Accessors == 0) RemFont(o->tf);
        Permit();
        if (o->seg) {
            if (o->chipcopy) FreeMem(o->chipcopy, o->chipsize);
            UnLoadSeg(o->seg);
        } else {
            FreeMem(o->tf->tf_CharData, o->datasize);
            FreeMem(o->tf, o->chipsize);
        }
        free(o);
    }
}

/* does a font's pixel shape match the screen's? */
static int same_shape(struct TextFont *tf)
{
    int tall = dev_pixh >= dev_pixw * 3 / 2, wide = dev_pixw >= dev_pixh * 3 / 2;
    return ((tf->tf_Flags & FPF_TALLDOT) != 0) == tall && ((tf->tf_Flags & FPF_WIDEDOT) != 0) == wide;
}

/* open a font without diskfont.library: a designed size that suits the
 * screen, else the nearest size from FONTS: scaled to it. Scaled fonts
 * are in the system list as "name.font*". */
static struct TextFont *open_own(const char *file, int ysize)
{
    struct TextAttr ta;
    struct TextFont *tf, *s;
    UWORD sz[24];
    char nm[24][40], sname[40];
    int n, i, best = -1;
    ta.ta_Name = (STRPTR)file;
    ta.ta_YSize = (UWORD)ysize;
    ta.ta_Style = 0;
    ta.ta_Flags = FPF_DESIGNED;
    tf = OpenFont(&ta);
    if (tf && tf->tf_YSize == ysize && same_shape(tf)) return tf;
    if (tf) CloseFont(tf);
    sprintf(sname, "%s*", file);
    ta.ta_Name = (STRPTR)sname;
    tf = OpenFont(&ta);
    if (tf && tf->tf_YSize == ysize) return tf;
    if (tf) CloseFont(tf);
    ta.ta_Name = (STRPTR)file;
    n = font_sizes(file, sz, nm, 24);
    for (i = 0; i < n; i++)            /* the nearest size, larger ones first */
        if (best < 0 || (sz[i] >= ysize && (sz[best] < ysize || sz[i] < sz[best])) ||
            (sz[i] < ysize && sz[best] < ysize && sz[i] > sz[best])) best = i;
    tf = NULL;
    if (best >= 0) {
        ta.ta_YSize = sz[best];
        tf = OpenFont(&ta);
        if (tf && tf->tf_YSize != sz[best]) { CloseFont(tf); tf = NULL; }
        if (!tf && load_font_file(nm[best], file)) tf = OpenFont(&ta);
    } else {
        ta.ta_YSize = 8;
        tf = OpenFont(&ta);                 /* topaz and other fonts in ROM */
    }
    if (!tf) return NULL;
    if (ysize < 3 || ysize > 180) return tf;
    if (tf->tf_YSize == ysize && same_shape(tf)) return tf;
    s = scale_font(tf, ysize, sname);
    if (!s) return tf;
    CloseFont(tf);
    ta.ta_Name = (STRPTR)sname;
    ta.ta_YSize = (UWORD)ysize;
    ta.ta_Flags = 0;
    return OpenFont(&ta);
}

/* ================================================================= */
/*  The font list                                                     */
/* ================================================================= */

static int add_font(const char *file)
{
    char nm[34];
    int k, n;
    if (nfonts >= MAX_FONTS) return -1;
    for (k = 0; k < nfonts; k++) if (!strcmp(fontfile[k], file)) return k;
    n = (int)strlen(file);
    if (n < 6 || strcmp(file + n - 5, ".font") || n - 5 > 31) return -1;
    memcpy(nm, file, n - 5);
    nm[n - 5] = 0;
    if (nm[0] >= 'a' && nm[0] <= 'z') nm[0] -= 32;
    strcpy(fontfile[nfonts], file);
    strcpy(fonts[nfonts].name, nm);
    fonts[nfonts].id = (short)nfonts;
    fonts[nfonts].outline = 0;
    return nfonts++;
}

static int cmp_names(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

void fonts_load(void)
{
    static char names[MAX_FONTS * 2][32];
    int n = 0, i, k;
    static const char *pref[] = { "times.font", "cgtimes.font", "dutch.font", "helvetica.font",
                                  "cgtriumvirate.font", "arial.font", "swiss.font", NULL };

    if (DiskfontBase) {
        ULONG size = 4096;
        UBYTE *buf = NULL;
        for (;;) {
            LONG more;
            buf = (UBYTE *)malloc(size);
            if (!buf) break;
            more = AvailFonts((STRPTR)buf, size, AFF_MEMORY | AFF_DISK);
            if (!more) break;
            free(buf);
            buf = NULL;
            size += (ULONG)more + 256;
        }
        if (buf) {
            struct AvailFontsHeader *h = (struct AvailFontsHeader *)buf;
            struct AvailFonts *af = (struct AvailFonts *)(h + 1);
            for (i = 0; i < h->afh_NumEntries && n < MAX_FONTS * 2; i++) {
                const char *nm = (const char *)af[i].af_Attr.ta_Name;
                int dup = 0;
                for (k = 0; k < n; k++) if (!strcmp(names[k], nm)) dup = 1;
                if (!dup && strlen(nm) < 32) strcpy(names[n++], nm);
            }
            free(buf);
        }
    } else {
        BPTR lock = Lock((CONST_STRPTR)"FONTS:", ACCESS_READ);
        struct TextFont *tf;
        if (lock) {
            struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocMem(sizeof *fib, MEMF_CLEAR);
            if (fib && Examine(lock, fib)) {
                while (ExNext(lock, fib) && n < MAX_FONTS * 2) {
                    int l = (int)strlen((char *)fib->fib_FileName);
                    if (fib->fib_DirEntryType < 0 && l > 5 && l < 32 &&
                        !strcmp((char *)fib->fib_FileName + l - 5, ".font"))
                        strcpy(names[n++], (char *)fib->fib_FileName);
                }
            }
            if (fib) FreeMem(fib, sizeof *fib);
            UnLock(lock);
        }
        Forbid();
        for (tf = (struct TextFont *)GfxBase->TextFonts.lh_Head; tf->tf_Message.mn_Node.ln_Succ;
             tf = (struct TextFont *)tf->tf_Message.mn_Node.ln_Succ) {
            const char *nm = tf->tf_Message.mn_Node.ln_Name;
            int dup = 0;
            for (k = 0; k < n; k++) if (!strcmp(names[k], nm)) dup = 1;
            if (!dup && nm && strlen(nm) < 32 && n < MAX_FONTS * 2) strcpy(names[n++], nm);
        }
        Permit();
    }
    for (i = 0; i < n; i++)               /* lower case, to sort and match */
        for (k = 0; names[i][k]; k++) if (names[i][k] >= 'A' && names[i][k] <= 'Z') names[i][k] += 32;
    qsort(names, (size_t)n, sizeof names[0], cmp_names);

    /* the system font first (monospaced), then the usual text font */
    nfonts = 0;
    add_font("topaz.font");
    for (i = 0; pref[i]; i++)
        for (k = 0; k < n; k++) if (!strcmp(names[k], pref[i])) { add_font(names[k]); i = 99; break; }
    for (k = 0; k < n; k++) add_font(names[k]);
}

/* ================================================================= */
/*  Opening fonts at a pixel height                                   */
/* ================================================================= */

#define NFCACHE 20
static struct { short font, ysize; long stamp; struct TextFont *tf; } fc[NFCACHE];
static long fstamp;

struct TextFont *font_for(int font, int ysize, int attr)
{
    int i, slot = 0;
    struct TextFont *tf = NULL;
    (void)attr;
    if (font < 0 || font >= nfonts) font = 0;
    if (ysize < 3) ysize = 3;
    if (ysize > 180) ysize = 180;
    for (i = 0; i < NFCACHE; i++)
        if (fc[i].tf && fc[i].font == font && fc[i].ysize == ysize) { fc[i].stamp = ++fstamp; return fc[i].tf; }
    for (i = 0; i < NFCACHE; i++) {
        if (!fc[i].tf) { slot = i; break; }
        if (fc[i].stamp < fc[slot].stamp) slot = i;
    }
    if (fc[slot].tf) { CloseFont(fc[slot].tf); fc[slot].tf = NULL; }
    edlog("font_for miss: font %d size %d", font, ysize);
    if (DiskfontBase) {
        struct TTextAttr ta;
        ULONG tags[3];
        tags[0] = TA_DeviceDPI;
        tags[1] = ((ULONG)(25400 / dev_pixw) << 16) | (ULONG)(25400 / dev_pixh);
        tags[2] = TAG_END;
        ta.tta_Name = (STRPTR)fontfile[font];
        ta.tta_YSize = (UWORD)ysize;
        ta.tta_Style = FSF_TAGGED;
        ta.tta_Flags = 0;
        ta.tta_Tags = (struct TagItem *)tags;
        tf = OpenDiskFont((struct TextAttr *)&ta);
    } else
        tf = open_own(fontfile[font], ysize);
    if (!tf) {
        struct TextAttr t8;
        t8.ta_Name = (STRPTR)"topaz.font"; t8.ta_YSize = 8; t8.ta_Style = 0; t8.ta_Flags = 0;
        tf = OpenFont(&t8);
    }
    fc[slot].tf = tf;
    fc[slot].font = (short)font;
    fc[slot].ysize = (short)ysize;
    fc[slot].stamp = ++fstamp;
    return tf;
}

void fonts_unload(void)
{
    int i;
    for (i = 0; i < NFCACHE; i++) if (fc[i].tf) { CloseFont(fc[i].tf); fc[i].tf = NULL; }
    own_fonts_free();
}

/* the height in pixels of a point size */
static int ypx(long size) { long v = pt2px_y(size); return (int)(v < 3 ? 3 : v); }

/* a character's advance in a font (the index of a missing one is the
 * font's default glyph) */
static int char_w(struct TextFont *tf, unsigned char c, int bold)
{
    int i = c < tf->tf_LoChar || c > tf->tf_HiChar ? tf->tf_HiChar - tf->tf_LoChar + 1 : c - tf->tf_LoChar;
    int w = tf->tf_CharSpace ? ((WORD *)tf->tf_CharSpace)[i] : tf->tf_XSize;
    if (tf->tf_CharKern) w += ((WORD *)tf->tf_CharKern)[i];
    if (bold && !(tf->tf_Style & FSF_BOLD)) w += tf->tf_BoldSmear ? tf->tf_BoldSmear : 1;
    return w < 0 ? 0 : w;
}

/* ---- metrics cache ----------------------------------------------- */

#define NCACHE 16
static struct { short font, size, attr; long stamp; METRICS m; } mc[NCACHE];
static long mstamp;

const METRICS *get_metrics(int font, int size, int attr)
{
    int i, slot = 0;
    struct TextFont *tf;
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
    edlog("metrics miss: font %d size %d attr %d", font, size, attr);
    tf = font_for(font, ypx(size), attr);
    for (i = 0; i < 256; i++)
        mc[slot].m.w[i] = (short)(i < 32 || !tf ? 0 : char_w(tf, lat_ch((unsigned char)i), attr & A_BOLD));
    mc[slot].m.asc = (short)(tf ? tf->tf_Baseline : 6);
    mc[slot].m.desc = (short)(tf ? tf->tf_YSize - tf->tf_Baseline : 2);
    mc[slot].font = (short)font; mc[slot].size = (short)size; mc[slot].attr = (short)attr;
    mc[slot].stamp = ++mstamp;
    return &mc[slot].m;
}

/* ================================================================= */
/*  Drawing a page                                                    */
/* ================================================================= */

typedef struct { long pn, pd, sn, sd; } SCALE;
#define SP(v, sc) ((v) * (sc)->pn / (sc)->pd)

static void set_text(struct TextFont *tf, int attr, int pen)
{
    ULONG want = ((attr & A_BOLD) ? FSF_BOLD : 0) | ((attr & A_ITALIC) ? FSF_ITALIC : 0);
    SetFont(grp, tf);
    SetSoftStyle(grp, want & ~tf->tf_Style, AskSoftStyle(grp));
    SetAPen(grp, PEN(pen));
    SetDrMd(grp, JAM1);
}

static short cw(long v) { return (short)(v < -16000 ? -16000 : v > 16000 ? 16000 : v); }

/* text in the ST character set at a pixel height */
void text_at(long x, long base, const char *st, int n, int font, int size_px, int attr, int pen)
{
    char buf[200];
    struct TextFont *tf = font_for(font, size_px, attr);
    if (!tf || !grp || n <= 0) return;
    if (n > 199) n = 199;
    lat_n(buf, st, n);
    set_text(tf, attr, pen);
    Move(grp, cw(x), cw(base));
    Text(grp, (STRPTR)buf, (ULONG)n);
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
                SetAPen(grp, PEN(pen));
                Move(grp, cw(x1), cw(y + 1));
                for (x = x1 + 2; x <= x2; x += 2) { Draw(grp, cw(x), cw(y + (up ? 1 : 0))); up = !up; }
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
    long xs[132];

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
                struct TextFont *tf = font_for(st.font, (int)py, st.attr);
                if (tf) {
                    set_text(tf, st.attr, p);
                    if (sc->pn == sc->pd) {
                        Move(grp, cw(x), cw(base));
                        Text(grp, (STRPTR)buf, (ULONG)n);
                    } else {
                        /* zoomed: characters go where the layout says; runs
                         * whose advances agree (within a pixel) go in one call */
                        int i = 0, j, bold = (st.attr & A_BOLD) != 0;
                        while (i < n) {
                            long x0 = ox + SP(xs[i], sc), px = x0;
                            for (j = i; j < n; j++) {
                                long want = ox + SP(xs[j], sc);
                                if (want - px > 1 || px - want > 1) break;
                                px += char_w(tf, (unsigned char)buf[j], bold);
                            }
                            if (j == i) j = i + 1;
                            Move(grp, cw(x0), cw(base));
                            Text(grp, (STRPTR)&buf[i], (ULONG)(j - i));
                            i = j;
                        }
                    }
                }
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
    if (dotted) SetDrPt(grp, 0xAAAA);
    line(x, y0, x, y1, pen);
    for (i = 0, k = 0; i < T->ncols; i++) {
        k += pt2px_x(T->w[i]);
        line(x + SP(k, sc), y0, x + SP(k, sc), y1, pen);
    }
    SetDrPt(grp, 0xFFFF);
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
    if (page < 0 || page >= d->npages || !grp) return;
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
                SetFont(grp, fixfont);
                SetSoftStyle(grp, 0, 0xFF);
                SetAPen(grp, PEN(npens >= 8 ? P_GREEN : fg));
                Move(grp, cw(ox + 2), cw(oy + SP(ln->py + ln->asc, &sc)));
                Text(grp, (STRPTR)t, 2);
            }
        }
    }
    edlog(" page %d: lines drawn", page);
    draw_hf(d, page, fg, &sc, ox, oy);
    edlog(" page %d: header and footer drawn", page);
    SetSoftStyle(grp, 0, 0xFF);
}

void draw_preview_page(DOC *d, int page, long ox, long oy, long n, long den)
{
    draw_page(d, page, ox, oy, n, den, NULL, -1, 0);
}

/* ================================================================= */
/*  PDF: character bitmaps from the fonts                             */
/* ================================================================= */

typedef struct {
    short font, size, attr, scale, ch;
    short w, h, ox, oy;
    unsigned char *bits;
} GLYPH;

#define NGLYPH 256
static GLYPH gcache[NGLYPH];
static int   ngc;

static void glyph_cache_free(void)
{
    int i;
    for (i = 0; i < ngc; i++) free(gcache[i].bits);
    ngc = 0;
}

/* the glyph of Latin-1 character c from a font, each pixel rep x rep */
static int font_glyph(GLYPH *g, struct TextFont *tf, unsigned char c, int rep)
{
    int i = c < tf->tf_LoChar || c > tf->tf_HiChar ? tf->tf_HiChar - tf->tf_LoChar + 1 : c - tf->tf_LoChar;
    ULONG loc = ((ULONG *)tf->tf_CharLoc)[i];
    long x0 = (long)(loc >> 16), w = (long)(loc & 0xFFFF), r, x, rb, sx, sy;
    const UBYTE *data = (const UBYTE *)tf->tf_CharData;
    g->w = (short)(w * rep);
    g->h = (short)(tf->tf_YSize * rep);
    g->ox = (short)((tf->tf_CharKern ? ((WORD *)tf->tf_CharKern)[i] : 0) * rep);
    g->oy = (short)(-(long)tf->tf_Baseline * rep);
    if (g->w <= 0) { g->w = g->h = 0; g->bits = NULL; return 1; }
    rb = (g->w + 7) / 8;
    g->bits = (unsigned char *)calloc((size_t)rb, (size_t)g->h);
    if (!g->bits) return 0;
    for (r = 0; r < tf->tf_YSize; r++)
        for (x = 0; x < w; x++) {
            long bit = x0 + x;
            if (!(data[r * tf->tf_Modulo + (bit >> 3)] & (0x80 >> (bit & 7)))) continue;
            for (sy = 0; sy < rep; sy++)
                for (sx = 0; sx < rep; sx++) {
                    long bx = x * rep + sx, by = r * rep + sy;
                    g->bits[by * rb + (bx >> 3)] |= (unsigned char)(0x80 >> (bx & 7));
                }
        }
    return 1;
}

/* thicken a glyph sideways by t pixels (bold) */
static void embolden(GLYPH *g, int t)
{
    int ow = g->w, orb = (ow + 7) / 8, nrb, r, x, k;
    unsigned char *nb;
    if (!g->bits) return;
    g->w = (short)(ow + t);
    nrb = (g->w + 7) / 8;
    nb = (unsigned char *)calloc((size_t)nrb, (size_t)g->h);
    if (!nb) { g->w = (short)ow; return; }
    for (r = 0; r < g->h; r++)
        for (x = 0; x < ow; x++)
            if (g->bits[r * orb + (x >> 3)] & (0x80 >> (x & 7)))
                for (k = 0; k <= t; k++)
                    nb[r * nrb + ((x + k) >> 3)] |= (unsigned char)(0x80 >> ((x + k) & 7));
    free(g->bits);
    g->bits = nb;
}

/* slant a glyph (italic): rows above the baseline move right */
static void slant(GLYPH *g)
{
    int rb = (g->w + 7) / 8, sh = (g->h * 13 / 64) + 1, nw = g->w + sh, nrb = (nw + 7) / 8, r, x;
    unsigned char *nb;
    if (!g->bits) return;
    nb = (unsigned char *)calloc((size_t)nrb, (size_t)g->h);
    if (!nb) return;
    for (r = 0; r < g->h; r++) {
        int off = (int)((-(long)(g->oy + r)) * 13 / 64) + sh / 2;
        if (off < 0) off = 0;
        for (x = 0; x < g->w; x++)
            if (g->bits[r * rb + (x >> 3)] & (0x80 >> (x & 7))) {
                int nx = x + off;
                if (nx < nw) nb[r * nrb + (nx >> 3)] |= (unsigned char)(0x80 >> (nx & 7));
            }
    }
    free(g->bits);
    g->bits = nb;
    g->w = (short)nw;
    g->ox = (short)(g->ox - sh / 2);
}

static GLYPH *get_glyph(int font, int size, int attr, int ch, int scale)
{
    int i, want, rep = 1;
    GLYPH *g;
    struct TextFont *tf;
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
    want = ypx(size) * scale;
    tf = font_for(font, want > 180 ? ypx(size) : want, 0);
    if (!tf) return NULL;
    if (tf->tf_YSize * 3 / 2 < want) rep = (want + tf->tf_YSize / 2) / tf->tf_YSize;
    if (rep < 1) rep = 1;
    if (!font_glyph(g, tf, lat_ch((unsigned char)ch), rep)) return NULL;
    if (attr & A_ITALIC) slant(g);
    if (attr & A_BOLD) embolden(g, (tf->tf_BoldSmear ? tf->tf_BoldSmear : 1) * scale);
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
    r.scale = pdf_scale < 1 ? 1 : pdf_scale;
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

/* ---- printing through printer.device ------------------------------------- */

/* Pages are drawn as bitmaps at the printer's resolution (prt.c), with
 * the glyphs the PDF uses, and dumped to the printer chosen in the
 * Printer preferences a band at a time (PRD_DUMPRPORT, without form
 * feeds between the bands); a form feed ends each page. */

union printerIO {
    struct IOStdReq    ios;
    struct IODRPReq    iodrp;
    struct IOPrtCmdReq iopc;
};

static struct MsgPort *pport;
static union printerIO *pio;
static int popen_ok;

static int prt_open(void)
{
    pport = CreateMsgPort();
    if (!pport) return 0;
    pio = (union printerIO *)CreateIORequest(pport, sizeof(union printerIO));
    if (!pio) { DeleteMsgPort(pport); pport = NULL; return 0; }
    if (OpenDevice((STRPTR)"printer.device", 0, (struct IORequest *)pio, 0)) {
        DeleteIORequest((struct IORequest *)pio); pio = NULL;
        DeleteMsgPort(pport); pport = NULL;
        return 0;
    }
    popen_ok = 1;
    return 1;
}

static void prt_close(void)
{
    if (popen_ok) CloseDevice((struct IORequest *)pio);
    popen_ok = 0;
    if (pio) DeleteIORequest((struct IORequest *)pio);
    if (pport) DeleteMsgPort(pport);
    pio = NULL; pport = NULL;
}

static struct PrinterExtendedData *prt_ped(void)
{
    struct PrinterData *pd = (struct PrinterData *)pio->ios.io_Device;
    return pd && pd->pd_SegmentData ? &pd->pd_SegmentData->ps_PED : NULL;
}

int printer_name(char *out)
{
    struct PrinterExtendedData *ped;
    if (!prt_open()) return 0;
    ped = prt_ped();
    sprintf(out, "%.40s", ped && ped->ped_PrinterName ? ped->ped_PrinterName : "printer.device");
    if (ped && ped->ped_XDotsInch) sprintf(out + strlen(out), " (%d dpi)", ped->ped_XDotsInch);
    prt_close();
    return 1;
}

typedef struct {
    PRTOUT o;
    struct ColorMap *cm;
    UBYTE *chip;                    /* the band in chip memory               */
    long  chipsize;
    int   page, npg, done, first;
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
    struct BitMap bm;
    struct RastPort rp;
    (void)y0;
    if (rb * h > c->chipsize) return 0;
    CopyMem((APTR)bits, c->chip, (ULONG)(rb * h));
    InitBitMap(&bm, 1, (WORD)(rb * 8), (WORD)h);
    bm.Planes[0] = (PLANEPTR)c->chip;
    InitRastPort(&rp);
    rp.BitMap = &bm;
    pio->iodrp.io_Command = PRD_DUMPRPORT;
    pio->iodrp.io_RastPort = &rp;
    pio->iodrp.io_ColorMap = c->cm;
    pio->iodrp.io_Modes = 0;
    pio->iodrp.io_SrcX = 0;
    pio->iodrp.io_SrcY = 0;
    pio->iodrp.io_SrcWidth = (UWORD)w;
    pio->iodrp.io_SrcHeight = (UWORD)h;
    pio->iodrp.io_DestCols = w;
    pio->iodrp.io_DestRows = h;
    pio->iodrp.io_Special = (UWORD)(SPECIAL_NOFORMFEED | (c->first ? 0 : SPECIAL_TRUSTME));
    c->first = 0;
    return DoIO((struct IORequest *)pio) == 0;
}

static void prt_progress(void *ctx, int done, int total)
{
    PCTX *c = (PCTX *)ctx;
    gauge_set((long)c->done * total + done, (long)c->npg * total);
}

int print_doc(DOC *d, const unsigned char *sel, int copies)
{
    static PCTX c;
    struct PrinterExtendedData *ped;
    int p, k, ok = 1, np;
    memset(&c, 0, sizeof c);
    if (!prt_open()) return 0;
    ped = prt_ped();
    if (!ped || !ped->ped_XDotsInch || !ped->ped_YDotsInch) { prt_close(); return 0; }   /* text only */
    c.o.pixw = 25400L / ped->ped_XDotsInch;
    c.o.pixh = 25400L / ped->ped_YDotsInch;
    c.o.w = (long)ped->ped_MaxXDots;
    c.o.h = 0;
    c.o.band = 64;
    c.o.render = prt_render;
    c.o.out = prt_out;
    c.o.progress = prt_progress;
    c.o.ctx = &c;
    c.chipsize = ((((long)PS(d).w * 25400L / 72L / c.o.pixw) + 15) / 16) * 2 * c.o.band;
    c.chip = (UBYTE *)AllocMem((ULONG)c.chipsize, MEMF_CHIP);
    c.cm = GetColorMap(2);
    if (!c.chip || !c.cm) {
        if (c.chip) FreeMem(c.chip, (ULONG)c.chipsize);
        if (c.cm) FreeColorMap(c.cm);
        prt_close();
        return 0;
    }
    SetRGB4CM(c.cm, 0, 15, 15, 15);                   /* paper */
    SetRGB4CM(c.cm, 1, 0, 0, 0);                      /* ink   */
    np = doc_npages(d);
    for (p = 0, k = 0; p < np; p++) if (!sel || sel[p]) k++;
    if (!k) return 0;
    c.npg = k * copies;
    c.first = 1;
    gauge_open(L(S_PRINTING));
    doc_layout(d);
    busy(1);
    for (k = 0; k < copies && ok; k++)
        for (p = 0; p < np && ok; p++) {
            if (sel && !sel[p]) continue;
            ok = print_page(d, p, &c.o);
            if (ok) {                                  /* a form feed after the page */
                static char ff[2] = { 12, 0 };
                pio->ios.io_Command = CMD_WRITE;
                pio->ios.io_Data = (APTR)ff;
                pio->ios.io_Length = 1;
                ok = DoIO((struct IORequest *)pio) == 0;
            }
            c.done++;
        }
    busy(0);
    gauge_close();
    FreeMem(c.chip, (ULONG)c.chipsize);
    FreeColorMap(c.cm);
    prt_close();
    glyph_cache_free();
    rs_end();
    return ok;
}
