/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemobj.c - pictures and drawings in the document view
 *
 * Objects float on a page. A click selects one; drag it to move it, drag
 * a corner handle to resize it. Double click a picture for its settings,
 * a drawing to edit its shapes in the drawing window.
 */
#include <gem.h>
#include <gemx.h>
#include <osbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gemui.h"

#define HS 4                        /* half size of a handle, pixels */

static long sx_of(DOC *d, GRECT *tp, long px) { return tp->g_x - d->scroll_x + view_page_left(d, tp) + zx(d, px); }
static long sy_of(DOC *d, GRECT *tp, int pg, long py) { return tp->g_y - d->scroll_y + view_page_top(d, pg) + zx(d, py); }

/* an object's rectangle on screen */
static void obj_scr(DOC *d, GRECT *tp, const OBJ *o, long *x0, long *y0, long *x1, long *y1)
{
    long left = sx_of(d, tp, 0), top = sy_of(d, tp, o->page, 0);
    *x0 = left + tw2px_xs(o->x, d->zoom, 100);
    *y0 = top + tw2px_ys(o->y, d->zoom, 100);
    *x1 = left + tw2px_xs(o->x + o->w, d->zoom, 100);
    *y1 = top + tw2px_ys(o->y + o->h, d->zoom, 100);
}

int obj_at(DOC *d, int page, long px, long py)
{
    int i;
    for (i = d->a.nobj - 1; i >= 0; i--) {
        OBJ *o = &d->a.objs[i];
        long x0 = tw2px_x(o->x), y0 = tw2px_y(o->y), x1 = tw2px_x(o->x + o->w), y1 = tw2px_y(o->y + o->h);
        if (o->page == page && px >= x0 && px < x1 && py >= y0 && py < y1) return i;
    }
    return -1;
}

/* screen coordinates kept on the screen: the VDI draws where it's told
 * when clipping is off, and a frame or handle below the screen's last
 * line (a picture partly scrolled out of the window) wrote past the
 * screen memory and crashed */
static short on_scr_x(long x) { return (short)(x < 0 ? 0 : x >= scr_w ? scr_w - 1 : x); }
static short on_scr_y(long y) { return (short)(y < 0 ? 0 : y >= scr_h ? scr_h - 1 : y); }

static void xor_frame(long x0, long y0, long x1, long y1)
{
    short p[10];
    x0 = on_scr_x(x0); x1 = on_scr_x(x1); y0 = on_scr_y(y0); y1 = on_scr_y(y1);
    vswr_mode(vhandle, MD_XOR);
    vsl_color(vhandle, 1); vsl_type(vhandle, 7); vsl_udsty(vhandle, (short)0xAAAA); vsl_width(vhandle, 1);
    p[0] = p[6] = p[8] = (short)x0; p[2] = p[4] = (short)x1;
    p[1] = p[3] = p[9] = (short)y0; p[5] = p[7] = (short)y1;
    v_pline(vhandle, 5, p);
    vsl_type(vhandle, 1);
    vswr_mode(vhandle, MD_REPLACE);
}

static void handle_box(long x, long y)
{
    short p[4];
    vsf_interior(vhandle, FIS_SOLID); vsf_color(vhandle, 1); vsf_perimeter(vhandle, 0);
    p[0] = on_scr_x(x - HS); p[1] = on_scr_y(y - HS); p[2] = on_scr_x(x + HS); p[3] = on_scr_y(y + HS);
    vswr_mode(vhandle, MD_XOR);
    vr_recfl(vhandle, p);
    vswr_mode(vhandle, MD_REPLACE);
}

void obj_draw_handles(DOC *d, GRECT *tp)
{
    long x0, y0, x1, y1;
    if (d->selobj < 0 || d->selobj >= d->a.nobj) return;
    obj_scr(d, tp, &d->a.objs[d->selobj], &x0, &y0, &x1, &y1);
    xor_frame(x0, y0, x1 - 1, y1 - 1);
    handle_box(x0, y0); handle_box(x1 - 1, y0);
    handle_box(x0, y1 - 1); handle_box(x1 - 1, y1 - 1);
}

void obj_select(DOC *d, int i)
{
    if (i == d->selobj) return;
    d->selobj = i;
    if (i >= 0) d->anchor = -1;
    refresh_text(d);
    update_menu(d);
    set_info(d);
}

static void obj_moved(DOC *d)
{
    d->dirty = 1;
    doc_changed(d);
}

/* which corner handle (0-3: tl tr bl br) is under the point, or -1 */
static int handle_at(DOC *d, GRECT *tp, short mx, short my)
{
    long x0, y0, x1, y1, hx[4], hy[4];
    int k;
    if (d->selobj < 0) return -1;
    obj_scr(d, tp, &d->a.objs[d->selobj], &x0, &y0, &x1, &y1);
    hx[0] = hx[2] = x0; hx[1] = hx[3] = x1 - 1;
    hy[0] = hy[1] = y0; hy[2] = hy[3] = y1 - 1;
    for (k = 0; k < 4; k++)
        if (mx >= hx[k] - HS - 1 && mx <= hx[k] + HS + 1 && my >= hy[k] - HS - 1 && my <= hy[k] + HS + 1) return k;
    return -1;
}

/* drag the object (corner < 0) or one of its corners, rubber banding a
 * frame; returns whether it changed */
static int drag(DOC *d, GRECT *tp, short mx, short my, int corner)
{
    OBJ *o = &d->a.objs[d->selobj];
    long x0, y0, x1, y1, nx0, ny0, nx1, ny1, dx = 0, dy = 0, pw = page_w_px(d), ph = page_h_px(d);
    short x, y, b, k, lx = mx, ly = my;
    int moved = 0, keep = o->type == OB_IMG;
    long aw = o->w, ah = o->h;
    obj_scr(d, tp, o, &x0, &y0, &x1, &y1);
    nx0 = x0; ny0 = y0; nx1 = x1; ny1 = y1;
    {
        short c[4];                       /* the frame stays in the text pane */
        c[0] = on_scr_x(tp->g_x); c[1] = on_scr_y(tp->g_y);
        c[2] = on_scr_x((long)tp->g_x + tp->g_w - 1); c[3] = on_scr_y((long)tp->g_y + tp->g_h - 1);
        vs_clip(vhandle, 1, c);
    }
    wind_update(BEG_MCTRL);
    graf_mouse(corner < 0 ? FLAT_HAND : POINT_HAND, NULL);
    graf_mouse(M_OFF, NULL);
    xor_frame(nx0, ny0, nx1 - 1, ny1 - 1);
    graf_mouse(M_ON, NULL);
    for (;;) {
        graf_mkstate(&x, &y, &b, &k);
        if (!(b & 1)) break;
        if (x == lx && y == ly) { evnt_timer(10L); continue; }
        lx = x; ly = y;
        if (!moved && (x < mx - 2 || x > mx + 2 || y < my - 2 || y > my + 2)) moved = 1;
        if (!moved) continue;
        graf_mouse(M_OFF, NULL);
        xor_frame(nx0, ny0, nx1 - 1, ny1 - 1);
        dx = x - mx; dy = y - my;
        nx0 = x0; ny0 = y0; nx1 = x1; ny1 = y1;
        if (corner < 0) { nx0 += dx; nx1 += dx; ny0 += dy; ny1 += dy; }
        else {
            if (corner == 0 || corner == 2) nx0 += dx; else nx1 += dx;
            if (corner == 0 || corner == 1) ny0 += dy; else ny1 += dy;
            if (nx1 < nx0 + 8) { if (corner == 0 || corner == 2) nx0 = nx1 - 8; else nx1 = nx0 + 8; }
            if (ny1 < ny0 + 8) { if (corner == 0 || corner == 1) ny0 = ny1 - 8; else ny1 = ny0 + 8; }
            if (keep && !(k & 3) && aw > 0 && ah > 0) {      /* pictures keep their shape, unless Shift */
                long nh = (nx1 - nx0) * ah * pt2px_y(100) / (aw * pt2px_x(100));
                if (corner == 0 || corner == 1) ny0 = ny1 - nh; else ny1 = ny0 + nh;
            }
        }
        xor_frame(nx0, ny0, nx1 - 1, ny1 - 1);
        graf_mouse(M_ON, NULL);
    }
    graf_mouse(M_OFF, NULL);
    xor_frame(nx0, ny0, nx1 - 1, ny1 - 1);
    graf_mouse(M_ON, NULL);
    graf_mouse(ARROW, NULL);
    vs_clip(vhandle, 0, NULL);
    wind_update(END_MCTRL);
    if (!moved) return 0;
    {
        /* back to page twips, to the pixel at this zoom, kept on the page */
        long top = sy_of(d, tp, o->page, 0), left = sx_of(d, tp, 0), z = d->zoom;
        long tx0 = px2tw_xs(nx0 - left, z, 100), ty0 = px2tw_ys(ny0 - top, z, 100);
        long tx1 = px2tw_xs(nx1 - left, z, 100), ty1 = px2tw_ys(ny1 - top, z, 100);
        long tpw = (long)PS(d).w * TWIPS, tph = (long)PS(d).h * TWIPS;
        int pg = o->page;
        (void)pw;
        /* moved onto another page */
        if (corner < 0) {
            long step = px2tw_ys(zx(d, ph) + PGAP, z, 100), half = px2tw_ys(PGAP / 2, z, 100);
            while (ty0 > tph + half && pg < doc_npages(d) - 1) { ty0 -= step; ty1 -= step; pg++; }
            while (ty1 < -half && pg > 0) { ty0 += step; ty1 += step; pg--; }
        }
        if (tx1 - tx0 > tpw) tx1 = tx0 + tpw;
        if (ty1 - ty0 > tph) ty1 = ty0 + tph;
        if (tx0 < 0) { tx1 -= tx0; tx0 = 0; }
        if (ty0 < 0) { ty1 -= ty0; ty0 = 0; }
        if (tx1 > tpw) { tx0 -= tx1 - tpw; tx1 = tpw; }
        if (ty1 > tph) { ty0 -= ty1 - tph; ty1 = tph; }
        if (corner >= 0 && o->type == OB_DRAW) {
            /* a drawing's shapes grow with it */
            long ow = o->w, oh = o->h, nw = tx1 - tx0, nh = ty1 - ty0;
            int s;
            for (s = 0; s < o->nsh && ow > 0 && oh > 0; s++) {
                SHAPE *sh = &o->sh[s];
                sh->x1 = (short)(sh->x1 * nw / ow); sh->x2 = (short)(sh->x2 * nw / ow);
                sh->y1 = (short)(sh->y1 * nh / oh); sh->y2 = (short)(sh->y2 * nh / oh);
            }
        }
        o->page = (short)pg;
        o->x = tx0;
        o->y = ty0;
        o->w = tx1 - tx0;
        o->h = ty1 - ty0;
        if (o->w < 6 * TWIPS) o->w = 6 * TWIPS;
        if (o->h < 6 * TWIPS) o->h = 6 * TWIPS;
    }
    return 1;
}

int obj_click(DOC *d, GRECT *tp, short mx, short my, short ks, short clicks)
{
    long px, py;
    int page, i, h;
    (void)ks;
    h = handle_at(d, tp, mx, my);
    if (h >= 0) {
        if (drag(d, tp, mx, my, h)) obj_moved(d);
        return 1;
    }
    page = view_to_page(d, tp, mx, my, &px, &py);
    i = obj_at(d, page, px, py);
    if (i < 0) {
        if (d->selobj >= 0) { d->selobj = -1; refresh_text(d); update_menu(d); }
        return 0;
    }
    obj_select(d, i);
    if (clicks >= 2) {
        if (d->a.objs[i].type == OB_DRAW) draw_edit(d, i);
        else obj_props(d);
        return 1;
    }
    if (drag(d, tp, mx, my, -1)) obj_moved(d);
    return 1;
}

int obj_key(DOC *d, short ks, short key)
{
    short scan = (key >> 8) & 0xff;
    OBJ *o;
    /* arrows: 6 points; with Shift, one screen pixel */
    long step = (ks & 3) ? px2tw_xs(1, d->zoom, 100) : 6L * TWIPS, stepy = (ks & 3) ? px2tw_ys(1, d->zoom, 100) : 6L * TWIPS;
    if (d->selobj < 0 || d->selobj >= d->a.nobj) return 0;
    o = &d->a.objs[d->selobj];
    switch (scan) {
    case 0x53: case 0x0E: obj_delete(d); return 1;            /* Delete, Backspace */
    case 0x01: obj_select(d, -1); return 1;                   /* Esc */
    case 0x1C: case 0x72:                                     /* Return */
        if (o->type == OB_DRAW) draw_edit(d, d->selobj); else obj_props(d);
        return 1;
    case 0x4B: o->x = o->x > step ? o->x - step : 0; break;
    case 0x4D: if (o->x + o->w + step <= (long)PS(d).w * TWIPS) o->x += step; break;
    case 0x48: o->y = o->y > stepy ? o->y - stepy : 0; break;
    case 0x50: if (o->y + o->h + stepy <= (long)PS(d).h * TWIPS) o->y += stepy; break;
    default:
        obj_select(d, -1);
        return 0;
    }
    obj_moved(d);
    return 1;
}

void obj_props(DOC *d)
{
    OBJ o;
    int r, i = d->selobj;
    if (i < 0 || i >= d->a.nobj) return;
    o = d->a.objs[i];
    r = dlg_objprops(d, &o);
    if (r == 0) return;
    if (r == 2) { obj_delete(d); return; }
    d->a.objs[i] = o;
    obj_moved(d);
    if (r == 3) draw_edit(d, i);
}

void obj_delete(DOC *d)
{
    int i = d->selobj;
    if (i < 0 || i >= d->a.nobj) return;
    draw_close_for(d, i);
    doc_obj_delete(d, i);
    d->selobj = -1;
    doc_changed(d);
}

/* where a new object goes: the caret's page, below the caret's line */
static void place_new(DOC *d, OBJ *o, long w, long h)
{
    long li = doc_find_line(d, d->cursor), tw;
    PGFMT f;
    LINE *ln = d->nlines ? &d->lines[li] : NULL;
    int page = ln ? ln->page : 0;
    page_fmt(d, page, &f);
    tw = PS(d).w - f.ml - f.mr;
    if (w > tw) { h = h * tw / w; w = tw; }
    if (h > PS(d).h - f.mt - f.mb) { w = w * (PS(d).h - f.mt - f.mb) / h; h = PS(d).h - f.mt - f.mb; }
    if (w < 6) w = 6;
    if (h < 6) h = 6;
    o->page = (short)page;
    o->w = w * TWIPS;
    o->h = h * TWIPS;
    o->x = (f.ml + (tw - w) / 2) * (long)TWIPS;
    o->y = (ln ? px2pt_y(ln->py + ln->lh) : f.mt) * (long)TWIPS;
    if (o->y + o->h > (long)(PS(d).h - f.mb) * TWIPS) o->y = (long)(PS(d).h - f.mb - h) * TWIPS;
    if (o->y < (long)f.mt * TWIPS) o->y = (long)f.mt * TWIPS;
}

/* insert the GEM IMG picture at path */
int picture_insert(DOC *d, const char *path)
{
    IMG im;
    OBJ o;
    int k, i;
    if (d->a.nobj >= MAX_OBJS || d->nimg >= MAX_IMGS) { alert(1, 3, AL_TOO_MANY_OBJS, AB_OK); return 0; }
    busy_begin(L(S_LOADING_PIC));
    k = img_load(path, &im);
    busy_end();
    if (!k) { alert(1, 3, AL_CANT_READ_IMG, AB_OK); return 0; }
    k = doc_img_add(d, &im);
    if (k < 0) { img_free(&im); return 0; }
    {
        const char *b = strrchr(path, '\\');
        strncpy(d->imgs[k].name, b ? b + 1 : path, 13);
        d->imgs[k].name[13] = 0;
    }
    memset(&o, 0, sizeof o);
    o.type = OB_IMG;
    o.img = (short)k;
    o.wrap = WR_BOX;
    place_new(d, &o, px2pt_x(im.w), px2pt_y(im.h));
    i = doc_obj_add(d, &o);
    if (i < 0) return 0;
    d->selobj = i;
    d->anchor = -1;
    obj_moved(d);
    return 1;
}

void cmd_picture(DOC *d)
{
    char path[300];
    if (d->a.nobj >= MAX_OBJS || d->nimg >= MAX_IMGS) { alert(1, 3, AL_TOO_MANY_OBJS, AB_OK); return; }
    if (ask_file(L(S_FS_PICTURE), "*.*", "", path)) picture_insert(d, path);
}

void cmd_drawing(DOC *d)
{
    OBJ o;
    int i;
    if (d->a.nobj >= MAX_OBJS) { alert(1, 3, AL_TOO_MANY_OBJS, AB_OK); return; }
    memset(&o, 0, sizeof o);
    o.type = OB_DRAW;
    o.img = -1;
    o.wrap = WR_BOX;
    place_new(d, &o, 170, 113);         /* 60 x 40 mm */
    i = doc_obj_add(d, &o);
    if (i < 0) return;
    d->selobj = i;
    d->anchor = -1;
    obj_moved(d);
    draw_edit(d, i);
}

/* ================================================================= */
/*  Drawing with VDI                                                   */
/* ================================================================= */

/* ---- full colour pictures on screens of 15 bits or more ---------------- */

/* vq_scrninfo() says where each colour's bits go in a pixel; the tables
 * give each 8-bit level its bits, so a pixel is three look-ups */
static int tc_ok, tc_bpp, tc_planes;
static unsigned long tc_lut[3][256];

static long cookie_vdo;
static long read_vdo(void)
{
    long *jar = *(long **)0x5A0L;
    cookie_vdo = -1;
    if (jar) for (; jar[0]; jar += 2) if (jar[0] == 0x5F56444FL) { cookie_vdo = jar[1]; break; }     /* _VDO */
    return 0;
}

int tc_init(void)
{
    short ext[57], out[272];
    int i, c, v;
    vq_extnd(vhandle, 1, ext);
    if (ext[4] < 15) return 0;
    for (i = 0; i < 272; i++) out[i] = -2;
    vq_scrninfo(vhandle, out);
    edlog("vq_scrninfo: format %d, clut %d, %d planes, bits r%d g%d b%d", out[0], out[1], out[2], out[8], out[9], out[10]);
    if (out[0] != 2 || out[1] != 2 || out[2] != ext[4] || out[2] > 32) {
        /* no answer (EmuTOS): a Falcon in high colour is RRRRRGGGGGxBBBBB */
        Supexec(read_vdo);
        edlog("no vq_scrninfo: %d planes, _VDO %lx", ext[4], cookie_vdo);
        if (ext[4] != 16 || (cookie_vdo >> 16) != 3) return 0;
        for (i = 0; i < 272; i++) out[i] = -1;
        out[0] = 2; out[1] = 2; out[2] = 16;
        for (i = 0; i < 5; i++) { out[16 + i] = (short)(11 + i); out[32 + i] = (short)(6 + i); out[48 + i] = (short)i; }
    }
    for (c = 0; c < 3; c++) {
        short pos[16];
        int n = 0;
        for (i = 0; i < 16; i++) {
            short p = out[16 + 16 * c + i];
            if (p < 0 || p > 31) break;
            pos[n++] = p;
        }
        if (!n) return 0;
        for (v = 0; v < 256; v++) {
            unsigned long px = 0, val = n <= 8 ? (unsigned long)v >> (8 - n) : (unsigned long)v * ((1UL << n) - 1) / 255;
            for (i = 0; i < n; i++) if (val & (1UL << i)) px |= 1UL << pos[i];
            tc_lut[c][v] = px;
        }
    }
    tc_planes = out[2];
    tc_bpp = (tc_planes + 7) / 8;
    tc_ok = 1;
    return 1;
}

/* ---- colour pictures on screens with a palette (4, 16 or 256 colours) ---
 * The pens' colours come from the VDI, and the pixel value of each pen
 * from drawing it into a little memory form. Pictures are dithered to
 * the pens (error diffusion), with a 15-bit colour cube that remembers
 * the nearest pen of each colour once it has been looked for. */
static int   pal_ok, pal_planes, pal_n;
static short pal_rgb[256][3];                  /* each pen, 0-255 */
static unsigned char pal_px[256];              /* each pen's pixel value */
static unsigned short *pal_cube;               /* 32768: pen + 1, 0 not known yet */
static int   pal_skip = -1;                    /* a pen the display mode changed */

int pal_init(void)
{
    short ext[57], rgb[3], pxy[8], col[2], i, k;
    unsigned short one[1], dev[8], std[8];
    MFDB src, dm, sm;
    vq_extnd(vhandle, 1, ext);
    if (ext[4] < 2 || ext[4] > 8) return 0;
    pal_planes = ext[4];
    pal_n = 1 << pal_planes;
    pal_cube = (unsigned short *)calloc(32768, sizeof(unsigned short));
    if (!pal_cube) return 0;
    memset(&src, 0, sizeof src); memset(&dm, 0, sizeof dm); memset(&sm, 0, sizeof sm);
    one[0] = 0x8000;
    src.fd_addr = one; src.fd_w = 16; src.fd_h = 1; src.fd_wdwidth = 1; src.fd_nplanes = 1;
    dm.fd_addr = dev; dm.fd_w = 16; dm.fd_h = 1; dm.fd_wdwidth = 1; dm.fd_nplanes = (short)pal_planes;
    sm = dm; sm.fd_addr = std; sm.fd_stand = 1;
    pxy[0] = pxy[1] = pxy[2] = pxy[3] = 0;
    pxy[4] = pxy[5] = pxy[6] = pxy[7] = 0;
    for (i = 0; i < pal_n; i++) {
        vq_color(vhandle, i, 1, rgb);
        for (k = 0; k < 3; k++) pal_rgb[i][k] = (short)((rgb[k] < 0 ? 0 : rgb[k] > 1000 ? 1000 : rgb[k]) * 255L / 1000);
        /* the pen's pixel value: drawn into memory, then read plane by plane */
        memset(dev, 0, sizeof dev);
        col[0] = i; col[1] = 0;
        vrt_cpyfm(vhandle, MD_REPLACE, pxy, &src, &dm, col);
        vr_trnfm(vhandle, &dm, &sm);
        pal_px[i] = 0;
        for (k = 0; k < pal_planes; k++) if (std[k] & 0x8000) pal_px[i] |= (unsigned char)(1 << k);
    }
    /* every pen the same pixel: the VDI didn't draw into memory */
    for (i = 1; i < pal_n && pal_px[i] == pal_px[0]; i++) ;
    if (i == pal_n) { free(pal_cube); pal_cube = NULL; return 0; }
    edlog("colour pictures on %d pens, pen 1 is pixel %d", pal_n, pal_px[1]);
    pal_ok = 1;
    return 1;
}

static int pal_nearest(int r, int g, int b)
{
    unsigned idx = (unsigned)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    int skip = pref_display == DISP_AMBER && pal_n >= 16 ? 6 : -1;
    if (skip != pal_skip) { memset(pal_cube, 0, 32768 * sizeof(unsigned short)); pal_skip = skip; }
    if (!pal_cube[idx]) {
        long best = 0x7fffffffL;
        int i, bi = 0, cr = (r & ~7) | 4, cg = (g & ~7) | 4, cb = (b & ~7) | 4;
        for (i = 0; i < pal_n; i++) {
            long dr = cr - pal_rgb[i][0], dg = cg - pal_rgb[i][1], db = cb - pal_rgb[i][2];
            long dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (i == skip) continue;
            if (pal_n >= 256 && i >= TH_PEN0 && i < TH_PEN0 + TH_NROLES) continue;   /* the colour theme's */
            if (dd < best) { best = dd; bi = i; }
        }
        pal_cube[idx] = (unsigned short)(bi + 1);
    }
    return pal_cube[idx] - 1;
}

/* w x h pixels of R G B, dithered to the pens, in the screen's format */
static unsigned char *pal_form(const unsigned char *rgb, long w, long h, MFDB *out)
{
    long wd = (w + 15) / 16, pl = wd * 2 * h, x, y;
    unsigned char *std = (unsigned char *)calloc((size_t)(pl * pal_planes), 1), *dev;
    short *e0 = (short *)calloc((size_t)(w + 2) * 3, sizeof(short)), *e1 = (short *)calloc((size_t)(w + 2) * 3, sizeof(short)), *t;
    MFDB sm;
    if (!std || !e0 || !e1) { free(std); free(e0); free(e1); return NULL; }
    for (y = 0; y < h; y++) {
        const unsigned char *s = rgb + y * w * 3;
        memset(e1, 0, (size_t)(w + 2) * 3 * sizeof(short));
        for (x = 0; x < w; x++) {
            int c[3], k, pen, px;
            for (k = 0; k < 3; k++) {
                int v = s[x * 3 + k] + e0[(x + 1) * 3 + k] / 16;
                c[k] = v < 0 ? 0 : v > 255 ? 255 : v;
            }
            pen = pal_nearest(c[0], c[1], c[2]);
            for (k = 0; k < 3; k++) {
                int err = c[k] - pal_rgb[pen][k];
                e0[(x + 2) * 3 + k] += (short)(err * 7);
                e1[x * 3 + k] += (short)(err * 3);
                e1[(x + 1) * 3 + k] += (short)(err * 5);
                e1[(x + 2) * 3 + k] += (short)err;
            }
            px = pal_px[pen];
            for (k = 0; k < pal_planes; k++)
                if (px & (1 << k)) std[k * pl + y * wd * 2 + (x >> 3)] |= (unsigned char)(0x80 >> (x & 7));
        }
        t = e0; e0 = e1; e1 = t;
    }
    free(e0); free(e1);
    dev = (unsigned char *)malloc((size_t)(pl * pal_planes));
    if (!dev) { free(std); return NULL; }
    memset(&sm, 0, sizeof sm);
    sm.fd_addr = std; sm.fd_w = (short)(wd * 16); sm.fd_h = (short)h; sm.fd_wdwidth = (short)wd;
    sm.fd_stand = 1; sm.fd_nplanes = (short)pal_planes;
    *out = sm;
    out->fd_addr = dev; out->fd_stand = 0;
    vr_trnfm(vhandle, &sm, out);
    free(std);
    return dev;
}

/* the last few pictures scaled for the screen */
#define NCACHE 6
static struct {
    DOC *d;
    const unsigned char *src;
    long w, h, use;
    int colour;
    MFDB m;
} cache[NCACHE];
static long cache_clock;

void img_cache_free(DOC *d)
{
    int i;
    for (i = 0; i < NCACHE; i++)
        if (cache[i].d && (!d || cache[i].d == d)) {
            free(cache[i].m.fd_addr);
            cache[i].d = NULL;
        }
}

/* colour: the picture in the screen's own pixels */
static MFDB *scaled(DOC *d, const IMG *im, long w, long h, int colour)
{
    int i, lru = 0;
    IMG sc;
    long wd, y;
    unsigned char *buf;
    for (i = 0; i < NCACHE; i++)
        if (cache[i].d == d && cache[i].src == im->bits && cache[i].w == w && cache[i].h == h && cache[i].colour == colour) {
            cache[i].use = ++cache_clock;
            return &cache[i].m;
        }
    if (w < 1 || h < 1 || w > 4000 || h > 4000) return NULL;
    for (i = 1; i < NCACHE; i++)
        if (!cache[i].d || cache[i].use < cache[lru].use) lru = i;
    if (!cache[lru].d) for (i = 0; i < NCACHE; i++) if (!cache[i].d) { lru = i; break; }
    if (cache[lru].d) { free(cache[lru].m.fd_addr); cache[lru].d = NULL; }
    if (colour && pal_ok) {
        unsigned char *rgb = img_scale_rgb(im, w, h);
        if (!rgb) return NULL;
        buf = pal_form(rgb, w, h, &cache[lru].m);
        free(rgb);
        if (!buf) return NULL;
        cache[lru].d = d; cache[lru].src = im->bits; cache[lru].w = w; cache[lru].h = h; cache[lru].colour = 1;
        cache[lru].use = ++cache_clock;
        return &cache[lru].m;
    }
    if (colour) {
        unsigned char *rgb = img_scale_rgb(im, w, h), *o;
        long x;
        if (!rgb) return NULL;
        wd = (w + 15) / 16;
        buf = (unsigned char *)malloc((size_t)(wd * 16 * tc_bpp * h));
        if (!buf) { free(rgb); return NULL; }
        for (y = 0; y < h; y++) {
            const unsigned char *s = rgb + y * w * 3;
            o = buf + y * wd * 16 * tc_bpp;
            for (x = 0; x < w; x++, s += 3) {
                unsigned long px = tc_lut[0][s[0]] | tc_lut[1][s[1]] | tc_lut[2][s[2]];
                switch (tc_bpp) {
                case 4: *o++ = (unsigned char)(px >> 24);       /* fall through */
                case 3: *o++ = (unsigned char)(px >> 16);       /* fall through */
                default: *o++ = (unsigned char)(px >> 8); *o++ = (unsigned char)px;
                }
            }
        }
        free(rgb);
        cache[lru].d = d; cache[lru].src = im->bits; cache[lru].w = w; cache[lru].h = h; cache[lru].colour = 1;
        cache[lru].use = ++cache_clock;
        cache[lru].m.fd_addr = buf;
        cache[lru].m.fd_w = (short)(wd * 16); cache[lru].m.fd_h = (short)h;
        cache[lru].m.fd_wdwidth = (short)wd; cache[lru].m.fd_stand = 0; cache[lru].m.fd_nplanes = (short)tc_planes;
        cache[lru].m.fd_r1 = cache[lru].m.fd_r2 = cache[lru].m.fd_r3 = 0;
        return &cache[lru].m;
    }
    if (!img_scale(im, w, h, &sc)) return NULL;
    wd = (w + 15) / 16;
    buf = (unsigned char *)calloc((size_t)(wd * 2 * h), 1);
    if (!buf) { img_free(&sc); return NULL; }
    for (y = 0; y < h; y++) memcpy(buf + y * wd * 2, sc.bits + y * sc.rb, (size_t)sc.rb);
    img_free(&sc);
    cache[lru].d = d; cache[lru].src = im->bits; cache[lru].w = w; cache[lru].h = h; cache[lru].colour = 0;
    cache[lru].use = ++cache_clock;
    cache[lru].m.fd_addr = buf;
    cache[lru].m.fd_w = (short)(wd * 16); cache[lru].m.fd_h = (short)h;
    cache[lru].m.fd_wdwidth = (short)wd; cache[lru].m.fd_stand = 0; cache[lru].m.fd_nplanes = 1;
    cache[lru].m.fd_r1 = cache[lru].m.fd_r2 = cache[lru].m.fd_r3 = 0;
    return &cache[lru].m;
}

/* one shape of a drawing whose box is at screen (ox, oy) */
static void draw_shape(const DOC *d, const SHAPE *s, long ox, long oy, long n, long den)
{
    long xy[2 * 56];
    short p[2 * 57];
    long x0 = ox + pt2px_x(s->x1) * n / den, y0 = oy + pt2px_y(s->y1) * n / den;
    long x1 = ox + pt2px_x(s->x2) * n / den, y1 = oy + pt2px_y(s->y2) * n / den;
    int k, np, pen = color_pen(s->color);
    short lw = (short)(s->lw * n / den);
    if (lw < 1 && s->lw) lw = 1;
    if (!(lw & 1)) lw++;                              /* VDI line widths are odd */
    vsl_color(vhandle, (short)pen); vsl_width(vhandle, lw); vsl_type(vhandle, 1);
    vsl_ends(vhandle, 0, 0);
    if (s->type == SH_FREE) {                   /* a polyline, 64 points at a time */
        const short *fp = free_pts(d, s);
        int i, m = 0;
        if (!fp || !s->lw) { vsl_width(vhandle, 1); return; }
        if (lw > 1) vsl_ends(vhandle, 2, 2);    /* round ends: the pieces join smoothly */
        vswr_mode(vhandle, MD_TRANS);
        for (i = 0; i < s->np; i++) {
            p[2 * m] = (short)(x0 + (x1 - x0) * fp[2 * i] / FREE_ONE);
            p[2 * m + 1] = (short)(y0 + (y1 - y0) * fp[2 * i + 1] / FREE_ONE);
            if (++m == 56 || i == s->np - 1) {
                if (m > 1) v_pline(vhandle, (short)m, p);
                p[0] = p[2 * (m - 1)]; p[1] = p[2 * (m - 1) + 1];
                m = 1;
            }
        }
        vsl_ends(vhandle, 0, 0);
        vsl_width(vhandle, 1);
        return;
    }
    if (s->type == SH_LINE) {
        if (!s->lw) return;
        p[0] = (short)x0; p[1] = (short)y0; p[2] = (short)x1; p[3] = (short)y1;
        vswr_mode(vhandle, MD_TRANS);
        v_pline(vhandle, 2, p);
        vsl_width(vhandle, 1);
        return;
    }
    np = shape_pts(s->type, x0 * 16, y0 * 16, x1 * 16, y1 * 16, xy);
    for (k = 0; k < np; k++) { p[2 * k] = (short)(xy[2 * k] / 16); p[2 * k + 1] = (short)(xy[2 * k + 1] / 16); }
    p[2 * np] = p[0]; p[2 * np + 1] = p[1];
    if (s->fill != FL_NONE) {
        vsf_perimeter(vhandle, 0);
        if (s->fill == FL_WHITE) {
            vswr_mode(vhandle, MD_REPLACE); vsf_interior(vhandle, FIS_SOLID); vsf_color(vhandle, 0);
        } else if (s->fill == FL_GREY) {
            vswr_mode(vhandle, MD_TRANS); vsf_interior(vhandle, FIS_PATTERN); vsf_style(vhandle, 4); vsf_color(vhandle, (short)pen);
        } else {
            vswr_mode(vhandle, MD_TRANS); vsf_interior(vhandle, FIS_SOLID); vsf_color(vhandle, (short)pen);
        }
        v_fillarea(vhandle, (short)np, p);
    }
    if (s->lw) {
        vswr_mode(vhandle, MD_TRANS);
        v_pline(vhandle, (short)(np + 1), p);
    }
    vsl_width(vhandle, 1);
    vswr_mode(vhandle, MD_TRANS);
}

static void draw_frame(long x0, long y0, long x1, long y1, short pen)
{
    short p[10];
    vsl_color(vhandle, pen); vsl_width(vhandle, 1); vsl_type(vhandle, 1);
    p[0] = p[6] = p[8] = (short)x0; p[2] = p[4] = (short)x1;
    p[1] = p[3] = p[9] = (short)y0; p[5] = p[7] = (short)y1;
    v_pline(vhandle, 5, p);
}

/* a picture or drawing on a page whose top left corner is at screen
 * (ox, oy), at n/den */
void draw_vdi_obj(DOC *d, const OBJ *o, long ox, long oy, long n, long den)
{
    long x = ox + tw2px_xs(o->x, n, den), y = oy + tw2px_ys(o->y, n, den);
    long w = ox + tw2px_xs(o->x + o->w, n, den) - x, h = oy + tw2px_ys(o->y + o->h, n, den) - y;
    int k;
    if (o->type == OB_IMG && o->img >= 0 && o->img < d->nimg) {
        /* colour, unless it has already failed for want of memory at this size:
         * trying again at every redraw would make typing crawl */
        static struct { const unsigned char *src; long w, h; } nocol[8];
        static int nnc;
        int colour = (tc_ok || pal_ok) && d->imgs[o->img].rgb != NULL, f;
        MFDB *m, scr;
        for (f = 0; f < 8 && colour; f++)
            if (nocol[f].src == d->imgs[o->img].bits && nocol[f].w == w && nocol[f].h == h) colour = 0;
        m = scaled(d, &d->imgs[o->img], w, h, colour);
        if (!m && colour) {
            nocol[nnc].src = d->imgs[o->img].bits; nocol[nnc].w = w; nocol[nnc].h = h;
            nnc = (nnc + 1) & 7;
            m = scaled(d, &d->imgs[o->img], w, h, colour = 0);
        }
        if (m) {
            short pxy[8], col[2];
            memset(&scr, 0, sizeof scr);
            pxy[0] = 0; pxy[1] = 0; pxy[2] = (short)(w - 1); pxy[3] = (short)(h - 1);
            pxy[4] = (short)x; pxy[5] = (short)y; pxy[6] = (short)(x + w - 1); pxy[7] = (short)(y + h - 1);
            col[0] = 1; col[1] = 0;
            if (colour) vro_cpyfm(vhandle, S_ONLY, pxy, m, &scr);
            else vrt_cpyfm(vhandle, MD_TRANS, pxy, m, &scr, col);
        } else {
            /* no memory: a crossed box */
            short p[4];
            draw_frame(x, y, x + w - 1, y + h - 1, 1);
            p[0] = (short)x; p[1] = (short)y; p[2] = (short)(x + w - 1); p[3] = (short)(y + h - 1);
            v_pline(vhandle, 2, p);
        }
    }
    if (o->type == OB_DRAW) {
        for (k = 0; k < o->nsh; k++) draw_shape(d, &o->sh[k], x, y, n, den);
        if (o->nsh == 0) {
            vsl_type(vhandle, 3);
            draw_frame(x, y, x + w - 1, y + h - 1, 1);
            vsl_type(vhandle, 1);
        }
    }
    if (o->border) {
        short t = (short)(n / den > 0 ? n / den : 1), i;
        for (i = 0; i < t; i++) draw_frame(x + i, y + i, x + w - 1 - i, y + h - 1 - i, 1);
    }
    vswr_mode(vhandle, MD_TRANS);
}

/* ================================================================= */
/*  The drawing window                                                 */
/* ================================================================= */

enum { T_SELECT, T_RECT, T_RRECT, T_ELLIPSE, T_LINE, T_TRI, T_FREE, NTOOLS };
enum { B_TOOL0, B_FILL = NTOOLS, B_WIDTH, B_COLOUR, B_FRONT, B_DELETE, B_DONE, NBTN };
#define DKIND (NAME | CLOSER | MOVER | SIZER)

static struct {
    int    open;
    short  wh;
    DOC   *d;
    int    oi, tool, sel;
    int    fill, lw, color;          /* for new shapes */
    GRECT  bt[NBTN];                 /* buttons, relative to the work area */
    short  barh;
    long   num, den, cx, cy;          /* canvas: scale, box origin on screen */
    char   name[60];
} dw = { 0, -1, NULL, 0, T_RECT, -1, FL_NONE, 1, 0 };

static const char *btn_text(int b, char *buf)
{
    static const int tools[NTOOLS] = { S_DT_SELECT, S_DT_RECT, S_DT_RRECT, S_DT_ELLIPSE, S_DT_LINE, S_DT_TRI, S_DT_FREE };
    static const int fills[NFILLS] = { S_FILL_NONE, S_FILL_WHITE, S_FILL_GREY, S_FILL_SOLID };
    const SHAPE *s = NULL;
    if (dw.d && dw.sel >= 0 && dw.oi < dw.d->a.nobj && dw.sel < dw.d->a.objs[dw.oi].nsh) s = &dw.d->a.objs[dw.oi].sh[dw.sel];
    if (b < NTOOLS) return L(tools[b]);
    switch (b) {
    case B_FILL: sprintf(buf, "%s: %s", L(S_DT_FILL), L(fills[s ? s->fill : dw.fill])); return buf;
    case B_WIDTH: sprintf(buf, "%s: %d", L(S_DT_WIDTH), s ? s->lw : dw.lw); return buf;
    case B_COLOUR: sprintf(buf, "%s", L(S_DT_COLOUR)); return buf;
    case B_FRONT: return L(S_DT_FRONT);
    case B_DELETE: return L(S_DELETE);
    case B_DONE: return L(S_DT_DONE);
    }
    return "";
}

/* lay the buttons out in rows across width w; returns the bar height */
static short lay_buttons(short w)
{
    char buf[60];
    short m = (short)(aes_3d ? 4 : 2), g = (short)(aes_3d ? 6 : 2);   /* 3D buttons stand 2 pixels out */
    short x = m, y = m, bh = (short)(hchar + 4), b;
    for (b = 0; b < NBTN; b++) {
        short bw = (short)((strlen(btn_text(b, buf)) + 2) * wchar);
        if (b == B_FILL || b == B_WIDTH) bw = (short)(bw + 3 * wchar);  /* room for longer values */
        if (x + bw > w - m && x > m) { x = m; y = (short)(y + bh + g); }
        dw.bt[b].g_x = x; dw.bt[b].g_y = y; dw.bt[b].g_w = bw; dw.bt[b].g_h = bh;
        x = (short)(x + bw + (b == T_FREE || b == B_COLOUR ? wchar : g));
    }
    return (short)(y + bh + m + 1);
}

static OBJ *dobj(void)
{
    if (!dw.open || !dw.d || !dw.d->used || dw.oi >= dw.d->a.nobj || dw.d->a.objs[dw.oi].type != OB_DRAW) return NULL;
    return &dw.d->a.objs[dw.oi];
}

/* fit the drawing's box into the canvas */
static void canvas_geom(GRECT *wa, GRECT *cv)
{
    OBJ *o = dobj();
    long bw, bh;
    cv->g_x = wa->g_x; cv->g_y = (short)(wa->g_y + dw.barh);
    cv->g_w = wa->g_w; cv->g_h = (short)(wa->g_h - dw.barh);
    if (!o) return;
    bw = tw2px_x(o->w); bh = tw2px_y(o->h);
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    dw.num = cv->g_w - 16; dw.den = bw;
    if ((cv->g_h - 16) * bw < dw.num * bh) { dw.num = cv->g_h - 16; dw.den = bh; }
    if (dw.num < 1) dw.num = 1;
    if (dw.num > 8 * dw.den) dw.num = 8 * dw.den;
    dw.cx = cv->g_x + (cv->g_w - bw * dw.num / dw.den) / 2;
    dw.cy = cv->g_y + (cv->g_h - bh * dw.num / dw.den) / 2;
}

/* the button bar: an AES object tree, so it has the AES's look (3D
 * where it has it) */
static OBJECT dwt[NBTN + 1];
static char   dwtxt[NBTN][60];

static void bar_tree(GRECT *wa)
{
    int b;
    memset(dwt, 0, sizeof dwt);
    dwt[0].ob_next = -1; dwt[0].ob_head = 1; dwt[0].ob_tail = NBTN;
    dwt[0].ob_type = G_BOX; dwt[0].ob_spec.index = 0x00001100L;   /* white, or the AES's grey */
    dwt[0].ob_x = wa->g_x; dwt[0].ob_y = wa->g_y; dwt[0].ob_width = wa->g_w; dwt[0].ob_height = dw.barh;
    if (aes_3d) dwt[0].ob_flags = OF_FL3DBAK;
    for (b = 0; b < NBTN; b++) {
        OBJECT *o = &dwt[b + 1];
        const char *t = btn_text(b, dwtxt[b]);
        if (t != dwtxt[b]) { strncpy(dwtxt[b], t, 59); dwtxt[b][59] = 0; }
        o->ob_next = (short)(b + 1 < NBTN ? b + 2 : 0); o->ob_head = o->ob_tail = -1;
        o->ob_type = G_BUTTON; o->ob_spec.free_string = dwtxt[b];
        o->ob_flags = (short)(OF_SELECTABLE | (aes_3d ? OF_FL3DACT : 0) | (b == NBTN - 1 ? OF_LASTOB : 0));
        o->ob_state = (short)(((b < NTOOLS && b == dw.tool) ? OS_SELECTED : 0) |
                              ((b >= B_FRONT && b <= B_DELETE && dw.sel < 0) ? OS_DISABLED : 0));
        o->ob_x = dw.bt[b].g_x; o->ob_y = dw.bt[b].g_y; o->ob_width = dw.bt[b].g_w; o->ob_height = dw.bt[b].g_h;
    }
    if (theme_on()) {                           /* drawn in the colour theme's colours */
        static USERBLK ub[NBTN + 1];
        theme_userdef(&dwt[0], &ub[0], 0);
        for (b = 0; b < NBTN; b++) theme_userdef(&dwt[b + 1], &ub[b + 1], (long)dwtxt[b]);
    }
}

static void sel_frame(const SHAPE *s)
{
    long x0 = dw.cx + pt2px_x(s->x1 < s->x2 ? s->x1 : s->x2) * dw.num / dw.den - 2;
    long y0 = dw.cy + pt2px_y(s->y1 < s->y2 ? s->y1 : s->y2) * dw.num / dw.den - 2;
    long x1 = dw.cx + pt2px_x(s->x1 < s->x2 ? s->x2 : s->x1) * dw.num / dw.den + 2;
    long y1 = dw.cy + pt2px_y(s->y1 < s->y2 ? s->y2 : s->y1) * dw.num / dw.den + 2;
    xor_frame(x0, y0, x1, y1);
    handle_box(x1, y1);
}

static void dw_draw(GRECT *area)
{
    GRECT r, wa, cv;
    OBJ *o = dobj();
    short clip[4], p[4];
    int k;
    wind_get(dw.wh, WF_WORKXYWH, &wa.g_x, &wa.g_y, &wa.g_w, &wa.g_h);
    canvas_geom(&wa, &cv);
    bar_tree(&wa);
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    wind_get(dw.wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        GRECT c = r;
        short x1 = c.g_x > area->g_x ? c.g_x : area->g_x, y1 = c.g_y > area->g_y ? c.g_y : area->g_y;
        short x2 = (c.g_x + c.g_w < area->g_x + area->g_w) ? c.g_x + c.g_w : area->g_x + area->g_w;
        short y2 = (c.g_y + c.g_h < area->g_y + area->g_h) ? c.g_y + c.g_h : area->g_y + area->g_h;
        if (x2 > x1 && y2 > y1) {
            clip[0] = x1; clip[1] = y1; clip[2] = (short)(x2 - 1); clip[3] = (short)(y2 - 1);
            vs_clip(vhandle, 1, clip);
            vswr_mode(vhandle, MD_REPLACE);
            vsf_perimeter(vhandle, 0);
            if (y1 < cv.g_y) objc_draw(dwt, 0, 1, x1, y1, (short)(x2 - x1), (short)(y2 - y1));
            vsf_interior(vhandle, FIS_PATTERN); vsf_style(vhandle, 1); vsf_color(vhandle, 1);
            p[0] = cv.g_x; p[1] = cv.g_y; p[2] = (short)(cv.g_x + cv.g_w - 1); p[3] = (short)(cv.g_y + cv.g_h - 1);
            vr_recfl(vhandle, p);
            if (o) {
                long bw = tw2px_x(o->w) * dw.num / dw.den, bh = tw2px_y(o->h) * dw.num / dw.den;
                vsf_interior(vhandle, FIS_SOLID); vsf_color(vhandle, 0);
                p[0] = (short)dw.cx; p[1] = (short)dw.cy; p[2] = (short)(dw.cx + bw - 1); p[3] = (short)(dw.cy + bh - 1);
                vswr_mode(vhandle, MD_REPLACE);
                vr_recfl(vhandle, p);
                draw_frame(dw.cx - 1, dw.cy - 1, dw.cx + bw, dw.cy + bh, 1);
                p[2] = (short)(dw.cx + bw - 1); p[3] = (short)(dw.cy + bh - 1);
                {
                    short c2[4];
                    c2[0] = clip[0] > p[0] ? clip[0] : p[0]; c2[1] = clip[1] > p[1] ? clip[1] : p[1];
                    c2[2] = clip[2] < p[2] ? clip[2] : p[2]; c2[3] = clip[3] < p[3] ? clip[3] : p[3];
                    if (c2[2] >= c2[0] && c2[3] >= c2[1]) {
                        vs_clip(vhandle, 1, c2);
                        for (k = 0; k < o->nsh; k++) draw_shape(dw.d, &o->sh[k], dw.cx, dw.cy, dw.num, dw.den);
                        vs_clip(vhandle, 1, clip);
                    }
                }
                if (dw.sel >= 0 && dw.sel < o->nsh) sel_frame(&o->sh[dw.sel]);
            }
        }
        wind_get(dw.wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    vs_clip(vhandle, 0, clip);
    vswr_mode(vhandle, MD_REPLACE);
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
}

static void dw_redraw(void)
{
    GRECT wa;
    if (!dw.open) return;
    wind_get(dw.wh, WF_WORKXYWH, &wa.g_x, &wa.g_y, &wa.g_w, &wa.g_h);
    dw_draw(&wa);
}

static void dw_set_name(void)
{
    sprintf(dw.name, " %s - %.30s ", L(S_DRAW_TITLE), dw.d ? dw.d->base : "");
    wind_set_str(dw.wh, WF_NAME, dw.name);
}

static void dw_close(void)
{
    if (!dw.open) return;
    winpos_save(" drawing ", dw.wh);
    wind_close(dw.wh);
    wind_delete(dw.wh);
    dw.open = 0;
    dw.wh = -1;
    dw.d = NULL;
}

/* close the window if it shows object i of d (which is going away);
 * later objects move down one */
void draw_close_for(DOC *d, int i)
{
    if (!dw.open || dw.d != d) return;
    if (dw.oi == i || i < 0) dw_close();
    else if (dw.oi > i) dw.oi--;
}

void draw_edit(DOC *d, int i)
{
    short x, y, w, h, ww;
    if (i < 0 || i >= d->a.nobj || d->a.objs[i].type != OB_DRAW) return;
    if (dw.open && (dw.d != d || dw.oi != i)) dw_close();
    dw.d = d;
    dw.oi = i;
    dw.sel = -1;
    if (dw.open) { wind_set(dw.wh, WF_TOP, 0, 0, 0, 0); dw_redraw(); return; }
    ww = (short)(desk.g_w * 3 / 4);
    if (ww < 60 * wchar && desk.g_w > 60 * wchar) ww = (short)(60 * wchar);
    dw.barh = lay_buttons(ww);
    wind_calc(WC_BORDER, DKIND, 0, 0, ww, (short)(desk.g_h * 3 / 4), &x, &y, &w, &h);
    if (w > desk.g_w) w = desk.g_w;
    if (h > desk.g_h) h = desk.g_h;
    x = (short)(desk.g_x + (desk.g_w - w) / 2);
    y = (short)(desk.g_y + (desk.g_h - h) / 2);
    if (winpos_get(" drawing ", &x, &y, &w, &h)) winpos_clamp(&x, &y, w, h);
    dw.wh = wind_create(DKIND, desk.g_x, desk.g_y, desk.g_w, desk.g_h);
    if (dw.wh < 0) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    dw.open = 1;
    dw_set_name();
    wind_open(dw.wh, x, y, w, h);
}

/* a change to the drawing: lay the document out again */
static void dw_changed(void)
{
    if (!dw.d) return;
    dw.d->dirty = 1;
    dw.d->selobj = dw.oi;
    doc_changed(dw.d);
    dw_redraw();
}

/* screen point to drawing points, kept inside the box */
static void to_pt(short mx, short my, short *px, short *py)
{
    OBJ *o = dobj();
    long x = px2pt_x((mx - dw.cx) * dw.den / dw.num), y = px2pt_y((my - dw.cy) * dw.den / dw.num);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (o && x > o->w / TWIPS) x = o->w / TWIPS;
    if (o && y > o->h / TWIPS) y = o->h / TWIPS;
    *px = (short)x; *py = (short)y;
}

static int shape_hit(const OBJ *o, short px, short py)
{
    int k;
    short slop = (short)(px2pt_x(3 * dw.den / dw.num) + 1);
    for (k = o->nsh - 1; k >= 0; k--) {
        const SHAPE *s = &o->sh[k];
        short ax = s->x1 < s->x2 ? s->x1 : s->x2, bx = s->x1 < s->x2 ? s->x2 : s->x1;
        short ay = s->y1 < s->y2 ? s->y1 : s->y2, by = s->y1 < s->y2 ? s->y2 : s->y1;
        if (px >= ax - slop && px <= bx + slop && py >= ay - slop && py <= by + slop) return k;
    }
    return -1;
}

static void xor_shape(int type, long x0, long y0, long x1, long y1)
{
    if (type == T_LINE) {
        short p[4];
        vswr_mode(vhandle, MD_XOR); vsl_color(vhandle, 1); vsl_width(vhandle, 1); vsl_type(vhandle, 1);
        p[0] = (short)x0; p[1] = (short)y0; p[2] = (short)x1; p[3] = (short)y1;
        v_pline(vhandle, 2, p);
        vswr_mode(vhandle, MD_REPLACE);
    } else xor_frame(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0);
}

/* rubber band from (mx, my); returns the end point */
static int rubber(int type, short mx, short my, short *ex, short *ey)
{
    short x, y, b, k, lx = mx, ly = my;
    wind_update(BEG_MCTRL);
    graf_mouse(THIN_CROSS, NULL);
    graf_mouse(M_OFF, NULL);
    xor_shape(type, mx, my, lx, ly);
    graf_mouse(M_ON, NULL);
    for (;;) {
        graf_mkstate(&x, &y, &b, &k);
        if (!(b & 1)) break;
        if (x == lx && y == ly) { evnt_timer(10L); continue; }
        graf_mouse(M_OFF, NULL);
        xor_shape(type, mx, my, lx, ly);
        lx = x; ly = y;
        xor_shape(type, mx, my, lx, ly);
        graf_mouse(M_ON, NULL);
    }
    graf_mouse(M_OFF, NULL);
    xor_shape(type, mx, my, lx, ly);
    graf_mouse(M_ON, NULL);
    graf_mouse(ARROW, NULL);
    wind_update(END_MCTRL);
    *ex = lx; *ey = ly;
    return lx != mx || ly != my;
}

/* a freehand stroke from (mx, my) while the button is held, drawn as it
 * goes inside the canvas; returns the number of screen points in xy */
#define FH_MAX 2000
static int freehand(short mx, short my, const GRECT *cv, short *xy)
{
    short x, y, b, k, lx = mx, ly = my, clip[4], p[4];
    int n = 0;
    wind_update(BEG_MCTRL);
    graf_mouse(THIN_CROSS, NULL);
    clip[0] = cv->g_x; clip[1] = cv->g_y; clip[2] = (short)(cv->g_x + cv->g_w - 1); clip[3] = (short)(cv->g_y + cv->g_h - 1);
    xy[n++] = mx; xy[n++] = my;
    for (;;) {
        graf_mkstate(&x, &y, &b, &k);
        if (!(b & 1)) break;
        if ((x == lx && y == ly) || n >= 2 * FH_MAX) { evnt_timer(5L); continue; }
        graf_mouse(M_OFF, NULL);
        vs_clip(vhandle, 1, clip);
        vswr_mode(vhandle, MD_REPLACE);
        vsl_color(vhandle, (short)color_pen(dw.color)); vsl_width(vhandle, 1); vsl_type(vhandle, 1);
        p[0] = lx; p[1] = ly; p[2] = x; p[3] = y;
        v_pline(vhandle, 2, p);
        vs_clip(vhandle, 0, clip);
        graf_mouse(M_ON, NULL);
        lx = x; ly = y;
        xy[n++] = x; xy[n++] = y;
    }
    graf_mouse(ARROW, NULL);
    wind_update(END_MCTRL);
    return n / 2;
}

/* the stroke as a shape: its points in drawing points, thinned to at most
 * MAX_FREEPTS, into the document's pool; 0 if it can't be */
static int freehand_shape(OBJ *o, const short *sxy, int n)
{
    static short pt[2 * FH_MAX], nrm[2 * MAX_FREEPTS];
    short minx = 32767, miny = 32767, maxx = -32767, maxy = -32767;
    int i, m = 0, step, at;
    SHAPE *s;
    /* drawing points, leaving out those less than a point from the last */
    for (i = 0; i < n; i++) {
        short px, py;
        to_pt(sxy[2 * i], sxy[2 * i + 1], &px, &py);
        if (m && i < n - 1 && abs(px - pt[2 * m - 2]) < 1 && abs(py - pt[2 * m - 1]) < 1) continue;
        pt[2 * m] = px; pt[2 * m + 1] = py; m++;
    }
    if (m < 2) return 0;
    step = (m + MAX_FREEPTS - 2) / (MAX_FREEPTS - 1);   /* at most MAX_FREEPTS, the last kept */
    if (step < 1) step = 1;
    for (i = 0, n = 0; i < m; i += step) { pt[2 * n] = pt[2 * i]; pt[2 * n + 1] = pt[2 * i + 1]; n++; }
    if (pt[2 * n - 2] != pt[2 * m - 2] || pt[2 * n - 1] != pt[2 * m - 1]) { pt[2 * n] = pt[2 * m - 2]; pt[2 * n + 1] = pt[2 * m - 1]; n++; }
    for (i = 0; i < n; i++) {
        if (pt[2 * i] < minx) minx = pt[2 * i];
        if (pt[2 * i] > maxx) maxx = pt[2 * i];
        if (pt[2 * i + 1] < miny) miny = pt[2 * i + 1];
        if (pt[2 * i + 1] > maxy) maxy = pt[2 * i + 1];
    }
    if (maxx <= minx) maxx = (short)(minx + 1);
    if (maxy <= miny) maxy = (short)(miny + 1);
    for (i = 0; i < n; i++) {
        nrm[2 * i] = (short)((long)(pt[2 * i] - minx) * FREE_ONE / (maxx - minx));
        nrm[2 * i + 1] = (short)((long)(pt[2 * i + 1] - miny) * FREE_ONE / (maxy - miny));
    }
    at = free_add(dw.d, nrm, n);
    if (at < 0) return 0;
    s = &o->sh[o->nsh];
    memset(s, 0, sizeof *s);
    s->type = SH_FREE;
    s->fill = FL_NONE;
    s->lw = (unsigned char)(dw.lw ? dw.lw : 1);
    s->color = (unsigned char)dw.color;
    s->x1 = minx; s->y1 = miny; s->x2 = maxx; s->y2 = maxy;
    s->p0 = at; s->np = (short)n;
    dw.sel = o->nsh++;
    return 1;
}

static SHAPE *cur_shape(void)
{
    OBJ *o = dobj();
    return o && dw.sel >= 0 && dw.sel < o->nsh ? &o->sh[dw.sel] : NULL;
}

static void button(int b, GRECT *wa)
{
    static const int fills[NFILLS] = { S_FILL_NONE, S_FILL_WHITE, S_FILL_GREY, S_FILL_SOLID };
    const char *items[NCOLORS + 1];
    char wbuf[6][4];
    OBJ *o = dobj();
    SHAPE *s = cur_shape();
    short px = (short)(wa->g_x + dw.bt[b].g_x), py = (short)(wa->g_y + dw.bt[b].g_y + dw.bt[b].g_h);
    int k, v;
    if (!o) return;
    if (b < NTOOLS) { dw.tool = b; if (b != T_SELECT) dw.sel = -1; dw_redraw(); return; }
    switch (b) {
    case B_FILL:
        for (k = 0; k < NFILLS; k++) items[k] = L(fills[k]);
        v = popup(items, NFILLS, s ? s->fill : dw.fill, px, py);
        if (v < 0) return;
        dw.fill = v;
        if (s) { s->fill = (unsigned char)v; dw_changed(); } else dw_redraw();
        return;
    case B_WIDTH:
        for (k = 0; k < 6; k++) { sprintf(wbuf[k], "%d", k); items[k] = wbuf[k]; }
        v = popup(items, 6, s ? s->lw : dw.lw, px, py);
        if (v < 0) return;
        dw.lw = v;
        if (s) { s->lw = (unsigned char)v; dw_changed(); } else dw_redraw();
        return;
    case B_COLOUR:
        for (k = 0; k < NCOLORS; k++) items[k] = L(S_COL_BLACK + k);
        v = popup(items, NCOLORS, s ? s->color : dw.color, px, py);
        if (v < 0) return;
        dw.color = v;
        if (s) { s->color = (unsigned char)v; dw_changed(); }
        return;
    case B_FRONT:
        if (s && dw.sel < o->nsh - 1) {
            SHAPE t = *s;
            memmove(&o->sh[dw.sel], &o->sh[dw.sel + 1], (o->nsh - dw.sel - 1) * sizeof(SHAPE));
            o->sh[o->nsh - 1] = t;
            dw.sel = o->nsh - 1;
            dw_changed();
        }
        return;
    case B_DELETE:
        if (s) {
            memmove(&o->sh[dw.sel], &o->sh[dw.sel + 1], (o->nsh - dw.sel - 1) * sizeof(SHAPE));
            o->nsh--;
            dw.sel = -1;
            dw_changed();
        }
        return;
    case B_DONE:
        dw_close();
        return;
    }
}

static void dw_click(short mx, short my, short clicks)
{
    GRECT wa, cv;
    OBJ *o = dobj();
    short h1, h2, x2, y2, px, py, qx, qy;
    int b, k;
    (void)clicks;
    if (!o) { dw_close(); return; }
    wind_get(0, WF_TOP, &h1, &h2, &x2, &y2);
    if (h1 != dw.wh) { wind_set(dw.wh, WF_TOP, 0, 0, 0, 0); return; }
    wind_get(dw.wh, WF_WORKXYWH, &wa.g_x, &wa.g_y, &wa.g_w, &wa.g_h);
    canvas_geom(&wa, &cv);
    for (b = 0; b < NBTN; b++)
        if (mx >= wa.g_x + dw.bt[b].g_x && mx < wa.g_x + dw.bt[b].g_x + dw.bt[b].g_w &&
            my >= wa.g_y + dw.bt[b].g_y && my < wa.g_y + dw.bt[b].g_y + dw.bt[b].g_h) {
            short mb, kk;
            do graf_mkstate(&x2, &y2, &mb, &kk); while (mb & 1);
            button(b, &wa);
            return;
        }
    if (my < cv.g_y) return;
    to_pt(mx, my, &px, &py);
    if (dw.tool == T_SELECT) {
        SHAPE *s;
        long sx1, sy1;
        k = -1;
        /* the resize handle of the selected shape */
        if ((s = cur_shape()) != NULL) {
            short bx = s->x1 < s->x2 ? s->x2 : s->x1, by = s->y1 < s->y2 ? s->y2 : s->y1;
            sx1 = dw.cx + pt2px_x(bx) * dw.num / dw.den + 2;
            sy1 = dw.cy + pt2px_y(by) * dw.num / dw.den + 2;
            if (mx >= sx1 - HS - 1 && mx <= sx1 + HS + 1 && my >= sy1 - HS - 1 && my <= sy1 + HS + 1) {
                short ax = s->x1 < s->x2 ? s->x1 : s->x2, ay = s->y1 < s->y2 ? s->y1 : s->y2;
                long ox = dw.cx + pt2px_x(ax) * dw.num / dw.den, oy = dw.cy + pt2px_y(ay) * dw.num / dw.den;
                if (rubber(s->type == SH_LINE ? T_LINE : T_RECT, (short)ox, (short)oy, &x2, &y2)) {
                    to_pt(x2, y2, &qx, &qy);
                    if (s->type == SH_LINE) {
                        /* keep the end that was not dragged */
                        if (s->x2 >= s->x1) { s->x1 = ax; s->x2 = qx; } else { s->x2 = ax; s->x1 = qx; }
                        if (s->y2 >= s->y1) { s->y1 = ay; s->y2 = qy; } else { s->y2 = ay; s->y1 = qy; }
                    } else { s->x1 = ax; s->y1 = ay; s->x2 = qx; s->y2 = qy; }
                    dw_changed();
                }
                return;
            }
        }
        k = shape_hit(o, px, py);
        if (k != dw.sel) { dw.sel = k; dw_redraw(); }
        if (k >= 0) {
            /* drag it: the rubber band shows the move as a line */
            if (rubber(T_LINE, mx, my, &x2, &y2)) {
                short dx, dy;
                SHAPE *t = &o->sh[k];
                to_pt(x2, y2, &qx, &qy);
                dx = (short)(qx - px); dy = (short)(qy - py);
                {
                    short ax = t->x1 < t->x2 ? t->x1 : t->x2, bx = t->x1 < t->x2 ? t->x2 : t->x1;
                    short ay = t->y1 < t->y2 ? t->y1 : t->y2, by = t->y1 < t->y2 ? t->y2 : t->y1;
                    if (ax + dx < 0) dx = (short)-ax;
                    if (bx + dx > o->w / TWIPS) dx = (short)(o->w / TWIPS - bx);
                    if (ay + dy < 0) dy = (short)-ay;
                    if (by + dy > o->h / TWIPS) dy = (short)(o->h / TWIPS - by);
                }
                t->x1 += dx; t->x2 += dx; t->y1 += dy; t->y2 += dy;
                dw_changed();
            }
        }
        return;
    }
    /* draw a new shape */
    if (o->nsh >= MAX_SHAPES) { alert(1, 3, AL_TOO_MANY_SHAPES, AB_OK); return; }
    if (dw.tool == T_FREE) {
        static short sxy[2 * FH_MAX];
        int n = freehand(mx, my, &cv, sxy);
        if (n >= 2 && freehand_shape(o, sxy, n)) dw_changed();
        else dw_redraw();
        return;
    }
    if (!rubber(dw.tool, mx, my, &x2, &y2)) return;
    to_pt(x2, y2, &qx, &qy);
    if (qx == px && qy == py) return;
    {
        SHAPE *s = &o->sh[o->nsh];
        static const unsigned char types[NTOOLS] = { 0, SH_RECT, SH_RRECT, SH_ELLIPSE, SH_LINE, SH_TRI, SH_FREE };
        memset(s, 0, sizeof *s);
        s->type = types[dw.tool];
        s->fill = (unsigned char)(s->type == SH_LINE ? FL_NONE : dw.fill);
        s->lw = (unsigned char)(s->type == SH_LINE && !dw.lw ? 1 : dw.lw);
        if (!s->lw && s->fill == FL_NONE) s->lw = 1;
        s->color = (unsigned char)dw.color;
        s->x1 = px; s->y1 = py; s->x2 = qx; s->y2 = qy;
        dw.sel = o->nsh++;
    }
    dw_changed();
}

static void dw_key(short ks, short key)
{
    short scan = (key >> 8) & 0xff;
    OBJ *o = dobj();
    SHAPE *s = cur_shape();
    int step = (ks & 3) ? 1 : 4;
    short dx = 0, dy = 0;
    if (!o) return;
    switch (scan) {
    case 0x01: dw_close(); return;
    case 0x53: case 0x0E: { GRECT wa; wind_get(dw.wh, WF_WORKXYWH, &wa.g_x, &wa.g_y, &wa.g_w, &wa.g_h); button(B_DELETE, &wa); return; }
    case 0x4B: dx = (short)-step; break;
    case 0x4D: dx = (short)step; break;
    case 0x48: dy = (short)-step; break;
    case 0x50: dy = (short)step; break;
    case 0x0F:                                   /* Tab: next shape */
        if (o->nsh) { dw.sel = (dw.sel + 1) % o->nsh; dw.tool = T_SELECT; dw_redraw(); }
        return;
    default: return;
    }
    if (!s) return;
    s->x1 += dx; s->x2 += dx; s->y1 += dy; s->y2 += dy;
    dw_changed();
}

static void dw_msg(short *m)
{
    GRECT r;
    switch (m[0]) {
    case WM_REDRAW: r.g_x = m[4]; r.g_y = m[5]; r.g_w = m[6]; r.g_h = m[7]; dw_draw(&r); break;
    case WM_TOPPED: wind_set(dw.wh, WF_TOP, 0, 0, 0, 0); break;
    case WM_CLOSED: dw_close(); break;
    case WM_BOTTOMED: wind_set(dw.wh, WF_BOTTOM, 0, 0, 0, 0); break;
    case WM_MOVED: wind_set(dw.wh, WF_CURRXYWH, m[4], m[5], m[6], m[7]); break;
    case WM_SIZED: {
        GRECT wa;
        short w = m[6] < 30 * wchar ? (short)(30 * wchar) : m[6], h = m[7] < 12 * hchar ? (short)(12 * hchar) : m[7];
        wind_set(dw.wh, WF_CURRXYWH, m[4], m[5], w, h);
        wind_get(dw.wh, WF_WORKXYWH, &wa.g_x, &wa.g_y, &wa.g_w, &wa.g_h);
        dw.barh = lay_buttons(wa.g_w);
        dw_redraw();
        break;
    }
    }
}

#ifdef EDDEMO
/* for the demo script: a freehand stroke through screen points, drawn with
 * tool t selected, as if dragged with the mouse */
void draw_demo_stroke(int t, const short *xy, int n)
{
    OBJ *o = dobj();
    GRECT wa, cv;
    if (!o) return;
    wind_get(dw.wh, WF_WORKXYWH, &wa.g_x, &wa.g_y, &wa.g_w, &wa.g_h);
    canvas_geom(&wa, &cv);
    dw.tool = t;
    if (n >= 2 && o->nsh < MAX_SHAPES && freehand_shape(o, xy, n)) dw_changed();
}
#endif

/* for the demo script: select shape k in the drawing window */
void draw_select_shape(int k)
{
    OBJ *o = dobj();
    if (!o || k >= o->nsh) return;
    dw.tool = T_SELECT;
    dw.sel = k;
    dw_redraw();
}

static int  d_owns(short wh) { return dw.open && wh == dw.wh; }
static void d_click(short x, short y, short c) { dw_click(x, y, c); }
static void d_close_tool(void) { dw_close(); }
static void d_docgone(DOC *d) { if (dw.d == d) dw_close(); }
const TOOL draw_tool = { d_owns, dw_msg, d_click, dw_key, d_close_tool, d_docgone };
