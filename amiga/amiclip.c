/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amiclip.c - the Amiga clipboard, inserting files, and auto-save
 *             (after gemclip.c)
 *
 * Copy puts the selection on clipboard unit 0 as IFF FTXT (plain text in
 * ISO-8859-1), so other Amiga programs can paste it, and keeps it with
 * its formatting in EdWord's own clipboard. Paste uses EdWord's own copy
 * if the clipboard still holds what EdWord put there, else the text.
 * The IFF is written and read directly, so iffparse.library isn't needed.
 */
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/clipboard.h>
#include <proto/exec.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "amiui.h"

static LONG our_id = -1;           /* the clip we wrote last */

static struct IOClipReq *cb_open(struct MsgPort **port)
{
    struct IOClipReq *io;
    *port = CreateMsgPort();
    if (!*port) return NULL;
    io = (struct IOClipReq *)CreateIORequest(*port, sizeof(struct IOClipReq));
    if (!io) { DeleteMsgPort(*port); return NULL; }
    if (OpenDevice((CONST_STRPTR)"clipboard.device", PRIMARY_CLIP, (struct IORequest *)io, 0)) {
        DeleteIORequest((struct IORequest *)io);
        DeleteMsgPort(*port);
        return NULL;
    }
    return io;
}

static void cb_close(struct IOClipReq *io, struct MsgPort *port)
{
    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
}

static int cb_write(struct IOClipReq *io, const void *data, long n)
{
    io->io_Command = CMD_WRITE;
    io->io_Data = (STRPTR)data;
    io->io_Length = n;
    DoIO((struct IORequest *)io);
    io->io_Offset += n;
    return io->io_Error == 0;
}

/* Latin-1 text onto the clipboard as FTXT */
static int put_ftxt(const char *t, long n)
{
    struct MsgPort *port;
    struct IOClipReq *io = cb_open(&port);
    ULONG hdr[5];
    int ok;
    if (!io) return 0;
    io->io_Offset = 0;
    io->io_Error = 0;
    io->io_ClipID = 0;
    hdr[0] = 0x464F524DUL;                       /* FORM */
    hdr[1] = 12 + n + (n & 1);
    hdr[2] = 0x46545854UL;                       /* FTXT */
    hdr[3] = 0x43485253UL;                       /* CHRS */
    hdr[4] = n;
    ok = cb_write(io, hdr, 20) && cb_write(io, t, n);
    if (ok && (n & 1)) ok = cb_write(io, "", 1);
    io->io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)io);
    our_id = io->io_ClipID;
    cb_close(io, port);
    return ok;
}

/* the text (Latin-1) of the clipboard's FTXT, malloc'ed; *ours: still ours */
static char *get_ftxt(long *len, int *ours)
{
    struct MsgPort *port;
    struct IOClipReq *io = cb_open(&port);
    char *buf = NULL, *out = NULL;
    long cap = 0, n = 0, k = 0;
    *len = 0;
    *ours = 0;
    if (!io) return NULL;
    io->io_Command = CBD_CURRENTREADID;
    DoIO((struct IORequest *)io);
    *ours = our_id >= 0 && io->io_ClipID == our_id;
    io->io_Offset = 0;
    io->io_ClipID = 0;
    io->io_Error = 0;
    for (;;) {
        if (n + 4096 > cap) {
            char *nb = (char *)realloc(buf, cap + 8192);
            if (!nb) break;
            buf = nb;
            cap += 8192;
        }
        io->io_Command = CMD_READ;
        io->io_Data = (STRPTR)(buf + n);
        io->io_Length = 4096;
        DoIO((struct IORequest *)io);
        if (io->io_Error || io->io_Actual == 0) break;
        n += io->io_Actual;
        io->io_Offset += io->io_Actual;
        if (n > 1000000L) break;
    }
    /* read past the end, which tells the device we are done */
    if (buf) {
        char dummy[4];
        io->io_Command = CMD_READ;
        io->io_Data = (STRPTR)dummy;
        io->io_Length = 1;
        while (io->io_Actual) { DoIO((struct IORequest *)io); io->io_Offset += io->io_Actual; if (!io->io_Actual) break; }
    }
    cb_close(io, port);
    if (!buf || n < 12 || memcmp(buf, "FORM", 4) || memcmp(buf + 8, "FTXT", 4)) { free(buf); return NULL; }
    out = (char *)malloc((size_t)n + 1);
    if (!out) { free(buf); return NULL; }
    {
        long p = 12;
        while (p + 8 <= n) {
            long sz = ((long)(unsigned char)buf[p + 4] << 24) | ((long)(unsigned char)buf[p + 5] << 16) |
                      ((long)(unsigned char)buf[p + 6] << 8) | (unsigned char)buf[p + 7];
            if (sz < 0 || p + 8 + sz > n) sz = n - p - 8;
            if (!memcmp(buf + p, "CHRS", 4)) { memcpy(out + k, buf + p + 8, (size_t)sz); k += sz; }
            p += 8 + sz + (sz & 1);
        }
    }
    free(buf);
    out[k] = 0;
    *len = k;
    return out;
}

/* ---- the document clipboard -------------------------------------------- */

int clip_copy(DOC *d)
{
    long a, b, i, k = 0;
    char *t;
    int ok;
    const DOC *src = d;
    DOC *blk = NULL;
    if (d->colsel && d->anchor >= 0) {           /* a block: its lines' parts */
        if (col_has_table(d)) { alert(1, 1, AL_COLSEL_TABLE, AB_OK); return 0; }
        if ((blk = (DOC *)malloc(sizeof(DOC))) == NULL) return 0;
        if (!col_copy(d, blk)) { free(blk); return 0; }
        doc_copy_doc(blk);
        src = blk; a = 0; b = blk->len;
    } else {
        if (!doc_sel(d, &a, &b)) return 0;
        doc_copy(d);
    }
    t = (char *)malloc((size_t)(b - a + 1));
    if (!t) { if (blk) { doc_free(blk); free(blk); } return 1; }
    for (i = a; i < b; i++) {
        unsigned char c = src->text[i].ch;
        if (c == CH_CELL) t[k++] = '\t';
        else if (ISBRK(c)) t[k++] = '\n';
        else t[k++] = (char)lat_ch(c);
    }
    busy(1);
    ok = put_ftxt(t, k);
    busy(0);
    free(t);
    if (blk) { doc_free(blk); free(blk); }
    (void)ok;
    return 1;
}

static int insert_doc(DOC *d, DOC *t)
{
    long a, b, from;
    int ok;
    if (d->colsel && d->anchor >= 0 && !block_delete(d)) d->colsel = 0;
    from = doc_sel(d, &a, &b) ? a : d->cursor;
    if (d->colmode && t->len > 0) {              /* Column Select: pasted as a block */
        DOC *tmp = (DOC *)malloc(sizeof(DOC));
        long k;
        if (!tmp) return 0;
        doc_init(tmp);                           /* in d's paragraph and table numbers */
        ok = doc_insert_doc(tmp, t, 0, t->len) ? 1 : 0;
        for (k = 0; k < tmp->len; k++) tmp->text[k].para = 0;
        doc_delete_sel(d);
        from = d->cursor;
        ok = ok && col_paste(d, tmp);
        doc_free(tmp);
        free(tmp);
    } else ok = t->len > 0 && doc_insert_doc(d, t, 0, t->len);
    if (ok) after_edit(d, from);
    return ok;
}

int insert_loaded(DOC *d, DOC *t) { return insert_doc(d, t); }

int clip_paste(DOC *d)
{
    static DOC t;
    long a, b, from, n;
    int ok = 0, ours;
    char *s;
    busy(1);
    s = get_ftxt(&n, &ours);
    busy(0);
    if (s && !ours && n) {
        doc_init(&t);
        t.ins = d->ins;
        ok = text_import_buf(&t, (unsigned char *)s, n, CS_WIN, 0);
        free(s);
        if (ok && t.len) ok = insert_doc(d, &t);
        doc_free(&t);
        return ok;
    }
    free(s);
    if (d->colmode || (d->colsel && d->anchor >= 0)) return doc_clip() ? insert_doc(d, doc_clip()) : 0;
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
    ok = put_ftxt(t, n);
    free(t);
    return ok;
}

char *clip_get_text(long *n)
{
    int ours;
    long i;
    char *s = get_ftxt(n, &ours);
    if (!s) return NULL;
    for (i = 0; i < *n; i++) s[i] = (char)st_ch((unsigned char)s[i]);
    return s;
}

/* ---- the clipboard's ten slots ------------------------------------------- */

/* Each slot is a file in T: (or RAM: when there is no T:), EdWordSlot1.rtf
 * to EdWordSlot0.rtf (a picture or drawing: .img), emptied when EdWord
 * starts and when it quits: they last while it runs. n is 0-9 for slots
 * 1-9, 0 */
static const char *slot_dir(void)
{
    static const char *dir;
    if (!dir) {
        struct Process *me = (struct Process *)FindTask(NULL);
        APTR ow = me->pr_WindowPtr;
        BPTR l;
        me->pr_WindowPtr = (APTR)-1;            /* no "insert volume T" requester */
        l = Lock((STRPTR)"T:", ACCESS_READ);
        me->pr_WindowPtr = ow;
        if (l) UnLock(l);
        dir = l ? "T:" : "RAM:";
    }
    return dir;
}
static void slot_path(int n, char *path) { sprintf(path, "%sEdWordSlot%d.rtf", slot_dir(), (n + 1) % 10); }
static void slot_img(int n, char *path)  { sprintf(path, "%sEdWordSlot%d.img", slot_dir(), (n + 1) % 10); }

static int slot_has_img(int n)
{
    char path[64];
    FILE *f;
    slot_img(n, path);
    if ((f = fopen(path, "rb")) == NULL) return 0;
    fclose(f);
    return 1;
}

static int slot_load(int n, DOC *t)
{
    char path[64];
    FILE *f;
    doc_init(t);
    slot_path(n, path);
    if ((f = fopen(path, "rb")) == NULL) return 0;
    fclose(f);
    return rtf_import(t, path) && t->len > 0;
}

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

/* a picture, or a drawing as it looks (in black and white), as GEM IMG */
static int object_save(DOC *d, const OBJ *o, const char *path)
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
    busy(1);
    ok = img_save(src, path);
    busy(0);
    img_free(&im);
    return ok;
}

/* the selection (or block, or picture) into slot n */
int slot_copy(DOC *d, int n)
{
    DOC *t;
    long a, b;
    char path[64];
    int ok;
    if (d->selobj >= 0 && d->selobj < d->a.nobj) {          /* a picture or drawing */
        slot_img(n, path);
        ok = object_save(d, &d->a.objs[d->selobj], path);
        if (ok) { slot_path(n, path); remove(path); }
        else alert(1, 3, AL_CANT_WRITE, AB_OK);
        return ok;
    }
    if ((t = (DOC *)malloc(sizeof(DOC))) == NULL) return 0;
    if (d->colsel && d->anchor >= 0) {
        if (col_has_table(d)) { alert(1, 1, AL_COLSEL_TABLE, AB_OK); free(t); return 0; }
        if (!col_copy(d, t)) { free(t); return 0; }
    } else if (!doc_sel(d, &a, &b) || !range_doc(d, a, b, t)) { free(t); return 0; }
    busy(1);
    slot_path(n, path);
    ok = rtf_export(t, path);
    busy(0);
    doc_free(t);
    free(t);
    if (!ok) alert(1, 3, AL_CANT_WRITE, AB_OK);
    else { slot_img(n, path); remove(path); }
    return ok;
}

int slot_paste(DOC *d, int n)
{
    DOC *t;
    int ok;
    char ip[64];
    if (slot_has_img(n)) { slot_img(n, ip); return picture_insert(d, ip); }
    if ((t = (DOC *)malloc(sizeof(DOC))) == NULL) return 0;
    busy(1);
    ok = slot_load(n, t);
    busy(0);
    if (ok) ok = insert_doc(d, t);
    else alert(1, 1, AL_SLOT_EMPTY, AB_OK, (n + 1) % 10);
    doc_free(t);
    free(t);
    return ok;
}

/* Settings > Clear Clipboard Slots, and at start and quit: all ten emptied */
void slots_clear(void)
{
    char path[64];
    int n;
    for (n = 0; n < 10; n++) {
        slot_path(n, path); remove(path);
        slot_img(n, path); remove(path);
    }
}

/* the start of slot n's text, for the popup, in ISO-8859-1 ("" if empty) */
void slot_peek(int n, char *out, int max)
{
    DOC *t;
    long k;
    int i = 0;
    char st[64];
    out[0] = 0;
    if (slot_has_img(n)) {                       /* a picture: its size */
        char path[64];
        IMG im;
        slot_img(n, path);
        memset(&im, 0, sizeof im);
        if (img_load(path, &im)) { sprintf(out, "[IMG %dx%d]", im.w, im.h); img_free(&im); }
        else strcpy(out, "[IMG]");
        return;
    }
    if (max > (int)sizeof st) max = sizeof st;
    if ((t = (DOC *)malloc(sizeof(DOC))) == NULL) return;
    if (slot_load(n, t))
        for (k = 0; k < t->len && i < max - 1; k++) {
            unsigned char c = t->text[k].ch;
            if (ISEND(c) || c == '\t') { if (i && st[i - 1] != ' ') st[i++] = ' '; continue; }
            st[i++] = (char)c;
        }
    if (i == max - 1 && max > 4) { st[max - 4] = st[max - 3] = st[max - 2] = '.'; }
    st[i] = 0;
    doc_free(t);
    free(t);
    strcpy(out, lat(st));
}

/* "Slot n" in the document's character set */
static void slot_head(int s, char *st)
{
    char h[40];
    sprintf(h, L(S_SLOT_N), (s + 1) % 10);
    stc(st, h, 40);
}

/* Settings > Clipboard Slots: all ten in a document of their own, each
 * under a heading "Slot n"; saving it writes them back */
int slots_fill(DOC *d)
{
    int s, ok = 1, i;
    long hat[10], hlen[10];
    DOC *t = (DOC *)malloc(sizeof(DOC));
    if (!t) return 0;
    for (s = 0; s < 10 && ok; s++) {
        char head[40];
        CELL c[44];
        PARA p;
        int n;
        doc_init(t);
        slot_head(s, head);
        for (n = 0; head[n] && n < 40; n++) { c[n] = d->ins; c[n].para = 0; c[n].ch = (unsigned char)head[n]; }
        c[n] = d->ins; c[n].para = 0; c[n].ch = '\n';
        hat[s] = d->len; hlen[s] = n + 1;
        ok = doc_insert(d, d->len, c, n + 1);
        if (slot_has_img(s)) {
            /* a picture: room under the heading, blank lines it goes beside */
            char path[64];
            IMG im;
            long hpt, k, nlines;
            CELL nl = c[n];
            slot_img(s, path);
            memset(&im, 0, sizeof im);
            hpt = img_load(path, &im) ? px2pt_y(im.h) : 72;
            if (im.w && px2pt_x(im.w) > PS(d).w - PS(d).ml - PS(d).mr)
                hpt = hpt * (PS(d).w - PS(d).ml - PS(d).mr) / px2pt_x(im.w);
            img_free(&im);
            nlines = hpt / 14 + 2;
            for (k = 0; k < nlines && ok; k++) ok = doc_insert(d, d->len, &nl, 1);
        } else if (ok && slot_load(s, t)) { d->cursor = d->len; d->anchor = -1; ok = doc_insert_doc(d, t, 0, t->len); }
        doc_free(t);
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
    free(t);
    /* the pictures: each under its heading, the text after it moved down */
    for (s = 0; s < 10 && ok; s++) {
        char path[64];
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
            ob->x = (long)f.ml * TWIPS;
            ob->y += 10L * TWIPS;
            sprintf(d->imgs[ob->img].name, "EDSLOT%d.IMG", (s + 1) % 10);
        }
    }
    d->cursor = 0;
    d->anchor = -1;
    d->selobj = -1;
    doc_touch_all(d);
    return ok;
}

/* which slot's heading the paragraph k..e is, or -1 */
static int head_of(DOC *d, long k, long e)
{
    PARA *p = doc_para_at(d, k);
    char want[40], got[40];
    long i;
    int s;
    if (p->toc != 1) return -1;
    for (i = 0; i < e - k && i < 39; i++) got[i] = (char)d->text[k + i].ch;
    got[i] = 0;
    for (s = 0; s < 10; s++) { slot_head(s, want); if (!strcmp(want, got)) return s; }
    return -1;
}

/* the slot each heading starts, by where they are */
static void slot_heads(DOC *d, long *pos)
{
    long k = 0;
    int s;
    for (s = 0; s < 10; s++) pos[s] = -1;
    while (k < d->len) {
        long e = k;
        while (e < d->len && !ISBRK(d->text[e].ch)) e++;
        if ((s = head_of(d, k, e)) >= 0) pos[s] = k;
        k = e + 1;
    }
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
    char path[64];
    doc_layout(d);
    slot_heads(d, hp);
    for (i = 0; i < 10; i++) pic[i] = 0;
    for (i = 0; i < d->a.nobj; i++) {                /* the pictures first */
        int sl = slot_of_obj(d, hp, &d->a.objs[i]);
        if (sl < 0 || pic[sl]) continue;
        slot_img(sl, path);
        if (object_save(d, &d->a.objs[i], path)) pic[sl] = 1; else ok = 0;
    }
    for (i = 0; i < 10; i++) if (!pic[i]) { slot_img(i, path); remove(path); }
    while (k <= d->len) {
        long e = k;
        int head = -1;
        if (k < d->len) {
            while (e < d->len && !ISBRK(d->text[e].ch)) e++;
            head = head_of(d, k, e);
        }
        if (k >= d->len || head >= 0) {
            if (cur >= 0) {         /* the slot before: start..k, without its last Return */
                long b = k;
                DOC *t;
                if (b > start && ISBRK(d->text[b - 1].ch)) b--;
                slot_path(cur, path);
                if (pic[cur]) remove(path);
                else if (b > start && (t = (DOC *)malloc(sizeof(DOC))) != NULL) {
                    if (range_doc(d, start, b, t)) ok = rtf_export(t, path) && ok;
                    doc_free(t);
                    free(t);
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
        sl = strrchr(path, '/');
        if (!sl) sl = strrchr(path, ':');
        dot = strrchr(path, '.');
        if (dot && (!sl || dot > sl)) *dot = 0;
        strcat(path, ".asv");
    } else
        sprintf(path, "%sUntitled%d.asv", progdir, d->slot + 1);
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
        char path[300];
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
