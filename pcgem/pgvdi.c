/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * pgvdi.c - GEMLib's VDI calls (as pcgem/include/vdi.h declares them) on
 *           PC GEM, through John Elliott's DJGPP GEM bindings.
 *
 * Bitmaps (MFDBs) live in normal 32-bit memory; the bindings' dj_vro_cpyfm
 * & co. move them to and from the screen in slices. An MFDB with no
 * address is the screen, as on the Atari.
 *
 * PC GEM's VDI has GDOS built in (fonts from ASSIGN.SYS), but only bitmap
 * fonts: vq_vgdos() says so, and the outline-font calls do what a GDOS
 * without outlines does. XGDOS for the PC (a separate resident program)
 * will add Speedo fonts underneath, as on the Atari.
 */
#include <string.h>
#include <dpmi.h>
#include <sys/movedata.h>
#include "pgkit.h"

#ifdef EDLOG
void edlog(const char *fmt, ...);
#define PGLOG(...) edlog(__VA_ARGS__)
#else
#define PGLOG(...) ((void)0)
#endif

/* GEMLib's MFDB: the same layout, but fd_addr is a 32-bit pointer */
typedef struct { void *fd_addr; WORD fd_w, fd_h, fd_wdwidth, fd_stand, fd_nplanes, fd_r1, fd_r2, fd_r3; } FE_MFDB;
typedef struct { short *control, *intin, *ptsin, *intout, *ptsout; } FE_VDIPB;

/* ---- the VDI's arrays ------------------------------------------------------
 * The bindings keep contrl/intin/ptsin/... in one small DOS block: intin
 * holds 80 words and ptsin 128 points, and nothing is checked - a longer
 * string or polygon runs on into the next DOS block and DOS's memory chain
 * is wrecked. They're replaced here by bigger ones, and every call is also
 * kept within what the driver says it takes (v_opnvwk's work_out). */
extern LPWORD pContrl, pIntin, pPtsin, pIntout, pPtsout;
extern LPLPTR pPblock;

#define MAXIN   1024                  /* words of intin: characters of text */
#define MAXPTS  2048                  /* points of ptsin */
static int max_in = 80, max_pts = 128; /* what may be passed: the bindings' until ours are in */

static void big_arrays(void)
{
    static int done;
    LPBYTE p;
    if (done) return;
    done = 1;
    vdi_alloc_arrays();               /* the bindings' own, and their parameter block */
    p = dos_alloc(24 + 2 * MAXIN + 4 * MAXPTS + 2 * 512 + 2 * 512 + 20);
    if (!p) return;                   /* keep theirs, and their limits */
    pContrl = (LPWORD)p;
    pIntin  = (LPWORD)(p + 24);
    pPtsin  = (LPWORD)(p + 24 + 2 * MAXIN);
    pIntout = (LPWORD)(p + 24 + 2 * MAXIN + 4 * MAXPTS);
    pPtsout = (LPWORD)(p + 24 + 2 * MAXIN + 4 * MAXPTS + 1024);
    pPblock = (LPLPTR)(p + 24 + 2 * MAXIN + 4 * MAXPTS + 2048);
    LSSET(pPblock,      pContrl);
    LSSET(pPblock + 4,  pIntin);
    LSSET(pPblock + 8,  pPtsin);
    LSSET(pPblock + 12, pIntout);
    LSSET(pPblock + 16, pPtsout);
    max_in = MAXIN; max_pts = MAXPTS;
}

/* the driver's own limits, from vq_extnd's extended answer: [14] vertices,
 * [15] intin words (-1: no limit) */
static void driver_limits(short h)
{
    WORD wo[57];
    memset(wo, 0, sizeof wo);
    vq_extnd(h, 1, wo);
    if (wo[14] > 0 && wo[14] < max_pts) max_pts = wo[14];
    if (wo[15] > 0 && wo[15] < max_in) max_in = wo[15];
    PGLOG("pg: VDI arrays %05lx, driver takes %d vertices, %d intin -> pieces of %d points, %d characters",
          (long)pContrl, wo[14], wo[15], max_pts, max_in);
}

void  pg_v_opnwk(short *wi, short *h, short *wo) { big_arrays(); v_opnwk(wi, h, wo); if (*h > 0) driver_limits(*h); }
void  pg_v_opnvwk(short *wi, short *h, short *wo) { big_arrays(); v_opnvwk(wi, h, wo); if (*h > 0) driver_limits(*h); }
void  pg_v_clswk(short h) { v_clswk(h); }
void  pg_v_clsvwk(short h) { v_clsvwk(h); }
void  pg_v_clrwk(short h) { v_clrwk(h); }
void  pg_v_updwk(short h) { v_updwk(h); }
/* EdWord, like most Atari programs, turns clipping off with a NULL array;
 * the bindings copy the array regardless, and in DJGPP NULL is a page fault */
void pg_vs_clip(short h, short on, short *pxy)
{
    WORD none[4] = { 0, 0, 0, 0 };
    vs_clip(h, on, pxy ? pxy : none);
}
short pg_vswr_mode(short h, short m) { return vswr_mode(h, m); }
void  pg_vs_color(short h, short i, const short *rgb) { vs_color(h, i, (WORD *)rgb); }
short pg_vq_color(short h, short i, short f, short *rgb) { vq_color(h, i, f, rgb); return i; }
void  pg_vq_extnd(short h, short f, short *wo) { vq_extnd(h, f, wo); }

/* GEM/3 has no vq_scrninfo: say "no answer" the way the front end checks */
void  pg_vq_scrninfo(short h, short *wo) { (void)h; wo[0] = -1; wo[1] = -1; wo[2] = 0; }

short pg_vq_gdos(void) { return 1; }               /* PC GEM's VDI is a GDOS */

/* ---- XGDOS for PC GEM ---------------------------------------------------------
 * XGDOS.EXE puts itself in front of GEMVDI on INT EFh; its entry starts
 * with a short jump, GEMVDI's 'GEMVDI23',0 (GEM.EXE checks for it), then
 * 'XGDOS',0. With it
 * there, the Speedo calls are real VDI calls, as on the Atari. */
static int xgdos = -1;

static int xgdos_here(void)
{
    if (xgdos < 0) {
        __dpmi_raddr a;
        char sig[6];
        xgdos = 0;
        if (!__dpmi_get_real_mode_interrupt_vector(0xEF, &a)) {
            dosmemget(a.segment * 16L + a.offset16 + 11, 6, sig);   /* after 'GEMVDI23',0 */
            xgdos = !memcmp(sig, "XGDOS", 6);
        }
        PGLOG("pg: XGDOS %s", xgdos ? "in front of GEMVDI: Speedo fonts" : "not there: GEM's bitmap fonts");
    }
    return xgdos;
}

long pg_vq_vgdos(void) { return xgdos_here() ? 0x5F46534DL : 0x5F464E54L; }   /* '_FSM' : '_FNT' */

/* a VDI call made by hand with the arrays given (word counts in contrl) */
static void raw_call(short *contrl, short *intin, short *ptsin, short *intout, short *ptsout)
{
    int ni = contrl[3], np = contrl[1];
    if (ni > max_in) ni = max_in;
    if (np > max_pts) np = max_pts;
    dosmemput(contrl, 24, pContrl);
    if (ni > 0) dosmemput(intin, 2 * ni, pIntin);
    if (np > 0) dosmemput(ptsin, 4 * np, pPtsin);
    vdi();
    dosmemget(pContrl, 24, contrl);
    if (intout && contrl[4] > 0) dosmemget(pIntout, 2 * (contrl[4] < 512 ? contrl[4] : 512), intout);
    if (ptsout && contrl[2] > 0) dosmemget(pPtsout, 4 * (contrl[2] < 256 ? contrl[2] : 256), ptsout);
}

static short simple(short h, short op, short nin, const short *in, short *out, short *pout)
{
    short c[12], o[64], p[16];
    memset(c, 0, sizeof c);
    c[0] = op; c[3] = nin; c[6] = h;
    raw_call(c, (short *)in, NULL, out ? out : o, pout ? pout : p);
    return c[4] > 0 ? (out ? out[0] : o[0]) : 0;
}

/* a long line in pieces, each starting where the last ended */
void pg_v_pline(short h, short n, short *pxy)
{
    while (n > max_pts) {
        v_pline(h, max_pts, pxy);
        pxy += 2 * (max_pts - 1);
        n -= max_pts - 1;
    }
    if (n >= 2) v_pline(h, n, pxy);
}

void pg_v_pmarker(short h, short n, short *pxy)
{
    while (n > 0) {
        short k = n > max_pts ? max_pts : n;
        v_pmarker(h, k, pxy);
        pxy += 2 * k; n -= k;
    }
}

/* a polygon can't be split: one bigger than the driver takes is cut short */
void pg_v_fillarea(short h, short n, short *pxy) { v_fillarea(h, n > max_pts ? max_pts : n, pxy); }
void  pg_v_circle(short h, short x, short y, short r) { v_circle(h, x, y, r); }
void  pg_vr_recfl(short h, short *pxy) { vr_recfl(h, pxy); }
/* text longer than intin takes, a piece at a time, each where the last
 * ended (left-aligned text; EdWord's long runs are) */
void pg_v_gtext(short h, short x, short y, const char *s)
{
    char piece[MAXIN + 1];
    size_t n = strlen(s);
    while ((long)n > max_in) {
        WORD ext[8];
        memcpy(piece, s, max_in); piece[max_in] = 0;
        v_gtext(h, x, y, (BYTE *)piece);
        vqt_extent(h, (BYTE *)piece, ext);
        x += ext[2] - ext[0];
        s += max_in; n -= max_in;
    }
    v_gtext(h, x, y, (BYTE *)s);
}

void pg_v_justified(short h, short x, short y, const char *s, short len, short ws, short cs)
{
    char piece[MAXIN + 1];
    size_t n = strlen(s);
    if ((long)n > max_in - 2) {       /* intin also holds the two flags */
        memcpy(piece, s, max_in - 2); piece[max_in - 2] = 0;
        s = piece;
    }
    v_justified(h, x, y, (BYTE *)s, len, ws, cs);
}

short pg_vsl_color(short h, short c) { return vsl_color(h, c); }
short pg_vsl_type(short h, short t) { return vsl_type(h, t); }
short pg_vsl_width(short h, short w) { return vsl_width(h, w); }
void  pg_vsl_ends(short h, short b, short e) { vsl_ends(h, b, e); }
void  pg_vsl_udsty(short h, short pat) { vsl_udsty(h, pat); }
short pg_vsm_type(short h, short t) { return vsm_type(h, t); }
short pg_vsm_color(short h, short c) { return vsm_color(h, c); }
short pg_vsf_interior(short h, short s) { return vsf_interior(h, s); }
short pg_vsf_color(short h, short c) { return vsf_color(h, c); }
short pg_vsf_style(short h, short s) { return vsf_style(h, s); }
short pg_vsf_perimeter(short h, short on) { return vsf_perimeter(h, on); }

short pg_vst_color(short h, short c) { return vst_color(h, c); }
short pg_vst_font(short h, short id) { return vst_font(h, id); }
short pg_vst_point(short h, short pt, short *cw, short *ch, short *bw, short *bh)
{
    return vst_point(h, pt, cw, ch, bw, bh);
}
void  pg_vst_height(short h, short ht, short *cw, short *ch, short *bw, short *bh)
{
    vst_height(h, ht, cw, ch, bw, bh);
}
/* any size with XGDOS; else the nearest bitmap size, as vst_point gives it */
short pg_vst_arbpt(short h, short pt, short *cw, short *ch, short *bw, short *bh)
{
    if (xgdos_here()) {
        short out[4], pts[8], r;
        r = simple(h, 246, 1, &pt, out, pts);
        *cw = pts[0]; *ch = pts[1]; *bw = pts[2]; *bh = pts[3];
        return r;
    }
    return vst_point(h, pt, cw, ch, bw, bh);
}
short pg_vst_effects(short h, short e) { return vst_effects(h, e); }
void  pg_vst_alignment(short h, short hin, short vin, short *hout, short *vout)
{
    vst_alignment(h, hin, vin, hout, vout);
}
short pg_vst_load_fonts(short h, short sel) { return vst_load_fonts(h, sel); }
void  pg_vst_unload_fonts(short h, short sel) { vst_unload_fonts(h, sel); }
void  pg_vst_scratch(short h, short m) { if (xgdos_here()) simple(h, 244, 1, &m, NULL, NULL); }
/* not passed on: XGDOS would keep a pointer into EdWord's memory, which it
 * can't reach from real mode */
void  pg_vst_error(short h, short m, short *code) { (void)h; (void)m; if (code) *code = 0; }
short pg_vst_charmap(short h, short m) { return xgdos_here() ? simple(h, 236, 1, &m, NULL, NULL) : 0; }

/* the extent of a long string: its pieces' widths added up */
void pg_vqt_extent(short h, const char *s, short *pts)
{
    char piece[MAXIN + 1];
    size_t n = strlen(s);
    short w = 0, ht = 0;
    if ((long)n <= max_in) { vqt_extent(h, (BYTE *)s, pts); return; }
    while (n > 0) {
        WORD ext[8];
        size_t k = (long)n > max_in ? (size_t)max_in : n;
        memcpy(piece, s, k); piece[k] = 0;
        vqt_extent(h, (BYTE *)piece, ext);
        w += ext[2] - ext[0];
        if (ext[5] - ext[3] > ht) ht = ext[5] - ext[3];
        s += k; n -= k;
    }
    pts[0] = 0; pts[1] = 0; pts[2] = w; pts[3] = 0;
    pts[4] = w; pts[5] = ht; pts[6] = 0; pts[7] = ht;
}
short pg_vqt_width(short h, short ch, short *cw, short *ld, short *rd)
{
    return vqt_width(h, (BYTE)ch, cw, ld, rd);
}
short pg_vqt_name(short h, short n, char *name) { return vqt_name(h, n, (BYTE *)name); }

/* XGDOS: its advance; no outlines: the cell width */
void  pg_vqt_advance(short h, short ch, short *ax, short *ay, short *rx, short *ry)
{
    WORD cw = 0, ld, rd;
    if (xgdos_here()) {
        short pts[8];
        simple(h, 247, 1, &ch, NULL, pts);
        *ax = pts[0]; *ay = pts[1];
        if (rx) *rx = pts[2];
        if (ry) *ry = pts[3];
        return;
    }
    vqt_width(h, (BYTE)ch, &cw, &ld, &rd);
    *ax = cw; *ay = 0;
    if (rx) *rx = 0;
    if (ry) *ry = 0;
}

void  pg_v_getoutline(short h, short ch, short *xy, char *bez, short max, short *count)
{
    (void)h; (void)ch; (void)xy; (void)bez; (void)max;
    *count = 0;
}

static MFDB *kit_mfdb(FE_MFDB *m, MFDB *out)
{
    memcpy(out, m, sizeof *out);
    return out;
}

/* a 32-bit bitmap (fd_addr set) goes through the bindings' slicing copies;
 * two screen MFDBs need nothing moved */
void pg_vro_cpyfm(short h, short mode, short *pxy, FE_MFDB *src, FE_MFDB *dst)
{
    MFDB s, d;
    kit_mfdb(src, &s); kit_mfdb(dst, &d);
    if (src->fd_addr || dst->fd_addr) dj_vro_cpyfm(h, mode, pxy, &s, &d);
    else vro_cpyfm(h, mode, pxy, &s, &d);
}

void pg_vrt_cpyfm(short h, short mode, short *pxy, FE_MFDB *src, FE_MFDB *dst, short *colors)
{
    MFDB s, d;
    kit_mfdb(src, &s); kit_mfdb(dst, &d);
    if (src->fd_addr || dst->fd_addr) dj_vrt_cpyfm(h, mode, pxy, &s, &d, colors);
    else vrt_cpyfm(h, mode, pxy, &s, &d, colors);
}

void pg_vr_trnfm(short h, FE_MFDB *src, FE_MFDB *dst)
{
    MFDB s, d;
    kit_mfdb(src, &s); kit_mfdb(dst, &d);
    dj_vr_trnfm(h, &s, &d);
}

/* gemrend.c asks vqt_fontinfo this way (opcode 131); answer it from the
 * bindings, in the standard reply layout. Other raw calls get nothing. */
void pg_vdi(FE_VDIPB *pb)
{
    if (pb->control[0] == 131) {
        WORD mn, mx, dist[5], mw, eff[3];
        vqt_font_info(pb->control[6], &mn, &mx, dist, &mw, eff);
        pb->intout[0] = mn; pb->intout[1] = mx;
        pb->ptsout[0] = mw;
        pb->ptsout[1] = dist[0]; pb->ptsout[3] = dist[1]; pb->ptsout[5] = dist[2];
        pb->ptsout[7] = dist[3]; pb->ptsout[9] = dist[4];
        pb->ptsout[2] = eff[0]; pb->ptsout[4] = eff[1]; pb->ptsout[6] = eff[2];
        pb->control[2] = 5; pb->control[4] = 2;
        return;
    }
    if (!xgdos_here()) { pb->control[2] = pb->control[4] = 0; return; }
    if (pb->control[0] == 243) {        /* v_getoutline: its two arrays must be in DOS memory */
        short max = pb->intin[1], in[6];
        short *xy, n;
        char *bez;
        LPBYTE dxy, dbez;
        memcpy(&xy, &pb->intin[2], sizeof xy);
        memcpy(&bez, &pb->intin[2 + sizeof(void *) / 2], sizeof bez);
        if (max <= 0 || max > 4096) max = 4096;
        dxy = dos_alloc(4L * max);
        dbez = dos_alloc(max);
        if (!dxy || !dbez) {
            if (dxy) dos_free(dxy);
            if (dbez) dos_free(dbez);
            pb->control[4] = 1; pb->intout[0] = 0;
            return;
        }
        in[0] = pb->intin[0]; in[1] = max;
        in[2] = (short)(dxy & 15); in[3] = (short)(dxy >> 4);       /* far pointers: offset, segment */
        in[4] = (short)(dbez & 15); in[5] = (short)(dbez >> 4);
        pb->control[3] = 6;
        raw_call(pb->control, in, pb->ptsin, pb->intout, pb->ptsout);
        n = pb->control[4] > 0 ? pb->intout[0] : 0;
        if (n > 0 && n <= max) {
            dosmemget(dxy, 4L * n, xy);
            dosmemget(dbez, n, bez);
        }
        dos_free(dxy); dos_free(dbez);
        return;
    }
    raw_call(pb->control, pb->intin, pb->ptsin, pb->intout, pb->ptsout);
}

/* more of the VDI (Fbench): straight to the bindings */
void  pg_v_bar(short h, short *pxy) { v_bar(h, pxy); }
void  pg_v_arc(short h, short x, short y, short r, short b, short e) { v_arc(h, x, y, r, b, e); }
void  pg_v_pieslice(short h, short x, short y, short r, short b, short e) { v_pieslice(h, x, y, r, b, e); }
void  pg_v_ellipse(short h, short x, short y, short xr, short yr) { v_ellipse(h, x, y, xr, yr); }
void  pg_v_ellarc(short h, short x, short y, short xr, short yr, short b, short e) { v_ellarc(h, x, y, xr, yr, b, e); }
void  pg_v_ellpie(short h, short x, short y, short xr, short yr, short b, short e) { v_ellpie(h, x, y, xr, yr, b, e); }
void  pg_v_rbox(short h, short *pxy) { v_rbox(h, pxy); }
void  pg_v_rfbox(short h, short *pxy) { v_rfbox(h, pxy); }
void  pg_v_contourfill(short h, short x, short y, short c) { v_contourfill(h, x, y, c); }
short pg_v_get_pixel(short h, short x, short y, short *pel, short *idx) { return v_get_pixel(h, x, y, pel, idx); }
void  pg_v_show_c(short h, short reset) { v_show_c(h, reset); }
void  pg_v_hide_c(short h) { v_hide_c(h); }
short pg_vsm_height(short h, short ht) { return vsm_height(h, ht); }
short pg_vst_rotation(short h, short a) { return vst_rotation(h, a); }
void  pg_vsf_udpat(short h, short *pat, short planes) { vsf_udpat(h, pat, planes); }
void  pg_vqt_attributes(short h, short *attr) { vqt_attributes(h, attr); }
