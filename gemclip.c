/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemclip.c - the GEM clipboard, inserting files, and auto-save
 *
 * Copy writes the selection to the clipboard folder as SCRAP.RTF (with
 * its formatting) and SCRAP.TXT (plain text), so other GEM programs can
 * paste it. Paste reads SCRAP.RTF if it is there, else SCRAP.TXT.
 * The clipboard folder comes from scrp_read(); if none is set up,
 * C:\CLIPBRD (or A:\CLIPBRD) is created and registered.
 */
#include <gem.h>
#include <osbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "gemui.h"

/* ---- the clipboard folder -------------------------------------------- */

static int scrap_dir(char *dir)
{
    char p[256];
    size_t n;
    p[0] = 0;
    scrp_read(p);
    if (!p[0]) {
        sprintf(p, "%c:\\CLIPBRD", (Drvmap() & 4) ? 'C' : 'A');
        (void)Dcreate(p);
        scrp_write(p);
    }
    /* some programs store a file name pattern such as C:\CLIPBRD\SCRAP.* */
    n = strlen(p);
    if (n && p[n - 1] != '\\') {
        char *b = strrchr(p, '\\');
        if (b && strchr(b, '.')) b[1] = 0;
        else strcat(p, "\\");
    }
    strcpy(dir, p);
    /* the folder may be registered but not exist yet */
    n = strlen(p);
    if (n > 3) { p[n - 1] = 0; (void)Dcreate(p); }
    return 1;
}

static void scrap_file(char *path, const char *name)
{
    char dir[256];
    scrap_dir(dir);
    sprintf(path, "%s%s", dir, name);
}

static void scrap_clear(void)
{
    static const char *old[] = { "SCRAP.TXT", "SCRAP.RTF", "SCRAP.ASC", "SCRAP.IMG", NULL };
    char path[300];
    int i;
    for (i = 0; old[i]; i++) { scrap_file(path, old[i]); remove(path); }
}

/* ---- the document clipboard -------------------------------------------- */

static int range_doc(DOC *d, long a, long b, DOC *t)
{
    doc_init(t);
    t->a = d->a;                 /* paragraph and table formats */
    t->a.ps.wrapcol = 0;
    t->a.nobj = 0;
    t->a.novr = 0;
    strcpy(t->base, d->base);
    return doc_insert(t, 0, d->text + a, b - a);
}

/* a selected picture or drawing goes to the clipboard as SCRAP.IMG, as
 * Papyrus and other programs do (a drawing as it looks, in black and
 * white) */
static int copy_object(DOC *d, const OBJ *o)
{
    char path[300];
    IMG im, *src = NULL;
    int ok;
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
    busy_ptr(1);
    scrap_clear();
    scrap_file(path, "SCRAP.IMG");
    ok = img_save(src, path);
    busy_ptr(0);
    img_free(&im);
    return ok;
}

/* copy the selection to the GEM clipboard (and the internal one) */
int clip_copy(DOC *d)
{
    DOC t;
    long a, b;
    char path[300];
    int ok;
    if (d->selobj >= 0 && d->selobj < d->a.nobj) return copy_object(d, &d->a.objs[d->selobj]);
    if (!doc_sel(d, &a, &b)) return 0;
    doc_copy(d);
    if (!range_doc(d, a, b, &t)) return 0;
    busy_ptr(1);
    scrap_clear();
    scrap_file(path, "SCRAP.RTF");
    ok = rtf_export(&t, path);
    scrap_file(path, "SCRAP.TXT");
    ok = text_export(&t, path, 1, CS_ATARI) && ok;
    busy_ptr(0);
    doc_free(&t);
    return ok;
}

/* the clipboard contents as a document (empty if there is nothing) */
static int clip_get(DOC *d, DOC *t)
{
    char path[300];
    FILE *f;
    doc_init(t);
    t->ins = d->ins;
    scrap_file(path, "SCRAP.RTF");
    if ((f = fopen(path, "rb")) != NULL) { fclose(f); return rtf_import(t, path); }
    scrap_file(path, "SCRAP.TXT");
    if ((f = fopen(path, "rb")) != NULL) { fclose(f); return text_import(t, path, CS_AUTO, 0); }
    return 0;
}

static int insert_doc(DOC *d, DOC *t)
{
    long a, b, from = doc_sel(d, &a, &b) ? a : d->cursor;
    int ok = t->len > 0 && doc_insert_doc(d, t, 0, t->len);
    if (ok) after_edit(d, from);
    return ok;
}

int insert_loaded(DOC *d, DOC *t) { return insert_doc(d, t); }

/* SCRAP.IMG, if it is newer than any text on the clipboard */
static int scrap_picture(char *path)
{
    struct stat si, st;
    char p2[300];
    scrap_file(path, "SCRAP.IMG");
    if (stat(path, &si)) return 0;
    scrap_file(p2, "SCRAP.RTF");
    if (!stat(p2, &st) && st.st_mtime > si.st_mtime) return 0;
    scrap_file(p2, "SCRAP.TXT");
    if (!stat(p2, &st) && st.st_mtime > si.st_mtime) return 0;
    return 1;
}

int clip_paste(DOC *d)
{
    DOC t;
    long a, b, from;
    int ok;
    char ip[300];
    if (scrap_picture(ip)) return picture_insert(d, ip);
    busy_ptr(1);
    ok = clip_get(d, &t);
    busy_ptr(0);
    if (ok && t.len) ok = insert_doc(d, &t);
    else {
        /* no clipboard folder contents: use our own clipboard */
        from = doc_sel(d, &a, &b) ? a : d->cursor;
        ok = doc_paste(d);
        if (ok) after_edit(d, from);
    }
    doc_free(&t);
    return ok;
}

/* plain text for the codes pane */
int clip_put_text(const char *s, long n)
{
    char path[300];
    FILE *f;
    long i;
    scrap_clear();
    scrap_file(path, "SCRAP.TXT");
    f = fopen(path, "wb");
    if (!f) return 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '\n') fputs("\r\n", f);
        else fputc(s[i], f);
    }
    fclose(f);
    return 1;
}

/* read SCRAP.TXT as ST text with \n line ends; caller frees */
char *clip_get_text(long *n)
{
    char path[300];
    unsigned char *b;
    long len, i, k = 0;
    scrap_file(path, "SCRAP.TXT");
    b = load_file(path, &len);
    if (!b) return NULL;
    for (i = 0; i < len; i++) {
        if (b[i] == '\r') { if (i + 1 < len && b[i + 1] == '\n') continue; b[k++] = '\n'; }
        else b[k++] = b[i];
    }
    *n = k;
    return (char *)b;
}

/* ---- Insert > File ------------------------------------------------------ */

int insert_file(DOC *d, const char *path)
{
    DOC t;
    int ok;
    doc_init(&t);
    t.ins = d->ins;
    busy_ptr(1);
    ok = rtf_import(&t, path);       /* RTF, or plain text in any line ending */
    busy_ptr(0);
    if (!ok) { alert(1, 3, AL_CANT_READ, AB_OK); doc_free(&t); return 0; }
    ok = insert_doc(d, &t);
    doc_free(&t);
    return ok;
}

/* ---- auto-save ------------------------------------------------------------ */

static long next_save;

/* where a backup goes: the document's name with .ASV, or UNTITLD<n>.ASV */
static void backup_path(DOC *d, char *path)
{
    char *dot, *bs;
    if (d->path[0]) {
        strcpy(path, d->path);
        bs = strrchr(path, '\\');
        dot = strrchr(path, '.');
        if (dot && (!bs || dot > bs)) *dot = 0;
        strcat(path, ".ASV");
    } else
        sprintf(path, "%sUNTITLD%d.ASV", progdir, d->slot + 1);
}

/* call often; saves every pref_asmin minutes while changes are pending */
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
        char path[300];
        if (!d || !d->used || (!d->dirty && !d->cv.dirty)) continue;
        if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
        busy_ptr(1);
        if (pref_asmode == 1 && d->path[0]) {
            if (rtf_export(d, d->path)) set_name(d);
        } else {
            int dirty = d->dirty;
            backup_path(d, path);
            rtf_export(d, path);
            d->dirty = dirty;               /* a backup doesn't count as saved */
        }
        busy_ptr(0);
    }
}

void autosave_reset(void) { next_save = 0; }

/* for the test script: save at the next tick */
void autosave_force(void) { next_save = 1; }
