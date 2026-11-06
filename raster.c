/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * raster.c - drawing into 1-bit page bands for the PDF export: filled
 *            and outlined shapes, thick lines, pictures
 *
 * Paths are collected as edges in 1/16 pixel units and filled with the
 * non-zero winding rule, a band of rows at a time. Pixels are set where
 * the pixel centre is inside.
 */
#include <stdlib.h>
#include <string.h>
#include "wp.h"

typedef struct { long x0, y0, x1, y1; } EDGE;
static EDGE *ed;
static int   ned, edcap;

void rs_begin(void) { ned = 0; }

void rs_end(void)
{
    free(ed);
    ed = NULL;
    ned = edcap = 0;
}

void rs_line(long x0, long y0, long x1, long y1)
{
    if (y0 == y1) return;
    if (ned >= edcap) {
        int c = edcap ? edcap * 2 : 256;
        EDGE *n = (EDGE *)realloc(ed, c * sizeof(EDGE));
        if (!n) return;
        ed = n; edcap = c;
    }
    ed[ned].x0 = x0; ed[ned].y0 = y0; ed[ned].x1 = x1; ed[ned].y1 = y1;
    ned++;
}

void rs_poly(const long *xy, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        rs_line(xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1]);
    }
}

/* integer square root, for the line normals */
static long isqrt(long v)
{
    long r = 0, b = 1L << 30;
    if (v <= 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* a line wid (1/16 px) wide as a quadrilateral; all of them wind the
 * same way, so overlapping ones don't cancel */
void rs_thick(long x0, long y0, long x1, long y1, long wid)
{
    long dx = x1 - x0, dy = y1 - y0, len = isqrt((dx / 4) * (dx / 4) + (dy / 4) * (dy / 4)) * 4, nx, ny, q[8], area;
    if (len == 0) { dx = 16; dy = 0; len = 16; }
    nx = -dy * wid / 2 / len;
    ny = dx * wid / 2 / len;
    q[0] = x0 + nx; q[1] = y0 + ny;
    q[2] = x1 + nx; q[3] = y1 + ny;
    q[4] = x1 - nx; q[5] = y1 - ny;
    q[6] = x0 - nx; q[7] = y0 - ny;
    area = (q[2] - q[0]) * (q[5] - q[1]) / 16 - (q[4] - q[0]) * (q[3] - q[1]) / 16;
    if (area < 0) {
        long t;
        t = q[2]; q[2] = q[6]; q[6] = t;
        t = q[3]; q[3] = q[7]; q[7] = t;
    }
    rs_poly(q, 4);
}

static long fdiv16(long v) { return v >= 0 ? v / 16 : -((15 - v) / 16); }

static void put_px(BAND *b, long x, long y, int pattern)
{
    unsigned char *p, m;
    if (x < 0 || x >= b->w || y < b->y0 || y >= b->y0 + b->h) return;
    switch (pattern) {
    case FL_GREY:  if ((x + y) & 1) return; break;
    case FP_DIAG1: if ((x + y) % 6 > 1) return; break;
    case FP_HLINE: if (y % 5 > 1) return; break;
    case FP_DIAG2: if (((x - y) % 6 + 6) % 6 > 1) return; break;
    case FP_CROSS: if (x % 6 && y % 6) return; break;
    case FP_DOTS:  if ((x & 3) || (y & 3)) return; break;
    case FP_VLINE: if (x % 5 > 1) return; break;
    }
    p = b->bits + (y - b->y0) * b->rb + (x >> 3);
    m = (unsigned char)(0x80 >> (x & 7));
    if (pattern == FL_WHITE) *p &= (unsigned char)~m; else *p |= m;
}

/* pixels x0 .. x1-1 of band row y in a pattern; solid black and white a
 * byte at a time */
static void put_span(BAND *b, long x0, long x1, long y, int pattern)
{
    unsigned char *row, m0, m1;
    long b0, b1, x;
    if (y < b->y0 || y >= b->y0 + b->h) return;
    if (x0 < 0) x0 = 0;
    if (x1 > b->w) x1 = b->w;
    if (x1 <= x0) return;
    if (pattern != FL_SOLID && pattern != FL_WHITE) {
        for (x = x0; x < x1; x++) put_px(b, x, y, pattern);
        return;
    }
    row = b->bits + (y - b->y0) * b->rb;
    b0 = x0 >> 3; b1 = (x1 - 1) >> 3;
    m0 = (unsigned char)(0xFF >> (x0 & 7));
    m1 = (unsigned char)(0xFF << (7 - ((x1 - 1) & 7)));
    if (pattern == FL_SOLID) {
        if (b0 == b1) { row[b0] |= (unsigned char)(m0 & m1); return; }
        row[b0] |= m0;
        if (b1 > b0 + 1) memset(row + b0 + 1, 0xFF, (size_t)(b1 - b0 - 1));
        row[b1] |= m1;
    } else {
        if (b0 == b1) { row[b0] &= (unsigned char)~(m0 & m1); return; }
        row[b0] &= (unsigned char)~m0;
        if (b1 > b0 + 1) memset(row + b0 + 1, 0, (size_t)(b1 - b0 - 1));
        row[b1] &= (unsigned char)~m1;
    }
}

/* fill the collected edges into the band; pattern FL_SOLID, FL_GREY or
 * FL_WHITE (clears) */
void rs_fill(BAND *b, int pattern)
{
    long xs[256], r;
    int ds[256], i, k;
    long by0 = b->y0 * 16, by1 = (b->y0 + b->h) * 16;
    for (r = 0; r < b->h; r++) {
        long yc = (b->y0 + r) * 16 + 8;
        int nx = 0, wind = 0;
        if (yc < by0 || yc > by1) continue;
        for (i = 0; i < ned && nx < 256; i++) {
            EDGE *e = &ed[i];
            long ya = e->y0 < e->y1 ? e->y0 : e->y1, yb = e->y0 < e->y1 ? e->y1 : e->y0;
            if (yc < ya || yc >= yb) continue;
            xs[nx] = e->x0 + (yc - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0);
            ds[nx] = e->y1 > e->y0 ? 1 : -1;
            for (k = nx; k > 0 && xs[k - 1] > xs[k]; k--) {
                long tx = xs[k]; int td = ds[k];
                xs[k] = xs[k - 1]; ds[k] = ds[k - 1];
                xs[k - 1] = tx; ds[k - 1] = td;
            }
            nx++;
        }
        for (k = 0; k + 1 < nx; k++) {
            wind += ds[k];
            if (wind) {
                long a = fdiv16(xs[k] + 7), e2 = fdiv16(xs[k + 1] + 7);
                put_span(b, a, e2, b->y0 + r, pattern);
            }
        }
    }
}

void rs_rect(BAND *b, long x0, long y0, long x1, long y1, int pattern)
{
    long y;
    if (y0 < b->y0) y0 = b->y0;
    if (y1 > b->y0 + b->h) y1 = b->y0 + b->h;
    for (y = y0; y < y1; y++) put_span(b, x0, x1, y, pattern);
}

/* sine table in 1/1024, a quarter circle in 12 steps */
static const short qsin[13] = { 0, 134, 265, 392, 512, 623, 724, 812, 887, 946, 989, 1015, 1024 };

static void ell_pt(int i, long cx, long cy, long rx, long ry, long *x, long *y)
{
    int q = (i / 12) & 3, k = i % 12;
    long s = qsin[k], c = qsin[12 - k];
    switch (q) {
    case 0: *x = cx + rx * c / 1024; *y = cy - ry * s / 1024; break;
    case 1: *x = cx - rx * s / 1024; *y = cy - ry * c / 1024; break;
    case 2: *x = cx - rx * c / 1024; *y = cy + ry * s / 1024; break;
    default: *x = cx + rx * s / 1024; *y = cy + ry * c / 1024; break;
    }
}

void rs_ellipse_path(long cx, long cy, long rx, long ry)
{
    long xy[96];
    int i;
    for (i = 0; i < 48; i++) ell_pt(i, cx, cy, rx, ry, &xy[2 * i], &xy[2 * i + 1]);
    rs_poly(xy, 48);
}

void rs_rrect_path(long x0, long y0, long x1, long y1, long r)
{
    long xy[2 * 52];
    int n = 0, i, corner;
    long cx[4], cy[4];
    if (r * 2 > x1 - x0) r = (x1 - x0) / 2;
    if (r * 2 > y1 - y0) r = (y1 - y0) / 2;
    cx[0] = x1 - r; cy[0] = y0 + r;     /* top right    */
    cx[1] = x0 + r; cy[1] = y0 + r;     /* top left     */
    cx[2] = x0 + r; cy[2] = y1 - r;     /* bottom left  */
    cx[3] = x1 - r; cy[3] = y1 - r;     /* bottom right */
    for (corner = 0; corner < 4; corner++)
        for (i = 0; i <= 12; i++) {
            ell_pt(corner * 12 + i, cx[corner], cy[corner], r, r, &xy[2 * n], &xy[2 * n + 1]);
            n++;
        }
    rs_poly(xy, n);
}

/* OR a picture (already at its final size) in at pixel x, y */
/* a picture scaled to w x h at x, y: only the band's rows, a run of ink
 * at a time (the same pixels as img_scale() and rs_blit()) */
void rs_img(BAND *b, const IMG *im, long x, long y, long w, long h)
{
    long yy, ya, yb;
    if (w < 1 || h < 1 || !im->bits || im->w < 1 || im->h < 1) return;
    ya = y > b->y0 ? y : b->y0;
    yb = y + h < b->y0 + b->h ? y + h : b->y0 + b->h;
    for (yy = ya; yy < yb; yy++) {
        const unsigned char *src = im->bits + ((yy - y) * im->h / h) * im->rb;
        long sx = 0, a;
        while (sx < im->w) {
            if (!src[sx >> 3] && !(sx & 7)) { sx += 8; continue; }
            if (!(src[sx >> 3] & (0x80 >> (sx & 7)))) { sx++; continue; }
            a = sx;
            while (sx < im->w && (src[sx >> 3] & (0x80 >> (sx & 7)))) sx++;
            /* printer x shows source floor(x * im->w / w): the x for a..sx-1 */
            put_span(b, x + (a * w + im->w - 1) / im->w, x + (sx * w + im->w - 1) / im->w, yy, FL_SOLID);
        }
    }
}

void rs_blit(BAND *b, const IMG *im, long x, long y)
{
    long r, c;
    for (r = 0; r < im->h; r++) {
        long yy = y + r;
        const unsigned char *src;
        if (yy < b->y0 || yy >= b->y0 + b->h) continue;
        src = im->bits + r * im->rb;
        for (c = 0; c < im->w; c++)
            if (src[c >> 3] & (0x80 >> (c & 7))) put_px(b, x + c, yy, FL_SOLID);
    }
}

/* the outline of a shape as a path; coordinates in 1/16 px */
static void shape_path(int type, long x0, long y0, long x1, long y1)
{
    long t;
    if (x1 < x0) { t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { t = y0; y0 = y1; y1 = t; }
    switch (type) {
    case SH_RECT: { long q[8]; q[0] = x0; q[1] = y0; q[2] = x1; q[3] = y0; q[4] = x1; q[5] = y1; q[6] = x0; q[7] = y1; rs_poly(q, 4); break; }
    case SH_RRECT: rs_rrect_path(x0, y0, x1, y1, ((x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0)) / 5); break;
    case SH_ELLIPSE: rs_ellipse_path((x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2); break;
    case SH_TRI: { long q[6]; q[0] = (x0 + x1) / 2; q[1] = y0; q[2] = x1; q[3] = y1; q[4] = x0; q[5] = y1; rs_poly(q, 3); break; }
    }
}

/* the outline points of a shape, for drawing its edge */
int shape_pts(int type, long x0, long y0, long x1, long y1, long *xy)
{
    int n = 0, i, corner;
    long t, r, cx[4], cy[4];
    if (x1 < x0) { t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { t = y0; y0 = y1; y1 = t; }
    switch (type) {
    case SH_RECT:
        xy[0] = x0; xy[1] = y0; xy[2] = x1; xy[3] = y0; xy[4] = x1; xy[5] = y1; xy[6] = x0; xy[7] = y1;
        return 4;
    case SH_TRI:
        xy[0] = (x0 + x1) / 2; xy[1] = y0; xy[2] = x1; xy[3] = y1; xy[4] = x0; xy[5] = y1;
        return 3;
    case SH_ELLIPSE:
        for (i = 0; i < 48; i++) ell_pt(i, (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2, &xy[2 * i], &xy[2 * i + 1]);
        return 48;
    case SH_RRECT:
        r = ((x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0)) / 5;
        cx[0] = x1 - r; cy[0] = y0 + r; cx[1] = x0 + r; cy[1] = y0 + r;
        cx[2] = x0 + r; cy[2] = y1 - r; cx[3] = x1 - r; cy[3] = y1 - r;
        for (corner = 0; corner < 4; corner++)
            for (i = 0; i <= 12; i++) { ell_pt(corner * 12 + i, cx[corner], cy[corner], r, r, &xy[2 * n], &xy[2 * n + 1]); n++; }
        return n;
    }
    return 0;
}

/* Draw a shape of a drawing whose box starts at page pixel (ox, oy);
 * shape points are converted from points and multiplied by num/den. */
void rs_shape(BAND *b, const DOC *d, const SHAPE *s, long ox, long oy, long num, long den, int fill_only)
{
    long x0 = (ox + pt2px_x(s->x1) * num / den) * 16, y0 = (oy + pt2px_y(s->y1) * num / den) * 16;
    long x1 = (ox + pt2px_x(s->x2) * num / den) * 16, y1 = (oy + pt2px_y(s->y2) * num / den) * 16;
    long lw = (long)s->lw * num / den * 16, xy[2 * 56];
    int n, i;
    if (lw < 16 && s->lw) lw = 16;
    if (s->type == SH_FREE) {
        /* a polyline through its points; a few segments at a time, so the
         * edge list stays short and overlaps can't cancel out */
        const short *p = free_pts(d, s);
        long px = 0, py = 0, qx, qy, top, bot;
        if (!p || fill_only || !s->lw) return;
        top = (y0 < y1 ? y0 : y1) - lw; bot = (y0 < y1 ? y1 : y0) + lw;
        if (bot / 16 < b->y0 || top / 16 > b->y0 + b->h) return;
        rs_begin();
        for (i = 0, n = 0; i < s->np; i++) {
            qx = x0 + (x1 - x0) * p[2 * i] / FREE_ONE;
            qy = y0 + (y1 - y0) * p[2 * i + 1] / FREE_ONE;
            if (i) { rs_thick(px, py, qx, qy, lw); n++; }
            if (n == 12) { rs_fill(b, FL_SOLID); rs_begin(); n = 0; }
            px = qx; py = qy;
        }
        if (n) rs_fill(b, FL_SOLID);
        return;
    }
    if (s->type == SH_LINE) {
        if (!fill_only && s->lw) { rs_begin(); rs_thick(x0, y0, x1, y1, lw); rs_fill(b, FL_SOLID); }
        return;
    }
    if (s->fill != FL_NONE) {
        rs_begin();
        shape_path(s->type, x0, y0, x1, y1);
        rs_fill(b, s->fill);
    }
    if (fill_only || !s->lw) return;
    n = shape_pts(s->type, x0, y0, x1, y1, xy);
    rs_begin();
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        rs_thick(xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1], lw);
    }
    rs_fill(b, FL_SOLID);
}

/* a picture or drawing, at num/den of screen resolution; only the parts
 * of colour color (or all, if color < 0) */
int rs_skip_rgb;            /* leave out colour pictures: the PDF has them */

void rs_obj(BAND *b, DOC *d, const OBJ *o, int color, long num, long den)
{
    long ox = tw2px_xs(o->x, num, den), oy = tw2px_ys(o->y, num, den);
    long ow = tw2px_xs(o->x + o->w, num, den) - ox, oh = tw2px_ys(o->y + o->h, num, den) - oy;
    int k;
    if (oy + oh < b->y0 || oy > b->y0 + b->h) return;
    if (o->type == OB_IMG && o->img >= 0 && o->img < d->nimg && color <= 0 && !(rs_skip_rgb && d->imgs[o->img].rgb)) {
        rs_img(b, &d->imgs[o->img], ox, oy, ow > 4000 ? 4000 : ow, oh > 4000 ? 4000 : oh);
    }
    if (o->type == OB_DRAW)
        for (k = 0; k < o->nsh; k++)
            if (color < 0 || o->sh[k].color == color || (o->sh[k].fill == FL_WHITE && color <= 0))
                rs_shape(b, d, &o->sh[k], ox, oy, num, den, color >= 0 && o->sh[k].color != color);
    if (o->border && color <= 0) {
        long t = num / den > 0 ? num / den : 1;
        rs_rect(b, ox, oy, ox + ow, oy + t, FL_SOLID);
        rs_rect(b, ox, oy + oh - t, ox + ow, oy + oh, FL_SOLID);
        rs_rect(b, ox, oy, ox + t, oy + oh, FL_SOLID);
        rs_rect(b, ox + ow - t, oy, ox + ow, oy + oh, FL_SOLID);
    }
}

/* the horizontal extent (points, in the box) of a shape between rows y0
 * and y1 (points, in the box); x1 <= x0 if it has none there */
void shape_extent(const SHAPE *s, long y0, long y1, long *x0, long *x1)
{
    long ax = s->x1 < s->x2 ? s->x1 : s->x2, bx = s->x1 < s->x2 ? s->x2 : s->x1;
    long ay = s->y1 < s->y2 ? s->y1 : s->y2, by = s->y1 < s->y2 ? s->y2 : s->y1;
    long lw = s->lw;
    *x0 = 0; *x1 = 0;
    if (y1 < ay - lw || y0 > by + lw) return;
    switch (s->type) {
    case SH_ELLIPSE: {
        long cy = (ay + by) / 2, ry = (by - ay) / 2, rx = (bx - ax) / 2, dy, h;
        if (ry <= 0) { *x0 = ax; *x1 = bx; return; }
        dy = (y0 <= cy && y1 >= cy) ? 0 : (y1 < cy ? cy - y1 : y0 - cy);
        if (dy > ry) return;
        h = rx * isqrt((ry * ry - dy * dy) * 256) / (ry * 16);
        *x0 = (ax + bx) / 2 - h - lw; *x1 = (ax + bx) / 2 + h + lw;
        return;
    }
    case SH_TRI: {
        long yy = y1 > by ? by : y1, h = by - ay, half;
        if (h <= 0) { *x0 = ax; *x1 = bx; return; }
        if (yy < ay) yy = ay;
        half = (bx - ax) * (yy - ay) / h / 2;
        *x0 = (ax + bx) / 2 - half - lw; *x1 = (ax + bx) / 2 + half + lw;
        return;
    }
    case SH_LINE: {
        /* the part of the segment within the band */
        long x_at0, x_at1, ya = s->y1, yb = s->y2, xa = s->x1, xb = s->x2;
        if (ya == yb) { *x0 = ax - lw; *x1 = bx + lw; return; }
        x_at0 = xa + (xb - xa) * ((y0 < ay ? ay : y0) - ya) / (yb - ya);
        x_at1 = xa + (xb - xa) * ((y1 > by ? by : y1) - ya) / (yb - ya);
        *x0 = (x_at0 < x_at1 ? x_at0 : x_at1) - lw - 2;
        *x1 = (x_at0 < x_at1 ? x_at1 : x_at0) + lw + 2;
        return;
    }
    default:
        *x0 = ax - lw; *x1 = bx + lw;
    }
}
