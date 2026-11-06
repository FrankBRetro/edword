/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * pdf.c - write a document as PDF 1.4, one bitmap image per page
 *
 * The platform layer draws each page (in bands) with the same GDOS fonts
 * used on screen, so the PDF looks exactly like the screen. Pages that
 * only use black text become 1-bit images; pages with coloured text
 * become 4-bit indexed images. Images are RunLength compressed and
 * streamed band by band, so memory use stays small.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "wp.h"

static void plog(PDFOUT *o, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    if (!o->log) return;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    o->log(o->ctx, buf);
}

static void rle_row(FILE *f, const unsigned char *s, long n, long *cnt)
{
    long i = 0;
    while (i < n) {
        long r = 1;
        while (i + r < n && r < 128 && s[i + r] == s[i]) r++;
        if (r >= 2) {
            fputc((int)(257 - r), f);
            fputc(s[i], f);
            *cnt += 2;
            i += r;
        } else {
            long j = i + 1;
            while (j < n && j - i < 128 && !(j + 1 < n && s[j] == s[j + 1])) j++;
            fputc((int)(j - i - 1), f);
            fwrite(s + i, 1, j - i, f);
            *cnt += 1 + j - i;
            i = j;
        }
    }
}

/* which colours does a page use? bit c set for colour c */
unsigned page_colours(DOC *d, int page)
{
    unsigned m = 0;
    long li, k;
    int i;
    for (li = 0; li < d->nlines; li++) {
        LINE *ln = &d->lines[li];
        if (ln->page != page) continue;
        if (ln->mark && ln->len) {
            CELL *c = &d->text[ln->start];
            if (c->color < NCOLORS) m |= 1u << c->color;
        }
        for (k = ln->start; k < ln->start + ln->len; k++) {
            CELL *c = &d->text[k];
            if ((c->ch > ' ' || c->ch == '\t') && c->color < NCOLORS) m |= 1u << c->color;
        }
    }
    for (li = 0; li < d->nrows; li++)
        if (d->rows[li].page == page) m |= 1;              /* table lines */
    for (i = 0; i < d->a.nobj; i++) {
        OBJ *o = &d->a.objs[i];
        if (o->page != page) continue;
        if (o->type == OB_IMG || o->border) m |= 1;
        for (k = 0; k < o->nsh; k++) if (o->sh[k].color < NCOLORS) m |= 1u << o->sh[k].color;
    }
    if (hf_visible(d, 0, page) || hf_visible(d, 1, page)) m |= 1;   /* black */
    for (i = 0; i < d->a.ntbl; i++)                                   /* charts */
        if (d->a.tbls[i].chart && d->cbox[i].page == page) {
            static CHDATA cd;
            int n = 0, k2;
            if (chart_data(d, i, &cd))
                n = d->a.tbls[i].chart == CH_PIE || d->a.tbls[i].chart == CH_PIE3D ? cd.ncat : cd.nser;
            m |= 1;
            for (k2 = 0; k2 < n; k2++) m |= 1u << chart_color(k2);
        }
    return m;
}

/* colour pictures go in as images of their own, under the page */
static int rgb_obj(DOC *d, const OBJ *ob)
{
    return ob->type == OB_IMG && ob->img >= 0 && ob->img < d->nimg && d->imgs[ob->img].rgb;
}

static void write_rgb_image(FILE *f, int num, const IMG *im, long *cnt)
{
    long y;
    fprintf(f, "%d 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB "
               "/BitsPerComponent 8 /Filter /RunLengthDecode /Length %d 0 R >>\nstream\n", num, im->w, im->h, num + 1);
    *cnt = 0;
    for (y = 0; y < im->h; y++) rle_row(f, im->rgb + y * (long)im->w * 3, (long)im->w * 3, cnt);
    fputc(128, f);
    (*cnt)++;
    fputs("\nendstream\nendobj\n", f);
}

int pdf_export(DOC *d, const char *path, PDFOUT *o)
{
    FILE *f;
    long *off, xref, W, H, rb1, rb4, band;
    int npages, nobj, i, p, err = 0, scale = o->scale < 1 ? 1 : o->scale, nrgb = 0, rgbnext;
    unsigned char *bits = NULL, *nib = NULL;

    doc_layout(d);
    npages = doc_npages(d);
    W = pt2px_x(PS(d).w) * scale;
    H = pt2px_y(PS(d).h) * scale;
    rb1 = (W + 7) / 8;
    rb4 = (W + 1) / 2;
    band = o->max_band < 1 ? 64 : o->max_band;
    if (band > H) band = H;

    plog(o, "pdf: %d pages of %ldx%ld pixels, bands of %ld rows, buffers %ld + %ld bytes",
         npages, W, H, band, rb1 * band, rb4 * band);
    if (o->colour)
        for (i = 0; i < d->a.nobj; i++) if (d->a.objs[i].page < npages && rgb_obj(d, &d->a.objs[i])) nrgb++;
    nobj = 2 + 4 * npages + 2 * nrgb;
    rgbnext = 3 + 4 * npages;
    off = (long *)calloc(nobj + 1, sizeof(long));
    bits = (unsigned char *)malloc(rb1 * band);
    nib = (unsigned char *)malloc(rb4 * band);
    if (!off || !bits || !nib) {
        plog(o, "pdf: not enough memory for the band buffers");
        free(off); free(bits); free(nib); return 0;
    }
    f = fopen(path, "wb");
    if (!f) { plog(o, "pdf: cannot create %s", path); free(off); free(bits); free(nib); return 0; }
    plog(o, "pdf: file created");
    rs_skip_rgb = nrgb > 0;

    fputs("%PDF-1.4\n%\342\343\317\323\n", f);
    off[1] = ftell(f);
    fputs("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", f);
    off[2] = ftell(f);
    fprintf(f, "2 0 obj\n<< /Type /Pages /Count %d /Kids [", npages);
    for (p = 0; p < npages; p++) fprintf(f, " %d 0 R", 3 + 4 * p);
    fputs(" ] >>\nendobj\n", f);

    for (p = 0; p < npages && !err; p++) {
        int ob = 3 + 4 * p;
        unsigned used = o->colour ? page_colours(d, p) : 1;
        int colour = (used & ~1u) != 0, first = rgbnext, k;
        long y0, cnt = 0;
        char cs[2000], xo[600];
        int haspics = 0;

        plog(o, "pdf: page %d, colours used %x, %s image", p + 1, used, colour ? "4-bit" : "1-bit");
        if (o->progress) o->progress(o->ctx, (int)(p * ((H + band - 1) / band)), (int)(npages * ((H + band - 1) / band)));
        /* this page's colour pictures, then the page on top of them */
        sprintf(xo, "/Im0 %d 0 R", ob + 2);
        cs[0] = 0;
        for (i = 0, k = 0; i < d->a.nobj && nrgb; i++) {
            const OBJ *pob = &d->a.objs[i];
            if (pob->page != p || !rgb_obj(d, pob) || strlen(cs) > sizeof cs - 80) continue;
            sprintf(xo + strlen(xo), " /Pc%d %d 0 R", k, first + 2 * k);
            {   /* points with two decimals, from twips */
                long v[4];
                int j;
                v[0] = pob->w; v[1] = pob->h; v[2] = pob->x; v[3] = (long)PS(d).h * TWIPS - pob->y - pob->h;
                strcat(cs, "q");
                for (j = 0; j < 4; j++) {
                    long t = v[j] < 0 ? 0 : v[j];
                    sprintf(cs + strlen(cs), " %ld.%02ld%s", t / TWIPS, (t % TWIPS) * 5, j == 0 ? " 0 0" : "");
                }
                sprintf(cs + strlen(cs), " cm /Pc%d Do Q\n", k);
            }
            k++;
            haspics = 1;
        }
        off[ob] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] "
                   "/Contents %d 0 R /Resources << /XObject << %s >> >> >>\nendobj\n",
                ob, PS(d).w, PS(d).h, ob + 1, xo);
        off[ob + 1] = ftell(f);
        sprintf(cs + strlen(cs), "q %d 0 0 %d 0 0 cm /Im0 Do Q", PS(d).w, PS(d).h);
        fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n",
                ob + 1, (int)strlen(cs) + 1, cs);

        off[ob + 2] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /XObject /Subtype /Image /Width %ld /Height %ld ",
                ob + 2, W, H);
        if (colour) {
            fputs("/ColorSpace [/Indexed /DeviceRGB 8 <FFFFFF", f);
            for (i = 0; i < NCOLORS; i++)
                fprintf(f, " %02X%02X%02X", color_rgb[i][0], color_rgb[i][1], color_rgb[i][2]);
            fputs(">] /BitsPerComponent 4", f);
            if (haspics) fputs(" /Mask [0 0]", f);          /* white shows the pictures */
        } else if (haspics)
            fputs("/ImageMask true /BitsPerComponent 1 /Decode [1 0]", f);
        else
            fputs("/ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0]", f);
        fprintf(f, " /Filter /RunLengthDecode /Length %d 0 R >>\nstream\n", ob + 3);

        for (y0 = 0; y0 < H && !err; y0 += band) {
            long h = H - y0 < band ? H - y0 : band, r, x;
            if (!colour) {
                memset(bits, 0, rb1 * h);
                if (!o->render(o->ctx, d, p, o->colour ? 0 : -1, y0, h, W, bits, rb1)) {
                    plog(o, "pdf: drawing rows %ld-%ld failed", y0, y0 + h - 1);
                    err = 1;
                }
                for (r = 0; r < h; r++) rle_row(f, bits + r * rb1, rb1, &cnt);
            } else {
                memset(nib, 0, rb4 * h);
                for (i = 0; i < NCOLORS && !err; i++) {
                    if (!(used & (1u << i))) continue;
                    memset(bits, 0, rb1 * h);
                    if (!o->render(o->ctx, d, p, i, y0, h, W, bits, rb1)) {
                        plog(o, "pdf: drawing rows %ld-%ld in colour %d failed", y0, y0 + h - 1, i);
                        err = 1; break;
                    }
                    for (r = 0; r < h; r++) {
                        unsigned char *b = bits + r * rb1, *nb = nib + r * rb4;
                        for (x = 0; x < W; x++)
                            if (b[x >> 3] & (0x80 >> (x & 7))) {
                                if (x & 1) nb[x >> 1] = (unsigned char)((nb[x >> 1] & 0xF0) | (i + 1));
                                else nb[x >> 1] = (unsigned char)((nb[x >> 1] & 0x0F) | ((i + 1) << 4));
                            }
                    }
                }
                for (r = 0; r < h; r++) rle_row(f, nib + r * rb4, rb4, &cnt);
            }
            if (o->progress)
                o->progress(o->ctx, (int)(p * ((H + band - 1) / band) + (y0 + h + band - 1) / band),
                            (int)(npages * ((H + band - 1) / band)));
        }
        fputc(128, f);                 /* end of data */
        cnt++;
        fputs("\nendstream\nendobj\n", f);
        off[ob + 3] = ftell(f);
        fprintf(f, "%d 0 obj\n%ld\nendobj\n", ob + 3, cnt);
        for (i = 0; i < d->a.nobj && nrgb; i++) {
            const OBJ *pob = &d->a.objs[i];
            if (pob->page != p || !rgb_obj(d, pob) || rgbnext >= nobj) continue;
            off[rgbnext] = ftell(f);
            write_rgb_image(f, rgbnext, &d->imgs[pob->img], &cnt);
            off[rgbnext + 1] = ftell(f);
            fprintf(f, "%d 0 obj\n%ld\nendobj\n", rgbnext + 1, cnt);
            rgbnext += 2;
        }
        if (ferror(f)) { plog(o, "pdf: write error on page %d", p + 1); err = 1; }
        plog(o, "pdf: page %d written, %ld bytes of image data", p + 1, cnt);
    }

    xref = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", nobj + 1);
    for (i = 1; i <= nobj; i++) fprintf(f, "%010ld 00000 n \n", off[i]);
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", nobj + 1, xref);

    if (ferror(f)) { plog(o, "pdf: write error at the end"); err = 1; }
    fclose(f);
    rs_skip_rgb = 0;
    plog(o, "pdf: file closed, %ld bytes", xref);
    free(bits);
    free(nib);
    free(off);
    return !err;
}
