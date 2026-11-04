/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * rtf.c - RTF import and export
 *
 * Import keeps text, paragraphs, tabs, page and column breaks,
 * bold/italic/underline, font size, font face (matched by name/family to
 * the loaded GDOS fonts), text colour, justification, line spacing,
 * indents, bullet and numbered lists (Word 6 and Word 97 styles), outline
 * levels (TOC headings), tables, columns and the page set-up.
 * EdWord's own settings live in ignorable {\*\ed...} groups:
 *   {\*\edwrapcol N}  {\*\edhf0|1 ...}  {\*\edlist N}  {\*\edtoc N}
 *   {\*\edpage\edpg..\edfol..\edml..\edmr..\edmt..\edmb..\edcols..\edgap..}
 *   {\*\edobj\edot..\edop..\edox..\edoy..\edow..\edoh..\edowr..\edobr..
 *      {\*\edsh\edst..\edsf..\edsc..\edsl..\edsxa..\edsya..\edsxb..\edsyb..}
 *      {\*\edimg\ediw..\edih.. hex rows}{\*\edimgname NAME}}
 * Files that don't start with {\rtf are loaded as plain text.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "wp.h"

#define MAXDEPTH 48
enum { D_NORMAL, D_FONTTBL, D_COLORTBL, D_HF0, D_HF1, D_LISTTBL, D_LISTOVR,
       D_EDPAGE, D_EDOBJ, D_EDSH, D_EDIMG, D_EDIMGNAME, D_EDIMGRGB, D_EDCHART };

typedef struct { int fnum, size, attr, skip, uc, dest, color; } RSTATE;
typedef struct { int num, idx; } FMAP;

/* paragraph state: not scoped by groups in RTF */
static struct {
    int just, sl, slmult, li, fi, list, toc, tocent, intbl;
} pst;

static FMAP fmap[64];
static int  nfmap, deffnum;
static char fname[64];
static int  fnlen, fcurnum, ffamily;
static int  ucskip;
static unsigned char ctab[64];
static int  nctab, cr, cg, cb, chave;

/* lists in Word 97 files: list ids and their kind; \ls numbers -> ids */
static long lid[32]; static int lkind[32], nlid, curnfc, inlevel;
static long lsid[32]; static int lsnum[32], nls, ovr_ls; static long ovr_id;

/* tables: the current row definition */
static long cellx[MAX_COLS]; static int ncellx, rowborder, rowleft, rowdirty;
static int  rowchart[4], rowhaschart;  /* the row's chart: type, place, width, height */
static int  curtbl, lastrow_tbl;

/* EdWord groups being read */
static PGOVR  eo;
static OBJ    eob;
static SHAPE  esh;
static IMG    eim;
static long   eimpos;
static int    eimhi, eimnib;
static unsigned char *ergb;            /* a picture's colours, PackBits */
static long   ergblen, ergbcap;

static void lower(char *dst, const char *src, int max)
{
    int i;
    for (i = 0; src[i] && i < max - 1; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static void first_word(char *dst, const char *src, int max)
{
    int i = 0;
    while (*src == ' ') src++;
    while (src[i] && src[i] != ' ' && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int has(const char *s, const char *w) { return strstr(s, w) != NULL; }

/* family of a font name: 0 sans, 1 serif, 2 mono, -1 unknown */
static int family_of(const char *l)
{
    if (has(l, "cour") || has(l, "mono") || has(l, "consol") || has(l, "typew") ||
        has(l, "fixed") || has(l, "system")) return 2;
    if (has(l, "sans") || has(l, "arial") || has(l, "helv") || has(l, "verdana") ||
        has(l, "calibri") || has(l, "swiss")) return 0;
    if (has(l, "times") || has(l, "roman") || has(l, "serif") || has(l, "georgia") ||
        has(l, "garamond") || has(l, "cambria") || has(l, "dutch") || has(l, "book"))
        return 1;
    return -1;
}

/* a loaded font of a family; the system font (0) is the monospaced one
 * when there is no other */
int font_family_find(int family)
{
    char gl[40];
    int i;
    for (i = 1; i < nfonts; i++) {
        lower(gl, fonts[i].name, sizeof gl);
        if (family_of(gl) == family) return i;
    }
    return family == 2 ? 0 : default_font();
}

int font_is_mono(int f)
{
    char gl[40];
    if (f < 0 || f >= nfonts) return 0;
    if (f == 0) return nfonts > 1;          /* the system font, when there are others */
    lower(gl, fonts[f].name, sizeof gl);
    return family_of(gl) == 2;
}

/* map an RTF font name/family onto one of the loaded fonts */
static int font_match(const char *rtfname, int family)
{
    char rl[64], rw[40], gl[40], gw[40];
    int i, base;

    lower(rl, rtfname, sizeof rl);
    first_word(rw, rl, sizeof rw);
    if (rw[0])
        for (i = 1; i < nfonts; i++) {
            lower(gl, fonts[i].name, sizeof gl);
            first_word(gw, gl, sizeof gw);
            if (has(gl, rw) || (gw[0] && has(rl, gw))) return i;
        }
    base = family_of(rl);
    if (base < 0) base = family;
    if (base >= 0) {
        for (i = 1; i < nfonts; i++) {
            lower(gl, fonts[i].name, sizeof gl);
            if (family_of(gl) == base) return i;
        }
        if (base == 2) return 0;    /* the system font is monospaced */
    }
    return default_font();
}

static void commit_font(void)
{
    while (fnlen > 0 && fname[fnlen - 1] == ' ') fnlen--;
    fname[fnlen] = 0;
    if (nfmap < 64) {
        fmap[nfmap].num = fcurnum;
        fmap[nfmap].idx = font_match(fname, ffamily);
        nfmap++;
    }
    fnlen = 0;
    ffamily = -1;
}

static int map_font(int num)
{
    int i;
    if (num < 0) num = deffnum;
    for (i = 0; i < nfmap; i++)
        if (fmap[i].num == num) return fmap[i].idx;
    return default_font();
}

static void pard(void)
{
    memset(&pst, 0, sizeof pst);
}

/* the table for the row being read: the previous row's if it matches */
static int row_table(DOC *d)
{
    short w[MAX_COLS];
    int i, n = ncellx;
    long prev = rowleft;
    if (n == 0) return curtbl;
    for (i = 0; i < n; i++) {
        long v = (cellx[i] - prev) / 20;
        w[i] = (short)(v < 12 ? 12 : v);
        prev = cellx[i];
    }
    if (curtbl && lastrow_tbl == curtbl && curtbl <= d->a.ntbl) {
        TBL *t = &d->a.tbls[curtbl - 1];
        if (t->ncols == n) {
            for (i = 0; i < n; i++) if (t->w[i] != w[i]) break;
            if (i == n) return curtbl;
        }
    }
    return doc_tbl_new(d, n, w, rowborder);
}

static int cur_para(DOC *d)
{
    PARA p;
    para_default(&p);
    p.just = (unsigned char)pst.just;
    if (pst.slmult && pst.sl > 0) {
        if (pst.sl <= 300) p.spacing = LS_SINGLE;
        else if (pst.sl <= 420) p.spacing = LS_ONEHALF;
        else p.spacing = LS_DOUBLE;
    }
    p.left = (short)(pst.li / 20);
    p.first = (short)(pst.fi / 20);
    if (p.left < 0) p.left = 0;
    if (p.first < -p.left) p.first = (short)-p.left;
    p.list = (unsigned char)pst.list;
    if (p.list && p.left == 24 && p.first == -18) { p.left = 0; p.first = 0; }   /* the default */
    p.toc = (unsigned char)pst.toc;
    p.tocent = (unsigned char)pst.tocent;
    if (pst.intbl) {
        if (rowdirty) {
            curtbl = row_table(d);
            rowdirty = 0;
            if (curtbl && curtbl <= d->a.ntbl && rowhaschart) {
                TBL *t = &d->a.tbls[curtbl - 1];
                t->chart = (unsigned char)rowchart[0]; t->cpos = (unsigned char)rowchart[1];
                t->cw = (short)rowchart[2]; t->ch = (short)rowchart[3];
            }
        }
        p.tbl = (unsigned char)curtbl;
        if (p.tbl) { p.left = 0; p.first = 0; p.list = 0; }
    }
    return para_find(d, &p);
}

/* append one ST character with the current style */
static void put_st(DOC *d, RSTATE *s, unsigned char ch)
{
    CELL c;
    if (s->skip) return;
    if (pst.intbl && ISBRK(ch)) ch = ' ';           /* one paragraph per cell */
    c.ch = ch;
    c.font = (unsigned char)map_font(s->fnum);
    c.size = (unsigned char)s->size;
    c.attr = (unsigned char)s->attr;
    c.color = (unsigned char)s->color;
    c.para = (unsigned char)cur_para(d);
    doc_insert(d, d->len, &c, 1);
    if (ISBRK(ch)) lastrow_tbl = 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* a text byte from the RTF stream (cp1252) */
static void put_text(DOC *d, RSTATE *s, unsigned char ch)
{
    if (s->dest == D_EDIMG) {
        int v = hexval(ch);
        if (v < 0 || !eim.bits) return;
        if (!eimnib) { eimhi = v; eimnib = 1; return; }
        eimnib = 0;
        if (eimpos < (long)eim.rb * eim.h) eim.bits[eimpos++] = (unsigned char)(eimhi * 16 + v);
        return;
    }
    if (s->dest == D_EDIMGRGB) {
        int v = hexval(ch);
        if (v < 0) return;
        if (!eimnib) { eimhi = v; eimnib = 1; return; }
        eimnib = 0;
        if (ergblen >= ergbcap) {
            long nc = ergbcap ? ergbcap * 2 : 4096;
            unsigned char *nb = (unsigned char *)realloc(ergb, (size_t)nc);
            if (!nb) return;
            ergb = nb; ergbcap = nc;
        }
        ergb[ergblen++] = (unsigned char)(eimhi * 16 + v);
        return;
    }
    if (s->dest == D_EDIMGNAME) {
        size_t n = strlen(eim.name);
        if (n < sizeof eim.name - 1 && ch > ' ') { eim.name[n] = (char)ch; eim.name[n + 1] = 0; }
        return;
    }
    if (s->skip) return;
    if (s->dest == D_HF0 || s->dest == D_HF1) {
        char *t = HF(d).text[s->dest - D_HF0];
        size_t n = strlen(t);
        if (n < sizeof HF(d).text[0] - 1) { t[n] = (char)win_to_st(ch); t[n + 1] = 0; }
        return;
    }
    if (s->dest == D_FONTTBL) {
        if (ch == ';') commit_font();
        else if (fnlen < 63) fname[fnlen++] = (char)ch;
        return;
    }
    if (s->dest == D_COLORTBL) {
        if (ch == ';' && nctab < 64) {
            ctab[nctab++] = (unsigned char)(chave ? nearest_color(cr, cg, cb) : 0);
            cr = cg = cb = chave = 0;
        }
        return;
    }
    if (s->dest != D_NORMAL) return;
    if (ucskip > 0) { ucskip--; return; }
    put_st(d, s, win_to_st(ch));
}

static const char *skipdest[] = {
    "stylesheet", "info", "pict", "header", "footer",
    "headerl", "headerr", "headerf", "footerl", "footerr", "footerf",
    "footnote", "object", "fldinst", "themedata", "colorschememapping",
    "datastore", "latentstyles", "rsidtbl", "generator", "xmlnstbl", "mmathPr",
    "filetbl", "revtbl", "listtext", "pntext", "pntxta", "pntxtb", "shppict",
    "nonshppict", "bkmkstart", "bkmkend", "userprops", "docvar", "listpicture",
    "shp", "shpinst", NULL
};

static int clampi(long v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int)v; }

/* our own groups */
static int ed_control(DOC *d, RSTATE *s, const char *w, int hasp, long p)
{
    if (!strcmp(w, "edchart")) {
        s->dest = D_EDCHART;
        memset(rowchart, 0, sizeof rowchart);
        rowhaschart = 1;
        rowdirty = 1;
        return 1;
    }
    if (s->dest == D_EDCHART) {
        if (!strcmp(w, "edct")) rowchart[0] = clampi(p, 0, NCHARTS - 1);
        else if (!strcmp(w, "edcp")) rowchart[1] = clampi(p, 0, 3);
        else if (!strcmp(w, "edcw")) rowchart[2] = clampi(p, 0, 2000);
        else if (!strcmp(w, "edch")) rowchart[3] = clampi(p, 0, 2000);
        return 1;
    }
    if (!strcmp(w, "edwrapcol") && hasp) { PS(d).wrapcol = (short)clampi(p, 0, 250); return 1; }
    if (!strcmp(w, "edhf") && hasp && (p == 0 || p == 1)) {
        s->dest = D_HF0 + (int)p;
        s->skip = 0;
        HF(d).text[p][0] = 0;
        return 1;
    }
    if (s->dest == D_HF0 || s->dest == D_HF1) {
        int i = s->dest - D_HF0;
        if (!strcmp(w, "edj") && hasp && p >= 0 && p <= 2) HF(d).just[i] = (unsigned char)p;
        else if (!strcmp(w, "edfs") && hasp && p >= 4 && p <= 72) HF(d).size = (unsigned char)p;
        else if (!strcmp(w, "edfirst") && hasp) HF(d).first = (unsigned char)(p != 0);
        return 1;
    }
    if (!strcmp(w, "edlist") && hasp) { pst.list = clampi(p, 0, 2); return 1; }
    if (!strcmp(w, "edtoc") && hasp) { pst.tocent = clampi(p, 0, 2); return 1; }
    if (!strcmp(w, "edpage")) { s->dest = D_EDPAGE; s->skip = 0; memset(&eo, 0, sizeof eo); eo.cols = 1; return 1; }
    if (s->dest == D_EDPAGE) {
        if (!strcmp(w, "edpg")) eo.page = (short)clampi(p, 0, 9999);
        else if (!strcmp(w, "edfol")) eo.following = (short)(p != 0);
        else if (!strcmp(w, "edml")) eo.ml = (short)clampi(p, 0, 1000);
        else if (!strcmp(w, "edmr")) eo.mr = (short)clampi(p, 0, 1000);
        else if (!strcmp(w, "edmt")) eo.mt = (short)clampi(p, 0, 1000);
        else if (!strcmp(w, "edmb")) eo.mb = (short)clampi(p, 0, 1000);
        else if (!strcmp(w, "edcols")) eo.cols = (short)clampi(p, 1, 4);
        else if (!strcmp(w, "edgap")) eo.colgap = (short)clampi(p, 0, 200);
        return 1;
    }
    if (!strcmp(w, "edobj")) {
        s->dest = D_EDOBJ; s->skip = 0;
        memset(&eob, 0, sizeof eob);
        eob.img = -1;
        return 1;
    }
    if (s->dest == D_EDOBJ) {
        if (!strcmp(w, "edot")) eob.type = (short)clampi(p, 0, 2);
        else if (!strcmp(w, "edop")) eob.page = (short)clampi(p, 0, 9999);
        else if (!strcmp(w, "edox")) eob.x = (short)clampi(p, -2000, 4000);
        else if (!strcmp(w, "edoy")) eob.y = (short)clampi(p, -2000, 4000);
        else if (!strcmp(w, "edow")) eob.w = (short)clampi(p, 4, 4000);
        else if (!strcmp(w, "edoh")) eob.h = (short)clampi(p, 4, 4000);
        else if (!strcmp(w, "edowr")) eob.wrap = (short)clampi(p, 0, 2);
        else if (!strcmp(w, "edobr")) eob.border = (short)(p != 0);
        else if (!strcmp(w, "edsh")) {
            s->dest = D_EDSH; memset(&esh, 0, sizeof esh); esh.lw = 1;
        } else if (!strcmp(w, "edimg")) {
            s->dest = D_EDIMG;
            img_free(&eim);
            memset(&eim, 0, sizeof eim);
            eimpos = 0; eimnib = 0;
        } else if (!strcmp(w, "edimgname")) { s->dest = D_EDIMGNAME; eim.name[0] = 0; }
        else if (!strcmp(w, "edimgrgb")) { s->dest = D_EDIMGRGB; ergblen = 0; eimnib = 0; }
        return 1;
    }
    if (s->dest == D_EDSH) {
        if (!strcmp(w, "edst")) esh.type = (unsigned char)clampi(p, 0, NSHAPES - 1);
        else if (!strcmp(w, "edsf")) esh.fill = (unsigned char)clampi(p, 0, NFILLS - 1);
        else if (!strcmp(w, "edsc")) esh.color = (unsigned char)clampi(p, 0, NCOLORS - 1);
        else if (!strcmp(w, "edsl")) esh.lw = (unsigned char)clampi(p, 0, 8);
        else if (!strcmp(w, "edsxa")) esh.x1 = (short)clampi(p, -4000, 4000);
        else if (!strcmp(w, "edsya")) esh.y1 = (short)clampi(p, -4000, 4000);
        else if (!strcmp(w, "edsxb")) esh.x2 = (short)clampi(p, -4000, 4000);
        else if (!strcmp(w, "edsyb")) esh.y2 = (short)clampi(p, -4000, 4000);
        return 1;
    }
    if (s->dest == D_EDIMG) {
        if (!strcmp(w, "ediw")) eim.w = (short)clampi(p, 1, 4000);
        else if (!strcmp(w, "edih")) eim.h = (short)clampi(p, 1, 4000);
        if (eim.w && eim.h && !eim.bits) {
            eim.rb = (short)((eim.w + 7) / 8);
            eim.bits = (unsigned char *)calloc((size_t)eim.rb * eim.h, 1);
        }
        return 1;
    }
    return 0;
}

/* a group of ours is ending */
static void ed_close(DOC *d, RSTATE *s)
{
    switch (s->dest) {
    case D_EDPAGE: {
        PGFMT f;
        f.ml = eo.ml; f.mr = eo.mr; f.mt = eo.mt; f.mb = eo.mb; f.cols = eo.cols; f.colgap = eo.colgap;
        page_ovr_set(d, eo.page, eo.following, &f);
        break;
    }
    case D_EDSH:
        if (eob.nsh < MAX_SHAPES) eob.sh[eob.nsh++] = esh;
        break;
    case D_EDIMG:
        break;
    case D_EDIMGRGB:
        /* the colours, PackBits; kept if there is room */
        if (eim.bits && eim.w && eim.h && (long)eim.w * eim.h * 3 <= img_rgb_max && !eim.rgb) {
            long want = (long)eim.w * eim.h * 3, o = 0, i = 0;
            eim.rgb = (unsigned char *)malloc((size_t)want);
            if (!eim.rgb) break;
            while (i < ergblen && o < want) {
                int c = (signed char)ergb[i++];
                if (c >= 0) { while (c-- >= 0 && i < ergblen && o < want) eim.rgb[o++] = ergb[i++]; }
                else if (c != -128 && i < ergblen) { int m = 1 - c; while (m-- > 0 && o < want) eim.rgb[o++] = ergb[i]; i++; }
            }
            if (o < want) { free(eim.rgb); eim.rgb = NULL; }
        }
        break;
    case D_EDOBJ:
        if (eob.type == OB_IMG) {
            if (eim.bits && eim.w && eim.h) {
                eob.img = (short)doc_img_add(d, &eim);
                if (eob.img < 0) img_free(&eim);
                memset(&eim, 0, sizeof eim);
            }
            if (eob.img < 0) break;
        }
        if (eob.type && eob.w > 0 && eob.h > 0) doc_obj_add(d, &eob);
        break;
    }
}

static void list_control(RSTATE *s, const char *w, int hasp, long p)
{
    if (s->dest == D_LISTTBL) {
        if (!strcmp(w, "listlevel")) inlevel++;
        else if (!strcmp(w, "levelnfc") && hasp && inlevel == 1) curnfc = (int)p;
        else if (!strcmp(w, "listid") && hasp && nlid < 32) {
            lid[nlid] = p;
            lkind[nlid] = curnfc == 23 ? LIST_BULLET : curnfc == 255 ? LIST_NONE : LIST_NUMBER;
            nlid++;
        } else if (!strcmp(w, "list")) { curnfc = 0; inlevel = 0; }
    } else if (s->dest == D_LISTOVR) {
        if (!strcmp(w, "listoverride")) { ovr_id = -1; ovr_ls = -1; }
        else if (!strcmp(w, "listid") && hasp) ovr_id = p;
        else if (!strcmp(w, "ls") && hasp && nls < 32) { lsid[nls] = ovr_id; lsnum[nls] = (int)p; nls++; }
    }
}

static void control(DOC *d, RSTATE *s, const char *w, int hasp, long p)
{
    int i;
    PAGESET *ps = &PS(d);

    if (!strncmp(w, "ed", 2) && ed_control(d, s, w, hasp, p)) return;
    if (s->dest == D_LISTTBL || s->dest == D_LISTOVR) { list_control(s, w, hasp, p); return; }
    if (!strcmp(w, "listtable")) { s->dest = D_LISTTBL; s->skip = 0; nlid = 0; return; }
    if (!strcmp(w, "listoverridetable")) { s->dest = D_LISTOVR; s->skip = 0; nls = 0; return; }
    for (i = 0; skipdest[i]; i++)
        if (!strcmp(w, skipdest[i])) { s->skip = 1; return; }
    if (s->dest > D_COLORTBL && s->dest != D_NORMAL) return;

    if (!strcmp(w, "fonttbl")) { s->dest = D_FONTTBL; fnlen = 0; ffamily = -1; return; }
    if (!strcmp(w, "colortbl")) { s->dest = D_COLORTBL; nctab = 0; cr = cg = cb = chave = 0; return; }
    if (s->dest == D_FONTTBL) {
        if (!strcmp(w, "f")) { fcurnum = (int)p; fnlen = 0; }
        else if (!strcmp(w, "froman")) ffamily = 1;
        else if (!strcmp(w, "fswiss")) ffamily = 0;
        else if (!strcmp(w, "fmodern")) ffamily = 2;
        return;
    }
    if (s->dest == D_COLORTBL) {
        if (!strcmp(w, "red")) { cr = (int)p; chave = 1; }
        else if (!strcmp(w, "green")) { cg = (int)p; chave = 1; }
        else if (!strcmp(w, "blue")) { cb = (int)p; chave = 1; }
        return;
    }

    /* page set-up, twips -> points */
    if (hasp && p > 0 && p < 32000) {
        short v = (short)(p / 20);
        if (!strcmp(w, "paperw")) { ps->w = v; ps->paper = -1; return; }
        if (!strcmp(w, "paperh")) { ps->h = v; ps->paper = -1; return; }
        if (!strcmp(w, "margl")) { ps->ml = v; return; }
        if (!strcmp(w, "margr")) { ps->mr = v; return; }
        if (!strcmp(w, "margt")) { ps->mt = v; return; }
        if (!strcmp(w, "margb")) { ps->mb = v; return; }
    }
    if (!strcmp(w, "landscape")) { ps->landscape = 1; return; }
    if (!strcmp(w, "cols") && hasp) { ps->cols = (short)clampi(p, 1, 4); return; }
    if (!strcmp(w, "colsx") && hasp) { ps->colgap = (short)clampi(p / 20, 0, 200); return; }

    /* tables */
    if (!strcmp(w, "trowd")) { ncellx = 0; rowborder = 0; rowleft = 0; rowdirty = 1; rowhaschart = 0; return; }
    if (!strcmp(w, "cellx") && hasp) { if (ncellx < MAX_COLS) cellx[ncellx++] = p; rowdirty = 1; return; }
    if (!strcmp(w, "trleft") && hasp) { rowleft = (int)p; return; }
    if (!strncmp(w, "clbrdr", 6) || !strncmp(w, "trbrdr", 6)) { rowborder = 1; return; }
    if (!strcmp(w, "intbl")) { pst.intbl = 1; return; }
    if (!strcmp(w, "cell")) {
        ucskip = 0;
        if (!s->skip) { put_st(d, s, CH_CELL); lastrow_tbl = curtbl; }
        return;
    }
    if (!strcmp(w, "row")) {
        ucskip = 0;
        if (s->skip) return;
        /* the row's last \cell becomes the paragraph break */
        if (d->len > 0 && d->text[d->len - 1].ch == CH_CELL) {
            d->text[d->len - 1].ch = '\n';
            doc_touch(d, d->len - 1, d->len);
        } else if (pst.intbl) {
            int keep = pst.intbl;
            pst.intbl = 0;
            put_st(d, s, '\n');
            pst.intbl = keep;
        }
        lastrow_tbl = curtbl;
        pst.intbl = 0;
        rowdirty = 1;
        return;
    }

    if (!strcmp(w, "par") || !strcmp(w, "line") || !strcmp(w, "sect"))
        { ucskip = 0; put_st(d, s, pst.intbl ? ' ' : '\n'); }
    else if (!strcmp(w, "page")) { ucskip = 0; put_st(d, s, CH_PAGE); }
    else if (!strcmp(w, "column")) { ucskip = 0; put_st(d, s, CH_COLBRK); }
    else if (!strcmp(w, "tab")) { ucskip = 0; put_st(d, s, '\t'); }
    else if (!strcmp(w, "pard")) pard();
    else if (!strcmp(w, "ql")) pst.just = J_LEFT;
    else if (!strcmp(w, "qc")) pst.just = J_CENTRE;
    else if (!strcmp(w, "qr")) pst.just = J_RIGHT;
    else if (!strcmp(w, "qj")) pst.just = J_FULL;
    else if (!strcmp(w, "sl") && hasp) pst.sl = (int)(p < 0 ? -p : p);
    else if (!strcmp(w, "slmult") && hasp) pst.slmult = (int)p;
    else if ((!strcmp(w, "li") || !strcmp(w, "lin")) && hasp) pst.li = (int)p;
    else if (!strcmp(w, "fi") && hasp) pst.fi = (int)p;
    else if (!strcmp(w, "outlinelevel") && hasp) pst.toc = p >= 0 && p <= 2 ? (int)p + 1 : 0;
    else if (!strcmp(w, "pnlvlblt")) pst.list = LIST_BULLET;
    else if (!strcmp(w, "pnlvlbody") || !strcmp(w, "pndec")) { if (pst.list != LIST_BULLET) pst.list = LIST_NUMBER; }
    else if (!strcmp(w, "ls") && hasp) {
        int k, j;
        pst.list = LIST_BULLET;
        for (k = 0; k < nls; k++)
            if (lsnum[k] == p)
                for (j = 0; j < nlid; j++) if (lid[j] == lsid[k]) pst.list = lkind[j];
    }
    else if (!strcmp(w, "b")) { if (!hasp || p) s->attr |= A_BOLD; else s->attr &= ~A_BOLD; }
    else if (!strcmp(w, "i")) { if (!hasp || p) s->attr |= A_ITALIC; else s->attr &= ~A_ITALIC; }
    else if (!strcmp(w, "ul") || !strcmp(w, "ulw") || !strcmp(w, "uld") || !strcmp(w, "uldb"))
        { if (!hasp || p) s->attr |= A_UNDER; else s->attr &= ~A_UNDER; }
    else if (!strcmp(w, "ulnone")) s->attr &= ~A_UNDER;
    else if (!strcmp(w, "plain")) { s->attr = 0; s->size = 12; s->fnum = -1; s->color = 0; }
    else if (!strcmp(w, "cf") && hasp) s->color = (p > 0 && p < nctab) ? ctab[p] : 0;
    else if (!strcmp(w, "fs") && hasp) s->size = clampi(p / 2, 4, 120);
    else if (!strcmp(w, "f") && hasp) s->fnum = (int)p;
    else if (!strcmp(w, "deff") && hasp) deffnum = (int)p;
    else if (!strcmp(w, "uc") && hasp) s->uc = (int)p;
    else if (!strcmp(w, "u") && hasp) { put_st(d, s, uni_to_st(p < 0 ? p + 65536L : p)); ucskip = s->uc; }
    else if (!strcmp(w, "emdash") || !strcmp(w, "endash")) put_st(d, s, '-');
    else if (!strcmp(w, "lquote") || !strcmp(w, "rquote")) put_st(d, s, '\'');
    else if (!strcmp(w, "ldblquote") || !strcmp(w, "rdblquote")) put_st(d, s, '"');
    else if (!strcmp(w, "bullet")) put_st(d, s, 0xF9);
}

/* tidy the page set-up after import: recognise standard paper */
static void fix_page(PAGESET *ps)
{
    int i;
    PAGESET t;
    if (ps->w < 144 || ps->h < 144) { ps->w = 595; ps->h = 842; }
    if (ps->paper < 0) {
        for (i = 0; i < NPAPER; i++) {
            page_set_paper(&t, i, ps->w > ps->h);
            if (abs(t.w - ps->w) < 4 && abs(t.h - ps->h) < 4) {
                ps->paper = (short)i; ps->landscape = t.landscape; ps->w = t.w; ps->h = t.h; break;
            }
        }
        if (ps->paper < 0) ps->paper = P_A4;
    }
    if (ps->cols < 1) ps->cols = 1;
}

int rtf_import(DOC *d, const char *path)
{
    RSTATE st[MAXDEPTH];
    int sp = 0, over = 0;
    long n, p = 0;
    unsigned char *b = load_file(path, &n);

    if (!b) return 0;
    if (n < 5 || strncmp((char *)b, "{\\rtf", 5)) {
        int ok = text_import_buf(d, b, n, CS_AUTO, 0);
        free(b);
        return ok;
    }

    nfmap = 0; deffnum = 0; fnlen = 0; ffamily = -1; fcurnum = 0; ucskip = 0; nctab = 0;
    nlid = 0; nls = 0; ncellx = 0; curtbl = 0; lastrow_tbl = 0; rowdirty = 1;
    memset(&eim, 0, sizeof eim);
    pard();
    memset(&st[0], 0, sizeof st[0]);
    st[0].fnum = -1; st[0].size = 12; st[0].uc = 1; st[0].dest = D_NORMAL;
    PS(d).paper = P_A4;

    while (p < n) {
        int c = b[p++];
        RSTATE *s = &st[sp];
        PROG(p, n);
        if (c == '{') {
            if (sp < MAXDEPTH - 1) { st[sp + 1] = st[sp]; sp++; } else over++;
        } else if (c == '}') {
            if (s->dest == D_FONTTBL && fnlen > 0) commit_font();
            if (over) over--;
            else if (sp > 0) {
                if (s->dest >= D_EDPAGE && s->dest != st[sp - 1].dest) ed_close(d, s);
                sp--;
            }
        } else if (c == '\\') {
            if (p >= n) break;
            c = b[p];
            if (isalpha(c)) {
                char w[32];
                int wl = 0, hasp = 0, neg = 0;
                long prm = 0;
                while (p < n && isalpha(b[p])) { if (wl < 31) w[wl++] = (char)b[p]; p++; }
                w[wl] = 0;
                if (p < n && b[p] == '-') { neg = 1; p++; }
                while (p < n && isdigit(b[p])) { hasp = 1; prm = prm * 10 + (b[p] - '0'); p++; }
                if (neg) prm = -prm;
                if (p < n && b[p] == ' ') p++;
                if (!strcmp(w, "bin")) { p += prm; continue; }
                control(d, s, w, hasp, prm);
            } else {
                p++;
                switch (c) {
                case '\'':
                    if (p + 1 < n) {
                        int v = hexval(b[p]) * 16 + hexval(b[p + 1]);
                        p += 2;
                        put_text(d, s, (unsigned char)v);
                    }
                    break;
                case '\\': case '{': case '}': put_text(d, s, (unsigned char)c); break;
                case '*':  if (s->dest == D_NORMAL) s->skip = 1; break;
                case '~':  put_text(d, s, ' '); break;
                case '_':  put_text(d, s, '-'); break;
                case '\r': case '\n': ucskip = 0; put_st(d, s, '\n'); break;
                default: break;
                }
            }
        } else if (c == '\r' || c == '\n') {
            /* line breaks in RTF source are not significant */
        } else {
            put_text(d, s, (unsigned char)c);
        }
    }
    free(b);
    img_free(&eim);
    free(ergb);
    ergb = NULL;
    ergblen = ergbcap = 0;
    fix_page(&PS(d));
    doc_norm_para(d);
    d->dirty = 0;
    return 1;
}

/* ---- export -------------------------------------------------------- */

static void put_rtf_char(FILE *f, unsigned char ch)
{
    if (ch == '\\' || ch == '{' || ch == '}') { fputc('\\', f); fputc(ch, f); }
    else if (ch >= 128) fprintf(f, "\\'%02x", st_to_win(ch));
    else fputc(ch, f);
}

/* the header and footer: our own form, which keeps the {fields}, and a
 * standard \header / \footer for other word processors */
static void write_hf(FILE *f, DOC *d)
{
    static const char *jq[3] = { "\\ql", "\\qc", "\\qr" };
    int i;
    const char *t;
    HEADFOOT *hf = &HF(d);
    if (!hf->first && (hf->text[0][0] || hf->text[1][0])) fputs("\\titlepg\r\n", f);
    for (i = 0; i < 2; i++) {
        if (!hf->text[i][0]) continue;
        fprintf(f, "{\\*\\edhf%d\\edj%d\\edfs%d\\edfirst%d ", i, hf->just[i], hf->size, hf->first);
        for (t = hf->text[i]; *t; t++) put_rtf_char(f, (unsigned char)*t);
        fprintf(f, "}\r\n{\\%s\\pard%s\\plain\\f%d\\fs%d ", i ? "footer" : "header",
                jq[hf->just[i] % 3], default_font(), hf->size * 2);
        for (t = hf->text[i]; *t; t++) {
            if (!strncmp(t, "{page}", 6)) { fputs("\\chpgn ", f); t += 5; }
            else if (!strncmp(t, "{pages}", 7)) { fputs("{\\field{\\*\\fldinst NUMPAGES}}", f); t += 6; }
            else if (!strncmp(t, "{date}", 6)) { fputs("\\chdate ", f); t += 5; }
            else if (!strncmp(t, "{time}", 6)) { fputs("\\chtime ", f); t += 5; }
            else if (!strncmp(t, "{file}", 6)) {
                const char *b;
                for (b = d->base; *b; b++) put_rtf_char(f, (unsigned char)*b);
                t += 5;
            }
            else put_rtf_char(f, (unsigned char)*t);
        }
        fputs("\\par}\r\n", f);
    }
}

/* bytes as PackBits, in hex */
static void put_packbits(FILE *f, const unsigned char *s, long n)
{
    long i = 0;
    while (i < n) {
        long r = i + 1;
        while (r < n && s[r] == s[i] && r - i < 128) r++;
        if (r - i >= 3) { fprintf(f, "%02x%02x", (unsigned)(257 - (r - i)) & 255, s[i]); i = r; continue; }
        r = i;
        while (r < n && r - i < 128 && !(r + 2 < n && s[r] == s[r + 1] && s[r] == s[r + 2])) r++;
        if (r == i) r = i + 1;
        fprintf(f, "%02x", (unsigned)(r - i - 1));
        for (; i < r; i++) fprintf(f, "%02x", s[i]);
    }
}

static void write_objects(FILE *f, DOC *d)
{
    int i, k;
    long r, c;
    for (i = 0; i < d->a.nobj; i++) {
        OBJ *o = &d->a.objs[i];
        fprintf(f, "{\\*\\edobj\\edot%d\\edop%d\\edox%d\\edoy%d\\edow%d\\edoh%d\\edowr%d\\edobr%d\r\n",
                o->type, o->page, o->x, o->y, o->w, o->h, o->wrap, o->border);
        for (k = 0; k < o->nsh && o->type == OB_DRAW; k++) {
            SHAPE *s = &o->sh[k];
            fprintf(f, "{\\*\\edsh\\edst%d\\edsf%d\\edsc%d\\edsl%d\\edsxa%d\\edsya%d\\edsxb%d\\edsyb%d}\r\n",
                    s->type, s->fill, s->color, s->lw, s->x1, s->y1, s->x2, s->y2);
        }
        if (o->type == OB_IMG && o->img >= 0 && o->img < d->nimg) {
            IMG *im = &d->imgs[o->img];
            fprintf(f, "{\\*\\edimgname %s}{\\*\\edimg\\ediw%d\\edih%d\r\n", im->name, im->w, im->h);
            for (r = 0; r < im->h; r++) {
                for (c = 0; c < im->rb; c++) fprintf(f, "%02x", im->bits[r * im->rb + c]);
                fputs("\r\n", f);
            }
            fputs("}", f);
            if (im->rgb) {                       /* the colours, PackBits by rows */
                long rl = (long)im->w * 3;
                fputs("{\\*\\edimgrgb\r\n", f);
                for (r = 0; r < im->h; r++) { put_packbits(f, im->rgb + r * rl, rl); fputs("\r\n", f); }
                fputs("}", f);
            }
        }
        fputs("}\r\n", f);
    }
}

static long twips_w(DOC *d)
{
    PGFMT g;
    page_fmt(d, 0, &g);
    return ((long)PS(d).w - g.ml - g.mr) * 20L;
}

static void para_props(FILE *f, DOC *d, const PARA *p, int intbl, int number)
{
    static const char *jq[4] = { "\\ql", "\\qc", "\\qr", "\\qj" };
    /* the generated list text first: other readers show it, EdWord skips it */
    if (p->list == LIST_BULLET) fputs("{\\pntext\\'95\\tab}", f);
    else if (p->list == LIST_NUMBER) fprintf(f, "{\\pntext %d.\\tab}", number);
    fprintf(f, "\\pard%s%s", intbl ? "\\intbl" : "", jq[p->just & 3]);
    if (p->spacing) fprintf(f, "\\sl%d\\slmult1", p->spacing == LS_DOUBLE ? 480 : 360);
    if (p->left || p->list) fprintf(f, "\\li%d", (p->left || p->first ? p->left : 24) * 20);
    if (p->first || p->list) fprintf(f, "\\fi%d", (p->left || p->first ? p->first : -18) * 20);
    if (p->toc) fprintf(f, "\\outlinelevel%d", p->toc - 1);
    if (p->tocent) fprintf(f, "{\\*\\edtoc%d}", p->tocent);
    if (p->tocent == TOCE_ENTRY) fprintf(f, "\\tqr\\tldot\\tx%ld", twips_w(d) - (long)p->left * 20);
    if (p->list == LIST_BULLET)
        fputs("{\\*\\edlist1}{\\*\\pn\\pnlvlblt\\pnindent360{\\pntxtb\\'95}}", f);
    else if (p->list == LIST_NUMBER)
        fputs("{\\*\\edlist2}{\\*\\pn\\pnlvlbody\\pndec\\pnstart1\\pnindent360{\\pntxta .}}", f);
    fputs("\\ltrpar ", f);     /* the space ends a control word, not a group */
}

static void row_def(FILE *f, DOC *d, const PARA *p)
{
    const TBL *t = &d->a.tbls[p->tbl - 1];
    long x = (long)p->left * 20;
    int i;
    fprintf(f, "\\trowd\\trgaph60\\trleft%ld", x);
    for (i = 0; i < t->ncols; i++) {
        if (t->border)
            fputs("\\clbrdrt\\brdrs\\brdrw10\\clbrdrl\\brdrs\\brdrw10\\clbrdrb\\brdrs\\brdrw10\\clbrdrr\\brdrs\\brdrw10", f);
        x += (long)t->w[i] * 20;
        fprintf(f, "\\cellx%ld", x);
    }
    if (t->chart) fprintf(f, "{\\*\\edchart\\edct%d\\edcp%d\\edcw%d\\edch%d}", t->chart, t->cpos, t->cw, t->ch);
    fputs("\r\n", f);
}

int rtf_export(DOC *d, const char *path)
{
    FILE *f = fopen(path, "wb");
    long k;
    int i, pf = -1, ps = -1, pa = -1, pc = -1, number = 0, prevlist = 0, prevleft = -1;
    PAGESET *pg = &PS(d);

    if (!f) return 0;
    doc_norm_para(d);
    fprintf(f, "{\\rtf1\\ansi\\ansicpg1252\\deff0\r\n{\\fonttbl");
    for (i = 0; i < nfonts; i++) {
        char l[40];
        int fam;
        lower(l, fonts[i].name, sizeof l);
        fam = i == 0 ? 2 : family_of(l);
        fprintf(f, "{\\f%d\\f%s %s;}", i, fam == 2 ? "modern" : fam == 1 ? "roman" : "swiss",
                fonts[i].name);
    }
    fprintf(f, "}\r\n{\\colortbl;");
    for (i = 0; i < NCOLORS; i++)
        fprintf(f, "\\red%d\\green%d\\blue%d;", color_rgb[i][0], color_rgb[i][1], color_rgb[i][2]);
    fprintf(f, "}\r\n\\paperw%d\\paperh%d\\margl%d\\margr%d\\margt%d\\margb%d%s\r\n",
            pg->w * 20, pg->h * 20, pg->ml * 20, pg->mr * 20, pg->mt * 20, pg->mb * 20,
            pg->landscape ? "\\landscape" : "");
    if (pg->cols > 1) fprintf(f, "\\cols%d\\colsx%d\r\n", pg->cols, pg->colgap * 20);
    if (pg->wrapcol > 0) fprintf(f, "{\\*\\edwrapcol%d}\r\n", pg->wrapcol);
    for (i = 0; i < d->a.novr; i++) {
        PGOVR *o = &d->a.ovr[i];
        fprintf(f, "{\\*\\edpage\\edpg%d\\edfol%d\\edml%d\\edmr%d\\edmt%d\\edmb%d\\edcols%d\\edgap%d}\r\n",
                o->page, o->following, o->ml, o->mr, o->mt, o->mb, o->cols, o->colgap);
    }
    write_hf(f, d);
    write_objects(f, d);

    for (k = 0; k < d->len; k++) {
        CELL *c = &d->text[k];
        PROG(k, d->len);
        if (k == 0 || ISBRK(d->text[k - 1].ch)) {
            const PARA *p = PARA_OF(d, c);
            int intbl = p->tbl && p->tbl <= d->a.ntbl;
            if (p->list == LIST_NUMBER) {
                number = (prevlist == LIST_NUMBER && prevleft == p->left) ? number + 1 : 1;
            }
            prevlist = p->list;
            prevleft = p->left;
            if (intbl) row_def(f, d, p);
            para_props(f, d, p, intbl, number);
            pf = -1;
        }
        if (c->font != pf || c->size != ps || c->attr != pa || c->color != pc) {
            fprintf(f, "\\plain\\f%d\\fs%d\\cf%d%s%s%s ", c->font, c->size * 2, c->color + 1,
                    c->attr & A_BOLD ? "\\b" : "",
                    c->attr & A_ITALIC ? "\\i" : "",
                    c->attr & A_UNDER ? "\\ul" : "");
            pf = c->font; ps = c->size; pa = c->attr; pc = c->color;
        }
        switch (c->ch) {
        case '\n':
            if (PARA_OF(d, c)->tbl && PARA_OF(d, c)->tbl <= d->a.ntbl) fputs("\\cell\\row\r\n", f);
            else fputs("\\par\r\n", f);
            break;
        case CH_CELL:   fputs("\\cell ", f); break;
        case CH_PAGE:   fputs("\\page\r\n", f); break;
        case CH_COLBRK: fputs("\\column\r\n", f); break;
        case '\t':      fputs("\\tab ", f); break;
        case '\\': case '{': case '}': fputc('\\', f); fputc(c->ch, f); break;
        default:
            if (c->ch >= 128) fprintf(f, "\\'%02x", st_to_win(c->ch));
            else fputc(c->ch, f);
        }
    }
    /* a table at the very end still needs its row closed */
    if (d->len > 0 && !ISBRK(d->text[d->len - 1].ch) && PARA_OF(d, &d->text[d->len - 1])->tbl)
        fputs("\\cell\\row\r\n", f);
    fprintf(f, "}\r\n");
    i = ferror(f);
    fclose(f);
    if (i) return 0;
    d->dirty = 0;
    return 1;
}
