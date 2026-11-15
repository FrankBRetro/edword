/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amiobj.c - pictures and drawings in the document view, and the
 *            drawing window (after gemobj.c)
 *
 * Objects float on a page. A click selects one; drag it to move it, drag
 * a corner handle to resize it. Double click a picture for its settings,
 * a drawing to edit its shapes in the drawing window.
 */
#include <graphics/gfxmacros.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiui.h"

#define HS 3                        /* half size of a handle, pixels */

static long sx_of(DOC *d, GRECT *tp, long px) { return tp->g_x - d->scroll_x + view_page_left(d, tp) + zx(d, px); }
static long sy_of(DOC *d, GRECT *tp, int pg, long py) { return tp->g_y - d->scroll_y + view_page_top(d, pg) + zx(d, py); }

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

static void handle_box(long x, long y) { xor_rect(x - HS, y - HS, x + HS, y + HS); }

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
    short x, y;
    int moved = 0, keep = o->type == OB_IMG, k;
    long aw = o->w, ah = o->h;
    struct Window *w = doc_win(d);
    obj_scr(d, tp, o, &x0, &y0, &x1, &y1);
    nx0 = x0; ny0 = y0; nx1 = x1; ny1 = y1;
    gfx_begin(w);
    clip_set(tp);
    xor_frame(nx0, ny0, nx1 - 1, ny1 - 1);
    while (track_next(&x, &y, &k)) {
        if (!moved && (x < mx - 2 || x > mx + 2 || y < my - 2 || y > my + 2)) moved = 1;
        if (!moved) continue;
        xor_frame(nx0, ny0, nx1 - 1, ny1 - 1);
        dx = x - mx; dy = y - my;
        nx0 = x0; ny0 = y0; nx1 = x1; ny1 = y1;
        if (corner < 0) { nx0 += dx; nx1 += dx; ny0 += dy; ny1 += dy; }
        else {
            if (corner == 0 || corner == 2) nx0 += dx; else nx1 += dx;
            if (corner == 0 || corner == 1) ny0 += dy; else ny1 += dy;
            if (nx1 < nx0 + 8) { if (corner == 0 || corner == 2) nx0 = nx1 - 8; else nx1 = nx0 + 8; }
            if (ny1 < ny0 + 8) { if (corner == 0 || corner == 1) ny0 = ny1 - 8; else ny1 = ny0 + 8; }
            if (keep && !(k & KQ_SHIFT) && aw > 0 && ah > 0) {      /* pictures keep their shape, unless Shift */
                long nh = (nx1 - nx0) * ah * pt2px_y(100) / (aw * pt2px_x(100));
                if (corner == 0 || corner == 1) ny0 = ny1 - nh; else ny1 = ny0 + nh;
            }
        }
        xor_frame(nx0, ny0, nx1 - 1, ny1 - 1);
    }
    xor_frame(nx0, ny0, nx1 - 1, ny1 - 1);
    gfx_end();
    if (!moved) return 0;
    {
        /* back to page twips, to the pixel at this zoom, kept on the page */
        long top = sy_of(d, tp, o->page, 0), left = sx_of(d, tp, 0), z = d->zoom;
        long tx0 = px2tw_xs(nx0 - left, z, 100), ty0 = px2tw_ys(ny0 - top, z, 100);
        long tx1 = px2tw_xs(nx1 - left, z, 100), ty1 = px2tw_ys(ny1 - top, z, 100);
        long tpw = (long)PS(d).w * TWIPS, tph = (long)PS(d).h * TWIPS;
        int pg = o->page;
        (void)pw;
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

int obj_click(DOC *d, GRECT *tp, short mx, short my, int qual, int clicks)
{
    long px, py;
    int page, i, h;
    (void)qual;
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
        track_end();
        if (d->a.objs[i].type == OB_DRAW) draw_edit(d, i);
        else obj_props(d);
        return 1;
    }
    if (drag(d, tp, mx, my, -1)) obj_moved(d);
    return 1;
}

int obj_key(DOC *d, int key, int qual)
{
    OBJ *o;
    long step, stepy;
    if (d->selobj < 0 || d->selobj >= d->a.nobj) return 0;
    /* arrows: 6 points; with Shift, one screen pixel */
    step = (qual & KQ_SHIFT) ? px2tw_xs(1, d->zoom, 100) : 6L * TWIPS;
    stepy = (qual & KQ_SHIFT) ? px2tw_ys(1, d->zoom, 100) : 6L * TWIPS;
    o = &d->a.objs[d->selobj];
    switch (key) {
    case K_DEL: case K_BS: obj_delete(d); return 1;
    case K_ESC: obj_select(d, -1); return 1;
    case K_RETURN: case K_ENTER:
        if (o->type == OB_DRAW) draw_edit(d, d->selobj); else obj_props(d);
        return 1;
    case K_LEFT: o->x = o->x > step ? o->x - step : 0; break;
    case K_RIGHT: if (o->x + o->w + step <= (long)PS(d).w * TWIPS) o->x += step; break;
    case K_UP: o->y = o->y > stepy ? o->y - stepy : 0; break;
    case K_DOWN: if (o->y + o->h + stepy <= (long)PS(d).h * TWIPS) o->y += stepy; break;
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

/* ---- other pictures through datatypes.library (Workbench 3 and newer) ----
 * Any format a picture datatype is installed for (GIF, JPEG, BMP...):
 * the picture's colours, read with PDTM_READPIXELARRAY (picture.datatype
 * 43 and newer), or from its own bitmap and colour table before that. */
struct Library *DataTypesBase;
extern struct UtilityBase *UtilityBase;

static ULONG dt_method(Object *o, APTR msg)
{
    struct IClass *cl = OCLASS(o);
    return CallHookPkt(&cl->cl_Dispatcher, o, msg);
}

static int dt_load(const char *path, IMG *im)
{
    Object *o;
    struct BitMapHeader *bmh = NULL;
    unsigned char *rgb;
    long w, h;
    int ok = 0;
    if (!DataTypesBase) DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39);
    if (!DataTypesBase || !UtilityBase) return 0;
    o = NewDTObject((APTR)path, DTA_GroupID, GID_PICTURE, PDTA_Remap, FALSE, PDTA_DestMode, PMODE_V43, TAG_END);
    if (!o) return 0;
    GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_END);
    w = bmh ? bmh->bmh_Width : 0;
    h = bmh ? bmh->bmh_Height : 0;
    rgb = w > 0 && h > 0 && w <= 4000 && h <= 4000 ? (unsigned char *)malloc((size_t)(w * h * 3)) : NULL;
    if (rgb) {
        if (DataTypesBase->lib_Version >= 43) {
            struct pdtBlitPixelArray m;
            m.MethodID = PDTM_READPIXELARRAY;
            m.pbpa_PixelData = rgb; m.pbpa_PixelFormat = PBPAFMT_RGB; m.pbpa_PixelArrayMod = (ULONG)(w * 3);
            m.pbpa_Left = 0; m.pbpa_Top = 0; m.pbpa_Width = (ULONG)w; m.pbpa_Height = (ULONG)h;
            ok = dt_method(o, &m) != 0;
        }
        if (!ok) {
            /* the picture's own bitmap and colours */
            struct gpLayout gl;
            struct BitMap *bm = NULL;
            struct ColorRegister *cr = NULL;
            ULONG nc = 0;
            gl.MethodID = DTM_PROCLAYOUT; gl.gpl_GInfo = NULL; gl.gpl_Initial = TRUE;
            dt_method(o, &gl);
            GetDTAttrs(o, PDTA_BitMap, (ULONG)&bm, PDTA_ColorRegisters, (ULONG)&cr, PDTA_NumColors, (ULONG)&nc, TAG_END);
            if (bm && cr && bm->Depth <= 8) {
                long x, y;
                int k;
                for (y = 0; y < h && y < bm->Rows; y++)
                    for (x = 0; x < w; x++) {
                        unsigned v = 0;
                        for (k = 0; k < bm->Depth; k++)
                            if (bm->Planes[k][y * bm->BytesPerRow + (x >> 3)] & (0x80 >> (x & 7))) v |= 1u << k;
                        if (v >= nc) v = 0;
                        rgb[(y * w + x) * 3] = cr[v].red; rgb[(y * w + x) * 3 + 1] = cr[v].green; rgb[(y * w + x) * 3 + 2] = cr[v].blue;
                    }
                ok = 1;
            }
        }
        if (ok) ok = img_from_rgb(im, w, h, rgb, w * 3, 3);
        free(rgb);
    }
    DisposeDTObject(o);
    return ok;
}

/* insert a picture: IFF ILBM, PNG or GEM IMG, or anything datatypes read */
int picture_insert(DOC *d, const char *path)
{
    IMG im;
    OBJ o;
    int k, i;
    if (d->a.nobj >= MAX_OBJS || d->nimg >= MAX_IMGS) { alert(1, 3, AL_TOO_MANY_OBJS, AB_OK); return 0; }
    busy_begin(L(S_LOADING_PIC));
    k = img_load(path, &im);
    if (!k) k = dt_load(path, &im);
    busy_end();
    if (!k) { alert(1, 3, AL_AMI_CANT_READ_PIC, AB_OK); return 0; }
    k = doc_img_add(d, &im);
    if (k < 0) { img_free(&im); return 0; }
    {
        const char *b = strrchr(path, '/');
        if (!b) b = strrchr(path, ':');
        stc(d->imgs[k].name, b ? b + 1 : path, 14);
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
    if (ask_file(L(S_AMI_FS_PICTURE), "#?", "", path, 0)) picture_insert(d, path);
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
/*  Drawing objects                                                   */
/* ================================================================= */

/* the last few pictures scaled for the screen: in colour (pic_make's,
 * for pic_put) or black and white */
#define NCACHE 6
static struct {
    DOC *d;
    const unsigned char *src;
    long w, h, use;
    int colour;
    IMG sc;
    unsigned char *pic;
} cache[NCACHE];
static long cache_clock;

static void cache_drop(int i)
{
    img_free(&cache[i].sc);
    free(cache[i].pic);
    cache[i].pic = NULL;
    cache[i].d = NULL;
}

void img_cache_free(DOC *d)
{
    int i;
    for (i = 0; i < NCACHE; i++)
        if (cache[i].d && (!d || cache[i].d == d)) cache_drop(i);
}

static int scaled(DOC *d, const IMG *im, long w, long h, int colour)
{
    int i, lru = 0;
    for (i = 0; i < NCACHE; i++)
        if (cache[i].d == d && cache[i].src == im->bits && cache[i].w == w && cache[i].h == h && cache[i].colour == colour) {
            cache[i].use = ++cache_clock;
            return i;
        }
    if (w < 1 || h < 1 || w > 3000 || h > 3000) return -1;
    for (i = 0; i < NCACHE; i++) if (!cache[i].d) { lru = i; break; } else if (cache[i].use < cache[lru].use) lru = i;
    if (cache[lru].d) cache_drop(lru);
    memset(&cache[lru].sc, 0, sizeof cache[lru].sc);
    if (colour) {
        unsigned char *rgb = img_scale_rgb(im, w, h);
        if (!rgb) return -1;
        cache[lru].pic = pic_make(rgb, w, h);
        free(rgb);
        if (!cache[lru].pic) return -1;
    } else if (!img_scale(im, w, h, &cache[lru].sc)) return -1;
    cache[lru].d = d; cache[lru].src = im->bits; cache[lru].w = w; cache[lru].h = h; cache[lru].colour = colour;
    cache[lru].use = ++cache_clock;
    return lru;
}

static void draw_shape(const DOC *d, const SHAPE *s, long ox, long oy, long n, long den)
{
    long xy[2 * 57];
    long x0 = ox + pt2px_x(s->x1) * n / den, y0 = oy + pt2px_y(s->y1) * n / den;
    long x1 = ox + pt2px_x(s->x2) * n / den, y1 = oy + pt2px_y(s->y2) * n / den;
    int k, np, pen = color_pen(s->color);
    int lw = (int)(s->lw * n / den);
    if (lw < 1 && s->lw) lw = 1;
    if (s->type == SH_FREE) {                   /* a polyline through its points */
        const short *fp = free_pts(d, s);
        int i;
        long px = 0, py = 0;
        if (!fp || !s->lw) return;
        for (i = 0; i < s->np; i++) {
            long qx = x0 + (x1 - x0) * fp[2 * i] / FREE_ONE, qy = y0 + (y1 - y0) * fp[2 * i + 1] / FREE_ONE;
            if (i) thick_line(px, py, qx, qy, pen, lw);
            px = qx; py = qy;
        }
        return;
    }
    if (s->type == SH_LINE) {
        if (s->lw) thick_line(x0, y0, x1, y1, pen, lw);
        return;
    }
    np = shape_pts(s->type, x0 * 16, y0 * 16, x1 * 16, y1 * 16, xy);
    for (k = 0; k < 2 * np; k++) xy[k] /= 16;
    if (s->fill != FL_NONE)
        poly_fill(xy, np, s->fill == FL_WHITE ? P_WHITE : pen, s->fill == FL_GREY);
    if (s->lw) {
        xy[2 * np] = xy[0]; xy[2 * np + 1] = xy[1];
        poly_line(xy, np + 1, pen, lw);
    }
}

/* a picture or drawing on a page whose top left corner is at (ox, oy) */
void draw_obj(DOC *d, const OBJ *o, long ox, long oy, long n, long den)
{
    long x = ox + tw2px_xs(o->x, n, den), y = oy + tw2px_ys(o->y, n, den);
    long w = ox + tw2px_xs(o->x + o->w, n, den) - x, h = oy + tw2px_ys(o->y + o->h, n, den) - y;
    int k;
    if (o->type == OB_IMG && o->img >= 0 && o->img < d->nimg) {
        /* in colour where the screen can, unless that has failed for want
         * of memory at this size (trying at every redraw would crawl) */
        static struct { const unsigned char *src; long w, h; } nocol[8];
        static int nnc;
        int colour = pic_colour() && d->imgs[o->img].rgb != NULL, f, k2;
        for (f = 0; f < 8 && colour; f++)
            if (nocol[f].src == d->imgs[o->img].bits && nocol[f].w == w && nocol[f].h == h) colour = 0;
        k2 = scaled(d, &d->imgs[o->img], w, h, colour);
        if (k2 < 0 && colour) {
            nocol[nnc].src = d->imgs[o->img].bits; nocol[nnc].w = w; nocol[nnc].h = h;
            nnc = (nnc + 1) & 7;
            k2 = scaled(d, &d->imgs[o->img], w, h, colour = 0);
        }
        if (k2 >= 0 && colour) pic_put(cache[k2].pic, w, h, x, y);
        else if (k2 >= 0) blit_bits(cache[k2].sc.bits, cache[k2].sc.rb, cache[k2].sc.w, cache[k2].sc.h, x, y, P_BLACK);
        else {
            frame(x, y, x + w - 1, y + h - 1, P_BLACK);
            line(x, y, x + w - 1, y + h - 1, P_BLACK);
        }
    }
    if (o->type == OB_DRAW) {
        for (k = 0; k < o->nsh; k++) draw_shape(d, &o->sh[k], x, y, n, den);
        if (o->nsh == 0) {
            SetDrPt(grp, 0xAAAA);
            frame(x, y, x + w - 1, y + h - 1, P_BLACK);
            SetDrPt(grp, 0xFFFF);
        }
    }
    if (o->border) {
        long t = n / den > 0 ? n / den : 1, i;
        for (i = 0; i < t; i++) frame(x + i, y + i, x + w - 1 - i, y + h - 1 - i, P_BLACK);
    }
}

/* ================================================================= */
/*  The drawing window                                                 */
/* ================================================================= */

enum { T_SELECT, T_RECT, T_RRECT, T_ELLIPSE, T_LINE, T_TRI, T_FREE, NTOOLS };
enum { B_TOOL0, B_FILL = NTOOLS, B_WIDTH, B_COLOUR, B_FRONT, B_DELETE, B_DONE, NBTN };

static struct {
    int    open;
    struct Window *w;
    DOC   *d;
    int    oi, tool, sel;
    int    fill, lw, color;
    GRECT  bt[NBTN];
    short  barh;
    long   num, den, cx, cy;
    char   name[60];
} dw = { 0, NULL, NULL, 0, T_RECT, -1, FL_NONE, 1, 0 };

static void work(GRECT *wa)
{
    wa->g_x = dw.w->BorderLeft; wa->g_y = dw.w->BorderTop;
    wa->g_w = (short)(dw.w->Width - dw.w->BorderLeft - dw.w->BorderRight);
    wa->g_h = (short)(dw.w->Height - dw.w->BorderTop - dw.w->BorderBottom);
}

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

static short lay_buttons(short w)
{
    char buf[60];
    short x = 4, y = 3, bh = (short)(hchar + 6), b;
    for (b = 0; b < NBTN; b++) {
        short bw = (short)(sys_width(btn_text(b, buf)) + 16);
        if (b == B_FILL || b == B_WIDTH) bw = (short)(bw + 3 * wchar);
        if (x + bw > w - 4 && x > 4) { x = 4; y = (short)(y + bh + 3); }
        dw.bt[b].g_x = x; dw.bt[b].g_y = y; dw.bt[b].g_w = bw; dw.bt[b].g_h = bh;
        x = (short)(x + bw + (b == T_FREE || b == B_COLOUR ? wchar : 3));
    }
    return (short)(y + bh + 4);
}

static OBJ *dobj(void)
{
    if (!dw.open || !dw.d || !dw.d->used || dw.oi >= dw.d->a.nobj || dw.d->a.objs[dw.oi].type != OB_DRAW) return NULL;
    return &dw.d->a.objs[dw.oi];
}

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

static void sel_frame(const SHAPE *s)
{
    long x0 = dw.cx + pt2px_x(s->x1 < s->x2 ? s->x1 : s->x2) * dw.num / dw.den - 2;
    long y0 = dw.cy + pt2px_y(s->y1 < s->y2 ? s->y1 : s->y2) * dw.num / dw.den - 2;
    long x1 = dw.cx + pt2px_x(s->x1 < s->x2 ? s->x2 : s->x1) * dw.num / dw.den + 2;
    long y1 = dw.cy + pt2px_y(s->y1 < s->y2 ? s->y2 : s->y1) * dw.num / dw.den + 2;
    xor_frame(x0, y0, x1, y1);
    handle_box(x1, y1);
}

static void dw_redraw(void)
{
    GRECT wa, cv;
    OBJ *o = dobj();
    int b, k;
    char buf[60];
    if (!dw.open) return;
    work(&wa);
    canvas_geom(&wa, &cv);
    gfx_begin(dw.w);
    clip_set(&wa);
    fill_rect(wa.g_x, wa.g_y, wa.g_x + wa.g_w - 1, cv.g_y - 1, P_TFACE);
    fill_g(&cv, P_DESK);
    for (b = 0; b < NBTN; b++) {
        GRECT r = dw.bt[b];
        r.g_x += wa.g_x; r.g_y += wa.g_y;
        button_box(&r, btn_text(b, buf), b < NTOOLS && b == dw.tool, b >= B_FRONT && b <= B_DELETE && dw.sel < 0);
    }
    if (o) {
        long bw = tw2px_x(o->w) * dw.num / dw.den, bh = tw2px_y(o->h) * dw.num / dw.den;
        GRECT c;
        fill_rect(dw.cx, dw.cy, dw.cx + bw - 1, dw.cy + bh - 1, P_WHITE);
        frame(dw.cx - 1, dw.cy - 1, dw.cx + bw, dw.cy + bh, P_BLACK);
        c.g_x = (short)dw.cx; c.g_y = (short)dw.cy; c.g_w = (short)bw; c.g_h = (short)bh;
        if (rc_inter(&wa, &c)) {
            clip_set(&c);
            for (k = 0; k < o->nsh; k++) draw_shape(dw.d, &o->sh[k], dw.cx, dw.cy, dw.num, dw.den);
            clip_set(&wa);
        }
        if (dw.sel >= 0 && dw.sel < o->nsh) sel_frame(&o->sh[dw.sel]);
    }
    clip_off();
    gfx_end();
}

static void dw_close(void)
{
    if (!dw.open) return;
    close_ewin(dw.w);
    dw.w = NULL;
    dw.open = 0;
    dw.d = NULL;
}

void draw_close_for(DOC *d, int i)
{
    if (!dw.open || dw.d != d) return;
    if (dw.oi == i || i < 0) dw_close();
    else if (dw.oi > i) dw.oi--;
}

void draw_edit(DOC *d, int i)
{
    long w, h;
    if (i < 0 || i >= d->a.nobj || d->a.objs[i].type != OB_DRAW) return;
    if (dw.open && (dw.d != d || dw.oi != i)) dw_close();
    dw.d = d;
    dw.oi = i;
    dw.sel = -1;
    if (dw.open) { WindowToFront(dw.w); ActivateWindow(dw.w); dw_redraw(); return; }
    w = desk.g_w * 3 / 4;
    if (w < 60 * wchar && desk.g_w > 60 * wchar) w = 60 * wchar;
    h = desk.g_h * 3 / 4;
    sprintf(dw.name, "%s - %.30s", L(S_DRAW_TITLE), lat(d->base));
    dw.w = open_ewin((struct TagItem *)(ULONG[]){ WA_Left, (ULONG)((scr_w - w) / 2), WA_Top, (ULONG)(desk.g_y + (desk.g_h - h) / 2),
        WA_Width, (ULONG)w, WA_Height, (ULONG)h, WA_Title, (ULONG)dw.name, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE, WA_SmartRefresh, TRUE, WA_Activate, TRUE,
        WA_MinWidth, 30 * wchar, WA_MinHeight, 12 * hchar, WA_MaxWidth, (ULONG)~0, WA_MaxHeight, (ULONG)~0, TAG_END },
        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY |
        IDCMP_RAWKEY | IDCMP_MENUPICK);
    if (!dw.w) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    dw.open = 1;
    {
        GRECT wa;
        work(&wa);
        dw.barh = lay_buttons(wa.g_w);
    }
    dw_redraw();
}

static void dw_changed(void)
{
    if (!dw.d) return;
    dw.d->dirty = 1;
    dw.d->selobj = dw.oi;
    doc_changed(dw.d);
    dw_redraw();
}

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
    if (type == T_LINE) xor_line(x0, y0, x1, y1);
    else xor_frame(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0);
}

/* rubber band from (mx, my) while the button is held; the end point */
static int rubber(int type, short mx, short my, short *ex, short *ey)
{
    short x, y, lx = mx, ly = my;
    int q;
    gfx_begin(dw.w);
    xor_shape(type, mx, my, lx, ly);
    while (track_next(&x, &y, &q)) {
        if (x == lx && y == ly) continue;
        xor_shape(type, mx, my, lx, ly);
        lx = x; ly = y;
        xor_shape(type, mx, my, lx, ly);
    }
    xor_shape(type, mx, my, lx, ly);
    gfx_end();
    *ex = lx; *ey = ly;
    return lx != mx || ly != my;
}

/* a freehand stroke from (mx, my) while the button is held, drawn as it
 * goes inside the canvas; returns the number of screen points in xy */
#define FH_MAX 2000
static int freehand(short mx, short my, const GRECT *cv, short *xy)
{
    short x, y, lx = mx, ly = my;
    int n = 0, q;
    gfx_begin(dw.w);
    clip_set(cv);
    xy[n++] = mx; xy[n++] = my;
    while (track_next(&x, &y, &q)) {
        if ((x == lx && y == ly) || n >= 2 * FH_MAX) continue;
        line(lx, ly, x, y, color_pen(dw.color));
        lx = x; ly = y;
        xy[n++] = x; xy[n++] = y;
    }
    clip_off();
    gfx_end();
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
    for (i = 0; i < n; i++) {
        short px, py;
        to_pt(sxy[2 * i], sxy[2 * i + 1], &px, &py);
        if (m && i < n - 1 && px == pt[2 * m - 2] && py == pt[2 * m - 1]) continue;
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
    short px = (short)(dw.w->LeftEdge + wa->g_x + dw.bt[b].g_x), py = (short)(dw.w->TopEdge + wa->g_y + dw.bt[b].g_y + dw.bt[b].g_h);
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

static void dw_click(short mx, short my)
{
    GRECT wa, cv;
    OBJ *o = dobj();
    short x2, y2, px, py, qx, qy;
    int b, k;
    if (!o) { dw_close(); return; }
    work(&wa);
    canvas_geom(&wa, &cv);
    for (b = 0; b < NBTN; b++)
        if (mx >= wa.g_x + dw.bt[b].g_x && mx < wa.g_x + dw.bt[b].g_x + dw.bt[b].g_w &&
            my >= wa.g_y + dw.bt[b].g_y && my < wa.g_y + dw.bt[b].g_y + dw.bt[b].g_h) {
            short x, y;
            int q;
            while (track_next(&x, &y, &q)) ;
            track_end();
            button(b, &wa);
            return;
        }
    if (my < cv.g_y) return;
    to_pt(mx, my, &px, &py);
    if (dw.tool == T_SELECT) {
        SHAPE *s;
        long sx1, sy1;
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
        if (k >= 0 && rubber(T_LINE, mx, my, &x2, &y2)) {
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
        return;
    }
    if (o->nsh >= MAX_SHAPES) { track_end(); alert(1, 3, AL_TOO_MANY_SHAPES, AB_OK); return; }
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

static void dw_key(int key, int qual)
{
    OBJ *o = dobj();
    SHAPE *s = cur_shape();
    int step = (qual & KQ_SHIFT) ? 1 : 4;
    short dx = 0, dy = 0;
    if (!o) return;
    switch (key) {
    case K_ESC: dw_close(); return;
    case K_DEL: case K_BS: { GRECT wa; work(&wa); button(B_DELETE, &wa); return; }
    case K_LEFT: dx = (short)-step; break;
    case K_RIGHT: dx = (short)step; break;
    case K_UP: dy = (short)-step; break;
    case K_DOWN: dy = (short)step; break;
    case K_TAB:
        if (o->nsh) { dw.sel = (dw.sel + 1) % o->nsh; dw.tool = T_SELECT; dw_redraw(); }
        return;
    default: return;
    }
    if (!s) return;
    s->x1 += dx; s->x2 += dx; s->y1 += dy; s->y2 += dy;
    dw_changed();
}

static void dw_msg(struct IntuiMessage *m)
{
    GRECT wa;
    switch (m->Class) {
    case IDCMP_REFRESHWINDOW: GT_BeginRefresh(dw.w); GT_EndRefresh(dw.w, TRUE); dw_redraw(); break;
    case IDCMP_CLOSEWINDOW: dw_close(); break;
    case IDCMP_NEWSIZE: work(&wa); dw.barh = lay_buttons(wa.g_w); dw_redraw(); break;
    case IDCMP_MOUSEBUTTONS:
        if (m->Code != SELECTDOWN) break;
        track_begin(dw.w);
        dw_click(m->MouseX, m->MouseY);
        track_end();
        break;
    }
}

static int  d_owns(struct Window *w) { return dw.open && w == dw.w; }
static void d_close_tool(void) { dw_close(); }
static void d_docgone(DOC *d) { if (dw.d == d) dw_close(); }
const TOOL draw_tool = { d_owns, dw_msg, dw_key, d_close_tool, d_docgone };
