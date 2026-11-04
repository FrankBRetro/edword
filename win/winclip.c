/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * winclip.c - the Windows clipboard, inserting files, and auto-save
 *             (after amiga/amiclip.c and gemclip.c)
 *
 * Copy puts the selection on the clipboard as Rich Text Format and as
 * plain text (Windows-1252), so other programs can paste it with or
 * without its formatting, and keeps it with all its formatting in
 * EdWord's own clipboard; a private format marks the clipboard as
 * EdWord's. A selected picture or drawing goes on as a bitmap (CF_DIB).
 * Paste uses EdWord's own copy while the clipboard is still ours, else
 * RTF, else text, else a bitmap, which becomes a picture.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "winui.h"

static UINT cf_rtf, cf_ours;

static void formats(void)
{
    if (!cf_rtf) cf_rtf = RegisterClipboardFormatA("Rich Text Format");
    if (!cf_ours) cf_ours = RegisterClipboardFormatA("EdWord document");
}

/* a temporary file's name */
static void temp_name(char *out, const char *name)
{
    DWORD n = GetTempPathA(MAX_PATH - 20, out);
    if (!n || n > MAX_PATH - 20) strcpy(out, progdir);
    strcat(out, name);
}

/* a whole file into memory (with a 0 after it) */
static char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    char *b = NULL;
    long cap = 0, k = 0;
    int c;
    *n = 0;
    if (!f) return NULL;
    for (;;) {
        if (k + 2 > cap) {
            char *nb = (char *)realloc(b, (size_t)(cap ? cap * 2 : 65536));
            if (!nb) { free(b); fclose(f); return NULL; }
            b = nb;
            cap = cap ? cap * 2 : 65536;
        }
        c = fgetc(f);
        if (c == EOF) break;
        b[k++] = (char)c;
    }
    fclose(f);
    if (b) b[k] = 0;
    *n = k;
    return b;
}

static int write_file(const char *path, const void *data, long n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) return 0;
    ok = fwrite(data, 1, (size_t)n, f) == (size_t)n;
    return fclose(f) == 0 && ok;
}

/* n bytes (and a 0) into a moveable global block for the clipboard */
static HGLOBAL global_copy(const void *data, long n)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (SIZE_T)n + 1);
    char *p;
    if (!h) return NULL;
    p = (char *)GlobalLock(h);
    memcpy(p, data, (size_t)n);
    p[n] = 0;
    GlobalUnlock(h);
    return h;
}

/* Windows-1252 text, with CR LF line ends */
static HGLOBAL text_block(const char *t, long n)
{
    long i, k = 0, lf = 0;
    HGLOBAL h;
    char *p;
    for (i = 0; i < n; i++) if (t[i] == '\n') lf++;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (SIZE_T)(n + lf + 1));
    if (!h) return NULL;
    p = (char *)GlobalLock(h);
    for (i = 0; i < n; i++) {
        if (t[i] == '\n') p[k++] = '\r';
        p[k++] = t[i];
    }
    p[k] = 0;
    GlobalUnlock(h);
    return h;
}

/* put text (and RTF, if given) on the clipboard */
static int put_clip(const char *t, long n, const char *rtf, long rn, int ours)
{
    HGLOBAL h;
    formats();
    if (!OpenClipboard(frame)) return 0;
    EmptyClipboard();
    if (rtf && (h = global_copy(rtf, rn)) != NULL) SetClipboardData(cf_rtf, h);
    if (t && (h = text_block(t, n)) != NULL) SetClipboardData(CF_TEXT, h);
    if (ours && (h = global_copy("", 0)) != NULL) SetClipboardData(cf_ours, h);
    CloseClipboard();
    return 1;
}

/* the clipboard's text (Windows-1252, LF line ends), malloc'ed */
static char *get_text(long *len)
{
    HGLOBAL h;
    const char *p;
    char *out = NULL;
    long i, k = 0, n;
    *len = 0;
    if (!IsClipboardFormatAvailable(CF_TEXT) || !OpenClipboard(frame)) return NULL;
    h = GetClipboardData(CF_TEXT);
    if (h && (p = (const char *)GlobalLock(h)) != NULL) {
        SIZE_T sz = GlobalSize(h);
        for (n = 0; (SIZE_T)n < sz && p[n]; n++) ;
        out = (char *)malloc((size_t)n + 1);
        if (out) {
            for (i = 0; i < n; i++) {
                if (p[i] == '\r') { if (i + 1 < n && p[i + 1] == '\n') continue; out[k++] = '\n'; continue; }
                out[k++] = p[i];
            }
            out[k] = 0;
            *len = k;
        }
        GlobalUnlock(h);
    }
    CloseClipboard();
    return out;
}

/* the clipboard's RTF, malloc'ed */
static char *get_rtf(long *len)
{
    HGLOBAL h;
    const char *p;
    char *out = NULL;
    long n;
    *len = 0;
    formats();
    if (!IsClipboardFormatAvailable(cf_rtf) || !OpenClipboard(frame)) return NULL;
    h = GetClipboardData(cf_rtf);
    if (h && (p = (const char *)GlobalLock(h)) != NULL) {
        SIZE_T sz = GlobalSize(h);
        for (n = 0; (SIZE_T)n < sz && p[n]; n++) ;
        out = (char *)malloc((size_t)n + 1);
        if (out) { memcpy(out, p, (size_t)n); out[n] = 0; *len = n; }
        GlobalUnlock(h);
    }
    CloseClipboard();
    return out;
}

/* ---- pictures ------------------------------------------------------------- */

/* a picture as a 24 bit DIB: from its colours if it has them, else its bits */
static HGLOBAL dib_of(const IMG *im)
{
    long w = im->w, h = im->h, drb = (w * 3 + 3) & ~3L, y, x;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(BITMAPINFOHEADER) + (SIZE_T)(drb * h));
    BITMAPINFOHEADER *bi;
    unsigned char *px;
    if (!g) return NULL;
    bi = (BITMAPINFOHEADER *)GlobalLock(g);
    memset(bi, 0, sizeof *bi);
    bi->biSize = sizeof *bi;
    bi->biWidth = w;
    bi->biHeight = h;
    bi->biPlanes = 1;
    bi->biBitCount = 24;
    bi->biCompression = BI_RGB;
    bi->biSizeImage = (DWORD)(drb * h);
    bi->biXPelsPerMeter = bi->biYPelsPerMeter = 3780;     /* 96 dpi */
    px = (unsigned char *)(bi + 1);
    for (y = 0; y < h; y++) {
        unsigned char *o = px + (h - 1 - y) * drb;
        if (im->rgb) {
            const unsigned char *s = im->rgb + y * w * 3;
            for (x = 0; x < w; x++) { o[3 * x] = s[3 * x + 2]; o[3 * x + 1] = s[3 * x + 1]; o[3 * x + 2] = s[3 * x]; }
        } else {
            const unsigned char *s = im->bits + y * im->rb;
            for (x = 0; x < w; x++) {
                unsigned char v = (s[x >> 3] & (0x80 >> (x & 7))) ? 0 : 255;
                o[3 * x] = o[3 * x + 1] = o[3 * x + 2] = v;
            }
        }
        for (x = w * 3; x < drb; x++) o[x] = 0;
    }
    GlobalUnlock(g);
    return g;
}

/* a selected picture, or a drawing as it looks (in black and white) */
static int copy_object(DOC *d, const OBJ *o)
{
    IMG im, *src = NULL;
    HGLOBAL g;
    memset(&im, 0, sizeof im);
    if (o->type == OB_IMG && o->img >= 0 && o->img < d->nimg) src = &d->imgs[o->img];
    else if (o->type == OB_DRAW) {
        OBJ c = *o;
        BAND b;
        long w = pt2px_x(o->w) + 1, h = pt2px_y(o->h) + 1;
        im.w = (short)w; im.h = (short)h; im.rb = (short)((w + 7) / 8);
        im.bits = (unsigned char *)calloc((size_t)im.rb, (size_t)h);
        if (!im.bits) return 0;
        c.x = c.y = 0;
        b.bits = im.bits; b.rb = im.rb; b.w = w; b.y0 = 0; b.h = h;
        rs_obj(&b, d, &c, -1, 1, 1);
        src = &im;
    }
    if (!src) return 0;
    busy(1);
    g = dib_of(src);
    busy(0);
    img_free(&im);
    if (!g) return 0;
    if (!OpenClipboard(frame)) { GlobalFree(g); return 0; }
    EmptyClipboard();
    SetClipboardData(CF_DIB, g);
    CloseClipboard();
    return 1;
}

/* the clipboard's bitmap as a picture: GDI turns any DIB into 32 bits */
static int paste_picture(DOC *d)
{
    HGLOBAL h;
    BITMAPINFO *bi;
    long w, hh, y, x;
    int ok = 0;
    IMG im;
    if (!IsClipboardFormatAvailable(CF_DIB) || !OpenClipboard(frame)) return 0;
    h = GetClipboardData(CF_DIB);
    if (!h || (bi = (BITMAPINFO *)GlobalLock(h)) == NULL) { CloseClipboard(); return 0; }
    w = bi->bmiHeader.biWidth;
    hh = bi->bmiHeader.biHeight < 0 ? -bi->bmiHeader.biHeight : bi->bmiHeader.biHeight;
    if (w > 0 && hh > 0 && w <= 4000 && hh <= 4000) {
        long ncol = bi->bmiHeader.biClrUsed, hdr = bi->bmiHeader.biSize;
        unsigned char *bits, *rgb;
        BITMAPINFOHEADER to;
        void *dbits = NULL;
        HDC dc = CreateCompatibleDC(NULL);
        HBITMAP sec, ob;
        if (!ncol && bi->bmiHeader.biBitCount <= 8) ncol = 1L << bi->bmiHeader.biBitCount;
        if (bi->bmiHeader.biCompression == BI_BITFIELDS && hdr == sizeof(BITMAPINFOHEADER)) ncol = 3;
        bits = (unsigned char *)bi + hdr + ncol * 4;
        memset(&to, 0, sizeof to);
        to.biSize = sizeof to;
        to.biWidth = w;
        to.biHeight = -hh;                      /* top down */
        to.biPlanes = 1;
        to.biBitCount = 32;
        to.biCompression = BI_RGB;
        sec = CreateDIBSection(dc, (BITMAPINFO *)&to, DIB_RGB_COLORS, &dbits, NULL, 0);
        if (sec && dbits) {
            ob = (HBITMAP)SelectObject(dc, sec);
            SetDIBitsToDevice(dc, 0, 0, (DWORD)w, (DWORD)hh, 0, 0, 0, (UINT)hh, bits, bi, DIB_RGB_COLORS);
            GdiFlush();
            rgb = (unsigned char *)malloc((size_t)(w * hh * 3));
            if (rgb) {
                const unsigned char *s = (const unsigned char *)dbits;
                for (y = 0; y < hh; y++)
                    for (x = 0; x < w; x++, s += 4) {
                        unsigned char *o = rgb + (y * w + x) * 3;
                        o[0] = s[2]; o[1] = s[1]; o[2] = s[0];
                    }
                ok = img_from_rgb(&im, w, hh, rgb, w * 3, 3);
                free(rgb);
            }
            SelectObject(dc, ob);
        }
        if (sec) DeleteObject(sec);
        DeleteDC(dc);
    }
    GlobalUnlock(h);
    CloseClipboard();
    if (!ok) return 0;
    return picture_insert_img(d, &im, L(S_W_CLIPPIC));
}

/* ---- the document clipboard -------------------------------------------- */

int clip_copy(DOC *d)
{
    long a, b, i, k = 0, rn = 0;
    char *t, *rtf = NULL, path[MAX_PATH + 20];
    DOC r;
    if (d->selobj >= 0 && d->selobj < d->a.nobj) return copy_object(d, &d->a.objs[d->selobj]);
    if (!doc_sel(d, &a, &b)) return 0;
    doc_copy(d);
    t = (char *)malloc((size_t)(b - a + 1));
    if (!t) return 1;
    for (i = a; i < b; i++) {
        unsigned char c = d->text[i].ch;
        if (c == CH_CELL) t[k++] = '\t';
        else if (ISBRK(c)) t[k++] = '\n';
        else t[k++] = (char)lat_ch(c);
    }
    busy(1);
    /* the RTF, by way of a file */
    doc_init(&r);
    r.a = d->a;                  /* paragraph and table formats */
    r.a.ps.wrapcol = 0;
    r.a.nobj = 0;
    r.a.novr = 0;
    strcpy(r.base, d->base);
    if (doc_insert(&r, 0, d->text + a, b - a)) {
        temp_name(path, "edclip.rtf");
        if (rtf_export(&r, path)) rtf = slurp(path, &rn);
        DeleteFileA(path);
    }
    doc_free(&r);
    put_clip(t, k, rtf, rn, 1);
    busy(0);
    free(rtf);
    free(t);
    return 1;
}

static int insert_doc(DOC *d, DOC *t)
{
    long a, b, from = doc_sel(d, &a, &b) ? a : d->cursor;
    int ok = t->len > 0 && doc_insert_doc(d, t, 0, t->len);
    if (ok) after_edit(d, from);
    return ok;
}

int insert_loaded(DOC *d, DOC *t) { return insert_doc(d, t); }

int clip_paste(DOC *d)
{
    static DOC t;
    long a, b, from, n;
    int ok = 0;
    char *s;
    formats();
    if (!IsClipboardFormatAvailable(cf_ours)) {
        busy(1);
        s = get_rtf(&n);
        if (s && n) {
            char path[MAX_PATH + 20];
            temp_name(path, "edpaste.rtf");
            doc_init(&t);
            t.ins = d->ins;
            ok = write_file(path, s, n) && rtf_import(&t, path);
            DeleteFileA(path);
            free(s);
            busy(0);
            if (ok && t.len) { ok = insert_doc(d, &t); doc_free(&t); return ok; }
            doc_free(&t);
            busy(1);
        } else free(s);
        s = get_text(&n);
        busy(0);
        if (s && n) {
            doc_init(&t);
            t.ins = d->ins;
            ok = text_import_buf(&t, (unsigned char *)s, n, CS_WIN, 0);
            free(s);
            if (ok && t.len) ok = insert_doc(d, &t);
            doc_free(&t);
            return ok;
        }
        free(s);
        if (IsClipboardFormatAvailable(CF_DIB)) return paste_picture(d);
    }
    from = doc_sel(d, &a, &b) ? a : d->cursor;
    ok = doc_paste(d);
    if (ok) after_edit(d, from);
    return ok;
}

/* plain text (ST characters) for the codes pane */
int clip_put_text(const char *s, long n)
{
    char *t = (char *)malloc((size_t)n + 1);
    int ok;
    if (!t) return 0;
    lat_n(t, s, n);
    ok = put_clip(t, n, NULL, 0, 0);
    free(t);
    return ok;
}

char *clip_get_text(long *n)
{
    long i;
    char *s = get_text(n);
    if (!s) return NULL;
    for (i = 0; i < *n; i++) s[i] = (char)st_ch((unsigned char)s[i]);
    return s;
}

/* ---- Insert > File ------------------------------------------------------ */

int insert_file(DOC *d, const char *path)
{
    static DOC t;
    int ok;
    doc_init(&t);
    t.ins = d->ins;
    busy(1);
    ok = rtf_import(&t, path);       /* RTF, or plain text in any line ending */
    busy(0);
    if (!ok) { alert(1, 3, AL_CANT_READ, AB_OK); doc_free(&t); return 0; }
    ok = insert_doc(d, &t);
    doc_free(&t);
    return ok;
}

/* ---- auto-save ------------------------------------------------------------ */

static long next_save;

/* where a backup goes: the document's name with .asv, or Untitled<n>.asv */
static void backup_path(DOC *d, char *path)
{
    char *dot, *sl;
    if (d->path[0]) {
        strcpy(path, d->path);
        sl = strrchr(path, '\\');
        if (!sl) sl = strrchr(path, '/');
        dot = strrchr(path, '.');
        if (dot && (!sl || dot > sl)) *dot = 0;
        strcat(path, ".asv");
    } else
        sprintf(path, "%sUntitled%d.asv", datadir, d->slot + 1);
}

void autosave_tick(void)
{
    long now;
    int i;
    if (!pref_autosave) { next_save = 0; return; }
    now = (long)time(NULL);
    if (!next_save) { next_save = now + pref_asmin * 60L; return; }
    if (now < next_save) return;
    next_save = now + pref_asmin * 60L;
    for (i = 0; i < MAX_DOCS; i++) {
        DOC *d = docs[i];
        char path[MAX_PATH + 20];
        if (!d || !d->used || (!d->dirty && !d->cv.dirty)) continue;
        if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
        busy(1);
        if (pref_asmode == 1 && d->path[0]) {
            if (rtf_export(d, d->path)) set_name(d);
        } else {
            int dirty = d->dirty;
            backup_path(d, path);
            rtf_export(d, path);
            d->dirty = dirty;
        }
        busy(0);
    }
}

void autosave_reset(void) { next_save = 0; }
