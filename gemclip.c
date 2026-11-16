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

/* Other programs are told the clipboard changed (SC_CHANGED, through the
 * AES's broadcast: MultiTOS, MagiC and others with AES 4), so a viewer
 * or another editor can show the new contents. */
static void scrap_changed(int picture)
{
#ifndef PCGEM
    short m[8];
    if (aes_global[0] < 0x0400) return;
    memset(m, 0, sizeof m);
    m[0] = 80;                                      /* SC_CHANGED */
    m[1] = aes_global[2];
    m[3] = (short)(picture ? 0x0002 : 0x0001);      /* a picture, or text */
    m[4] = picture ? 0x2E49 : 0x2E52;               /* ".I" / ".R" */
    m[5] = picture ? 0x4D47 : 0x5446;               /* "MG" / "TF" */
    shel_write(7, 0, 0, (char *)m, NULL);           /* SWM_BROADCAST */
#else
    (void)picture;
#endif
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
static int object_save(DOC *d, const OBJ *o, const char *path, int scrap)
{
    IMG im, *src = NULL;
    int ok;
    memset(&im, 0, sizeof im);
    if (o->type == OB_IMG && o->img >= 0 && o->img < d->nimg) src = &d->imgs[o->img];
    else if (o->type == OB_DRAW) {
        OBJ c = *o;
        BAND b;
        long w = tw2px_x(o->w) + 1, h = tw2px_y(o->h) + 1;
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
    if (scrap) scrap_clear();
    ok = img_save(src, path);
    busy_ptr(0);
    img_free(&im);
    if (ok && scrap) scrap_changed(1);
    return ok;
}

static int copy_object(DOC *d, const OBJ *o)
{
    char path[300];
    scrap_file(path, "SCRAP.IMG");
    return object_save(d, o, path, 1);
}

/* copy the selection to the GEM clipboard (and the internal one) */
int clip_copy(DOC *d)
{
    DOC t;
    long a, b;
    char path[300];
    int ok;
    if (d->selobj >= 0 && d->selobj < d->a.nobj) return copy_object(d, &d->a.objs[d->selobj]);
    if (d->colsel && d->anchor >= 0) {           /* a block: its lines' parts */
        if (col_has_table(d)) { alert(1, 1, AL_COLSEL_TABLE, AB_OK); return 0; }
        if (!col_copy(d, &t)) return 0;
    } else {
        if (!doc_sel(d, &a, &b)) return 0;
        doc_copy(d);
        if (!range_doc(d, a, b, &t)) return 0;
    }
    busy_ptr(1);
    scrap_clear();
    scrap_file(path, "SCRAP.RTF");
    ok = rtf_export(&t, path);
    scrap_file(path, "SCRAP.TXT");
    ok = text_export(&t, path, 1, CS_ATARI) && ok;
    busy_ptr(0);
    doc_free(&t);
    if (ok) scrap_changed(0);
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
    long a, b, from;
    int ok;
    if (d->colsel && d->anchor >= 0 && !block_delete(d)) d->colsel = 0;
    from = doc_sel(d, &a, &b) ? a : d->cursor;
    if (d->colmode && t->len > 0) {              /* Column Select: pasted as a block */
        DOC tmp;
        long k;
        /* in d's paragraph and table numbers */
        doc_init(&tmp);
        ok = doc_insert_doc(&tmp, t, 0, t->len) ? 1 : 0;
        for (k = 0; k < tmp.len; k++) tmp.text[k].para = 0;
        doc_delete_sel(d);
        from = d->cursor;
        ok = ok && col_paste(d, &tmp);
        doc_free(&tmp);
    } else ok = t->len > 0 && doc_insert_doc(d, t, 0, t->len);
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

#ifndef EDLITE
/* ---- the clipboard's ten slots ------------------------------------------- */

/* Each slot is a file in the clipboard folder, EDSLOT1.RTF to EDSLOT0.RTF,
 * emptied when EdWord starts and when it quits: they last while it runs.
 * n is 0-9 for slots 1-9, 0 */
static void slot_path(int n, char *path)
{
    char name[16];
    sprintf(name, "EDSLOT%d.RTF", (n + 1) % 10);
    scrap_file(path, name);
}

/* a picture or drawing in a slot: EDSLOTn.IMG instead */
static void slot_img(int n, char *path)
{
    char name[16];
    sprintf(name, "EDSLOT%d.IMG", (n + 1) % 10);
    scrap_file(path, name);
}

static int slot_has_img(int n)
{
    char path[300];
    FILE *f;
    slot_img(n, path);
    if ((f = fopen(path, "rb")) == NULL) return 0;
    fclose(f);
    return 1;
}

static int slot_load(int n, DOC *t)
{
    char path[300];
    FILE *f;
    doc_init(t);
    slot_path(n, path);
    if ((f = fopen(path, "rb")) == NULL) return 0;
    fclose(f);
    return rtf_import(t, path) && t->len > 0;
}

/* the selection (or block) into slot n */
int slot_copy(DOC *d, int n)
{
    DOC t;
    long a, b;
    char path[300];
    int ok;
    if (d->selobj >= 0 && d->selobj < d->a.nobj) {          /* a picture or drawing */
        slot_img(n, path);
        ok = object_save(d, &d->a.objs[d->selobj], path, 0);
        if (ok) { slot_path(n, path); remove(path); }
        else alert(1, 3, AL_CANT_WRITE, AB_OK);
        return ok;
    }
    if (d->colsel && d->anchor >= 0) {
        if (col_has_table(d)) { alert(1, 1, AL_COLSEL_TABLE, AB_OK); return 0; }
        if (!col_copy(d, &t)) return 0;
    } else {
        if (!doc_sel(d, &a, &b)) return 0;
        if (!range_doc(d, a, b, &t)) return 0;
    }
    busy_ptr(1);
    slot_path(n, path);
    ok = rtf_export(&t, path);
    busy_ptr(0);
    doc_free(&t);
    if (!ok) alert(1, 3, AL_CANT_WRITE, AB_OK);
    else { slot_img(n, path); remove(path); }
    return ok;
}

int slot_paste(DOC *d, int n)
{
    DOC t;
    int ok;
    char ip[300];
    if (slot_has_img(n)) { slot_img(n, ip); return picture_insert(d, ip); }
    busy_ptr(1);
    t.ins = d->ins;
    ok = slot_load(n, &t);
    busy_ptr(0);
    if (!ok) { doc_free(&t); alert(1, 1, AL_SLOT_EMPTY, AB_OK, (n + 1) % 10); return 0; }
    ok = insert_doc(d, &t);
    doc_free(&t);
    return ok;
}

/* Tools > Clear Clipboard Slots: all ten emptied */
void slots_clear(void)
{
    char path[300];
    int n;
    for (n = 0; n < 10; n++) {
        slot_path(n, path); remove(path);
        slot_img(n, path); remove(path);
    }
}

/* the start of slot n's text, for the popup ("" if it's empty) */
void slot_peek(int n, char *out, int max)
{
    DOC t;
    long k;
    int i = 0;
    out[0] = 0;
    if (slot_has_img(n)) {                       /* a picture: its name and size */
        char path[300];
        IMG im;
        slot_img(n, path);
        memset(&im, 0, sizeof im);
        if (img_load(path, &im)) { sprintf(out, "[IMG %dx%d]", im.w, im.h); img_free(&im); }
        else strcpy(out, "[IMG]");
        return;
    }
    if (slot_load(n, &t))
        for (k = 0; k < t.len && i < max - 1; k++) {
            unsigned char c = t.text[k].ch;
            if (ISEND(c) || c == '\t') { if (i && out[i - 1] != ' ') out[i++] = ' '; continue; }
            out[i++] = (char)c;
        }
    if (i == max - 1 && max > 4) { out[max - 4] = out[max - 3] = out[max - 2] = '.'; }
    out[i] = 0;
    doc_free(&t);
}

/* Tools > Clipboard Slots: all ten in a document of their own, each under
 * a heading "Slot n"; saving it writes them back */
int slots_fill(DOC *d)
{
    int s, ok = 1, i;
    long hat[10], hlen[10];
    for (s = 0; s < 10 && ok; s++) {
        DOC t;
        char head[40];
        CELL c[44];
        PARA p;
        int n;
        doc_init(&t);                       /* (freed below, picture or not) */
        sprintf(head, L(S_SLOT_N), (s + 1) % 10);
        for (n = 0; head[n] && n < 40; n++) { c[n] = d->ins; c[n].para = 0; c[n].ch = (unsigned char)head[n]; }
        c[n] = d->ins; c[n].para = 0; c[n].ch = '\n';
        hat[s] = d->len; hlen[s] = n + 1;
        ok = doc_insert(d, d->len, c, n + 1);
        if (slot_has_img(s)) {
            /* a picture: room under the heading, blank lines it goes beside */
            char path[300];
            IMG im;
            long hpt, k, nlines;
            CELL nl = c[n];
            slot_img(s, path);
            memset(&im, 0, sizeof im);
            hpt = img_load(path, &im) ? px2pt_y(im.h) : 72;
            if (im.w && px2pt_x(im.w) > PS(d).w - PS(d).ml - PS(d).mr)       /* made to fit the width */
                hpt = hpt * (PS(d).w - PS(d).ml - PS(d).mr) / px2pt_x(im.w);
            img_free(&im);
            nlines = hpt / 14 + 2;
            for (k = 0; k < nlines && ok; k++) ok = doc_insert(d, d->len, &nl, 1);
        } else if (ok && slot_load(s, &t)) { d->cursor = d->len; d->anchor = -1; ok = doc_insert_doc(d, &t, 0, t.len); }
        doc_free(&t);
        if (ok && !ISBRK(d->text[d->len - 1].ch)) {
            CELL e = d->text[d->len - 1];
            e.ch = '\n';
            ok = doc_insert(d, d->len, &e, 1);
        }
        para_default(&p);
        p.toc = 1;
        i = para_find(d, &p);
        {
            long k;
            for (k = hat[s]; k < hat[s] + hlen[s]; k++) d->text[k].para = (unsigned char)i;
        }
    }
    /* the pictures: each under its heading, the text after it moved down */
    for (s = 0; s < 10 && ok; s++) {
        char path[300];
        int o;
        if (!slot_has_img(s)) continue;
        doc_touch_all(d);
        doc_layout(d);
        d->cursor = hat[s];
        slot_img(s, path);
        o = picture_add(d, path);
        if (o >= 0) {
            OBJ *ob = &d->a.objs[o];
            PGFMT f;
            page_fmt(d, ob->page, &f);
            ob->x = (long)f.ml * TWIPS;                      /* at the left, the blank lines beside it */
            ob->y += 10L * TWIPS;                            /* clear of the heading (and the gap round it) */
            sprintf(d->imgs[ob->img].name, "EDSLOT%d.IMG", (s + 1) % 10);
        }
    }
    d->cursor = 0;
    d->anchor = -1;
    d->selobj = -1;
    doc_touch_all(d);
    return ok;
}

/* the slot each heading starts, by where they are (page, then down it) */
static int slot_heads(DOC *d, long *pos)
{
    long k = 0;
    int s, n = 0;
    for (s = 0; s < 10; s++) pos[s] = -1;
    while (k < d->len) {
        long e = k;
        PARA *p = doc_para_at(d, k);
        while (e < d->len && !ISBRK(d->text[e].ch)) e++;
        if (p->toc == 1) {
            char want[40], got[40];
            long i;
            for (i = 0; i < e - k && i < 39; i++) got[i] = (char)d->text[k + i].ch;
            got[i] = 0;
            for (s = 0; s < 10; s++) { sprintf(want, L(S_SLOT_N), (s + 1) % 10); if (!strcmp(want, got)) { pos[s] = k; n++; } }
        }
        k = e + 1;
    }
    return n;
}

/* the slot whose heading is the last one above an object */
static int slot_of_obj(DOC *d, const long *pos, const OBJ *o)
{
    int s, best = -1;
    long bp = -1, by = -1, oy = tw2px_y(o->y);
    for (s = 0; s < 10; s++) {
        LINE *ln;
        if (pos[s] < 0) continue;
        ln = &d->lines[doc_find_line(d, pos[s])];
        if (ln->page > o->page || (ln->page == o->page && ln->py > oy)) continue;
        if (ln->page > bp || (ln->page == bp && ln->py > by)) { bp = ln->page; by = ln->py; best = s; }
    }
    return best;
}

/* the slots' document saved: each heading's text back to its slot */
int slots_save(DOC *d)
{
    long k = 0, start = -1, hp[10];
    int cur = -1, ok = 1, i, pic[10];
    char path[300];
    /* the pictures and drawings first: each to the slot it is under */
    doc_layout(d);
    slot_heads(d, hp);
    for (i = 0; i < 10; i++) pic[i] = 0;
    for (i = 0; i < d->a.nobj; i++) {
        int sl = slot_of_obj(d, hp, &d->a.objs[i]);
        if (sl < 0 || pic[sl]) continue;
        slot_img(sl, path);
        if (object_save(d, &d->a.objs[i], path, 0)) pic[sl] = 1; else ok = 0;
    }
    for (i = 0; i < 10; i++) if (!pic[i]) { slot_img(i, path); remove(path); }   /* taken out */
    while (k <= d->len) {
        long e = k;
        int head = -1;
        if (k < d->len) {
            PARA *p = doc_para_at(d, k);
            while (e < d->len && !ISBRK(d->text[e].ch)) e++;
            if (p->toc == 1) {
                int s;
                char want[40], got[40];
                long i;
                for (i = 0; i < e - k && i < 39; i++) got[i] = (char)d->text[k + i].ch;
                got[i] = 0;
                for (s = 0; s < 10; s++) { sprintf(want, L(S_SLOT_N), (s + 1) % 10); if (!strcmp(want, got)) head = s; }
            }
        }
        if (k >= d->len || head >= 0) {
            if (cur >= 0) {         /* the slot before: start..k, without its last Return */
                long b = k;
                DOC t;
                if (b > start && ISBRK(d->text[b - 1].ch)) b--;
                slot_path(cur, path);
                if (pic[cur]) remove(path);                       /* a picture: that is the slot */
                else if (b > start && range_doc(d, start, b, &t)) {
                    ok = rtf_export(&t, path) && ok;
                    doc_free(&t);
                } else remove(path);
            }
            if (k >= d->len) break;
            cur = head;
            start = e < d->len ? e + 1 : e;
        }
        k = e + 1;
    }
    if (ok) d->dirty = 0;
    return ok;
}

#else
/* EdWord Lite: no slots */
int slot_copy(DOC *d, int n) { (void)d; (void)n; return 0; }
int slot_paste(DOC *d, int n) { (void)d; (void)n; return 0; }
void slots_clear(void) {}
void slot_peek(int n, char *out, int max) { (void)n; (void)max; out[0] = 0; }
int slots_fill(DOC *d) { (void)d; return 0; }
int slots_save(DOC *d) { (void)d; return 0; }
#endif

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
