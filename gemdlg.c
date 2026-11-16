/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemdlg.c - dialogs built in code (no resource file), popup menus, the
 *            file selector, alerts and the progress gauge
 *
 * Dialogs are described row by row and laid out when they are finished:
 * labels line up in a column, a row that would be too wide wraps, buttons
 * go bottom right. So every language fits without hand-placed objects.
 */
#include <gem.h>
#include <mint/cookie.h>
#include <osbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "gemui.h"
#include "edrsc.h"

/* ================================================================= */
/*  Dialogs: object trees from the resource file (gemrsc.c)           */
/* ================================================================= */

/* item types, for the shortcut letters and the look */
enum { T_LABEL, T_TEXT, T_RADIO, T_CHECK, T_EDIT, T_BUTTON, T_BIG, T_TITLE };

int dlg_on(DLG *g, short i) { return i >= 0 && g->it[i].obj >= 0 && (g->o[g->it[i].obj].ob_state & OS_SELECTED); }
short dlg_obj(DLG *g, short i) { return i >= 0 ? g->it[i].obj : -1; }

/* ---- the look: 3D objects (AES 3.4 / MultiTOS / MagiC / XaAES) and the
 * MagiC object extensions (check boxes, radio buttons, underlined
 * shortcut letters), when the AES says it has them ------------------- */

int aes_3d, aes_magic, is_magic, win_dialogs = 1;

void dlg_init(void)
{
    short o1 = 0, o2 = 0, o3 = 0, o4 = 0;
    /* only an AES that has appl_getinfo is asked (GEMLib checks, with
     * appl_find("?AGI") padded to the 8 characters the AES compares), and
     * only an answer that says it worked is believed: TOS 4's AES 3.40
     * hasn't the call, and what it left in intout from other calls turned
     * 3D off and MagiC's object extensions on, which it can't draw */
    if (appl_getinfo(AES_OBJECT, &o1, &o2, &o3, &o4) == 1) {
        aes_3d = o1 != 0;
        aes_magic = (o4 & 0x0004) != 0;                /* OS_WHITEBAK objects */
    } else if (aes_global[0] >= 0x0340) aes_3d = 1;    /* TOS 4 */
    eddiag("dlg_init: AES %04x, ?AGI %d, getinfo(13) %d: %d %d %d %d -> 3D %d, MagiC %d",
           aes_global[0], appl_find("?AGI\0\0\0\0"), appl_getinfo(AES_OBJECT, &o1, &o2, &o3, &o4), o1, o2, o3, o4, aes_3d, aes_magic);
    {
        long v;
        is_magic = Getcookie(0x4D616758L, &v) == C_FOUND;   /* 'MagX': MagiC itself */
    }
    edlog("AES 3D objects %d, MagiC objects %d, MagiC %d", aes_3d, aes_magic, is_magic);
}

static int upc(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c >= 0x80 ? st_upper((unsigned char)c) : c; }

/* give each button and check box a shortcut letter, unique in the dialog */
/* each button's Alternate letter: the first free letter that starts a
 * word, else any free letter in it; a digit only when it starts the label
 * (no 1_5_0 in a row of numbers) */
static int short_ok(const char *s, int k, int pass)
{
    int c = upc((unsigned char)s[k]);
    int start = k == 0 || s[k - 1] == ' ' || s[k - 1] == '/' || s[k - 1] == '-';
    if (c >= '0' && c <= '9') return k == 0;
    if (!((c >= 'A' && c <= 'Z') || c >= 0x80)) return 0;
    return pass || start;
}

static void pick_shortcuts(DLG *g)
{
    char used[64];
    int nu = 0, i, k, pass;
    used[0] = 0;
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        it->ul = -1;
        if (it->type != T_BUTTON && it->type != T_BIG && it->type != T_CHECK && it->type != T_RADIO) continue;
        for (pass = 0; pass < 2 && it->ul < 0; pass++)
            for (k = 0; it->s[k]; k++) {
                int c = upc((unsigned char)it->s[k]);
                if (!short_ok(it->s, k, pass) || memchr(used, c, nu)) continue;
                if (nu < (int)sizeof used) used[nu++] = (char)c;
                it->ul = (short)k;
                it->key = (short)c;
                break;
            }
    }
}

static void dlg_style(DLG *g)
{
    int i;
    pick_shortcuts(g);
    if (!aes_3d && !aes_magic) return;
    for (i = 0; i < g->n; i++) {
        OBJECT *o = &g->o[i];
        if (aes_3d) {
            if (i == 0 || o->ob_type == G_STRING || o->ob_type == G_IBOX) o->ob_flags |= OF_FL3DBAK;
            else if (o->ob_type == G_BUTTON) o->ob_flags |= (o->ob_flags & OF_EXIT) ? OF_FL3DACT : OF_FL3DIND;
            else if (o->ob_type == G_FTEXT) {
                /* as TOS 4's own dialogs: the underscores on the grey, no
                 * white box and no frame (MagiC's sunken frame took the
                 * top of the text off in a row this high) */
                o->ob_flags |= OF_FL3DBAK;
                o->ob_spec.tedinfo->te_color = 0x1100;
                o->ob_spec.tedinfo->te_thickness = 0;
            }
        }
    }
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        OBJECT *o;
        if (it->obj < 0) continue;
        o = &g->o[it->obj];
        if (aes_magic && (it->type == T_CHECK || it->type == T_RADIO)) {
            /* a real check box or round radio button, with its letter */
            o->ob_state |= (short)(OS_WHITEBAK | 0x8000 | ((it->ul >= 0 ? it->ul : 0x7F) << 8));
            o->ob_flags &= ~OF_FL3DMASK;
            /* on the grey of a 3D dialog, not on a white square of its own */
            if (aes_3d) o->ob_flags |= OF_FL3DBAK;
        } else if (aes_magic && it->ul >= 0 && (it->type == T_BUTTON || it->type == T_BIG))
            o->ob_state |= (short)(OS_WHITEBAK | (it->ul << 8));
    }
    if (aes_3d && g->framed) g->o[0].ob_spec.index = 0x00011100L;
}

/* ---- dialogs from the resource file: each object is an item of its
 * own, so dlg_on() and dlg_run()'s answer are object numbers ---------- */

int dlg_tree(DLG *g, int tree)
{
    OBJECT *t = rsc_tree(tree);
    short n, i, title = rsc_title(tree);
    if (!t) return 0;
    for (n = 0; n < MAXDOBJ && !(t[n].ob_flags & OF_LASTOB); n++) ;
    if (n >= MAXDOBJ || n >= MAXDITEM) return 0;
    n++;
    memset(g, 0, sizeof *g);
    g->framed = 1;
    g->edit1 = -1;
    memcpy(g->o, t, sizeof(OBJECT) * (size_t)n);
    g->n = g->nit = n;
    g->tree = (short)(tree + 1);
    for (i = 0; i < n; i++) {
        OBJECT *o = &g->o[i];
        DITEM *it = &g->it[i];
        it->obj = i;
        it->ul = -1;
        it->type = T_TEXT;
        switch (o->ob_type) {
        case G_STRING:
            it->type = i == title ? T_TITLE : T_TEXT;
            it->s = o->ob_spec.free_string;
            break;
        case G_BUTTON:
            it->type = (o->ob_flags & OF_EXIT) ? T_BUTTON : (o->ob_flags & OF_RBUTTON) ? T_RADIO : T_CHECK;
            it->s = o->ob_spec.free_string;
            break;
        case G_FTEXT: case G_FBOXTEXT:
            if (g->nte < MAXTED) {
                TEDINFO *te = &g->te[g->nte++];
                const char *m;
                short k = 0;
                *te = *o->ob_spec.tedinfo;                 /* a copy: the text goes where the caller says */
                o->ob_spec.tedinfo = te;
                /* the AES's rsrc_load made te_txtlen the length of the
                 * empty text in the file, one: the room is the
                 * template's, a character for each underscore */
                for (m = te->te_ptmplt; m && *m; m++) if (*m == '_') k++;
                if (k) te->te_txtlen = (short)(k + 1);
            }
            it->type = T_EDIT;
            it->s = "";
            if (g->edit1 < 0) g->edit1 = i;
            break;
        default:
            it->s = "";
            break;
        }
        it->wc = (short)(o->ob_width / wchar);
    }
    return 1;
}

/* after the texts and states are set: shortcuts and the 3D look */
void dlg_tree_end(DLG *g)
{
    g->w = g->o[0].ob_width;
    g->h = g->o[0].ob_height;
    dlg_style(g);
}

/* a dialog's tree, or an alert that the resource file is missing or old */
int dlg_load(DLG *g, int tree)
{
    if (dlg_tree(g, tree)) return 1;
    alert(1, 3, AL_NO_RSC, AB_OK);
    return 0;
}

/* an object not wanted this time */
void dlg_hide(DLG *g, short obj)
{
    if (obj <= 0 || obj >= g->n) return;
    g->o[obj].ob_flags |= OF_HIDETREE;
    g->o[obj].ob_flags &= ~OF_EDITABLE;
}

void dlg_set(DLG *g, short obj, int on)
{
    if (obj <= 0 || obj >= g->n) return;
    if (on) g->o[obj].ob_state |= OS_SELECTED;
    else g->o[obj].ob_state &= ~OS_SELECTED;
}

/* a text that changes: cut to the room the object has */
void dlg_str(DLG *g, short obj, char *s)
{
    int room;
    if (obj <= 0 || obj >= g->n) return;
    room = g->o[obj].ob_width / wchar - (g->o[obj].ob_type == G_BUTTON ? 1 : 0);
    if (room < 1) room = 1;
    if ((int)strlen(s) > room) s[room] = 0;
    g->o[obj].ob_spec.free_string = s;
    g->it[obj].s = s;
}

/* an edit field's text: buf as long as the field's valid string, and one more */
void dlg_edtext(DLG *g, short obj, char *buf)
{
    if (obj <= 0 || obj >= g->n || (g->o[obj].ob_type != G_FTEXT && g->o[obj].ob_type != G_FBOXTEXT)) return;
    g->o[obj].ob_spec.tedinfo->te_ptext = buf;
    g->it[obj].buf = buf;
}

/* ---- where windows were: dialogs and tool windows open again where
 * they were last closed, for the rest of the session ------------------- */

typedef struct { char key[28]; short x, y, w, h; } WPOS;
static WPOS wpos[48];
static int  nwpos;

int winpos_get(const char *key, short *x, short *y, short *w, short *h)
{
    int i;
    for (i = 0; i < nwpos; i++)
        if (!strncmp(wpos[i].key, key, sizeof wpos[i].key - 1)) {
            *x = wpos[i].x; *y = wpos[i].y;
            if (w) *w = wpos[i].w;
            if (h) *h = wpos[i].h;
            return 1;
        }
    return 0;
}

void winpos_set(const char *key, short x, short y, short w, short h)
{
    int i;
    for (i = 0; i < nwpos; i++) if (!strncmp(wpos[i].key, key, sizeof wpos[i].key - 1)) break;
    if (i == nwpos) {
        if (nwpos < (int)(sizeof wpos / sizeof wpos[0])) nwpos++;
        else i = nwpos - 1;
        strncpy(wpos[i].key, key, sizeof wpos[i].key - 1);
        wpos[i].key[sizeof wpos[i].key - 1] = 0;
    }
    wpos[i].x = x; wpos[i].y = y; wpos[i].w = w; wpos[i].h = h;
}

/* remember an open window's outer rectangle */
void winpos_save(const char *key, short wh)
{
    short x, y, w, h;
    if (wh < 0) return;
    wind_get(wh, WF_CURRXYWH, &x, &y, &w, &h);
    if (w > 0 && h > 0) winpos_set(key, x, y, w, h);
}

/* keep a window of w x h on the desk */
void winpos_clamp(short *x, short *y, short w, short h)
{
    if (*x + w > desk.g_x + desk.g_w) *x = (short)(desk.g_x + desk.g_w - w);
    if (*y + h > desk.g_y + desk.g_h) *y = (short)(desk.g_y + desk.g_h - h);
    if (*x < desk.g_x) *x = desk.g_x;
    if (*y < desk.g_y) *y = desk.g_y;
}

/* ---- typing into an edit field ------------------------------------------
 * MagiC's objc_edit moves the text cursor but in EdWord's fields wouldn't
 * take characters, Backspace or Delete. There EdWord edits the field's
 * text itself (by its validation string) and objc_edit only places the
 * cursor. 1: key used, *pos the cursor's new place in the text. */
static int valid_ch(char v, int *c)
{
    int ch = *c;
    switch (v) {
    case '9': return ch >= '0' && ch <= '9';
    case 'A': if (ch >= 'a' && ch <= 'z') *c = ch - 32; return (*c >= 'A' && *c <= 'Z') || ch == ' ' || ch >= 0x80;
    case 'a': return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == ' ' || ch >= 0x80;
    case 'N': if (ch >= 'a' && ch <= 'z') *c = ch - 32;
              return (*c >= 'A' && *c <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch >= 0x80;
    case 'n': return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch >= 0x80;
    case 'F': case 'm': return ch != ':' && ch != '\\';
    default:  return 1;                          /* X x P p and any other: anything */
    }
}

int field_edit(OBJECT *tree, short obj, short key, short *pos)
{
    TEDINFO *t;
    char *b;
    int len, max, p, ch = key & 0xff, scan = (key >> 8) & 0xff, vlen;
    if (obj <= 0 || !(tree[obj].ob_flags & OF_EDITABLE)) return 0;
    t = tree[obj].ob_spec.tedinfo;
    if (!t || !t->te_ptext) return 0;
    b = t->te_ptext;
    len = (int)strlen(b);
    max = t->te_txtlen - 1;
    p = *pos < 0 ? 0 : *pos > len ? len : *pos;
    if (scan == 0x0E) {                          /* Backspace */
        if (p == 0) return 1;
        memmove(b + p - 1, b + p, (size_t)(len - p + 1));
        *pos = (short)(p - 1);
        return 1;
    }
    if (scan == 0x53) {                          /* Delete */
        if (p < len) memmove(b + p, b + p + 1, (size_t)(len - p));
        *pos = (short)p;
        return 1;
    }
    if (scan == 0x01) { b[0] = 0; *pos = 0; return 1; }   /* Esc: empty */
    if (ch < 32 || ch == 127) return 0;          /* the cursor keys: the AES's */
    vlen = t->te_pvalid ? (int)strlen(t->te_pvalid) : 0;
    if (vlen && !valid_ch(t->te_pvalid[p < vlen ? p : vlen - 1], &ch)) return 1;
    if (len >= max) return 1;                    /* full */
    memmove(b + p + 1, b + p, (size_t)(len - p + 1));
    b[p] = (char)ch;
    *pos = (short)(p + 1);
    return 1;
}

/* the cursor at pos in a field just drawn again: ED_INIT puts it at the
 * end, the left arrow brings it back */
void field_cursor(OBJECT *tree, short obj, short *idx, short pos)
{
    int n;
    objc_edit(tree, obj, 0, idx, ED_INIT);
    for (n = *idx - pos; n > 0; n--) objc_edit(tree, obj, 0x4B00, idx, ED_CHAR);
}

/* ---- running a dialog ------------------------------------------------- */

/* Dialogs open in windows of their own, so the documents and the other
 * windows stay usable while a dialog is open; menus and documents work
 * as usual, and other dialogs can be open at the same time (each
 * dialog's own, though: asking for one that is open brings it to the
 * front). A dialog for a document opens over its window, its name in
 * the title. Each dlg_run() waits for its own dialog's answer; while it
 * waits, every other window - the other dialogs too - works as usual.
 * A dialog answered while a later one is still open closes at once; its
 * caller goes on when the later one has been answered. Without a free
 * window the dialog runs the old way, with form_do. */
typedef struct {
    DLG  *g;
    short wh, edit, idx, title, dy, st0, ex;
    long  sp0;
    int   cur_on, done, force;
    DOC  *owner;
    char  wtitle[80], key[60];
} DLGWIN;
#define MAXDLGWIN 4             /* each waits on the stack (a DLG is 6 KB): enough at once */
static DLGWIN dws[MAXDLGWIN];
int dlg_busy;                   /* dialogs open */

static DLGWIN *dw_of_wh(short wh)
{
    int i;
    if (wh <= 0) return NULL;
    for (i = 0; i < MAXDLGWIN; i++) if (dws[i].g && dws[i].wh == wh) return &dws[i];
    return NULL;
}

static DLGWIN *dw_of(DLG *g)
{
    int i;
    for (i = 0; i < MAXDLGWIN; i++) if (dws[i].g == g) return &dws[i];
    return NULL;
}

static int dw_top(DLGWIN *e)
{
    short h, d1, d2, d3;
    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    return e->wh > 0 && h == e->wh;
}

/* the window closed and the tree put back as it was (its values kept) */
static void dw_shut(DLGWIN *e)
{
    DLG *g = e->g;
    short i;
    if (!g || e->wh <= 0) return;
    if (e->edit && e->cur_on && dw_top(e)) objc_edit(g->o, e->edit, 0, &e->idx, ED_END);
    winpos_save(e->key, e->wh);
    wind_close(e->wh);
    wind_delete(e->wh);
    e->wh = 0;
    for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) g->o[i].ob_y -= hchar / 4;
    g->o[0].ob_height -= hchar / 2 + hchar / 4;
    if (e->title > 0) {
        g->o[e->title].ob_flags &= ~OF_HIDETREE;
        for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) if (i != e->title) g->o[i].ob_y += e->dy;
        g->o[0].ob_height += e->dy;
    }
    g->o[0].ob_state = e->st0; g->o[0].ob_spec.index = e->sp0;
}

static void dlg_shut(DLG *g)
{
    DLGWIN *e = dw_of(g);
    if (!e) return;
    dw_shut(e);
    e->g = NULL;
    dlg_busy--;
}

void dlg_close(DLG *g) { dlg_shut(g); }

/* the newest dialog to the front (a document can't close under it) */
void dlg_front(void)
{
    int i;
    for (i = MAXDLGWIN - 1; i >= 0; i--) if (dws[i].g && dws[i].wh > 0) { wind_set(dws[i].wh, WF_TOP, 0, 0, 0, 0); return; }
}

/* a dialog open for document d (it can't close yet): brought to the front */
int dlg_open_for(DOC *d)
{
    int i;
    for (i = 0; i < MAXDLGWIN; i++)
        if (dws[i].g && (dws[i].owner == d || !d)) {
            if (dws[i].wh > 0) wind_set(dws[i].wh, WF_TOP, 0, 0, 0, 0);
            return 1;
        }
    return 0;
}

static short dlg_finish(DLG *g, short ex)
{
    short i;
    if (ex > 0 && ex < g->n) g->o[ex].ob_state &= ~OS_SELECTED;
    for (i = 0; i < g->nit; i++) if (g->it[i].obj == ex) return i;
    return -1;
}

static short dlg_modal(DLG *g)
{
    short x, y, w, h, ex;
    form_center(g->o, &x, &y, &w, &h);
    g->x = x; g->y = y;
    form_dial(FMD_START, 0, 0, 0, 0, x, y, w, h);
    objc_draw(g->o, 0, 8, x, y, w, h);
#ifdef EDDEMO
    if (demo_on) {
        ex = (short)demo_dialog(g->o, x, y, w, h);
        if (ex < 0) ex = form_do(g->o, g->edit1 >= 0 ? g->edit1 : 0) & 0x7fff;
    } else
#endif
    ex = form_do(g->o, g->edit1 >= 0 ? g->edit1 : 0) & 0x7fff;
    form_dial(FMD_FINISH, 0, 0, 0, 0, x, y, w, h);
    return dlg_finish(g, ex);
}

static void dw_redraw(DLGWIN *e, const GRECT *area)
{
    GRECT r, a = *area;
    DLG *g = e->g;
    short x1, y1, x2, y2;
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    wind_get(e->wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        x1 = r.g_x > a.g_x ? r.g_x : a.g_x;
        y1 = r.g_y > a.g_y ? r.g_y : a.g_y;
        x2 = r.g_x + r.g_w < a.g_x + a.g_w ? r.g_x + r.g_w : a.g_x + a.g_w;
        y2 = r.g_y + r.g_h < a.g_y + a.g_h ? r.g_y + r.g_h : a.g_y + a.g_h;
        if (x2 > x1 && y2 > y1) objc_draw(g->o, 0, 8, x1, y1, x2 - x1, y2 - y1);
        wind_get(e->wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
}

/* the item with this shortcut letter */
static short dlg_shortcut(DLG *g, int c)
{
    short i;
    c = upc(c);
    for (i = 0; i < g->nit; i++)
        if (g->it[i].ul >= 0 && g->it[i].key == c && g->it[i].obj >= 0 &&
            !(g->o[g->it[i].obj].ob_state & OS_DISABLED)) return g->it[i].obj;
    return -1;
}

/* the Cancel button, for Undo and Esc */
static short dlg_cancel_obj(DLG *g)
{
    short i;
    if (g->cancel > 0) return g->cancel;
    for (i = 0; i < g->nit; i++)
        if ((g->it[i].type == T_BUTTON || g->it[i].type == T_BIG) && g->it[i].s == L(S_CANCEL)) return g->it[i].obj;
    return -1;
}

/* the letter of an Alternate key, from the keyboard table */
static int alt_letter(short key)
{
    _KEYTAB *kt = Keytbl((void *)-1, (void *)-1, (void *)-1);
    short scan = (key >> 8) & 0xff;
    if (!kt || scan <= 0 || scan >= 128) return 0;
    return ((unsigned char *)kt->unshift)[scan];
}

/* answered: its window goes now (its caller goes on when it can) */
static void dw_answer(DLGWIN *e, short ex, int force)
{
    e->ex = ex;
    e->done = 1;
    e->force = force;
    if (!e->g->stay || force) dw_shut(e);
}

static void dw_msg(DLGWIN *e, short *msg)
{
    GRECT a;
    short w, h;
    DLG *g = e->g;
    switch (msg[0]) {
    case WM_REDRAW:
        a.g_x = msg[4]; a.g_y = msg[5]; a.g_w = msg[6]; a.g_h = msg[7];
        /* the text cursor is drawn by XOR: take it off first */
        if (e->edit && e->cur_on && dw_top(e)) { objc_edit(g->o, e->edit, 0, &e->idx, ED_END); e->cur_on = 0; }
        dw_redraw(e, &a);
        if (e->edit && dw_top(e)) { objc_edit(g->o, e->edit, 0, &e->idx, ED_END); e->cur_on = 1; }
        else e->cur_on = 0;
        break;
    case WM_TOPPED:
        wind_set(e->wh, WF_TOP, 0, 0, 0, 0);
        if (e->edit && !e->cur_on) { objc_edit(g->o, e->edit, 0, &e->idx, ED_END); e->cur_on = 1; }
        break;
    case WM_CLOSED: {
        short ex = dlg_cancel_obj(g);
        dw_answer(e, ex < 0 ? 0 : ex, 1);
        break;
    }
    case WM_MOVED:
        winpos_clamp(&msg[4], &msg[5], msg[6], msg[7]);
        wind_set(e->wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
        wind_get(e->wh, WF_WORKXYWH, &g->o[0].ob_x, &g->o[0].ob_y, &w, &h);
        g->x = g->o[0].ob_x; g->y = g->o[0].ob_y;
        break;
    case WM_BOTTOMED:
        wind_set(e->wh, WF_BOTTOM, 0, 0, 0, 0);
        break;
    }
}

static void dw_key(DLGWIN *e, short ks, short key)
{
    DLG *g = e->g;
    short scan = (key >> 8) & 0xff, o, next = e->edit;
    int cont;
    if (scan == 0x61 || (scan == 0x01 && !e->edit)) {         /* Undo, or Esc: Cancel */
        o = dlg_cancel_obj(g);
        if (o > 0) { dw_answer(e, o, 0); return; }
    }
    if ((ks & 8) && (o = dlg_shortcut(g, alt_letter(key))) > 0) {   /* Alternate + letter */
        cont = form_button(g->o, o, 1, &next);
        if (!cont) dw_answer(e, next, 0);
        return;
    }
    if (e->edit && !e->cur_on) { objc_edit(g->o, e->edit, 0, &e->idx, ED_END); e->cur_on = 1; }
    {
        short np = e->idx;
        if (e->edit && scan != 0x1C && scan != 0x72 && scan != 0x0F && field_edit(g->o, e->edit, key, &np)) {
            GRECT fr;
            objc_edit(g->o, e->edit, 0, &e->idx, ED_END);
            objc_offset(g->o, e->edit, &fr.g_x, &fr.g_y);
            fr.g_w = g->o[e->edit].ob_width; fr.g_h = g->o[e->edit].ob_height;
            dw_redraw(e, &fr);
            field_cursor(g->o, e->edit, &e->idx, np);
            return;
        }
    }
    cont = form_keybd(g->o, e->edit, next, key, &next, &key);
    if (key && e->edit) objc_edit(g->o, e->edit, key, &e->idx, ED_CHAR);
    if (!cont) { dw_answer(e, next, 0); return; }
    if (next && next != e->edit && (g->o[next].ob_flags & OF_EDITABLE)) {
        objc_edit(g->o, e->edit, 0, &e->idx, ED_END);
        e->edit = next;
        objc_edit(g->o, e->edit, 0, &e->idx, ED_INIT);
    }
}

static void dw_click(DLGWIN *e, short mx, short my, short br)
{
    DLG *g = e->g;
    short next;
    int cont;
    if (!dw_top(e)) {
        wind_set(e->wh, WF_TOP, 0, 0, 0, 0);
        if (e->edit && !e->cur_on) { objc_edit(g->o, e->edit, 0, &e->idx, ED_END); e->cur_on = 1; }
    }
    next = objc_find(g->o, 0, 8, mx, my);
    if (next < 0) return;
    cont = form_button(g->o, next, br, &next);
    if (!cont) { dw_answer(e, next, 0); return; }
    if (next > 0 && next != e->edit && (g->o[next].ob_flags & OF_EDITABLE)) {
        if (e->edit && e->cur_on) objc_edit(g->o, e->edit, 0, &e->idx, ED_END);
        e->edit = next;
        objc_edit(g->o, e->edit, 0, &e->idx, ED_INIT);
        e->cur_on = 1;
    }
}

/* The event loops of dialogs (and EdWord's file selector): what isn't
 * for the window being waited on goes where it belongs - another open
 * dialog, a document, a tool window. */
int dlg_dispatch_msg(short *msg)
{
    DLGWIN *e = msg[0] >= 20 && msg[0] < 40 ? dw_of_wh(msg[3]) : NULL;
    if (!e) return 0;
    if (!e->done) dw_msg(e, msg);
    return 1;
}

int dlg_dispatch_key(short ks, short key)
{
    short h, d1, d2, d3;
    DLGWIN *e;
    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    e = dw_of_wh(h);
    if (!e || e->done) return 0;
    dw_key(e, ks, key);
    return 1;
}

int dlg_dispatch_click(short mx, short my, short br)
{
    DLGWIN *e = dw_of_wh(wind_find(mx, my));
    if (!e || e->done) return 0;
    dw_click(e, mx, my, br);
    return 1;
}

short dlg_run(DLG *g)
{
    short x, y, w, h, wx, wy, ww, wh, kind;
    short msg[8], mx, my, mb, ks, key, br, ev, i, k;
    const char *tname = NULL;
    DLGWIN *e;

    eddiag("dlg_run: open %d, windows %d, 3D %d, MagiC %d, %d objects, %d items",
           dlg_busy, win_dialogs, aes_3d, aes_magic, g->n, g->nit);
    e = dw_of(g);
    if (e && e->wh > 0) {
        /* still open (stay): on with the same window, drawn again with
         * what the caller changed */
        GRECT a;
        wind_get(e->wh, WF_WORKXYWH, &g->o[0].ob_x, &g->o[0].ob_y, &w, &h);
        g->x = g->o[0].ob_x; g->y = g->o[0].ob_y;
        a.g_x = g->x; a.g_y = g->y; a.g_w = w; a.g_h = h;
        e->done = 0;
        dw_redraw(e, &a);
        goto run;
    }
    /* the same dialog is open already (for another document, say): to the front */
    for (k = 0; k < MAXDLGWIN; k++)
        if (dws[k].g && g->tree && dws[k].g->tree == g->tree) {
            if (dws[k].wh > 0) wind_set(dws[k].wh, WF_TOP, 0, 0, 0, 0);
            return -1;
        }
    for (k = 0; k < MAXDLGWIN && dws[k].g; k++) ;
    if (!win_dialogs || k == MAXDLGWIN) return dlg_modal(g);
    e = &dws[k];
    memset(e, 0, sizeof *e);
    e->st0 = g->o[0].ob_state; e->sp0 = g->o[0].ob_spec.index;
    e->title = -1;
    e->owner = ui_doc && ui_doc->used && ui_doc->wh > 0 && !ui_doc->iconified ? ui_doc : NULL;
    kind = NAME | MOVER | CLOSER;
    /* the title goes into the window's title bar */
    for (i = 0; i < g->nit; i++) if (g->it[i].type == T_TITLE) { e->title = g->it[i].obj; tname = g->it[i].s; }
    if (e->title > 0) {
        e->dy = (short)(hchar + hchar / 2);
        g->o[e->title].ob_flags |= OF_HIDETREE;
        for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) if (i != e->title) g->o[i].ob_y -= e->dy;
        g->o[0].ob_height -= e->dy;
    }
    g->o[0].ob_state &= ~OS_OUTLINED;
    g->o[0].ob_spec.index &= ~0x00FF0000L;            /* no frame: the window has one */
    /* a little room above the first row and under the buttons */
    for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) g->o[i].ob_y += hchar / 4;
    g->o[0].ob_height += hchar / 2 + hchar / 4;
    w = g->o[0].ob_width; h = g->o[0].ob_height;
    wind_calc(WC_BORDER, kind, 0, 0, w, h, &wx, &wy, &ww, &wh);
    sprintf(e->key, " %.50s ", tname ? tname : "EdWord");
    /* a document's dialog: its name in the title, over its window */
    if (e->owner) sprintf(e->wtitle, " %.40s - %.30s ", tname ? tname : "EdWord", e->owner->base);
    else strcpy(e->wtitle, e->key);
    {
        GRECT o;
        if (e->owner && doc_area(e->owner, &o)) {       /* over its document's window */
            x = (short)(o.g_x + (o.g_w - ww) / 2);
            y = (short)(o.g_y + (o.g_h - wh) / 3);
        } else if (!winpos_get(e->key, &x, &y, NULL, NULL)) {
            x = (short)(desk.g_x + (desk.g_w - ww) / 2);
            y = (short)(desk.g_y + (desk.g_h - wh) / 2);
        }
    }
    winpos_clamp(&x, &y, ww, wh);
    e->wh = wind_create(kind, x, y, ww, wh);
    if (e->wh <= 0) {                                  /* no window to spare */
        e->wh = 0;
        for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) g->o[i].ob_y -= hchar / 4;
        g->o[0].ob_height -= hchar / 2 + hchar / 4;
        if (e->title > 0) {
            g->o[e->title].ob_flags &= ~OF_HIDETREE;
            for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) if (i != e->title) g->o[i].ob_y += e->dy;
            g->o[0].ob_height += e->dy;
        }
        g->o[0].ob_state |= OS_OUTLINED;
        g->o[0].ob_spec.index |= 0x00020000L;
        return dlg_modal(g);
    }
    e->g = g;
    dlg_busy++;
    wind_set_str(e->wh, WF_NAME, e->wtitle);
    wind_open(e->wh, x, y, ww, wh);
    wind_get(e->wh, WF_WORKXYWH, &g->o[0].ob_x, &g->o[0].ob_y, &w, &h);
    g->x = g->o[0].ob_x; g->y = g->o[0].ob_y;
run:
    e->edit = g->edit1 > 0 && (g->o[g->edit1].ob_flags & OF_EDITABLE) ? g->edit1 : 0;
#ifdef EDDEMO
    if (demo_on) {
        GRECT a;
        a.g_x = g->x; a.g_y = g->y; a.g_w = w; a.g_h = h;
        dw_redraw(e, &a);
        pump_redraws();
        k = (short)demo_dialog(g->o, g->x, g->y, w, h);
        if (k >= 0) dw_answer(e, k, 0);
    }
#endif
    if (e->edit && !e->done) { objc_edit(g->o, e->edit, 0, &e->idx, ED_INIT); e->cur_on = 1; }
    while (!e->done) {
        ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        msg, bgprint_active() ? 20L : 500L, &mx, &my, &mb, &ks, &key, &br);
        if (ev & MU_MESAG) {
            if (!dlg_dispatch_msg(msg)) handle_msg(msg);
            if (quit_now && !e->done) { short c = dlg_cancel_obj(g); dw_answer(e, c < 0 ? 0 : c, 1); }
        }
        if ((ev & MU_KEYBD) && !dlg_dispatch_key(ks, key)) handle_key(ks, key);
        if ((ev & MU_BUTTON) && !dlg_dispatch_click(mx, my, br)) handle_click(mx, my, ks, br);
        if ((ev & MU_TIMER) && bgprint_active()) bgprint_idle(60);
    }
    k = e->ex;
    /* stay: the window stays for the caller's next dlg_run (or dlg_close),
     * unless it was closed or EdWord is quitting */
    if (!g->stay || e->force) { dw_shut(e); e->g = NULL; dlg_busy--; }
    return dlg_finish(g, k & 0x7fff);
}

/* ================================================================= */
/*  Alerts                                                            */
/* ================================================================= */

/* An alert in a window of its own, like the dialogs: the documents and
 * the other windows go on working while it waits. Laid out as the AES's:
 * its sign on the left, up to five lines, the buttons under them. Where
 * there's no window to spare (or none yet) the AES's own alert. */
static int alert_run(DLG *gp, int def, int icon, int nl, int nb, int w, int bw, char lines[5][34], char btn[3][12]);

static int alert_win(int def, int icon, const char *t, const char *b)
{
    static int busy;                    /* one alert of ours at a time */
    static char lines[5][34], btn[3][12];
    DLG *gp;
    int r;
    int nl = 0, nb = 0, w = 0, bw = 0;
    const char *p;
    if (!win_dialogs || busy) return -1;
    for (p = t; *p && nl < 5; nl++) {
        int k = 0;
        while (*p && *p != '|' && k < 33) lines[nl][k++] = *p++;
        lines[nl][k] = 0;
        if (*p == '|') p++;
        if (k > w) w = k;
    }
    for (p = b; *p && nb < 3; nb++) {
        int k = 0;
        while (*p && *p != '|' && k < 11) btn[nb][k++] = *p++;
        btn[nb][k] = 0;
        if (*p == '|') p++;
        if (k + 2 > bw) bw = k + 2;
    }
    if (!nb) return -1;
    /* (its dialog is big: in memory only while it is shown) */
    if ((gp = (DLG *)calloc(1, sizeof(DLG))) == NULL) return -1;
    busy = 1;
    r = alert_run(gp, def, icon, nl, nb, w, bw, lines, btn);
    busy = 0;
    free(gp);
    return r;
}

static int alert_run(DLG *gp, int def, int icon, int nl, int nb, int w, int bw, char lines[5][34], char btn[3][12])
{
#define g (*gp)
    int i, x;
    short ex;
    g.framed = 1;
    g.edit1 = -1;
    /* the box, the sign, the lines, the buttons */
    {
        int cw = w + 7 > nb * (bw + 2) + 6 ? w + 7 : nb * (bw + 2) + 6, ch = nl + 4, n = 0;
        OBJECT *o = g.o;
        o[0].ob_next = -1; o[0].ob_head = 1; o[0].ob_type = G_BOX; o[0].ob_spec.index = 0x00021100L;
        o[0].ob_width = (short)(cw * wchar); o[0].ob_height = (short)(ch * hchar);
        g.it[0].obj = 0; g.it[0].type = T_TEXT; g.it[0].s = ""; g.it[0].ul = -1;
        n = 1;
        if (icon >= 1 && icon <= 3) {
            static const char sign[4] = { 0, '!', '?', 'X' };
            o[n].ob_type = G_BOXCHAR;
            o[n].ob_spec.index = ((long)(unsigned char)sign[icon] << 24) | 0x00FF1100L;
            o[n].ob_x = (short)wchar; o[n].ob_y = (short)hchar;
            o[n].ob_width = (short)(3 * wchar); o[n].ob_height = (short)(2 * hchar);
            g.it[n].obj = n; g.it[n].type = T_TEXT; g.it[n].s = ""; g.it[n].ul = -1;
            n++;
        }
        for (i = 0; i < nl; i++, n++) {
            o[n].ob_type = G_STRING; o[n].ob_spec.free_string = lines[i];
            o[n].ob_x = (short)(5 * wchar); o[n].ob_y = (short)((1 + i) * hchar);
            o[n].ob_width = (short)(strlen(lines[i]) * wchar); o[n].ob_height = hchar;
            g.it[n].obj = n; g.it[n].type = T_TEXT; g.it[n].s = lines[i]; g.it[n].ul = -1;
        }
        x = cw - nb * (bw + 2);
        for (i = 0; i < nb; i++, n++) {
            o[n].ob_type = G_BUTTON; o[n].ob_spec.free_string = btn[i];
            o[n].ob_flags = (short)(OF_SELECTABLE | OF_EXIT | (i + 1 == def ? OF_DEFAULT : 0));
            o[n].ob_x = (short)((x + i * (bw + 2)) * wchar); o[n].ob_y = (short)((nl + 2) * hchar);
            o[n].ob_width = (short)(bw * wchar); o[n].ob_height = hchar;
            g.it[n].obj = n; g.it[n].type = T_BUTTON; g.it[n].s = btn[i]; g.it[n].ul = -1;
        }
        for (i = 1; i < n; i++) { o[i].ob_next = (short)(i + 1); o[i].ob_head = o[i].ob_tail = -1; }
        o[n - 1].ob_next = 0;
        o[n - 1].ob_flags |= OF_LASTOB;
        o[0].ob_tail = (short)(n - 1);
        g.n = g.nit = (short)n;
        g.cancel = (short)(def == nb && nb > 1 ? n - nb : n - 1);     /* Esc: the last button, or the first */
        dlg_tree_end(&g);
        ex = dlg_run(&g);
        if (ex < 0) return -1;
        ex = (short)(g.it[ex].obj - (n - nb) + 1);
        return ex >= 1 && ex <= nb ? ex : def;
    }
#undef g
}

int alert(int def, int icon, int text, int buttons, ...)
{
    char t[300], a[400];
    va_list ap;
    int r;
    va_start(ap, buttons);
    vsprintf(t, L(text), ap);
    va_end(ap);
#ifdef EDDEMO
    if (demo_on) return demo_alert() ? def : def;
#endif
    if ((r = alert_win(def, icon, t, L(buttons))) > 0) return r;
    sprintf(a, "[%d][%s][%s]", icon, t, L(buttons));
    return form_alert(def, a);
}

/* ================================================================= */
/*  Popup menu                                                        */
/* ================================================================= */

/* the AES has popup menus (menu_popup): AES 3.30 and later (TOS 4,
 * MultiTOS), MagiC, XaAES, MyAES... */
static int aes_popups(void)
{
    static int known = -1;
    short o1 = 0, o2 = 0, o3 = 0, o4 = 0;
    if (known >= 0) return known;
    known = 0;
    if (appl_getinfo(9, &o1, &o2, &o3, &o4) == 1) known = o2 != 0;
    else if (aes_global[0] >= 0x0330) known = 1;     /* menu_popup came with AES 3.30 (TOS 4) */
    edlog("AES popup menus: %d", known);
    return known;
}

/* the AES's own popup menu: its look, its keyboard, its mouse handling */
static int aes_popup(const char **items, int n, int cur, int cur2, short x, short y)
{
    static OBJECT *t;
    static char (*txt)[40];
    static int cap;
    MENU mn, out;
    short i, w = 0;
    if (n > cap) {
        free(t); free(txt);
        cap = n + 16;
        t = (OBJECT *)calloc((size_t)cap + 1, sizeof(OBJECT));
        txt = (char (*)[40])calloc((size_t)cap, 40);
        if (!t || !txt) { free(t); free(txt); t = NULL; txt = NULL; cap = 0; return -2; }
    }
    for (i = 0; i < n; i++) {
        sprintf(txt[i], "  %.32s ", items[i][0] == '-' ? "" : items[i]);
        if (items[i][0] == '-') memset(txt[i], '-', strlen(txt[i]) > 3 ? 3 : strlen(txt[i]));
        if ((short)strlen(txt[i]) > w) w = (short)strlen(txt[i]);
    }
    for (i = 0; i < n; i++)
        if (items[i][0] == '-') { memset(txt[i], '-', (size_t)w); txt[i][w] = 0; }
    memset(t, 0, sizeof(OBJECT) * ((size_t)n + 1));
    t[0].ob_next = -1; t[0].ob_head = 1; t[0].ob_tail = n;
    t[0].ob_type = G_BOX;
    t[0].ob_spec.index = 0x00FF1100L;
    t[0].ob_width = (short)(w * wchar);
    t[0].ob_height = (short)(n * hchar);
    for (i = 1; i <= n; i++) {
        t[i].ob_next = (short)(i < n ? i + 1 : 0);
        t[i].ob_head = t[i].ob_tail = -1;
        t[i].ob_type = G_STRING;
        t[i].ob_spec.free_string = txt[i - 1];
        t[i].ob_y = (short)((i - 1) * hchar);
        t[i].ob_width = t[0].ob_width;
        t[i].ob_height = hchar;
        if (items[i - 1][0] == '-') t[i].ob_state = OS_DISABLED;
        else if (i - 1 == cur || i - 1 == cur2) t[i].ob_state = OS_CHECKED;
    }
    t[n].ob_flags |= OF_LASTOB;
    if (x + t[0].ob_width > scr_w - 2) x = (short)(scr_w - 2 - t[0].ob_width);
    if (x < 1) x = 1;
    mn.mn_tree = t;
    mn.mn_menu = 0;
    mn.mn_item = 1;
    mn.mn_scroll = (short)(n * hchar > scr_h - desk.g_y - hchar ? 1 : 0);
    mn.mn_keystate = 0;
    memset(&out, 0, sizeof out);
    if (!menu_popup(&mn, x, y, &out) || out.mn_tree != t || out.mn_item < 1 || out.mn_item > n) return -1;
    return out.mn_item - 1;
}

int popup(const char **items, int n, int cur, short x, short y) { return popup2(items, n, cur, -1, x, y); }

/* a popup from the resource file: the AES's own popup with the tree as
 * it is, or EdWord's with its texts; -3 without the file */
int popup_rsc(int tree, int cur, short x, short y)
{
    static char txt[24][40];
    const char *items[24];
    OBJECT *t = rsc_tree(tree);
    short n, i;
    if (!t) return -3;
    n = t[0].ob_tail;
    if (n > 24) n = 24;
#ifdef EDDEMO
    if (!demo_on)
#endif
    if (aes_popups()) {
        MENU mn, out;
        for (i = 1; i <= n; i++) {
            t[i].ob_state &= ~OS_CHECKED;
            if (i - 1 == cur) t[i].ob_state |= OS_CHECKED;
        }
        if (x + t[0].ob_width > scr_w - 2) x = (short)(scr_w - 2 - t[0].ob_width);
        if (x < 1) x = 1;
        mn.mn_tree = t;
        mn.mn_menu = 0;
        mn.mn_item = 1;
        mn.mn_scroll = 0;
        mn.mn_keystate = 0;
        memset(&out, 0, sizeof out);
        if (!menu_popup(&mn, x, y, &out) || out.mn_tree != t || out.mn_item < 1 || out.mn_item > n) return -1;
        return out.mn_item - 1;
    }
    for (i = 0; i < n; i++) {
        int k;
        sprintf(txt[i], "%.39s", rsc_entry(tree, (short)(i + 1)));
        for (k = (int)strlen(txt[i]); k > 0 && txt[i][k - 1] == ' '; k--) txt[i][k - 1] = 0;
        items[i] = txt[i];
    }
    return popup2(items, n, cur, -1, x, y);
}

/* a popup with two items checked (cur2 < 0: one) */
int popup2(const char **items, int n, int cur, int cur2, short x, short y)
{
    static OBJECT t[40];
    static char txt[40][36];
    short i, w = 0, mx, my, mb, ks, hl = -1, o, bx, by, bw, bh;

#ifdef EDDEMO
    if (!demo_on)
#endif
    if (aes_popups()) {
        int r = aes_popup(items, n, cur, cur2, x, y);
        if (r != -2) return r;
    }
    if (n > 38) n = 38;
    for (i = 0; i < n; i++) {
        sprintf(txt[i], "%c %.30s ", i == cur || i == cur2 ? 8 : ' ', items[i]);   /* 8 = check mark */
        if ((short)strlen(txt[i]) > w) w = (short)strlen(txt[i]);
    }
    for (i = 0; i < n; i++)                          /* separators: a line of dashes */
        if (items[i][0] == '-') { memset(txt[i], '-', (size_t)(w < 35 ? w : 35)); txt[i][w < 35 ? w : 35] = 0; }
    memset(t, 0, sizeof t);
    t[0].ob_next = -1; t[0].ob_head = 1; t[0].ob_tail = n;
    t[0].ob_type = G_BOX;
    t[0].ob_spec.index = 0x00FF1100L;
    t[0].ob_width = (short)(w * wchar);
    t[0].ob_height = (short)(n * hchar);
    if (x + t[0].ob_width > scr_w - 2) x = (short)(scr_w - 2 - t[0].ob_width);
    if (y + t[0].ob_height > scr_h - 2) y = (short)(scr_h - 2 - t[0].ob_height);
    if (x < 1) x = 1;
    if (y < desk.g_y) y = desk.g_y;
    t[0].ob_x = x; t[0].ob_y = y;
    for (i = 1; i <= n; i++) {
        t[i].ob_next = (short)(i < n ? i + 1 : 0);
        t[i].ob_head = t[i].ob_tail = -1;
        t[i].ob_type = G_STRING;
        t[i].ob_flags = OF_SELECTABLE;
        t[i].ob_spec.free_string = txt[i - 1];
        t[i].ob_y = (short)((i - 1) * hchar);
        t[i].ob_width = t[0].ob_width;
        t[i].ob_height = hchar;
        if (items[i - 1][0] == '-') { t[i].ob_state = OS_DISABLED; t[i].ob_flags = 0; }
    }
    t[n].ob_flags |= OF_LASTOB;
    bx = (short)(x - 1); by = (short)(y - 1); bw = (short)(t[0].ob_width + 2); bh = (short)(t[0].ob_height + 2);

    wind_update(BEG_UPDATE);
    wind_update(BEG_MCTRL);
    form_dial(FMD_START, 0, 0, 0, 0, bx, by, bw, bh);
    objc_draw(t, 0, 2, bx, by, bw, bh);
#ifdef EDDEMO
    if (demo_on) {
        int r = demo_dialog(t, bx, by, bw, bh);
        form_dial(FMD_FINISH, 0, 0, 0, 0, bx, by, bw, bh);
        wind_update(END_MCTRL);
        wind_update(END_UPDATE);
        return r >= 1 ? r - 1 : -1;
    }
#endif
    do graf_mkstate(&mx, &my, &mb, &ks); while (mb & 1);         /* release the click */
    for (;;) {
        graf_mkstate(&mx, &my, &mb, &ks);
        o = objc_find(t, 0, 1, mx, my);
        if (o < 1 || (t[o].ob_state & OS_DISABLED)) o = -1;
        if (o != hl) {
            if (hl > 0) objc_change(t, hl, 0, bx, by, bw, bh, 0, 1);
            if (o > 0) objc_change(t, o, 0, bx, by, bw, bh, OS_SELECTED, 1);
            hl = o;
        }
        if (mb & 1) break;
        evnt_timer(20L);
    }
    do graf_mkstate(&mx, &my, &mb, &ks); while (mb & 1);
    form_dial(FMD_FINISH, 0, 0, 0, 0, bx, by, bw, bh);
    wind_update(END_MCTRL);
    wind_update(END_UPDATE);
    return hl > 0 ? hl - 1 : -1;
}

/* ================================================================= */
/*  File selector                                                     */
/* ================================================================= */

static char seldir[256];

/* The documents folder from the preferences, made if it isn't there yet:
 * 1 with its path in out. Not on a drive the machine doesn't have, which
 * would only bring up TOS's "insert disk" alert. */
static int docdir_ready(char *out)
{
    char p[130];
    int n, i, drv;
    _DTA *old = Fgetdta(), dta;
    long r;
    strncpy(p, pref_docdir, sizeof p - 1);
    p[sizeof p - 1] = 0;
    n = (int)strlen(p);
    while (n > 3 && p[n - 1] == '\\') p[--n] = 0;
    if (n < 3 || p[1] != ':' || p[2] != '\\') return 0;
    drv = (p[0] & ~32) - 'A';
    if (drv < 0 || drv > 25 || !(Drvmap() & (1L << drv))) return 0;
    for (i = 3; i <= n; i++)
        if (p[i] == '\\' || !p[i]) {
            char c = p[i];
            p[i] = 0;
            (void)Dcreate(p);           /* it may be there already */
            p[i] = c;
        }
    Fsetdta(&dta);
    r = n > 3 ? Fsfirst(p, FA_DIR) : 0;
    Fsetdta(old);
    if (r != 0 || (n > 3 && !(dta.dta_attribute & FA_DIR))) return 0;
    strcpy(out, p);
    return 1;
}

/* the preference has changed: the file selector starts there next time */
void docdir_changed(void) { seldir[0] = 0; }

int ask_file(const char *title, const char *pattern, const char *defname, char *out)
{
    char name[64];
    short btn = 0, ok;
    char *s;
#ifdef EDDEMO
    if (demo_on) return demo_file(out);
#endif
    if (!seldir[0] && docdir_ready(seldir)) strcat(seldir, "\\*.*");
    if (!seldir[0]) {
        seldir[0] = (char)('A' + Dgetdrv());
        seldir[1] = ':';
        Dgetpath(seldir + 2, 0);
        strcat(seldir, "\\*.*");
    }
    s = strrchr(seldir, '\\');
    if (s) strcpy(s + 1, pattern);
    strncpy(name, defname ? defname : "", sizeof name - 1);
    name[sizeof name - 1] = 0;
    ok = -1;
    if (fsel_own()) { ok = (short)fsel_window(seldir, name, title); btn = 1; }   /* EdWord's own, in a window */
    if (ok < 0) {
        if (aes_global[0] >= 0x0140) ok = fsel_exinput(seldir, name, &btn, title);
        else ok = fsel_input(seldir, name, &btn);
    }
    /* the windows under the selector are drawn again now, before a long
     * load or save starts, so the selector isn't left on the screen */
    pump_redraws();
    if (!ok || !btn || !name[0]) return 0;
    strcpy(out, seldir);
    s = strrchr(out, '\\');
    if (s) s[1] = 0; else out[0] = 0;
    strcat(out, name);
    return 1;
}

/* ================================================================= */
/*  Small helpers for the dialogs below                               */
/* ================================================================= */

static int pt2mm(int pt) { return (int)((pt * 254L + (pt >= 0 ? 360 : -360)) / 720); }
static int mm2pt(int mm) { return (int)((mm * 720L + (mm >= 0 ? 127 : -127)) / 254); }
/* a field in mm back to points; unchanged fields keep their exact value */
static int field_pt(const char *buf, int old) { int mm = atoi(buf); return mm == pt2mm(old) ? old : mm2pt(mm); }

/* ================================================================= */
/*  Page layout                                                       */
/* ================================================================= */

int dlg_page(DOC *d)
{
    static char mt[4], mb[4], ml[4], mr[4], nc[2], gap[4], wc[4];
    DLG g;
    PAGESET *ps = &PS(d);
    PGFMT f;
    short i;
    int page = d->nlines ? d->lines[doc_find_line(d, d->cursor)].page : 0, v, how, k;

    page_fmt(d, page, &f);
    sprintf(mt, "%d", pt2mm(f.mt)); sprintf(mb, "%d", pt2mm(f.mb));
    sprintf(ml, "%d", pt2mm(f.ml)); sprintf(mr, "%d", pt2mm(f.mr));
    sprintf(nc, "%d", f.cols > 9 ? 9 : f.cols); sprintf(gap, "%d", pt2mm(f.colgap));
    sprintf(wc, "%d", ps->wrapcol);
    if (!dlg_load(&g, RT_PAGE)) return 0;
    for (i = 0; i < NPAPER; i++) dlg_set(&g, (short)(DPG_PAPER0 + i), ps->paper == i);
    dlg_set(&g, DPG_PORTRAIT, !ps->landscape);
    dlg_set(&g, DPG_LAND, ps->landscape);
    dlg_edtext(&g, DPG_MT, mt); dlg_edtext(&g, DPG_MB, mb);
    dlg_edtext(&g, DPG_ML, ml); dlg_edtext(&g, DPG_MR, mr);
    dlg_edtext(&g, DPG_COLS, nc); dlg_edtext(&g, DPG_GAP, gap);
    dlg_edtext(&g, DPG_WRAP, wc);
    /* start with what this page has now: the whole document, this page, or onwards */
    how = d->a.novr ? 1 : 0;
    for (k = 0; k < d->a.novr; k++) {
        PGOVR *o = &d->a.ovr[k];
        if (o->page == page) { how = o->following ? 2 : 1; break; }
        if (o->page < page && o->following) how = 2;
    }
    dlg_set(&g, DPG_WHOLE, how == 0);
    dlg_set(&g, DPG_ONE, how == 1);
    dlg_set(&g, DPG_AFTER, how == 2);
    g.cancel = DPG_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DPG_OK) return 0;

    for (i = 0; i < NPAPER; i++)
        if (dlg_on(&g, (short)(DPG_PAPER0 + i))) break;
    v = dlg_on(&g, DPG_LAND) ? 1 : 0;
    if (i < NPAPER) page_set_paper(ps, i, v);
    else if (v != ps->landscape) { short t = ps->w; ps->w = ps->h; ps->h = t; ps->landscape = (short)v; }
    f.mt = (short)field_pt(mt, f.mt); f.mb = (short)field_pt(mb, f.mb);
    f.ml = (short)field_pt(ml, f.ml); f.mr = (short)field_pt(mr, f.mr);
    v = atoi(nc);
    f.cols = (short)(v < 1 ? 1 : v > 4 ? 4 : v);
    f.colgap = (short)field_pt(gap, f.colgap);
    v = atoi(wc);
    ps->wrapcol = (short)(v > 250 ? 250 : v);
    if (dlg_on(&g, DPG_ONE) || dlg_on(&g, DPG_AFTER)) page_ovr_set(d, page, dlg_on(&g, DPG_AFTER), &f);
    else {
        ps->mt = f.mt; ps->mb = f.mb; ps->ml = f.ml; ps->mr = f.mr;
        ps->cols = f.cols; ps->colgap = f.colgap;
        page_ovr_clear(d);
    }
    /* keep at least 3 cm of text width and height */
    if (ps->ml + ps->mr > ps->w - 85) ps->ml = ps->mr = (short)((ps->w - 85) / 2);
    if (ps->mt + ps->mb > ps->h - 85) ps->mt = ps->mb = (short)((ps->h - 85) / 2);
    d->dirty = 1;
    return 1;
}

int dlg_wrapcol(DOC *d)
{
    static char wc[4];
    DLG g;
    int v;
    sprintf(wc, "%d", PS(d).wrapcol);
    if (!dlg_load(&g, RT_WRAP)) return 0;
    dlg_edtext(&g, DWR_WRAP, wc);
    g.cancel = DWR_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DWR_OK) return 0;
    v = atoi(wc);
    PS(d).wrapcol = (short)(v > 250 ? 250 : v);
    d->dirty = 1;
    return 1;
}

int dlg_size(int *size)
{
    static char sz[4];
    DLG g;
    int v;
    sprintf(sz, "%d", *size);
    if (!dlg_load(&g, RT_SIZE)) return 0;
    dlg_edtext(&g, DSZ_SIZE, sz);
    g.cancel = DSZ_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DSZ_OK) return 0;
    v = atoi(sz);
    if (v <= 0) return 0;
    *size = v < 4 ? 4 : v > 150 ? 150 : v;         /* out of range: the nearest size there is */
    return 1;
}

/* ================================================================= */
/*  Progress gauge                                                    */
/* ================================================================= */

static DLG  *ggp;                     /* while the gauge is up */
#define gg (*ggp)
static short g_bar, g_fill, g_txt, g_open, g_btn = -1, g_want_cancel, g_stop;
static int   g_last;
static char  g_text[32];

static short g_wh = -1;
static char  g_title[80];

static void gauge_draw(const GRECT *area)
{
    GRECT r, a = *area;
    short x1, y1, x2, y2;
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    wind_get(g_wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        x1 = r.g_x > a.g_x ? r.g_x : a.g_x;
        y1 = r.g_y > a.g_y ? r.g_y : a.g_y;
        x2 = r.g_x + r.g_w < a.g_x + a.g_w ? r.g_x + r.g_w : a.g_x + a.g_w;
        y2 = r.g_y + r.g_h < a.g_y + a.g_h ? r.g_y + r.g_h : a.g_y + a.g_h;
        if (x2 > x1 && y2 > y1) objc_draw(gg.o, 0, 8, x1, y1, x2 - x1, y2 - y1);
        wind_get(g_wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
}

/* the gauge's own window messages; others go to their windows */
int gauge_msg(short *msg)
{
    GRECT a;
    if (g_wh < 0 || msg[3] != g_wh) return 0;
    switch (msg[0]) {
    case WM_REDRAW:
        a.g_x = msg[4]; a.g_y = msg[5]; a.g_w = msg[6]; a.g_h = msg[7];
        gauge_draw(&a);
        break;
    case WM_TOPPED:
        wind_set(g_wh, WF_TOP, 0, 0, 0, 0);
        break;
    case WM_MOVED: {
        short w, h;
        winpos_clamp(&msg[4], &msg[5], msg[6], msg[7]);
        wind_set(g_wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
        wind_get(g_wh, WF_WORKXYWH, &gg.o[0].ob_x, &gg.o[0].ob_y, &w, &h);
        gg.x = gg.o[0].ob_x; gg.y = gg.o[0].ob_y;
        break;
    }
    }
    return 1;
}

/* a progress window with a bar and a percentage, centred over the given
 * area (or where it was last time) */
void gauge_open(const char *title, const GRECT *over)
{
    short x, y, w, h, root, wx, wy, ww, wh;
    root = 0;
    if (!ggp && (ggp = (DLG *)calloc(1, sizeof(DLG))) == NULL) { g_want_cancel = 0; return; }
    if (!dlg_load(&gg, g_want_cancel ? RT_GAUGEC : RT_GAUGE)) { g_want_cancel = 0; return; }
    /* the tree: the bar (a white box), its black fill, the percentage, Cancel */
    g_bar = DGA_BAR; g_fill = DGA_FILL; g_txt = DGA_PCT;
    g_btn = g_want_cancel ? DGC_CANCEL : -1;
    strcpy(g_text, "  0%");
    dlg_str(&gg, g_txt, g_text);
    gg.framed = 0;
    gg.o[g_fill].ob_width = 0;
    gg.o[g_fill].ob_height = gg.o[g_bar].ob_height;
    g_want_cancel = 0;
    g_stop = 0;
    dlg_tree_end(&gg);
    gg.o[g_bar].ob_flags &= ~OF_FL3DMASK;
    (void)root;
    g_last = -1;
    g_open = 1;
    /* a long job for a document: its name in the title, over its window */
    if (ui_doc && ui_doc->used && ui_doc->wh > 0) {
        static GRECT dov;
        sprintf(g_title, " %.30s - %.30s ", title, ui_doc->base);
        if (!over && doc_area(ui_doc, &dov)) over = &dov;
    } else sprintf(g_title, " %.50s ", title);
    gg.o[0].ob_state &= ~OS_OUTLINED;
    gg.o[0].ob_spec.index &= ~0x00FF0000L;
    w = gg.o[0].ob_width; h = gg.o[0].ob_height;
    wind_calc(WC_BORDER, NAME | MOVER, 0, 0, w, h, &wx, &wy, &ww, &wh);
    if (over || !winpos_get(" progress ", &x, &y, NULL, NULL)) {
        x = (short)(desk.g_x + (desk.g_w - ww) / 2);
        y = (short)(desk.g_y + (desk.g_h - wh) / 2);
        if (over && over->g_w > ww && over->g_h > wh) {
            x = (short)(over->g_x + (over->g_w - ww) / 2);
            y = (short)(over->g_y + (over->g_h - wh) / 2);
        }
    }
    winpos_clamp(&x, &y, ww, wh);
    g_wh = win_dialogs ? wind_create(NAME | MOVER, x, y, ww, wh) : -1;
    if (g_wh >= 0) {
        wind_set_str(g_wh, WF_NAME, g_title);
        wind_open(g_wh, x, y, ww, wh);
        wind_get(g_wh, WF_WORKXYWH, &gg.o[0].ob_x, &gg.o[0].ob_y, &w, &h);
        gg.x = gg.o[0].ob_x; gg.y = gg.o[0].ob_y; gg.w = w; gg.h = h;
        edlog("gauge: window %d at %d,%d", g_wh, x, y);
        pump_redraws();
        busy_ptr(1);
        return;
    }
    /* no window: a box drawn over the screen, as before */
    gg.o[0].ob_state |= OS_OUTLINED;
    gg.o[0].ob_spec.index |= 0x00020000L;
    form_center(gg.o, &x, &y, &w, &h);
    gg.x = x; gg.y = y; gg.w = w; gg.h = h;
    edlog("gauge: at %d,%d size %dx%d, drawing", x, y, w, h);
    wind_update(BEG_UPDATE);
    form_dial(FMD_START, 0, 0, 0, 0, x, y, w, h);
    graf_mouse(M_OFF, NULL);
    objc_draw(gg.o, 0, 8, x, y, w, h);
    graf_mouse(M_ON, NULL);
    busy_ptr(1);
    edlog("gauge: drawn");
}

void gauge_set(long done, long total)
{
    int pct;
    if (!g_open) return;
    busy_ptr_track();
    pct = total > 0 ? (int)(done * 100L / total) : 0;
    if (pct > 100) pct = 100;
    if (g_wh >= 0) pump_redraws();                  /* keep the other windows drawn */
    if (pct == g_last) return;
    g_last = pct;
    edlog("gauge: %d%% (%ld of %ld)", pct, done, total);
    {
        /* only what changed: the new piece of the bar and the
         * percentage. Drawing the whole gauge again cleared the bar to
         * white and filled it black each time, and with a screen of
         * several planes drawn a plane at a time that flashed colours. */
        GRECT a[2];
        short ow = gg.o[g_fill].ob_width, nw = (short)((long)gg.o[g_bar].ob_width * pct / 100), fx, fy, i;
        gg.o[g_fill].ob_width = nw;
        sprintf(g_text, "%3d%%", pct);
        objc_offset(gg.o, g_fill, &fx, &fy);
        if (nw >= ow) { a[0].g_x = (short)(fx + ow); a[0].g_w = (short)(nw - ow); }
        else { a[0].g_x = (short)(fx + nw); a[0].g_w = (short)(ow - nw); }
        a[0].g_y = fy; a[0].g_h = gg.o[g_fill].ob_height;
        objc_offset(gg.o, g_txt, &a[1].g_x, &a[1].g_y);
        a[1].g_w = gg.o[g_txt].ob_width; a[1].g_h = gg.o[g_txt].ob_height;
        for (i = 0; i < 2; i++) {
            if (a[i].g_w <= 0 || a[i].g_h <= 0) continue;
            if (g_wh >= 0) gauge_draw(&a[i]);
            else {
                graf_mouse(M_OFF, NULL);
                objc_draw(gg.o, 0, 8, a[i].g_x, a[i].g_y, a[i].g_w, a[i].g_h);
                graf_mouse(M_ON, NULL);
            }
        }
    }
#ifdef EDDEMO
    if (demo_on && pct >= 40 && pct < 60) demo_dialog(gg.o, gg.x, gg.y, gg.w, gg.h);
#endif
}

/* the next gauge has a Cancel button (see gauge_cancelled) */
void gauge_cancelable(void) { g_want_cancel = 1; }

/* has Cancel been clicked (or Esc or Undo pressed) since the gauge opened?
 * Called between pieces of the work; it doesn't wait. */
int gauge_cancelled(void)
{
    short mx, my, mb, ks, key, br, ev, msg[8];
    if (!g_open || g_btn < 0) return 0;
    if (g_stop) return 1;
    ev = evnt_multi(MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    msg, 0L, &mx, &my, &mb, &ks, &key, &br);
    if ((ev & MU_KEYBD) && ((key & 0xFF) == 27 || (key >> 8) == 0x61)) g_stop = 1;
    if ((ev & MU_BUTTON) && objc_find(gg.o, g_btn, 0, mx, my) == g_btn) g_stop = 1;
    if (g_stop) {
        GRECT a;
        gg.o[g_btn].ob_state |= OS_SELECTED;
        a.g_x = gg.x; a.g_y = gg.y; a.g_w = gg.w; a.g_h = gg.h;
        if (g_wh >= 0) gauge_draw(&a);
        else { graf_mouse(M_OFF, NULL); objc_draw(gg.o, 0, 8, gg.x, gg.y, gg.w, gg.h); graf_mouse(M_ON, NULL); }
        edlog("gauge: cancelled");
    }
    return g_stop;
}

void gauge_close(void)
{
    if (!g_open) return;
    g_open = 0;
    busy_ptr(0);
    if (g_wh >= 0) {
        winpos_save(" progress ", g_wh);
        wind_close(g_wh);
        wind_delete(g_wh);
        g_wh = -1;
    } else {
        form_dial(FMD_FINISH, 0, 0, 0, 0, gg.x, gg.y, gg.w, gg.h);
        wind_update(END_UPDATE);
    }
    free(ggp);
    ggp = NULL;
    edlog("gauge: closed");
}

/* ---- a long job: the gauge comes up only when it takes a while ---- */

static const char *b_title;
static clock_t b_t0;
static int b_shown, b_depth;

static void busy_hook(long v, long total)
{
    busy_ptr_track();
    if (!b_shown) {
        /* not for what is over in a moment, or nearly done already */
        if (clock() - b_t0 < CLOCKS_PER_SEC / 3 || v > 600) return;
        pump_defer = 1;
        gauge_open(b_title, NULL);
        b_shown = 1;
    }
    gauge_set(v, total);
}

/* ---- the busy bee: over EdWord's own windows only -------------------- */

static int  bz_on, bz_shown = -1;
static clock_t bz_t;

/* is (x, y) in one of EdWord's windows? (not the desktop, the menu bar or
 * another program's window) */
static int ours_at(short x, short y)
{
    short h = wind_find(x, y), owner, d1, d2, d3;
    if (h <= 0) return 0;
    if (aes_global[0] >= 0x400 && wind_get(h, WF_OWNER, &owner, &d1, &d2, &d3) && owner != ap_id) return 0;
    return 1;
}

/* the pointer as it should be now: the bee where EdWord is busy, the
 * arrow elsewhere; called as the work goes on */
void busy_ptr_track(void)
{
    short mx, my, mb, ks;
    int want = 0;
    if (bz_on) {
        if (bz_shown >= 0 && (long)(clock() - bz_t) < CLOCKS_PER_SEC / 20) return;   /* not too often */
        bz_t = clock();
        graf_mkstate(&mx, &my, &mb, &ks);
        want = ours_at(mx, my);
    }
    if (want == bz_shown) return;
    bz_shown = want;
    graf_mouse(want ? BUSYBEE : ARROW, NULL);
}

void busy_ptr(int on)
{
    bz_on = on;
    bz_shown = bz_on ? -1 : bz_shown;
    if (!on && bz_shown != 0) { bz_shown = 0; graf_mouse(ARROW, NULL); return; }
    busy_ptr_track();
}

void busy_begin(const char *title)
{
    if (b_depth++) return;
    b_title = title;
    b_t0 = clock();
    b_shown = 0;
    prog_part(0, 1000);
    prog_hook = busy_hook;
    busy_ptr(1);
}

void busy_end(void)
{
    if (!b_depth || --b_depth) return;
    prog_hook = NULL;
    if (b_shown) { gauge_close(); pump_defer = 0; pump_flush(); }
    b_shown = 0;
    busy_ptr(0);
}

/* ================================================================= */
/*  Preferences                                                       */
/* ================================================================= */

/* ================================================================= */
/*  Headers and footers                                               */
/* ================================================================= */

int dlg_headfoot(DOC *d)
{
    static char hb[2][57], sz[3];
    DLG g;
    int i, k, v;

    for (i = 0; i < 2; i++) {
        strncpy(hb[i], HF(d).text[i], 56);
        hb[i][56] = 0;
    }
    sprintf(sz, "%d", HF(d).size);
    if (!dlg_load(&g, RT_HEADFOOT)) return 0;
    dlg_edtext(&g, DHF_TEXT0, hb[0]);
    dlg_edtext(&g, DHF_TEXT1, hb[1]);
    for (k = 0; k < 3; k++) {
        dlg_set(&g, (short)(DHF_J00 + k), HF(d).just[0] == k);
        dlg_set(&g, (short)(DHF_J10 + k), HF(d).just[1] == k);
    }
    dlg_edtext(&g, DHF_SIZE, sz);
    dlg_set(&g, DHF_FIRST, HF(d).first);
    g.cancel = DHF_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DHF_OK) return 0;
    for (i = 0; i < 2; i++) {
        strcpy(HF(d).text[i], hb[i]);
        for (k = 0; k < 3; k++) if (dlg_on(&g, (short)((i ? DHF_J10 : DHF_J00) + k))) HF(d).just[i] = (unsigned char)k;
    }
    v = atoi(sz);
    HF(d).size = (unsigned char)(v < 4 ? 4 : v > 150 ? 150 : v);
    HF(d).first = (unsigned char)dlg_on(&g, DHF_FIRST);
    d->dirty = 1;
    return 1;
}

/* ================================================================= */
/*  Paragraph indents                                                 */
/* ================================================================= */

int dlg_indents(DOC *d, int *left, int *first)
{
    static char lb[4], fb[5];
    DLG g;
    int l, f;
    (void)d;
    sprintf(lb, "%d", pt2mm(*left));
    sprintf(fb, "%d", pt2mm(*first));
    if (!dlg_load(&g, RT_INDENTS)) return 0;
    dlg_edtext(&g, DIN_LEFT, lb);
    dlg_edtext(&g, DIN_FIRST, fb);
    g.cancel = DIN_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DIN_OK) return 0;
    l = field_pt(lb, *left);
    f = field_pt(fb, *first);
    if (l < 0) l = 0;
    if (l > 400) l = 400;
    if (f < -l) f = -l;
    if (f > 400) f = 400;
    *left = l;
    *first = f;
    return 1;
}

/* ================================================================= */
/*  Tables                                                            */
/* ================================================================= */

/* insert: rows, columns, border; properties: also the column widths.
 * 1: OK; 0: cancelled; or (Table Settings) the command of the row,
 * column or table button pressed */
int dlg_table(DOC *d, int insert, int *rows, int *cols, short *w, int *border)
{
    static char rb[4], cb[3], wb[MAX_COLS][4];
    DLG g;
    short i, ok;
    int n;
    (void)d;
    sprintf(rb, "%d", *rows);
    sprintf(cb, "%d", *cols);
    if (!dlg_load(&g, insert ? RT_TBLINS : RT_TBLPROP)) return 0;
    if (insert) {
        dlg_edtext(&g, DTI_ROWS, rb); dlg_edtext(&g, DTI_COLS, cb);
        dlg_set(&g, DTI_GRID, *border);
        g.cancel = DTI_CANCEL; ok = DTI_OK;
    } else {
        dlg_edtext(&g, DTP_ROWS, rb); dlg_edtext(&g, DTP_COLS, cb);
        for (i = 0; i < MAX_COLS; i++) {
            /* a field for each column there is */
            if (i < *cols) { sprintf(wb[i], "%d", pt2mm(w[i])); dlg_edtext(&g, (short)(DTP_W0 + i), wb[i]); }
            else dlg_hide(&g, (short)(DTP_W0 + i));
        }
        dlg_set(&g, DTP_GRID, *border);
        g.cancel = DTP_CANCEL; ok = DTP_OK;
    }
    dlg_tree_end(&g);
    i = dlg_run(&g);
    if (!insert) {                          /* a row, a column, the table */
        static const short ob[5] = { DTP_ROWINS, DTP_ROWDEL, DTP_COLINS, DTP_COLDEL, DTP_TBLDEL };
        static const short cm[5] = { C_ROWINS, C_ROWDEL, C_COLINS, C_COLDEL, C_TBLDEL };
        for (n = 0; n < 5; n++) if (i == ob[n]) return cm[n];
    }
    if (i != ok) return 0;
    n = atoi(rb);
    *rows = n < 1 ? 1 : n > 200 ? 200 : n;
    n = atoi(cb);
    n = n < 1 ? 1 : n > MAX_COLS ? MAX_COLS : n;
    if (!insert)
        for (i = 0; i < *cols && i < MAX_COLS; i++) {
            int v = field_pt(wb[i], w[i]);
            w[i] = (short)(v < 12 ? 12 : v);
        }
    *cols = n;
    *border = dlg_on(&g, insert ? DTI_GRID : DTP_GRID);
    return 1;
}

/* ================================================================= */
/*  Picture and drawing properties                                    */
/* ================================================================= */

/* returns 0 cancel, 1 OK, 2 delete, 3 edit the drawing (the object
 * numbers of the picture's dialog and the drawing's are the same up to
 * their buttons) */
int dlg_objprops(DOC *d, OBJ *o)
{
    static char xb[5], yb[5], wb[5], hb[5], pb[5];
    DLG g;
    short ok, del, edt = -1, i, shape = -1;
    int v, drw = o->type == OB_DRAW;
    char x0[5], y0[5], w0[5], h0[5];
    sprintf(xb, "%d", pt2mm(o->x / TWIPS)); sprintf(yb, "%d", pt2mm(o->y / TWIPS));
    sprintf(wb, "%d", pt2mm(o->w / TWIPS)); sprintf(hb, "%d", pt2mm(o->h / TWIPS));
    strcpy(x0, xb); strcpy(y0, yb); strcpy(w0, wb); strcpy(h0, hb);
    sprintf(pb, "%d", o->page + 1);
    if (!dlg_load(&g, drw ? RT_DRAWPROPS : RT_PICPROPS)) return 0;
    if (drw) {
        dlg_edtext(&g, DDP_X, xb); dlg_edtext(&g, DDP_Y, yb); dlg_edtext(&g, DDP_W, wb); dlg_edtext(&g, DDP_H, hb);
        dlg_edtext(&g, DDP_PAGE, pb);
        dlg_set(&g, DDP_WNONE, o->wrap == WR_NONE);
        dlg_set(&g, DDP_WBOX, o->wrap == WR_BOX);
        dlg_set(&g, shape = DDP_WSHAPE, o->wrap == WR_SHAPE);
        dlg_set(&g, DDP_FRAME, o->border);
        del = DDP_DEL; edt = DDP_EDIT; ok = DDP_OK; g.cancel = DDP_CANCEL;
    } else {
        dlg_edtext(&g, DPP_X, xb); dlg_edtext(&g, DPP_Y, yb); dlg_edtext(&g, DPP_W, wb); dlg_edtext(&g, DPP_H, hb);
        dlg_edtext(&g, DPP_PAGE, pb);
        dlg_set(&g, DPP_WNONE, o->wrap == WR_NONE);
        dlg_set(&g, DPP_WBOX, o->wrap != WR_NONE);
        dlg_set(&g, DPP_FRAME, o->border);
        del = DPP_DEL; ok = DPP_OK; g.cancel = DPP_CANCEL;
    }
    dlg_tree_end(&g);
    i = dlg_run(&g);
    if (i == del) return 2;
    if (i != ok && i != edt) return 0;
    /* only what was changed: the rest keeps its exact position */
    if (strcmp(xb, x0)) o->x = field_pt(xb, o->x / TWIPS) * (long)TWIPS;
    if (strcmp(yb, y0)) o->y = field_pt(yb, o->y / TWIPS) * (long)TWIPS;
    if (strcmp(wb, w0)) { v = field_pt(wb, o->w / TWIPS); o->w = (v < 6 ? 6 : v) * (long)TWIPS; }
    if (strcmp(hb, h0)) { v = field_pt(hb, o->h / TWIPS); o->h = (v < 6 ? 6 : v) * (long)TWIPS; }
    v = atoi(pb) - 1;
    o->page = (short)(v < 0 ? 0 : v > doc_npages(d) ? doc_npages(d) : v);
    o->wrap = dlg_on(&g, drw ? DDP_WNONE : DPP_WNONE) ? WR_NONE : shape >= 0 && dlg_on(&g, shape) ? WR_SHAPE : WR_BOX;
    o->border = (short)dlg_on(&g, drw ? DDP_FRAME : DPP_FRAME);
    return i == edt ? 3 : 1;
}

/* ================================================================= */
/*  Document summary and About                                        */
/* ================================================================= */

void dlg_summary(DOC *d)
{
    static char v[8][16], nm[48];
    DLG g;
    DSTATS s;
    int i;
    long val[8];
    doc_layout(d);
    doc_stats(d, &s);
    val[0] = s.pages; val[1] = s.words; val[2] = s.chars; val[3] = s.chars_ns;
    val[4] = s.paras; val[5] = s.lines; val[6] = s.tables; val[7] = s.objects;
    if (!dlg_load(&g, RT_SUMMARY)) return;
    strncpy(nm, d->base, sizeof nm - 1);
    dlg_str(&g, DSU_NAME, nm);
    for (i = 0; i < 8; i++) {
        static const short vo[8] = { DSU_V0, DSU_V1, DSU_V2, DSU_V3, DSU_V4, DSU_V5, DSU_V6, DSU_V7 };
        sprintf(v[i], "%8ld", val[i]);
        dlg_str(&g, vo[i], v[i]);
    }
    g.cancel = DSU_OK;
    dlg_tree_end(&g);
    dlg_run(&g);
}

void dlg_about(void)
{
    static char vline[60];
    DLG g;
    if (!dlg_load(&g, RT_ABOUT)) return;
#ifdef ED_CPU
    sprintf(vline, "%s %s (%s)", L(S_VERSION), ED_VERSION, ED_CPU);
#else
    sprintf(vline, "%s %s", L(S_VERSION), ED_VERSION);
#endif
    dlg_str(&g, DAB_VERSION, vline);
    if (ED_ABOUT[0]) dlg_str(&g, DAB_LINE1, (char *)ED_ABOUT);
    {   /* the memory free now: the biggest blocks of ST RAM and of Fast RAM */
        static char mline[90];
#ifndef PCGEM
        long st, fast = 0;
        if (Sversion() >= 0x1900) {          /* GEMDOS 0.19 and later: Mxalloc */
            st = (long)Mxalloc(-1L, 0);
            fast = (long)Mxalloc(-1L, 1);
            if (fast < 0) fast = 0;
        } else st = (long)Malloc(-1L);
        if (st < 0) st = 0;
        if (fast > 0) sprintf(mline, L(S_FREE_RAM2), (st + fast) / 1024, st / 1024, fast / 1024);
        else sprintf(mline, L(S_FREE_RAM), st / 1024);
#else
        mline[0] = 0;
#endif
        dlg_str(&g, DAB_MEMORY, mline);
    }
    g.cancel = DAB_OK;
    dlg_tree_end(&g);
    dlg_run(&g);
}

/* ================================================================= */
/*  Text import / export options                                      */
/* ================================================================= */

int dlg_textimport(int eol, int det, int *cs, int *join)
{
    static const int eols[] = { S_EOL_NONE, S_EOL_LF, S_EOL_CRLF, S_EOL_CR, S_EOL_MIXED };
    static const char *css[] = { "", "Atari ST", "Windows", "UTF-8", "UTF-16" };
    static char info[90];
    DLG g;
    int i;

    sprintf(info, L(S_FOUND), L(eols[eol]), css[det]);
    if (!dlg_load(&g, RT_TEXTIMP)) return 0;
    dlg_str(&g, DTXI_INFO, info);
    dlg_set(&g, DTXI_CS0, 1);
    dlg_set(&g, DTXI_ONE, 1);
    g.cancel = DTXI_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DTXI_OK) return 0;
    *cs = CS_AUTO;
    for (i = 0; i < 5; i++) if (dlg_on(&g, (short)(DTXI_CS0 + i))) *cs = i;
    *join = dlg_on(&g, DTXI_JOIN);
    return 1;
}

int dlg_textexport(int *crlf, int *cs)
{
    DLG g;
    int i;
    if (!dlg_load(&g, RT_TEXTEXP)) return 0;
    dlg_set(&g, DTXE_DOS, 1);
    dlg_set(&g, DTXE_CS0, 1);
    g.cancel = DTXE_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DTXE_OK) return 0;
    *crlf = dlg_on(&g, DTXE_DOS);
    *cs = CS_ATARI;
    for (i = 0; i < 4; i++) if (dlg_on(&g, (short)(DTXE_CS0 + i))) *cs = i + 1;
    return 1;
}

/* ================================================================= */
/*  Go to page and Print                                              */
/* ================================================================= */

int dlg_goto(DOC *d, int *page)
{
    static char pb[5];
    static char of[40];
    DLG g;
    int v;
    sprintf(pb, "%d", *page + 1);
    of[0] = ' ';
    sprintf(of + 1, L(S_OF_N), doc_npages(d));
    if (!dlg_load(&g, RT_GOTO)) return 0;
    dlg_edtext(&g, DGT_PAGE, pb);
    dlg_str(&g, DGT_OF, of);
    g.cancel = DGT_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DGT_OK) return 0;
    v = atoi(pb);
    if (v < 1) v = 1;
    if (v > doc_npages(d)) v = doc_npages(d);
    *page = v - 1;
    return 1;
}

/* pages from..to (1-based, inclusive) and copies */
int dlg_print(DOC *d, const char *printer, unsigned char *sel, int *copies, int *colour)
{
    DLG g;
    static char fb[5], tb[5], lb[41], cb[3], pr[60], cur[40];
    int np = doc_npages(d), a, b, c, k, page = d->nlines ? d->lines[doc_find_line(d, d->cursor)].page : 0;
    sprintf(fb, "%d", 1);
    sprintf(tb, "%d", np);
    if (!lb[0]) strcpy(lb, "1");
    sprintf(cb, "%d", *copies > 0 ? *copies : 1);
    sprintf(pr, "%.56s", printer);
    sprintf(cur, "%s (%d)", L(S_CUR_PAGE), page + 1);
    if (!dlg_load(&g, RT_PRINT)) return 0;
    dlg_str(&g, PR_PRINTER, pr);
    dlg_str(&g, PR_CUR, cur);
    dlg_edtext(&g, PR_FROM, fb);
    dlg_edtext(&g, PR_TO, tb);
    dlg_edtext(&g, PR_LISTED, lb);
    dlg_edtext(&g, PR_COPIES, cb);
    dlg_set(&g, PR_ALL, 1);
    dlg_set(&g, PR_COLOUR, *colour);
    dlg_set(&g, PR_MONO, !*colour);
    g.cancel = PR_CANCEL;
    dlg_tree_end(&g);
    for (;;) {
        if (dlg_run(&g) != PR_OK) return 0;
        memset(sel, 0, (size_t)np);
        if (dlg_on(&g, PR_ALL)) memset(sel, 1, (size_t)np);
        else if (dlg_on(&g, PR_CUR)) sel[page < np ? page : np - 1] = 1;
        else if (dlg_on(&g, PR_LIST)) page_list(lb, np, sel);
        else {
            a = atoi(fb); b = atoi(tb);
            if (a < 1) a = 1;
            if (b > np) b = np;
            for (k = a; k <= b; k++) sel[k - 1] = 1;
        }
        for (k = 0; k < np && !sel[k]; k++) ;
        if (k < np) break;
        alert(1, 1, AL_NO_PAGES, AB_OK);
    }
    c = atoi(cb);
    *copies = c < 1 ? 1 : c > 99 ? 99 : c;
    *colour = dlg_on(&g, PR_COLOUR);
    return 1;
}

/* Insert > Page Numbers: a page number in the header or footer */
int dlg_pagenum(DOC *d)
{
    DLG g;
    static char pw[34], ow[24];
    short r;
    int where = 1, style = 1, just = 1, i;
    if (hf_pagenum_find(d, &where, &style)) just = HF(d).just[where];
    if (!dlg_load(&g, RT_PAGENUM)) return 0;
    dlg_set(&g, DPN_TOP, where == 0);
    dlg_set(&g, DPN_BOTTOM, where == 1);
    for (i = 0; i < 3; i++) dlg_set(&g, (short)(DPN_J0 + i), just == i);
    for (i = 0; i < NPNSTYLES; i++) dlg_set(&g, (short)(DPN_S0 + i), style == i);
    dlg_set(&g, DPN_FIRST, HF(d).first);
    g.cancel = DPN_CANCEL;
    dlg_tree_end(&g);
    r = dlg_run(&g);
    if (r == DPN_REMOVE) { hf_pagenum_clear(d); return 1; }
    if (r != DPN_OK) return 0;
    where = dlg_on(&g, DPN_TOP) ? 0 : 1;
    for (i = 0; i < 3; i++) if (dlg_on(&g, (short)(DPN_J0 + i))) just = i;
    for (i = 0; i < NPNSTYLES; i++) if (dlg_on(&g, (short)(DPN_S0 + i))) style = i;
    strncpy(pw, L(S_PN_PAGE), sizeof pw - 1);
    strncpy(ow, L(S_PN_OF), sizeof ow - 1);
    hf_pagenum_set(d, where, just, style, pw, ow);
    HF(d).first = (unsigned char)dlg_on(&g, DPN_FIRST);
    d->dirty = 1;
    return 1;
}

/* ================================================================= */
/*  New from Template                                                 */
/* ================================================================= */

int dlg_template(int *kind)
{
    static char nm[41], ad[3][41], ti[41];
    DLG g;
    int i;
    strncpy(nm, tpl_name, 40);
    for (i = 0; i < 3; i++) strncpy(ad[i], tpl_addr[i], 40);
    strncpy(ti, tpl_title, 40);
    if (!dlg_load(&g, RT_TEMPLATE)) return 0;
    for (i = 0; i < NTPL; i++) dlg_set(&g, (short)(DTM_K0 + i), *kind == i);
    dlg_edtext(&g, DTM_NAME, nm);
    dlg_edtext(&g, DTM_A0, ad[0]); dlg_edtext(&g, DTM_A1, ad[1]); dlg_edtext(&g, DTM_A2, ad[2]);
    dlg_edtext(&g, DTM_NTITLE, ti);
    g.cancel = DTM_CANCEL;
    dlg_tree_end(&g);
    if (dlg_run(&g) != DTM_OK) return 0;
    for (i = 0; i < NTPL; i++) if (dlg_on(&g, (short)(DTM_K0 + i))) *kind = i;
    strcpy(tpl_name, nm);
    for (i = 0; i < 3; i++) strcpy(tpl_addr[i], ad[i]);
    strcpy(tpl_title, ti);
    return 1;
}

/* ================================================================= */
/*  Colour theme                                                      */
/* ================================================================= */

/* (the colour theme is a window of its own: gemtwin.c) */
