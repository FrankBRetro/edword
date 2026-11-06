/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemrend.c - GDOS fonts, text metrics, line drawing and the page
 *             renderer behind the bitmap PDF export
 *
 * SpeedoGDOS / FSMGDOS: a scratch buffer is reserved before the fonts are
 * loaded, font errors are reported to us rather than written on the
 * screen, outline fonts are sized with vst_arbpt() (any point size) and
 * the Atari character map is selected, so accented characters print.
 *
 * The document view and the print preview are drawn with the VDI at any
 * scale. PDF pages are drawn in memory by pagedraw.c, with character
 * bitmaps made here from outline fonts (v_getoutline) or the bitmap
 * fonts' own data.
 */
#include <gem.h>
#include <gemx.h>
#include <osbind.h>
#include <mint/cookie.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gemui.h"

static short speedo_err;
static short cur_h = -1, cur_font = -1, cur_size = -1, cur_eff = -1, cur_pen = -1;

/* VDI pens for the text colours: black red green blue cyan magenta yellow grey */
static const short pen_of[NCOLORS] = { 1, 2, 3, 4, 5, 7, 6, 9 };

int color_pen(int c)
{
    short p;
    if (!colour_on || c <= 0 || c >= NCOLORS) return 1;
    p = pen_of[c];
    return p < npens ? p : 1;
}

void style_reset(void) { cur_font = cur_size = cur_eff = cur_pen = -1; }

static int trace;      /* log each VDI call while measuring a new font */

static void set_style_h(short h, int font, int size, int attr, int pen)
{
    short cw, ch, bw, bh, eff, r;
    if (h != cur_h) { cur_h = h; style_reset(); }
    if (font >= nfonts) font = 0;
    if (font != cur_font) {
        if (trace) edlog("  vst_font(%d, %d) ...", h, fonts[font].id);
        r = vst_font(h, fonts[font].id);
        if (trace) edlog("  ... selected font %d", r);
        (void)r;
        cur_font = font; cur_size = -1;
    }
    if (size != cur_size) {
        cw = ch = bw = bh = 0;
        if (fonts[font].outline && !no_arbpt) {
            if (trace) edlog("  vst_arbpt(%d, %d) ...", h, size);
            r = vst_arbpt(h, size, &cw, &ch, &bw, &bh);
        } else {
            if (trace) edlog("  vst_point(%d, %d) ...", h, size);
            r = vst_point(h, size, &cw, &ch, &bw, &bh);
        }
        if (trace) edlog("  ... size %d, char %dx%d, cell %dx%d", r, cw, ch, bw, bh);
        cur_size = size;
    }
    eff = ((attr & A_BOLD) ? 1 : 0) | ((attr & A_ITALIC) ? 4 : 0);
    if (eff != cur_eff) {
        if (trace) edlog("  vst_effects(%d) ...", eff);
        vst_effects(h, eff);
        cur_eff = eff;
    }
    if (pen != cur_pen) {
        if (trace) edlog("  vst_color(%d) ...", pen);
        vst_color(h, pen);
        cur_pen = pen;
    }
}

void set_text_style(int font, int size, int attr, int color)
{
    set_style_h(vhandle, font, size, attr, color_pen(color));
}

/* the AES system font, for the codes pane and toolbar */
void set_sys_font(void)
{
    short dmy;
    cur_h = vhandle;
    style_reset();
    vst_font(vhandle, 1);
    vst_height(vhandle, hchar >= 16 ? 13 : 6, &dmy, &dmy, &dmy, &dmy);
    vst_effects(vhandle, 0);
    vst_color(vhandle, 1);
    vst_alignment(vhandle, 0, 0, &dmy, &dmy);           /* left, baseline */
}

/* vqt_fontinfo with full-size reply buffers. gemlib's binding keeps only
 * 2 intout and 10 ptsout words on the stack; SpeedoGDOS returns more for
 * outline fonts, which overwrote EdWord's stack and crashed it. */
static short fi_contrl[16], fi_intin[16], fi_ptsin[16], fi_intout[512], fi_ptsout[256];

static void safe_fontinfo(short h, short dist[5], short *maxw)
{
    VDIPB pb;
    static int logged;
    memset(fi_contrl, 0, sizeof fi_contrl);
    memset(fi_ptsout, 0, sizeof fi_ptsout);
    fi_contrl[0] = 131;              /* vqt_fontinfo */
    fi_contrl[6] = h;
    pb.control = fi_contrl;
    pb.intin = fi_intin;
    pb.ptsin = fi_ptsin;
    pb.intout = fi_intout;
    pb.ptsout = fi_ptsout;
    vdi(&pb);
    if (!logged) {
        edlog("  vqt_fontinfo returned %d intout and %d ptsout words (standard: 2 and 5 points)",
              fi_contrl[4], fi_contrl[2]);
        logged = 1;
    }
    *maxw = fi_ptsout[0];
    dist[0] = fi_ptsout[1]; dist[1] = fi_ptsout[3]; dist[2] = fi_ptsout[5];
    dist[3] = fi_ptsout[7]; dist[4] = fi_ptsout[9];
}

/* ---- metrics cache ----------------------------------------------- */

#define NCACHE 16
static struct { short font, size, attr; long stamp; METRICS m; } mc[NCACHE];
static long mstamp;
static int  nlog;

const METRICS *get_metrics(int font, int size, int attr)
{
    int i, slot = 0;
    short ext[8], base, dist[5], maxw;
    char s[3];

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
    trace = nlog < 40;
    if (trace)
        edlog("metrics: font %d (id %d, %s%s) %dpt attr %d", font, fonts[font].id, fonts[font].name,
              fonts[font].outline ? ", outline" : "", size, attr);
    set_style_h(vhandle, font, size, attr, 1);

    if (fonts[font].outline) {
        /* outline fonts: SpeedoGDOS's own advance widths */
        short ax, ay, rx, ry;
        if (trace) edlog("  vqt_advance for 224 characters ...");
        for (i = 0; i < 256; i++) {
            short w = 0;
            if (i >= 32) {
                vqt_advance(vhandle, (short)i, &ax, &ay, &rx, &ry);
                w = ax;
                if (w < 0) w = 0;
                if ((attr & A_BOLD) && w) w++;
                if (trace && i == 32) edlog("  ... space: advance %d,%d remainder %d,%d", ax, ay, rx, ry);
            }
            mc[slot].m.w[i] = w;
        }
        base = mc[slot].m.w['X'];
    } else {
        /* width of c = extent("Xc") - extent("X"): cancels italic overhang */
        if (trace) edlog("  vqt_extent for 224 characters ...");
        s[0] = 'X'; s[1] = 0;
        vqt_extent(vhandle, s, ext);
        base = ext[2] - ext[0];
        for (i = 0; i < 256; i++) {
            short w = 0;
            if (i >= 32) {
                s[1] = (char)i; s[2] = 0;
                vqt_extent(vhandle, s, ext);
                w = (ext[2] - ext[0]) - base;
                if (w < 0) w = 0;
            }
            mc[slot].m.w[i] = w;
        }
    }
    if (trace) edlog("  widths done; vqt_fontinfo ...");
    safe_fontinfo(vhandle, dist, &maxw);
    mc[slot].m.asc  = dist[4];
    mc[slot].m.desc = dist[0] + 1;
    if (nlog < 40) {
        nlog++;
        edlog("  ok: 'X' %d px wide, ascent %d, descent %d%s", base, dist[4], dist[0],
              gdos_fsm && speedo_err ? " (Speedo error reported)" : "");
        if (gdos_fsm && speedo_err) { edlog("  Speedo error code %d", speedo_err); speedo_err = 0; }
    }
    trace = 0;
    mc[slot].font = font; mc[slot].size = size; mc[slot].attr = attr;
    mc[slot].stamp = ++mstamp;
    return &mc[slot].m;
}

/* ---- fonts --------------------------------------------------------- */

void fonts_load(void)
{
    short i, k, n = 1, add;
    long g;
    char name[40];

    has_gdos = vq_gdos() != 0;
    g = has_gdos ? vq_vgdos() : GDOS_NONE;
    edlog("fonts: vq_gdos %d, vq_vgdos %08lx (%s)", has_gdos, g,
          g == GDOS_FSM ? "SpeedoGDOS/FSMGDOS" : g == GDOS_FNT ? "FontGDOS" :
          g == GDOS_NONE ? "none" : "original GDOS/AMCGDOS/NVDI");
    if (safe_mode) { has_gdos = 0; g = GDOS_NONE; edlog("fonts: safe mode, skipping GDOS"); }
    gdos_fsm = (g == GDOS_FSM);
    if (gdos_fsm) {
        long fsmc;
        if (Getcookie(0x46534D43L, &fsmc) == C_FOUND && fsmc)    /* 'FSMC' */
            edlog("fonts: FSMC cookie %08lx, type %.4s", fsmc, (char *)fsmc);
        edlog("fonts: vst_scratch(SCRATCH_BOTH)");
        vst_scratch(vhandle, SCRATCH_BOTH);     /* must precede vst_load_fonts */
        edlog("fonts: vst_error(APP_ERROR)");
        vst_error(vhandle, APP_ERROR, &speedo_err);
    }
    if (has_gdos) {
        edlog("fonts: vst_load_fonts ...");
        add = vst_load_fonts(vhandle, 0);
        edlog("fonts: vst_load_fonts loaded %d more fonts", add);
        if (add > 0) n += add;
    }
    if (gdos_fsm) {
        edlog("fonts: vst_charmap(1)");
        vst_charmap(vhandle, 1);                /* Atari character set */
        if (speedo_err) edlog("fonts: Speedo error %d after loading", speedo_err);
    }

    nfonts = 0;
    for (i = 1; i <= n && nfonts < MAX_FONTS; i++) {
        short id;
        int dup = 0;
        memset(name, 0, sizeof name);
        id = vqt_name(vhandle, i, name);         /* name[32] flags an outline font */
        for (k = 0; k < nfonts; k++) if (fonts[k].id == id) dup = 1;
        edlog("fonts: %2d: id %5d  %-32.32s%s%s", i, id, name, name[32] ? "  outline" : "",
              dup ? "  (duplicate, skipped)" : "");
        if (dup) continue;
        fonts[nfonts].id = id;
        fonts[nfonts].outline = (short)(gdos_fsm && name[32] != 0);
        name[32] = 0;
        for (k = 31; k >= 0 && (name[k] == ' ' || name[k] == 0); k--) name[k] = 0;
        strcpy(fonts[nfonts].name, name[0] ? name : "System");
        nfonts++;
    }
    edlog("fonts: %d fonts available", nfonts);
    style_reset();
}

void fonts_unload(void)
{
    if (has_gdos) { edlog("fonts: vst_unload_fonts"); vst_unload_fonts(vhandle, 0); }
}


/* ---- drawing a page with the VDI ------------------------------------- */

/* Layout positions are multiplied by pn/pd and point sizes by sn/sd: the
 * zoomed document view and the print preview draw pages at other sizes.
 * Text that would come out tiny is "greeked" as a bar. */
typedef struct { long pn, pd, sn, sd; } SCALE;

#define SP(v, sc) ((v) * (sc)->pn / (sc)->pd)

static void greek(short h, long x1, long x2, long base, int pen, long xh)
{
    short pl[4];
    long i;
    vsl_color(h, pen); vsl_type(h, 1); vsl_width(h, 1);
    for (i = 0; i < xh; i++) {
        pl[0] = (short)x1; pl[2] = (short)(x2 > x1 ? x2 - 1 : x1);
        pl[1] = pl[3] = (short)(base - 1 - i);
        v_pline(h, 2, pl);
    }
}

static void hline(short h, long x1, long x2, long y, int pen, int type)
{
    short pl[4];
    vsl_color(h, pen); vsl_type(h, type); vsl_width(h, 1);
    pl[0] = (short)x1; pl[2] = (short)x2; pl[1] = pl[3] = (short)y;
    v_pline(h, 2, pl);
    vsl_type(h, 1);
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

/* a wavy line under misspelt words of line li (spelling as you type) */
static void live_marks(short h, DOC *d, long li, long ox, long base, const SCALE *sc)
{
    LWALK w;
    long ws = -1, wx = 0;
    int pen = npens >= 4 ? 2 : 1;
    lw_init(&w, d, li);
    for (;;) {
        int at_end = w.k >= w.end || ISEND(d->text[w.k].ch);
        unsigned char c = at_end ? ' ' : d->text[w.k].ch;
        int in = wch(c) || (c == '\'' && ws >= 0 && w.k + 1 < w.end && wch(d->text[w.k + 1].ch));
        if (in && ws < 0) { ws = w.k; wx = w.x; }
        if (!in && ws >= 0) {
            if (!(d->cursor == w.k && d->anchor < 0) && word_misspelt(d, ws, w.k)) {
                short pl[64];
                long x1 = ox + SP(wx, sc), x2 = ox + SP(w.x, sc), y = base + SP(2, sc) + 1, x;
                int n = 0, up = 0;
                for (x = x1; x <= x2 && n < 62; x += 2) { pl[n++] = (short)x; pl[n++] = (short)(y + (up ? 0 : 1)); up = !up; }
                if (n >= 4) { vsl_color(h, pen); vsl_type(h, 1); vsl_width(h, 1); v_pline(h, (short)(n / 2), pl); }
            }
            ws = -1;
        }
        if (at_end) break;
        lw_step(&w);
    }
}

/* Draw line li: ox is the device x of the line's own left edge, base its
 * baseline. color >= 0 draws only cells of that colour; pen >= 0 forces a
 * pen. Words are drawn one at a time at their layout positions, so
 * outline font rounding never accumulates; underlines are drawn as lines
 * so they run unbroken under spaces. */
/* The point size to draw at n/den of size. A bitmap font may not come
 * in the scaled size, and the VDI then gives a bigger one; step down
 * until the characters fit the space the layout gives them. */
static int zoom_size(short h, int font, int size, int attr, int pen, const SCALE *sc)
{
    static int kf = -1, ks, ka, kr;
    static long kn, kd;
    int sz = (int)(size * sc->sn / sc->sd);
    long want;
    short cw, l, r;
    if (sc->sn == sc->sd || sz < 5 || (font < nfonts && fonts[font].outline)) return sz;
    if (font == kf && size == ks && (attr & A_BOLD) == ka && sc->sn == kn && sc->sd == kd) return kr;
    want = get_metrics(font, size, attr)->w['n'] * sc->sn / sc->sd + 1;
    for (; sz > 5; sz--) {
        set_style_h(h, font, sz, attr & A_BOLD, pen);
        vqt_width(h, 'n', &cw, &l, &r);
        if (cw <= want) break;
    }
    kf = font; ks = size; ka = attr & A_BOLD; kn = sc->sn; kd = sc->sd; kr = sz;
    return sz;
}

static void draw_line_ex(short h, DOC *d, long li, long ox, long base, const SCALE *sc,
                         int color, int pen)
{
    LWALK w;
    LINE *ln = &d->lines[li];
    char buf[132];
    long xs[132];
    short pl[4];

    /* list marker */
    if (ln->mark && ln->len && (color < 0 || d->text[ln->start].color == color)) {
        CELL st = d->text[ln->start];
        int p = pen >= 0 ? pen : color_pen(st.color);
        long mx = ox + SP(ln->markx - ln->x, sc);
        if (ln->mark < 0) {
            const METRICS *m = get_metrics(st.font, st.size, st.attr);
            long r = SP(m->asc / 6 + 1, sc);
            if (r < 1) r = 1;
            vsf_interior(h, FIS_SOLID); vsf_color(h, p); vsf_perimeter(h, 1);
            v_circle(h, (short)(mx + r + 1), (short)(base - SP(m->asc, sc) / 3), (short)r);
        } else {
            long sz = st.size * sc->sn / sc->sd;
            mx = ox + SP(doc_marker_x(d, li) - ln->x, sc);
            doc_marker(d, li, buf);
            if (sz >= 5) {
                set_style_h(h, st.font, (int)sz, st.attr, p);
                v_gtext(h, (short)mx, (short)base, buf);
            }
        }
    }
    lw_init(&w, d, li);
    while (w.k < w.end) {
        CELL *c = &d->text[w.k], st;
        long sx, x, sz;
        int n = 0;
        if (ISEND(c->ch)) break;
        if (c->ch == '\t' && w.k - ln->start == ln->tabk && (color < 0 || c->color == color)) {
            long x1 = ox + SP(w.x, sc), x2 = ox + SP(w.x + ln->tabw, sc) - SP(4, sc);
            short dots[2];
            int p = pen >= 0 ? pen : color_pen(c->color);
            long step = SP(6, sc) > 2 ? SP(6, sc) : 3, xx;
            vsm_color(h, p); vsm_type(h, 1);
            for (xx = ((x1 - ox) / step + 1) * step + ox; xx < x2; xx += step) {
                dots[0] = (short)xx; dots[1] = (short)(base - 1);
                v_pmarker(h, 1, dots);
            }
        }
        if (c->ch == ' ' || c->ch == '\t' || (color >= 0 && c->color != color)) { lw_step(&w); continue; }
        st = *c;
        sx = w.x;
        while (w.k < w.end && n < 128) {
            c = &d->text[w.k];
            if (ISEND(c->ch) || c->ch == ' ' || c->ch == '\t' || c->font != st.font ||
                c->size != st.size || ((c->attr ^ st.attr) & (A_BOLD | A_ITALIC)) || c->color != st.color)
                break;
            xs[n] = w.x;
            buf[n++] = (char)c->ch;
            lw_step(&w);
        }
        buf[n] = 0;
        x = ox + SP(sx, sc);
        if (x > -2000 && x < 30000) {
            int p = pen >= 0 ? pen : color_pen(st.color);
            sz = zoom_size(h, st.font, st.size, st.attr, p, sc);
            if (sz < 5) {
                long xh = pt2px_y(st.size) * sc->sn / sc->sd / 3;
                greek(h, x, ox + SP(w.x, sc), base, p, xh < 1 ? 1 : xh);
            } else {
#ifdef EDLOG
                static int first = 1;
#endif
                set_style_h(h, st.font, (int)sz, st.attr, p);
#ifdef EDLOG
                if (first) edlog("first text: font %d %dpt, \"%.20s\"", st.font, st.size, buf);
#endif
                if (sc->sn == sc->sd && !(st.font < nfonts && fonts[st.font].outline))
                    v_gtext(h, (short)x, (short)base, buf);
                else if (n == 1) v_gtext(h, (short)x, (short)base, buf);
                else {
                    /* zoomed (the screen font may not come in this size),
                     * or an outline font (SpeedoGDOS's v_gtext spaces the
                     * characters its own way, not by the advance widths the
                     * layout uses): the word stretched or squeezed to the
                     * width the layout gives it, in one call, so each
                     * character is within a pixel of its place (one call
                     * a character was much slower, above all with GDOS) */
                    long wpx = ox + SP(w.x, sc) - x;
                    (void)xs;
                    if (wpx > 0 && wpx < 32000) v_justified(h, (short)x, (short)base, buf, (short)wpx, 0, 1);
                    else v_gtext(h, (short)x, (short)base, buf);
                }
#ifdef EDLOG
                if (first) { edlog("  drawn"); first = 0; }
#endif
            }
        }
    }

    lw_init(&w, d, li);
    while (w.k < w.end) {
        CELL *c = &d->text[w.k];
        long ux, t, i, uy;
        int col;
        if (ISEND(c->ch)) break;
        if (!(c->attr & A_UNDER) || (color >= 0 && c->color != color)) { lw_step(&w); continue; }
        ux = w.x;
        col = c->color;
        uy = get_metrics(c->font, c->size, c->attr)->desc / 2 + 1;
        t = pt2px_y(c->size) / 16 + 1;
        while (w.k < w.end && !ISEND(d->text[w.k].ch) && (d->text[w.k].attr & A_UNDER) &&
               d->text[w.k].color == col)
            lw_step(&w);
        vsl_color(h, pen >= 0 ? pen : color_pen(col));
        vsl_type(h, 1);
        vsl_width(h, 1);
        t = SP(t, sc);
        if (t < 1) t = 1;
        for (i = 0; i < t; i++) {
            pl[0] = (short)(ox + SP(ux, sc));
            pl[2] = (short)(ox + SP(w.x, sc) - 1);
            pl[1] = pl[3] = (short)(base + SP(uy, sc) + i);
            v_pline(h, 2, pl);
        }
    }
}

/* a string with metrics m at (x, base); an outline font one character at
 * a time at the widths the layout uses (as draw_line_ex does) */
static void gtext_m(short h, int font, long x, long base, const char *t, const METRICS *m, const SCALE *sc)
{
    char one[2];
    long cx = 0;
    if (!(font < nfonts && fonts[font].outline)) { v_gtext(h, (short)x, (short)base, t); return; }
    one[1] = 0;
    for (; *t; t++) {
        if (*t != ' ') { one[0] = *t; v_gtext(h, (short)(x + SP(cx, sc)), (short)base, one); }
        cx += m->w[(unsigned char)*t];
    }
}

/* the header and footer of a page; (ox,oy) is the page's top left corner */
static void draw_hf(short h, DOC *d, int page, int pen, const SCALE *sc, long ox, long oy)
{
    char buf[120];
    int i, k, f = default_font();
    const METRICS *m = get_metrics(f, HF(d).size, 0);
    PGFMT g;
    long tw, mx, mt, mb, ph = page_h_px(d), lh = m->asc + m->desc;
    long w, x, base, sz = HF(d).size * sc->sn / sc->sd;

    page_fmt(d, page, &g);
    tw = text_w_px(d, page); mx = pt2px_x(g.ml); mt = pt2px_y(g.mt); mb = pt2px_y(g.mb);
    for (i = 0; i < 2; i++) {
        if (!hf_visible(d, i, page)) continue;
        hf_expand(d, i, page, doc_npages(d), pref_date, buf, sizeof buf);
        for (w = 0, k = 0; buf[k]; k++) w += m->w[(unsigned char)buf[k]];
        x = mx + (HF(d).just[i] == J_CENTRE ? (tw - w) / 2 : HF(d).just[i] == J_RIGHT ? tw - w : 0);
        if (i == 0) base = mt > lh ? (mt - lh) / 2 + m->asc : m->asc;
        else base = ph - (mb > lh ? (mb + lh) / 2 - m->asc : lh) + m->asc;
        if (sz < 5) greek(h, ox + SP(x, sc), ox + SP(x + w, sc), oy + SP(base, sc), pen,
                          pt2px_y(HF(d).size) * sc->sn / sc->sd / 3 + 1);
        else {
            set_style_h(h, f, (int)sz, 0, pen);
            gtext_m(h, f, ox + SP(x, sc), oy + SP(base, sc), buf, m, sc);
        }
    }
}

/* the grid of a table row */
static void draw_row(short h, DOC *d, TROW *t, const SCALE *sc, long ox, long oy, int pen)
{
    const TBL *T = &d->a.tbls[t->tbl];
    long x = ox + SP(t->x, sc), y0 = oy + SP(t->y, sc), y1 = oy + SP(t->y + t->h, sc), k = 0;
    int i;
    short pl[4];
    for (i = 0; i < T->ncols; i++) k += pt2px_x(T->w[i]);
    hline(h, x, x + SP(k, sc), y0, pen, 1);
    hline(h, x, x + SP(k, sc), y1, pen, 1);
    vsl_color(h, pen);
    pl[1] = (short)y0; pl[3] = (short)y1;
    pl[0] = pl[2] = (short)x;
    v_pline(h, 2, pl);
    for (i = 0, k = 0; i < T->ncols; i++) {
        k += pt2px_x(T->w[i]);
        pl[0] = pl[2] = (short)(x + SP(k, sc));
        v_pline(h, 2, pl);
    }
}

/* a table's chart: colours on colour screens, else patterns */
static void draw_vdi_chart(short h, DOC *d, int t, long ox, long oy, const SCALE *sc, int fg, int dark)
{
    static CHOP op[CH_MAXOPS];
    static const short pint[7] = { FIS_SOLID, FIS_PATTERN, FIS_HATCH, FIS_HATCH, FIS_HATCH, FIS_HATCH, FIS_PATTERN };
    static const short psty[7] = { 1, 4, 1, 3, 2, 6, 2 };
    const CHBOX *cb = &d->cbox[t];
    const METRICS *m = get_metrics(default_font(), CHART_PT, 0);
    long bx = ox + SP(cb->x, sc), by = oy + SP(cb->y, sc), sz = CHART_PT * sc->sn / sc->sd;
    int n = chart_ops(d, t, cb->w, cb->h, op, CH_MAXOPS), i, k, colour = colour_on && npens >= 8 && !dark;
    short p[2 * (CH_MAXPTS + 1)];
    vsl_type(h, 1); vsl_width(h, 1); vsl_color(h, fg);
    for (i = 0; i < n; i++) {
        const CHOP *o = &op[i];
        for (k = 0; k < o->n; k++) {
            p[2 * k] = (short)(bx + SP(o->xy[2 * k], sc));
            p[2 * k + 1] = (short)(by + SP(o->xy[2 * k + 1], sc));
        }
        if (o->op == CO_POLY && o->n >= 3) {
            int sr = o->series % 7;
            vswr_mode(h, MD_REPLACE);
            vsf_perimeter(h, 0);
            if (colour) {
                vsf_interior(h, FIS_SOLID); vsf_color(h, color_pen(chart_color(o->series)));
            } else {
                vsf_interior(h, pint[sr]); vsf_style(h, psty[sr]); vsf_color(h, fg);
                if (pint[sr] != FIS_SOLID) {           /* a pattern on white */
                    vsf_interior(h, FIS_SOLID); vsf_color(h, dark ? 0 : 0);
                    v_fillarea(h, o->n, p);
                    vsf_interior(h, pint[sr]); vsf_style(h, psty[sr]); vsf_color(h, fg);
                    vswr_mode(h, MD_TRANS);
                }
            }
            v_fillarea(h, o->n, p);
            if (o->shade == 2) {                       /* a 3D side: a little darker */
                vswr_mode(h, MD_TRANS);
                vsf_interior(h, FIS_PATTERN); vsf_style(h, 2); vsf_color(h, fg);
                v_fillarea(h, o->n, p);
            }
            vswr_mode(h, MD_TRANS);
            p[2 * o->n] = p[0]; p[2 * o->n + 1] = p[1];
            vsl_color(h, fg);
            v_pline(h, (short)(o->n + 1), p);
        } else if (o->op == CO_LINE) {
            vswr_mode(h, MD_TRANS);
            vsl_color(h, fg);
            v_pline(h, 2, p);
        } else if (o->op == CO_TEXT && sz >= 5) {
            long w = 0;
            for (k = 0; o->text[k]; k++) w += m->w[(unsigned char)o->text[k]];
            if (o->align == 1) w /= 2; else if (o->align == 0) w = 0;
            vswr_mode(h, MD_TRANS);
            set_style_h(h, default_font(), (int)sz, 0, fg);
            gtext_m(h, default_font(), bx + SP(o->xy[0] - w, sc), by + SP(o->xy[1], sc), o->text, m, sc);
        }
    }
    vsf_interior(h, FIS_SOLID);
    vswr_mode(h, MD_TRANS);
}

/* Draw a page with its top left corner at device (ox, oy), at n/den of
 * its size. pen >= 0 forces a pen (the dark display modes); live: the
 * editing view (spelling marks, heading marks, dotted table grids). */
void draw_page(DOC *d, int page, long ox, long oy, long n, long den, const GRECT *clip, int pen, int live)
{
    SCALE sc;
    long li, a, e, r;
    int i, dark = pen >= 0;
    int fg = dark ? pen : 1;
    sc.pn = sc.sn = n; sc.pd = sc.sd = den;
    if (page < 0 || page >= d->npages) return;
    vswr_mode(vhandle, MD_TRANS);
    /* only what reaches the area being drawn */
    for (i = 0; i < d->a.nobj; i++)
        if (d->a.objs[i].page == page) {
            const OBJ *o = &d->a.objs[i];
            long y0 = oy + tw2px_ys(o->y, n, den) - 4, y1 = oy + tw2px_ys(o->y + o->h, n, den) + 4;
            if (clip && (y0 > clip->g_y + clip->g_h || y1 < clip->g_y)) continue;
            draw_vdi_obj(d, o, ox, oy, n, den);
        }
    vswr_mode(vhandle, MD_TRANS);
    for (r = 0; r < d->nrows; r++) {
        TROW *t = &d->rows[r];
        if (t->page != page || t->tbl < 0 || t->tbl >= d->a.ntbl) continue;
        if (clip && (oy + SP(t->y, &sc) - 2 > clip->g_y + clip->g_h || oy + SP(t->y + t->h, &sc) + 2 < clip->g_y)) continue;
        if (d->a.tbls[t->tbl].border) draw_row(vhandle, d, t, &sc, ox, oy, fg);
        else if (live) {          /* a grid you can see while editing, but which doesn't print */
            vsl_type(vhandle, 3);
            draw_row(vhandle, d, t, &sc, ox, oy, fg);
            vsl_type(vhandle, 1);
        }
    }
    for (i = 0; i < d->a.ntbl; i++)
        if (d->a.tbls[i].chart && d->cbox[i].page == page && d->cbox[i].w > 0) {
            long cy = oy + SP(d->cbox[i].y, &sc);
            if (!clip || (cy <= clip->g_y + clip->g_h && cy + SP(d->cbox[i].h, &sc) >= clip->g_y))
                draw_vdi_chart(vhandle, d, i, ox, oy, &sc, fg, dark);
        }
    a = d->pgline[page];
    e = d->pgline[page + 1];
    for (li = a; li < e; li++) {
        LINE *ln = &d->lines[li];
        long y = oy + SP(ln->py, &sc), lh = SP(ln->asc + ln->desc, &sc);
        if (clip && (y > clip->g_y + clip->g_h || y + lh + 4 < clip->g_y)) continue;
        draw_line_ex(vhandle, d, li, ox + SP(ln->x, &sc), oy + SP(ln->py + ln->asc, &sc), &sc, -1, pen);
        if (live && pref_live) live_marks(vhandle, d, li, ox + SP(ln->x, &sc), oy + SP(ln->py + ln->asc, &sc), &sc);
        if (live && (ln->flags & LF_FIRST) && ln->len) {
            const PARA *p = PARA_OF(d, &d->text[ln->start]);
            if (p->toc) {                      /* a heading the contents will list */
                char t[4];
                short dmy;
                t[0] = 'H'; t[1] = (char)('0' + p->toc); t[2] = 0;
                cur_h = -1;
                vst_font(vhandle, 1);
                vst_height(vhandle, hchar >= 16 ? 6 : 4, &dmy, &dmy, &dmy, &dmy);
                vst_color(vhandle, npens >= 4 ? 3 : fg);
                v_gtext(vhandle, (short)(ox + 2), (short)(oy + SP(ln->py + ln->asc, &sc)), t);
                style_reset();
                cur_h = -1;
            }
        }
    }
    {   /* the header and footer: only if their margins are in the area */
        PGFMT g;
        page_fmt(d, page, &g);
        if (!clip || oy + SP(pt2px_y(g.mt), &sc) >= clip->g_y ||
            oy + SP(page_h_px(d) - pt2px_y(g.mb), &sc) <= clip->g_y + clip->g_h)
            draw_hf(vhandle, d, page, fg, &sc, ox, oy);
    }
    vswr_mode(vhandle, MD_REPLACE);
}

/* the print preview: page drawn at n/den of its size */
void draw_preview_page(DOC *d, int page, long ox, long oy, long n, long den)
{
    draw_page(d, page, ox, oy, n, den, NULL, -1, 0);
}

/* The Line-A font header (as in MiNTLib's <mint/linea.h>, which can't be
 * included here because it defines its own LINE). */
typedef struct lafont {
    short font_id, size;
    char  name[32];
    short first_ade, last_ade, top, ascent, half, descent, bottom;
    short max_char_width, max_cell_width, left_offset, right_offset;
    short thicken, ul_size, lighten, skew, flags;
    char  *h_table;
    short *off_table;
    char  *dat_table;
    short form_width, form_height;
    struct lafont *next_font;
} LAFONT;
extern void linea0(void);
extern char *__aline;               /* Line-A variables, set by linea0()  */
extern LAFONT **__fonts;            /* the three system fonts             */

/* ---- PDF page rendering: in memory, nothing is drawn on screen ------ */

/* Each character is turned into a small bitmap and OR-ed into the page
 * band in memory:
 *  - outline fonts (SpeedoGDOS / FSMGDOS / NVDI): the outline from
 *    v_getoutline() is filled by our own scan converter, at the PDF's
 *    resolution, so 2x and 3x PDFs are sharp;
 *  - bitmap fonts: the glyphs come from the font's own data, found
 *    through the Line-A font list (the system font always is), and are
 *    enlarged for 2x and 3x.
 * Bold and italic are added the way the VDI does it: thickening and
 * slanting. Characters are placed at their laid-out positions, so the
 * PDF lines break and justify exactly as on screen. */

typedef struct {
    short font, size, attr, scale, ch;
    short w, h, ox, oy;             /* bitmap size; offset from the pen   */
    unsigned char *bits;            /* rows of (w + 7) / 8 bytes          */
    short next;                     /* the next in its hash chain, or -1  */
} GLYPH;

#define NGLYPH  384
#define MAXVERT 1500
static GLYPH gcache[NGLYPH];
static int   ngc;
#define GHASH 256                        /* glyphs are found by a hash, not a search */
static short ghead[GHASH];
static int   ghash_ok;
#define GKEY(f, s, a, c, sc) ((unsigned)((c) * 7 + (f) * 31 + (s) * 13 + (a) * 3 + (sc)) & (GHASH - 1))
static short oxy[4 * MAXVERT];           /* twice what GDOS is told  */
static char  obez[2 * MAXVERT];

/* how a font's outline coordinates are oriented (found from 'H' and 'p') */
enum { OR_UNKNOWN, OR_DOWN_BASE, OR_UP_BASE, OR_DOWN_TOP, OR_NONE, OR_GLYPH_TOP };
#ifdef EDLOG
static const char *or_name[] = { "?", "y down from the baseline", "y up from the baseline",
                                 "y down from the top of the cell", "none",
                                 "y down from the top of each character" };
#endif
static signed char orient[MAX_FONTS];

static void glyph_cache_free(void)
{
    int i;
    for (i = 0; i < ngc; i++) free(gcache[i].bits);
    ngc = 0;
    for (i = 0; i < GHASH; i++) ghead[i] = -1;
    ghash_ok = 1;
}

/* PDF trace in EDWORD.LOG: everything up to the first few hundred glyphs */
#ifdef EDLOG
static int nglog;
#define GLOG (nglog < 400)
#else
#define GLOG 0
#endif

/* v_getoutline, made by hand like safe_fontinfo(): the reply buffers are
 * far bigger than the standard says, and the outline buffers have room
 * to spare beyond the size we tell GDOS, in case a GDOS writes more. */
static short go_contrl[16], go_intin[16], go_ptsin[16], go_intout[256], go_ptsout[256];
#define NPI ((int)(sizeof(void *) / sizeof(short)))

static int outline_of(int font, int psize, int ch)
{
    VDIPB pb;
    short nv;
    void *pxy = oxy, *pbez = obez;
    set_style_h(vhandle, font, psize, 0, 1);
    memset(go_contrl, 0, sizeof go_contrl);
    memset(go_intout, 0, sizeof go_intout);
    go_contrl[0] = 243;                               /* v_getoutline */
    go_contrl[3] = (short)(2 + 2 * NPI);
    go_contrl[6] = vhandle;
    go_intin[0] = (short)ch;
    go_intin[1] = MAXVERT;
    memcpy(&go_intin[2], &pxy, sizeof pxy);
    memcpy(&go_intin[2 + NPI], &pbez, sizeof pbez);
    pb.control = go_contrl; pb.intin = go_intin; pb.ptsin = go_ptsin;
    pb.intout = go_intout; pb.ptsout = go_ptsout;
    if (GLOG) edlog("    v_getoutline: font %d at %d pt, char %d ...", fonts[font].id, psize, ch);
    vdi(&pb);
    nv = go_intout[0];
    if (GLOG) {
        edlog("    ... returned %d points (%d intout, %d ptsout words)%s", nv, go_contrl[4], go_contrl[2],
              nv > MAXVERT ? " - too many, ignored" : "");
        if (gdos_fsm && speedo_err) { edlog("    Speedo error code %d", speedo_err); speedo_err = 0; }
    }
    return nv > 0 && nv <= MAXVERT ? nv : 0;
}

/* v_getbitmap_info's y offset of a character (1/65536 pixel), made by
 * hand with big reply buffers. Only used to find where each character
 * sits when the outlines don't say (OR_GLYPH_TOP). */
static long bitmap_yoff(int ch)
{
    VDIPB pb;
    short in = (short)ch;
    memset(go_contrl, 0, sizeof go_contrl);
    memset(go_intout, 0, sizeof go_intout);
    go_contrl[0] = 239;                               /* v_getbitmap_info */
    go_contrl[3] = 1;
    go_contrl[6] = vhandle;
    pb.control = go_contrl; pb.intin = &in; pb.ptsin = go_ptsin;
    pb.intout = go_intout; pb.ptsout = go_ptsout;
    vdi(&pb);
    return (long)go_intout[8] * 65536L + (unsigned short)go_intout[9];
}

static void y_range(int n, long *mn, long *mx);

/* For OR_GLYPH_TOP fonts: where the top of a character is, from the
 * bitmap y offset. 'H' and 'x' both stand on the baseline, so their
 * outlines give their tops; the offsets of other characters are then
 * mapped the same way, whatever sign, units or origin the GDOS uses. */
typedef struct { short font, size, valid; long yH, yx, tH, tx; } CALIB;
static CALIB cal[16];
static int   ncal;

static CALIB *calib(int font, int psize)
{
    CALIB *c;
    long mn, mx;
    int i, n;
    for (i = 0; i < ncal; i++)
        if (cal[i].font == font && cal[i].size == psize) return &cal[i];
    c = &cal[ncal < 16 ? ncal++ : (ncal = 1, 0)];
    c->font = (short)font; c->size = (short)psize;
    n = outline_of(font, psize, 'H'); y_range(n, &mn, &mx); c->tH = -mx; c->yH = bitmap_yoff('H');
    n = outline_of(font, psize, 'x'); y_range(n, &mn, &mx); c->tx = -mx; c->yx = bitmap_yoff('x');
    c->valid = c->yH != c->yx && c->tH != c->tx;
    edlog("pdf: font %d %d pt: 'H' top %ld, bitmap y %ld; 'x' top %ld, bitmap y %ld%s",
          fonts[font].id, psize, c->tH, c->yH, c->tx, c->yx,
          c->valid ? "" : " - no bitmap offsets, characters stand on the baseline");
    return c;
}

/* the top of a character (pixels, y down from the baseline); the
 * character's outline must just have been fetched (n points) */
static long glyph_top(int font, int psize, int ch, int n)
{
    CALIB *c = calib(font, psize);           /* already made: no outline calls */
    long mn, mx, y, t;
    double v;
    if (!c->valid) {                         /* stand it on the baseline */
        y_range(n, &mn, &mx);
        return -mx;
    }
    y = bitmap_yoff(ch);
    v = (double)(y - c->yH) * (double)(c->tx - c->tH) / (double)(c->yx - c->yH);
    t = c->tH + (long)(v >= 0 ? v + 0.5 : v - 0.5);
    if (GLOG) edlog("    bitmap y %ld -> top %ld", y, t);
    return t;
}

static void y_range(int n, long *mn, long *mx)
{
    int i;
    *mn = 32767; *mx = -32767;
    for (i = 0; i < n; i++) {
        if (oxy[2 * i + 1] < *mn) *mn = oxy[2 * i + 1];
        if (oxy[2 * i + 1] > *mx) *mx = oxy[2 * i + 1];
    }
}

/* work out the orientation of a font's outlines once */
static int font_orient(int font)
{
    long hmin, hmax, tol;
    int n;
    if (orient[font]) return orient[font];
    orient[font] = OR_NONE;
    if (!fonts[font].outline) return OR_NONE;
    n = outline_of(font, 36, 'H');
    if (!n) { edlog("pdf: no outline from font %d, its bitmaps are used instead", fonts[font].id); return OR_NONE; }
    y_range(n, &hmin, &hmax);
    tol = 2 + pt2px_y(36) / 20;
    if (hmin <= tol && hmin >= -tol && hmax > 0) {
        /* 'H' from 0 upwards or downwards: is 'p' too? Then each
         * character starts at its own top (SpeedoGDOS) */
        long pmin, pmax;
        int np = outline_of(font, 36, 'p');
        if (np) {
            y_range(np, &pmin, &pmax);
            edlog("pdf: font %d outline 'p' y %ld..%ld", fonts[font].id, pmin, pmax);
            if (pmin <= tol && pmin >= -tol && pmax > 0) {
                orient[font] = OR_GLYPH_TOP;
                edlog("pdf: font %d outline 'H' y %ld..%ld: %s", fonts[font].id, hmin, hmax, or_name[OR_GLYPH_TOP]);
                return OR_GLYPH_TOP;
            }
        }
    }
    if (hmax <= tol && hmax >= -tol && hmin < 0) orient[font] = OR_DOWN_BASE;
    else if (hmin <= tol && hmin >= -tol && hmax > 0) orient[font] = OR_UP_BASE;
    else orient[font] = OR_DOWN_TOP;
    edlog("pdf: font %d outline 'H' y %ld..%ld: %s", fonts[font].id, hmin, hmax, or_name[(int)orient[font]]);
    return orient[font];
}

/* ---- the scan converter ---------------------------------------------- */

typedef struct { long x0, y0, x1, y1; } EDGE;      /* 1/16 pixel units */
static EDGE *edges;
static int   nedge, edgecap;

static void add_edge(long x0, long y0, long x1, long y1)
{
    if (y0 == y1) return;
    if (nedge >= edgecap) {
        int nc = edgecap ? edgecap * 2 : 512;
        EDGE *ne = (EDGE *)realloc(edges, nc * sizeof(EDGE));
        if (!ne) return;
        edges = ne; edgecap = nc;
    }
    edges[nedge].x0 = x0; edges[nedge].y0 = y0;
    edges[nedge].x1 = x1; edges[nedge].y1 = y1;
    nedge++;
}

/* outline points -> edges (y down, origin on the baseline, 1/16 px),
 * slanted for italic */
static void build_edges(int n, int ori, long base_off, int italic)
{
    long cx = 0, cy = 0, sx = 0, sy = 0;
    int i, open = 0;
#define PX(i) ((long)oxy[2 * (i)] * 16)
#define PY(i) (ori == OR_UP_BASE ? -(long)oxy[2 * (i) + 1] * 16 : \
               ori == OR_DOWN_TOP || ori == OR_GLYPH_TOP ? ((long)oxy[2 * (i) + 1] - base_off) * 16 : \
               (long)oxy[2 * (i) + 1] * 16)
#define SLANT(x, y) ((x) - (italic ? (y) * 13 / 64 : 0))
    nedge = 0;
    for (i = 0; i < n;) {
        if (i == 0 || (obez[i] & 2)) {
            if (open) add_edge(SLANT(cx, cy), cy, SLANT(sx, sy), sy);
            sx = cx = PX(i); sy = cy = PY(i);
            open = 1;
        }
        if ((obez[i] & 1) && i + 3 < n) {               /* cubic Bezier */
            long x1 = PX(i + 1), y1 = PY(i + 1), x2 = PX(i + 2), y2 = PY(i + 2);
            long x3 = PX(i + 3), y3 = PY(i + 3), px = cx, py = cy;
            int k, steps = 8;
            for (k = 1; k <= steps; k++) {
                long t = k * 256L / steps, u = 256 - t;     /* t in 1/256 */
                long bx = (u * u / 256 * u / 256 * cx + 3 * (u * u / 256) * t / 256 * x1 +
                           3 * u * (t * t / 256) / 256 * x2 + t * t / 256 * t / 256 * x3) / 256;
                long by = (u * u / 256 * u / 256 * cy + 3 * (u * u / 256) * t / 256 * y1 +
                           3 * u * (t * t / 256) / 256 * y2 + t * t / 256 * t / 256 * y3) / 256;
                if (k == steps) { bx = x3; by = y3; }
                add_edge(SLANT(px, py), py, SLANT(bx, by), by);
                px = bx; py = by;
            }
            cx = x3; cy = y3;
            i += 3;
            if (i == n - 1) i++;
        } else {
            if (i + 1 < n && !(obez[i + 1] & 2)) {
                long nx = PX(i + 1), ny = PY(i + 1);
                add_edge(SLANT(cx, cy), cy, SLANT(nx, ny), ny);
                cx = nx; cy = ny;
            }
            i++;
        }
    }
    if (open) add_edge(SLANT(cx, cy), cy, SLANT(sx, sy), sy);
#undef PX
#undef PY
#undef SLANT
}

/* floor division by 16 that is defined for negative numbers too */
static long fdiv16(long v) { return v >= 0 ? v / 16 : -((15 - v) / 16); }

/* fill the edges (non-zero winding) into a new glyph bitmap */
/* set bits x0 .. x1-1 of a bitmap row (MSB first) */
static void bits_span(unsigned char *row, long x0, long x1)
{
    long b0, b1;
    unsigned char m0, m1;
    if (x1 <= x0) return;
    b0 = x0 >> 3; b1 = (x1 - 1) >> 3;
    m0 = (unsigned char)(0xFF >> (x0 & 7));
    m1 = (unsigned char)(0xFF << (7 - ((x1 - 1) & 7)));
    if (b0 == b1) { row[b0] |= (unsigned char)(m0 & m1); return; }
    row[b0] |= m0;
    if (b1 > b0 + 1) memset(row + b0 + 1, 0xFF, (size_t)(b1 - b0 - 1));
    row[b1] |= m1;
}

/* OR a row of n bytes into dst moved right by sh bits (dst has room for it) */
static void or_shifted(unsigned char *dst, long dbytes, const unsigned char *src, long n, int sh)
{
    long i, bo = sh >> 3;
    int bit = sh & 7;
    for (i = 0; i < n; i++) {
        unsigned v = src[i];
        if (!v) continue;
        if (i + bo < dbytes) dst[i + bo] |= (unsigned char)(v >> bit);
        if (bit && i + bo + 1 < dbytes) dst[i + bo + 1] |= (unsigned char)(v << (8 - bit));
    }
}

static int scan_fill(GLYPH *g)
{
    long minx = 0x7fffffffL, maxx = -0x7fffffffL, miny = 0x7fffffffL, maxy = -0x7fffffffL;
    long xs[64], r;
    int ds[64], i, rb;
    if (!nedge) { g->w = g->h = 0; g->bits = NULL; return 1; }
    for (i = 0; i < nedge; i++) {
        EDGE *e = &edges[i];
        if (e->x0 < minx) minx = e->x0; if (e->x1 < minx) minx = e->x1;
        if (e->x0 > maxx) maxx = e->x0; if (e->x1 > maxx) maxx = e->x1;
        if (e->y0 < miny) miny = e->y0; if (e->y1 < miny) miny = e->y1;
        if (e->y0 > maxy) maxy = e->y0; if (e->y1 > maxy) maxy = e->y1;
    }
    if (fdiv16(maxx) - fdiv16(minx) > 2000 || fdiv16(maxy) - fdiv16(miny) > 2000 ||
        fdiv16(minx) < -2000 || fdiv16(miny) < -2000 || fdiv16(maxx) > 4000 || fdiv16(maxy) > 2000) {
        edlog("    outline too big (x %ld..%ld, y %ld..%ld px), character skipped",
              fdiv16(minx), fdiv16(maxx), fdiv16(miny), fdiv16(maxy));
        g->w = g->h = 0; g->bits = NULL;
        return 1;
    }
    g->ox = (short)fdiv16(minx); g->oy = (short)fdiv16(miny);
    g->w = (short)(fdiv16(maxx) - g->ox + 2);
    g->h = (short)(fdiv16(maxy) - g->oy + 2);
    rb = (g->w + 7) / 8;
    g->bits = (unsigned char *)calloc(rb, g->h);
    if (!g->bits) { edlog("    out of memory for a %dx%d glyph", g->w, g->h); return 0; }
    for (r = 0; r < g->h; r++) {
        long yc = (g->oy + r) * 16L + 8;               /* pixel centre */
        int nx = 0, wind = 0, k;
        unsigned char *row = g->bits + r * rb;
        for (i = 0; i < nedge && nx < 64; i++) {
            EDGE *e = &edges[i];
            long ya = e->y0 < e->y1 ? e->y0 : e->y1, yb = e->y0 < e->y1 ? e->y1 : e->y0;
            if (yc < ya || yc >= yb) continue;
            xs[nx] = e->x0 + (yc - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0);
            ds[nx] = e->y1 > e->y0 ? 1 : -1;
            for (k = nx; k > 0 && xs[k - 1] > xs[k]; k--) {   /* keep sorted */
                long tx = xs[k]; int td = ds[k];
                xs[k] = xs[k - 1]; ds[k] = ds[k - 1];
                xs[k - 1] = tx; ds[k - 1] = td;
            }
            nx++;
        }
        for (k = 0; k + 1 < nx; k++) {
            wind += ds[k];
            if (wind) {
                long a = fdiv16(xs[k] - 8 + 15) - g->ox, b = fdiv16(xs[k + 1] - 8 + 15) - g->ox;  /* centres inside */
                if (a < 0) a = 0;
                if (b > g->w) b = g->w;
                bits_span(row, a, b);
            }
        }
    }
    return 1;
}

/* ---- bitmap fonts: glyphs from the font data ---------------------------- */

static int la_ready;

/* a pointer we may follow: even, and not in low memory or the I/O area */
static int ptr_ok(const void *p)
{
    unsigned long a = (unsigned long)p;
#if defined(__m68k__) || defined(__mc68000__)
    return p && !(a & 1) && a >= 0x800UL && !(a >= 0x00F00000UL && a < 0x01000000UL) && a < 0xFF000000UL;
#else
    return p && !(a & 1);                   /* the PC test build */
#endif
}

/* Under MiNT with memory protection (XaAES, MyAES...) and with NVDI,
 * the Line-A variables and the fonts' data are system memory that a
 * program may not read: doing so stopped EdWord with a memory violation.
 * So the font lists are walked, and a character's bits copied out, in
 * supervisor mode (Supexec), and the glyph is built from the copy. */
static LAFONT *font_header(int id, int size)
{
    LAFONT *best = NULL, **ring, *f;
    int r, n;
    if (!la_ready) { linea0(); la_ready = 1; }
    if (!ptr_ok(__aline)) return NULL;
    ring = (LAFONT **)((char *)__aline - 456L);
    for (r = 0; r < 3; r++)
        for (f = ring[r], n = 0; ptr_ok(f) && n < 200; f = f->next_font, n++) {
            if (f->size <= 0 || f->size > 200) break;
            if (f->font_id == id && f->size <= size && (!best || f->size > best->size)) best = f;
        }
    if (!best && ptr_ok(__fonts)) {         /* fall back to the system font */
        for (r = 0; r < 3; r++) {
            f = __fonts[r];
            if (ptr_ok(f) && f->size <= size && (!best || f->size > best->size)) best = f;
        }
        if (!best && ptr_ok(__fonts[0])) best = __fonts[0];
    }
    return best;
}

#define LG_ROWS 200
#define LG_RB   40
static struct {
    short id, size, ch, ok;
    short fsize, fid, top, rows, cols;
    unsigned char bits[LG_ROWS * LG_RB];
} lg;

/* supervisor mode: find the font, copy the character's bits into lg */
static long la_fetch(void)
{
    LAFONT *f = font_header(lg.id, lg.size);
    long x0, x1, r, c;
    lg.ok = 0;
    if (!f || lg.ch < f->first_ade || lg.ch > f->last_ade || !ptr_ok(f->off_table) || !ptr_ok(f->dat_table) ||
        f->form_width <= 0 || f->form_height <= 0 || f->form_height > LG_ROWS) return 0;
    x0 = f->off_table[lg.ch - f->first_ade];
    x1 = f->off_table[lg.ch - f->first_ade + 1];
    if (x0 < 0 || x1 < x0 || x1 > f->form_width * 8L || x1 - x0 > LG_RB * 8) return 0;
    lg.fsize = f->size; lg.fid = f->font_id; lg.top = f->top;
    lg.rows = f->form_height; lg.cols = (short)(x1 - x0);
    memset(lg.bits, 0, sizeof lg.bits);
    for (r = 0; r < f->form_height; r++)
        for (c = 0; c < x1 - x0; c++) {
            long bit = x0 + c, idx = r * f->form_width + (bit >> 3);
            if (!(f->flags & 4)) idx ^= 1;              /* not in Motorola (standard) form */
            if (f->dat_table[idx] & (0x80 >> (bit & 7))) lg.bits[r * LG_RB + (c >> 3)] |= (unsigned char)(0x80 >> (c & 7));
        }
    lg.ok = 1;
    return 0;
}

static int bitmap_glyph(GLYPH *g, int font, int size, int ch, int scale)
{
    long r, c, rb;
    int sy, sx;
    lg.id = fonts[font].id; lg.size = (short)size; lg.ch = (short)ch;
    Supexec(la_fetch);
    if (GLOG) edlog("    bitmap: font %d %d pt -> %s (id %d, %d pt)", fonts[font].id, size,
                    lg.ok ? "found" : "none", lg.ok ? lg.fid : 0, lg.ok ? lg.fsize : 0);
    if (!lg.ok || lg.cols <= 0) { g->w = g->h = 0; g->bits = NULL; return 1; }
    /* the VDI doubles a font when asked for twice its size (or more); so
     * does the PDF, to match the screen's layout */
    if (lg.fsize > 0 && size >= 2 * lg.fsize) scale *= 2;
    g->w = (short)(lg.cols * scale);
    g->h = (short)(lg.rows * scale);
    g->ox = 0;
    g->oy = (short)(-lg.top * scale);
    rb = (g->w + 7) / 8;
    g->bits = (unsigned char *)calloc(rb, g->h);
    if (!g->bits) { edlog("    out of memory for a %dx%d glyph", g->w, g->h); return 0; }
    /* each run of the font's pixels becomes a span scale times as wide,
     * and each row is copied to the scale-1 rows under it */
    for (r = 0; r < lg.rows; r++) {
        const unsigned char *src = lg.bits + r * LG_RB;
        unsigned char *row = g->bits + r * scale * rb;
        for (c = 0; c < lg.cols; ) {
            long a;
            if (!(src[c >> 3] & (0x80 >> (c & 7)))) { c++; continue; }
            a = c;
            while (c < lg.cols && (src[c >> 3] & (0x80 >> (c & 7)))) c++;
            bits_span(row, a * scale, c * scale);
        }
        for (sy = 1; sy < scale; sy++) memcpy(row + sy * rb, row, (size_t)rb);
    }
    (void)sx;
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
    nb = (unsigned char *)calloc(nrb, g->h);
    if (!nb) { g->w = (short)ow; return; }
    for (r = 0; r < g->h; r++)
        for (k = 0; k <= t; k++)                  /* the row OR-ed in at each offset */
            or_shifted(nb + (long)r * nrb, nrb, g->bits + (long)r * orb, orb, k);
    (void)x;
    free(g->bits);
    g->bits = nb;
}

/* slant a bitmap glyph (italic), rows above the baseline move right */
static void slant(GLYPH *g)
{
    int rb = (g->w + 7) / 8, sh = (g->h * 13 / 64) + 1, nw = g->w + sh, nrb = (nw + 7) / 8, r, x;
    unsigned char *nb;
    if (!g->bits) return;
    nb = (unsigned char *)calloc(nrb, g->h);
    if (!nb) return;
    for (r = 0; r < g->h; r++) {
        int off = (int)((-(long)(g->oy + r)) * 13 / 64) + (g->oy < 0 ? 0 : 0);
        off += sh / 2;
        if (off < 0) off = 0;
        or_shifted(nb + (long)r * nrb, nrb, g->bits + (long)r * rb, rb, off);
        (void)x;
    }
    free(g->bits);
    g->bits = nb;
    g->w = (short)nw;
    g->ox = (short)(g->ox - sh / 2);
}

#ifdef EDDEMO
static clock_t tm_glyph, tm_look;
#endif
#ifdef EDDEMO
static GLYPH *get_glyph2(int font, int size, int attr, int ch, int scale);
#endif
static GLYPH *get_glyph(int font, int size, int attr, int ch, int scale)
#ifdef EDDEMO
{
    clock_t t = clock();
    GLYPH *g = get_glyph2(font, size, attr, ch, scale);
    tm_look += clock() - t;
    return g;
}
static GLYPH *get_glyph2(int font, int size, int attr, int ch, int scale)
#endif
{
    int i, ori;
    GLYPH *g;
    attr &= A_BOLD | A_ITALIC;
    if (font >= nfonts) font = 0;
    if (!ghash_ok) glyph_cache_free();
    for (i = ghead[GKEY(font, size, attr, ch, scale)]; i >= 0; i = g->next) {
        g = &gcache[i];
        if (g->ch == ch && g->font == font && g->size == size && g->attr == attr && g->scale == scale) return g;
    }
#ifdef EDDEMO
    { clock_t t0 = clock(); GLYPH *r;
#endif
    if (ngc >= NGLYPH) glyph_cache_free();
    g = &gcache[ngc];
    memset(g, 0, sizeof *g);
    g->font = (short)font; g->size = (short)size; g->attr = (short)attr;
    g->scale = (short)scale; g->ch = (short)ch;
    if (GLOG) edlog("  glyph %d: char %d '%c', font %d (id %d, %s), %d pt, attr %d, scale %d",
                    ngc, ch, ch >= 32 && ch < 127 ? ch : '?', font, fonts[font].id,
                    fonts[font].outline ? "outline" : "bitmap", size, attr, scale);
    ori = font_orient(font);
    if (ori != OR_NONE) {
        long base_off = 0, mn, mx;
        int n;
        if (ori == OR_DOWN_TOP) {
            n = outline_of(font, size * scale, 'H');
            y_range(n, &mn, &mx);
            base_off = mx;
        }
        if (ori == OR_GLYPH_TOP) calib(font, size * scale);   /* before the outline below */
        n = outline_of(font, size * scale, ch);
        if (ori == OR_GLYPH_TOP && n) base_off = -glyph_top(font, size * scale, ch, n);
        build_edges(n, ori, base_off, attr & A_ITALIC);
        if (GLOG) edlog("    %d edges", nedge);
        if (!scan_fill(g)) return NULL;
    } else {
        if (!bitmap_glyph(g, font, size, ch, scale)) return NULL;
        if (attr & A_ITALIC) slant(g);
    }
    if (attr & A_BOLD) embolden(g, (int)(pt2px_x(size) * scale / 24 + 1));
    if (GLOG) edlog("    -> %dx%d bitmap, offset %d,%d", g->w, g->h, g->ox, g->oy);
#ifdef EDLOG
    nglog++;
#endif
    {   /* into its hash chain */
        unsigned k = GKEY(font, size, attr, ch, scale);
        g->next = ghead[k];
        ghead[k] = (short)ngc;
    }
    ngc++;
#ifdef EDDEMO
    r = g; tm_glyph += clock() - t0; return r; }
#endif
    return g;
}


/* ---- PDF export ----------------------------------------------------------- */

static GLYPHBM gbm;

static const GLYPHBM *pdf_glyph(void *ctx, int font, int size, int attr, int ch, int scale)
{
    GLYPH *g = get_glyph(font, size, attr, ch, scale);
    (void)ctx;
    if (!g) return NULL;
    gbm.w = g->w; gbm.h = g->h; gbm.ox = g->ox; gbm.oy = g->oy; gbm.bits = g->bits;
    return &gbm;
}

typedef struct {
    int  scale, nband;
    void (*gauge)(long done, long total);
} RCTX;

static int render_cb(void *ctx, DOC *d, int page, int color, long y0, long h, long w,
                     unsigned char *bits, long rb)
{
    RCTX *r = (RCTX *)ctx;
    BAND b;
#ifdef EDLOG
    if (r->nband < 200) edlog(" band: page %d, colour %d, rows %ld-%ld of width %ld", page + 1, color, y0, y0 + h - 1, w);
    r->nband++;
#endif
    b.bits = bits; b.rb = rb; b.w = w; b.y0 = y0; b.h = h;
    page_render(d, page, color, &b, r->scale, pdf_glyph, NULL, pref_date);
    if (GLOG) edlog("  band done");
    return 1;
}

static void progress_cb(void *ctx, int done, int total)
{
    RCTX *r = (RCTX *)ctx;
    if (r->gauge) r->gauge(done, total);
}

#ifdef EDLOG
static void log_cb(void *ctx, const char *msg) { (void)ctx; edlog("%s", msg); }
#endif

/* write the PDF; gauge (may be NULL) is told how far it has got */
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
#ifdef EDLOG
    o.log = log_cb;
    nglog = 0;
    edlog_more();
#endif
    memset(orient, 0, sizeof orient);
    ncal = 0;
    edlog("pdf: exporting %s at %dx, colour %s, %ld characters in %ld lines", path, r.scale,
          colour_on ? "on" : "off", d->len, d->nlines);
    edlog("pdf: %ld bytes free (largest block), stack near %08lx", (long)Malloc(-1L), (long)&r);
    {
        int i;
        for (i = 0; i < nfonts; i++)
            edlog("pdf: font %d: id %d, %s, \"%s\"", i, fonts[i].id, fonts[i].outline ? "outline" : "bitmap", fonts[i].name);
    }
    doc_layout(d);
    ok = pdf_export(d, path, &o);
    edlog("pdf: pages written, freeing %d cached glyphs", ngc);
    glyph_cache_free();
    free(edges);
    edges = NULL;
    nedge = edgecap = 0;
    rs_end();
    style_reset();
    edlog("pdf: done, %s", ok ? "ok" : "failed");
    return ok;
}

/* ---- printing through GDOS ----------------------------------------------- */

/* Pages are drawn as bitmaps at the printer's resolution (prt.c) with the
 * same glyphs the PDF uses, and copied to the GDOS printer workstation
 * (device 21) with vrt_cpyfm, one band at a time. */

int printer_name(char *out)
{
#ifdef EDDEMO
    if (demo_on) { strcpy(out, "GDOS 21"); return 1; }     /* the manual's screenshot */
#endif
    if (!has_gdos) return 0;
    strcpy(out, "GDOS 21");
    return 1;
}

typedef struct { PRTOUT o; short ph, npens, ink[7]; int page, npg, done, stop; } PCTX;

static int prt_render(void *ctx, DOC *d, int page, int color, long y0, long h, long w,
                      unsigned char *bits, long rb)
{
    PCTX *c = (PCTX *)ctx;
    BAND b;
    busy_ptr_track();
    b.bits = bits; b.rb = rb; b.w = w; b.y0 = y0; b.h = h;
    page_render(d, page, color, &b, c->o.scale, pdf_glyph, NULL, pref_date);
    return 1;
}

static int prt_out(void *ctx, int color, long y0, long h, long w, const unsigned char *bits, long rb)
{
    PCTX *c = (PCTX *)ctx;
    MFDB src, dst;
    short pxy[8], col[2];
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.fd_addr = (void *)bits;
    src.fd_w = (short)(rb * 8); src.fd_h = (short)h; src.fd_wdwidth = (short)(rb / 2);
    src.fd_stand = 0; src.fd_nplanes = 1;
    pxy[0] = 0; pxy[1] = 0; pxy[2] = (short)(w - 1); pxy[3] = (short)(h - 1);
    pxy[4] = 0; pxy[5] = (short)y0; pxy[6] = (short)(w - 1); pxy[7] = (short)(y0 + h - 1);
    /* the ink in the printer's pen for this colour (black: pen 1) */
    col[0] = (short)(color > PRT_INK && color < PRT_INK + 7 ? c->ink[color - PRT_INK] :
                     color > 0 && color < NCOLORS ? pen_of[color] : 1);
    if (col[0] >= c->npens) col[0] = 1;
    col[1] = 0;
    vrt_cpyfm(c->ph, MD_TRANS, pxy, &src, &dst, col);
    if (gauge_cancelled()) { c->stop = 1; return 0; }
    return 1;
}

static void prt_progress(void *ctx, int done, int total)
{
    PCTX *c = (PCTX *)ctx;
    gauge_set((long)c->done * total + done, (long)c->npg * total);
}

#ifdef EDDEMO
/* demo command PRTIME: page 1 to a 300 dpi printer that throws it away,
 * with the time spent drawing and in total in the log (200ths of a second) */
static clock_t tm_render;
static int tm_rend(void *ctx, DOC *d, int page, int color, long y0, long h, long w, unsigned char *bits, long rb)
{
    clock_t t = clock();
    int r = prt_render(ctx, d, page, color, y0, h, w, bits, rb);
    tm_render += clock() - t;
    return r;
}
static int tm_out(void *ctx, int color, long y0, long h, long w, const unsigned char *bits, long rb)
{
    (void)ctx; (void)color; (void)y0; (void)h; (void)w; (void)bits; (void)rb;
    return 1;
}
void print_timing(DOC *d, int colour)
{
    PCTX c;
    clock_t t0;
    memset(&c, 0, sizeof c);
    c.o.pixw = c.o.pixh = 85; c.o.band = 128;
    c.o.render = tm_rend; c.o.out = tm_out; c.o.ctx = &c; c.o.colour = colour;
    c.o.progress = prt_progress; c.npg = 1;
    doc_layout(d);
    tm_render = tm_glyph = tm_look = 0;
    gauge_cancelable();
    gauge_open(L(S_PRINTING), NULL);
    t0 = clock();
    print_page(d, 0, &c.o);
    gauge_close();
    {
        char path[300];
        FILE *f;
        sprintf(path, "%sPRTIME.TXT", progdir);
        f = fopen(path, "a");
        if (f) {
            fprintf(f, "page 1 at 300 dpi%s, scale %d: %ld ticks in all, %ld drawing (%ld getting glyphs, %ld of it making them), %ld resampling; %d glyphs cached\r\n",
                    colour ? " in colour" : "", c.o.scale, (long)(clock() - t0), (long)tm_render, (long)tm_look, (long)tm_glyph,
                    (long)(clock() - t0 - tm_render), ngc);
            fclose(f);
        }
    }
    glyph_cache_free();
}
#endif

int print_doc(DOC *d, const unsigned char *sel, int copies, int colour)
{
    short work_in[11], work_out[57], i;
    PCTX c;
    int p, k, ok = 1, np;
    if (!has_gdos) return 0;
    for (i = 0; i < 10; i++) work_in[i] = 1;
    work_in[0] = 21;
    work_in[10] = 2;
    memset(&c, 0, sizeof c);
    v_opnwk(work_in, &c.ph, work_out);
    edlog("print: v_opnwk(21) -> handle %d, %dx%d pixels of %dx%d microns", c.ph, work_out[0] + 1,
          work_out[1] + 1, work_out[3], work_out[4]);
    if (c.ph <= 0) return 0;
    /* ask the printer again: Hatari's extended VDI mode overwrites the
     * size v_opnwk returns with its screen size, whatever the device,
     * but leaves vq_extnd alone (same values on a real machine) */
    vq_extnd(c.ph, 0, work_out);
    edlog("print: vq_extnd -> %dx%d pixels of %dx%d microns", work_out[0] + 1,
          work_out[1] + 1, work_out[3], work_out[4]);
    if (work_out[3] <= 0 || work_out[4] <= 0) { v_clswk(c.ph); return 0; }
    /* a colour printer: each colour in its own pen, set to EdWord's colour */
    c.npens = work_out[13];
    c.o.colour = colour && c.npens >= 8;
    edlog("print: %d pens, %s", c.npens, c.o.colour ? "colour" : "black and white");
    if (colour && !c.o.colour && alert(1, 2, AL_PRT_NO_COLOUR, AB_PRINT_CANCEL, c.npens) != 1) { v_clswk(c.ph); return -1; }
    if (c.o.colour) {
        int k;
        for (k = 1; k < NCOLORS; k++)
            if (pen_of[k] < c.npens) {
                short rgb[3];
                rgb[0] = (short)(color_rgb[k][0] * 1000L / 255);
                rgb[1] = (short)(color_rgb[k][1] * 1000L / 255);
                rgb[2] = (short)(color_rgb[k][2] * 1000L / 255);
                vs_color(c.ph, pen_of[k], rgb);
            }
        /* the inks of colour pictures: pure colours in pens of their own (10 to
         * 15) when the printer has 16 or more, else the nearest text colour's */
        for (k = 1; k < 7; k++) {
            static const short near[7] = { 0, 1, 2, 6, 3, 5, 4 };     /* text colour nearest each ink */
            if (c.npens >= 16) {
                short rgb[3];
                c.ink[k] = (short)(9 + k);
                rgb[0] = (short)(k & 1 ? 1000 : 0); rgb[1] = (short)(k & 2 ? 1000 : 0); rgb[2] = (short)(k & 4 ? 1000 : 0);
                vs_color(c.ph, c.ink[k], rgb);
            } else c.ink[k] = pen_of[near[k]];
        }
    }
    c.o.w = work_out[0] + 1L; c.o.h = work_out[1] + 1L;
    c.o.pixw = work_out[3]; c.o.pixh = work_out[4];
    c.o.band = 128;
    c.o.render = prt_render;
    c.o.out = prt_out;
    c.o.progress = prt_progress;
    c.o.ctx = &c;
    np = doc_npages(d);
    for (p = 0, k = 0; p < np; p++) if (!sel || sel[p]) k++;
    if (!k) { v_clswk(c.ph); return 0; }       /* nothing to print: close the printer again */
    c.npg = k * copies;
    memset(orient, 0, sizeof orient);
    ncal = 0;
    gauge_cancelable();
    gauge_open(L(S_PRINTING), NULL);
    doc_layout(d);
    for (k = 0; k < copies && ok; k++)
        for (p = 0; p < np && ok; p++) {
            if (sel && !sel[p]) continue;
            ok = print_page(d, p, &c.o);
            if (!ok) break;
            /* the driver sends the page now, and doesn't come back until it
             * has: the arrow, so no busy bee is left over other programs */
            busy_ptr(0);
            v_updwk(c.ph);                      /* print the page ... */
            v_clrwk(c.ph);                      /* ... and feed it out */
            busy_ptr(1);
            c.done++;
            if (gauge_cancelled()) { c.stop = 1; ok = 0; }
        }
    gauge_close();
    if (c.stop) edlog("print: cancelled after %d pages", c.done);
    v_clswk(c.ph);
    glyph_cache_free();
    free(edges);
    edges = NULL;
    nedge = edgecap = 0;
    rs_end();
    style_reset();
    return c.stop ? -1 : ok;
}
