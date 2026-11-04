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
    ok = put_ftxt(t, k);
    busy(0);
    free(t);
    (void)ok;
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
