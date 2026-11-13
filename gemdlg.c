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
static void pick_shortcuts(DLG *g)
{
    char used[64];
    int nu = 0, i, k;
    used[0] = 0;
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        it->ul = -1;
        if (it->type != T_BUTTON && it->type != T_BIG && it->type != T_CHECK && it->type != T_RADIO) continue;
        for (k = 0; it->s[k]; k++) {
            int c = upc((unsigned char)it->s[k]);
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80)) continue;
            if (memchr(used, c, nu)) continue;
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
                g->te[g->nte] = *o->ob_spec.tedinfo;      /* a copy: the text goes where the caller says */
                o->ob_spec.tedinfo = &g->te[g->nte++];
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

/* ---- running a dialog ------------------------------------------------- */

/* Dialogs open in windows of their own, so the documents and the other
 * windows stay usable while a dialog is open; menus and documents work
 * as usual. One dialog is open at a time: asking for another brings the
 * open one to the front. Without a free window the dialog runs the old
 * way, with form_do. */
static DLG  *open_dlg;
static short dlg_wh = -1;
int dlg_busy;

void dlg_front(void) { if (dlg_busy && dlg_wh >= 0) wind_set(dlg_wh, WF_TOP, 0, 0, 0, 0); }

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

static int dlg_is_top(void)
{
    short h, d1, d2, d3;
    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    return h == dlg_wh;
}

static void dlg_redraw(DLG *g, const GRECT *area, short edit, short idx, int cursor)
{
    GRECT r, a = *area;
    short x1, y1, x2, y2;
    (void)idx;
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    wind_get(dlg_wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        x1 = r.g_x > a.g_x ? r.g_x : a.g_x;
        y1 = r.g_y > a.g_y ? r.g_y : a.g_y;
        x2 = r.g_x + r.g_w < a.g_x + a.g_w ? r.g_x + r.g_w : a.g_x + a.g_w;
        y2 = r.g_y + r.g_h < a.g_y + a.g_h ? r.g_y + r.g_h : a.g_y + a.g_h;
        if (x2 > x1 && y2 > y1) {
            eddiag("  objc_draw %d,%d %dx%d", x1, y1, x2 - x1, y2 - y1);
            objc_draw(g->o, 0, 8, x1, y1, x2 - x1, y2 - y1);
            eddiag("  drawn");
        }
        wind_get(dlg_wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
    (void)edit; (void)cursor;
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

short dlg_run(DLG *g)
{
    short x, y, w, h, wx, wy, ww, wh, ex = -1, edit, idx = 0, next, cont, kind = NAME | MOVER | CLOSER;
    short msg[8], mx, my, mb, ks, key, br, ev, title = -1, i, dy = 0, st0;
    long sp0;
    int cur_on = 0;
    const char *tname = NULL;
    static char wtitle[60];

    eddiag("dlg_run: busy %d, windows %d, 3D %d, MagiC %d, %d objects, %d items, root %d,%d %dx%d",
           dlg_busy, win_dialogs, aes_3d, aes_magic, g->n, g->nit, g->o[0].ob_x, g->o[0].ob_y, g->o[0].ob_width, g->o[0].ob_height);
#ifdef EDDIAG
    for (i = 0; i < g->n; i++)
        eddiag("  obj %d: type %x flags %x state %x spec %lx next %d head %d tail %d at %d,%d %dx%d", i, g->o[i].ob_type,
               g->o[i].ob_flags, g->o[i].ob_state, (long)g->o[i].ob_spec.index, g->o[i].ob_next, g->o[i].ob_head, g->o[i].ob_tail,
               g->o[i].ob_x, g->o[i].ob_y, g->o[i].ob_width, g->o[i].ob_height);
#endif
    if (dlg_busy) { dlg_front(); return -1; }
    if (!win_dialogs) return dlg_modal(g);
    st0 = g->o[0].ob_state; sp0 = g->o[0].ob_spec.index;
    /* the title goes into the window's title bar */
    for (i = 0; i < g->nit; i++) if (g->it[i].type == T_TITLE) { title = g->it[i].obj; tname = g->it[i].s; }
    if (title > 0) {
        dy = (short)(hchar + hchar / 2);
        g->o[title].ob_flags |= OF_HIDETREE;
        for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) if (i != title) g->o[i].ob_y -= dy;
        g->o[0].ob_height -= dy;
    }
    g->o[0].ob_state &= ~OS_OUTLINED;
    g->o[0].ob_spec.index &= ~0x00FF0000L;            /* no frame: the window has one */
    /* a little room above the first row and under the buttons */
    for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) g->o[i].ob_y += hchar / 4;
    g->o[0].ob_height += hchar / 2 + hchar / 4;
    w = g->o[0].ob_width; h = g->o[0].ob_height;
    eddiag("dlg_run: title %d, work %dx%d; wind_calc ...", title, w, h);
    wind_calc(WC_BORDER, kind, 0, 0, w, h, &wx, &wy, &ww, &wh);
    eddiag("dlg_run: ... border %d,%d %dx%d", wx, wy, ww, wh);
    sprintf(wtitle, " %.50s ", tname ? tname : "EdWord");
    if (!winpos_get(wtitle, &x, &y, NULL, NULL)) {
        x = (short)(desk.g_x + (desk.g_w - ww) / 2);
        y = (short)(desk.g_y + (desk.g_h - wh) / 2);
    }
    winpos_clamp(&x, &y, ww, wh);
    eddiag("dlg_run: at %d,%d (desk %d,%d %dx%d); wind_create ...", x, y, desk.g_x, desk.g_y, desk.g_w, desk.g_h);
    dlg_wh = wind_create(kind, x, y, ww, wh);
    eddiag("dlg_run: wind_create %d at %d,%d %dx%d", dlg_wh, x, y, ww, wh);
    if (dlg_wh < 0) {                                  /* no window to spare */
        for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) g->o[i].ob_y -= hchar / 4;
        g->o[0].ob_height -= hchar / 2 + hchar / 4;
        if (title > 0) {
            g->o[title].ob_flags &= ~OF_HIDETREE;
            for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) if (i != title) g->o[i].ob_y += dy;
            g->o[0].ob_height += dy;
        }
        g->o[0].ob_state |= OS_OUTLINED;
        g->o[0].ob_spec.index |= 0x00020000L;
        return dlg_modal(g);
    }
    wind_set_str(dlg_wh, WF_NAME, wtitle);
    wind_open(dlg_wh, x, y, ww, wh);
    eddiag("dlg_run: opened");
    wind_get(dlg_wh, WF_WORKXYWH, &g->o[0].ob_x, &g->o[0].ob_y, &w, &h);
    g->x = g->o[0].ob_x; g->y = g->o[0].ob_y;
    open_dlg = g;
    dlg_busy = 1;
    edit = g->edit1 > 0 ? g->edit1 : 0;
#ifdef EDDEMO
    if (demo_on) {
        GRECT a;
        a.g_x = g->x; a.g_y = g->y; a.g_w = w; a.g_h = h;
        dlg_redraw(g, &a, 0, 0, 0);
        pump_redraws();
        ex = (short)demo_dialog(g->o, g->x, g->y, w, h);
        if (ex >= 0) goto done;
    }
#endif
    if (edit) { objc_edit(g->o, edit, 0, &idx, ED_INIT); cur_on = 1; }
    next = edit;
    for (;;) {
        eddiag("dlg_run: waiting");
        ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        msg, 0L, &mx, &my, &mb, &ks, &key, &br);
        eddiag("dlg_run: event %x, message %d for %d", ev, msg[0], msg[3]);
        if (ev & MU_MESAG) {
            if (msg[3] == dlg_wh && msg[0] >= 20 && msg[0] < 40) {
                GRECT a;
                switch (msg[0]) {
                case WM_REDRAW:
                    a.g_x = msg[4]; a.g_y = msg[5]; a.g_w = msg[6]; a.g_h = msg[7];
                    eddiag("dlg_run: redraw %d,%d %dx%d", a.g_x, a.g_y, a.g_w, a.g_h);
                    /* the text cursor is drawn by XOR: take it off first */
                    if (edit && cur_on && dlg_is_top()) { objc_edit(g->o, edit, 0, &idx, ED_END); cur_on = 0; }
                    dlg_redraw(g, &a, edit, idx, 1);
                    if (edit && dlg_is_top()) { objc_edit(g->o, edit, 0, &idx, ED_END); cur_on = 1; }
                    else cur_on = 0;
                    break;
                case WM_TOPPED:
                    wind_set(dlg_wh, WF_TOP, 0, 0, 0, 0);
                    if (edit && !cur_on) { objc_edit(g->o, edit, 0, &idx, ED_END); cur_on = 1; }
                    break;
                case WM_CLOSED:
                    ex = dlg_cancel_obj(g);
                    if (ex < 0) ex = 0;
                    goto done;
                case WM_MOVED:
                    winpos_clamp(&msg[4], &msg[5], msg[6], msg[7]);
                    wind_set(dlg_wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
                    wind_get(dlg_wh, WF_WORKXYWH, &g->o[0].ob_x, &g->o[0].ob_y, &w, &h);
                    g->x = g->o[0].ob_x; g->y = g->o[0].ob_y;
                    break;
                }
            } else handle_msg(msg);
            if (quit_now) { ex = dlg_cancel_obj(g); goto done; }
        }
        if ((ev & MU_KEYBD) && dlg_is_top()) {
            short scan = (key >> 8) & 0xff, o;
            if (scan == 0x61 || (scan == 0x01 && !edit)) {         /* Undo, or Esc: Cancel */
                o = dlg_cancel_obj(g);
                if (o > 0) { ex = o; goto done; }
            }
            if ((ks & 8) && (o = dlg_shortcut(g, alt_letter(key))) > 0) {   /* Alternate + letter */
                cont = form_button(g->o, o, 1, &next);
                if (!cont) { ex = next; goto done; }
                continue;
            }
            if (edit && !cur_on) { objc_edit(g->o, edit, 0, &idx, ED_END); cur_on = 1; }
            cont = form_keybd(g->o, edit, next, key, &next, &key);
            if (key && edit) objc_edit(g->o, edit, key, &idx, ED_CHAR);
            if (!cont) { ex = next; goto done; }
            if (next && next != edit && (g->o[next].ob_flags & OF_EDITABLE)) {
                objc_edit(g->o, edit, 0, &idx, ED_END);
                edit = next;
                objc_edit(g->o, edit, 0, &idx, ED_INIT);
            }
        } else if (ev & MU_KEYBD) handle_key(ks, key);
        if (ev & MU_BUTTON) {
            if (wind_find(mx, my) != dlg_wh) { handle_click(mx, my, ks, br); continue; }
            if (!dlg_is_top()) { wind_set(dlg_wh, WF_TOP, 0, 0, 0, 0); continue; }
            next = objc_find(g->o, 0, 8, mx, my);
            if (next < 0) continue;
            cont = form_button(g->o, next, br, &next);
            if (!cont) { ex = next; goto done; }
            if (next > 0 && next != edit && (g->o[next].ob_flags & OF_EDITABLE)) {
                if (edit && cur_on) objc_edit(g->o, edit, 0, &idx, ED_END);
                edit = next;
                objc_edit(g->o, edit, 0, &idx, ED_INIT);
                cur_on = 1;
            }
        }
    }
done:
    if (edit && cur_on && dlg_is_top()) objc_edit(g->o, edit, 0, &idx, ED_END);
    winpos_save(wtitle, dlg_wh);
    wind_close(dlg_wh);
    wind_delete(dlg_wh);
    dlg_wh = -1;
    open_dlg = NULL;
    dlg_busy = 0;
    /* the tree as it was, so the dialog can be shown again (Print after
     * an alert, Apply in the colour theme) */
    for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) g->o[i].ob_y -= hchar / 4;
    g->o[0].ob_height -= hchar / 2 + hchar / 4;
    if (title > 0) {
        g->o[title].ob_flags &= ~OF_HIDETREE;
        for (i = g->o[0].ob_head; i > 0; i = g->o[i].ob_next) if (i != title) g->o[i].ob_y += dy;
        g->o[0].ob_height += dy;
    }
    g->o[0].ob_state = st0; g->o[0].ob_spec.index = sp0;
    return dlg_finish(g, ex & 0x7fff);
}

/* ================================================================= */
/*  Alerts                                                            */
/* ================================================================= */

int alert(int def, int icon, int text, int buttons, ...)
{
    char t[300], a[400];
    va_list ap;
    va_start(ap, buttons);
    vsprintf(t, L(text), ap);
    va_end(ap);
    sprintf(a, "[%d][%s][%s]", icon, t, L(buttons));
#ifdef EDDEMO
    if (demo_on) return demo_alert() ? def : def;
#endif
    return form_alert(def, a);
}

/* ================================================================= */
/*  Popup menu                                                        */
/* ================================================================= */

/* the AES has popup menus (menu_popup): AES 4, MagiC, XaAES, MyAES... */
static int aes_popups(void)
{
    static int known = -1;
    short o1 = 0, o2 = 0, o3 = 0, o4 = 0;
    if (known >= 0) return known;
    known = 0;
    if (appl_getinfo(9, &o1, &o2, &o3, &o4) == 1) known = o2 != 0;
    else if (aes_global[0] >= 0x0400) known = 1;
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
    if (aes_global[0] >= 0x0140) ok = fsel_exinput(seldir, name, &btn, title);
    else ok = fsel_input(seldir, name, &btn);
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
    if (v < 4 || v > 120) return 0;
    *size = v;
    return 1;
}

/* ================================================================= */
/*  Progress gauge                                                    */
/* ================================================================= */

static DLG   gg;
static short g_bar, g_fill, g_txt, g_open, g_btn = -1, g_want_cancel, g_stop;
static int   g_last;
static char  g_text[32];

static short g_wh = -1;
static char  g_title[60];

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
    sprintf(g_title, " %.50s ", title);
    gg.o[0].ob_state &= ~OS_OUTLINED;
    gg.o[0].ob_spec.index &= ~0x00FF0000L;
    w = gg.o[0].ob_width; h = gg.o[0].ob_height;
    wind_calc(WC_BORDER, NAME | MOVER, 0, 0, w, h, &wx, &wy, &ww, &wh);
    if (!winpos_get(" progress ", &x, &y, NULL, NULL)) {
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

/* the second page of the preferences: how pages break, where documents go */
static void prefs_more(void)
{
    static char dd[41];
    DLG g;
    short kp[3], ok;
    int i;
    strncpy(dd, pref_docdir, 40);
    dd[40] = 0;
    if (!dlg_load(&g, RT_PREFS2)) return;
    for (i = 0; i < 3; i++) { kp[i] = (short)(PF2_KEEP0 + i); dlg_set(&g, kp[i], lay_keep == i); }
    for (i = 0; i < NLANG; i++) dlg_set(&g, PF2_DICT0 + i, spell_lang == i);
    dlg_set(&g, PF2_LIVE, pref_live);
    for (i = 0; i < 3; i++) dlg_set(&g, PF2_PDF1 + i, pdf_scale == i + 1);
    dlg_edtext(&g, PF2_DOCDIR, dd);
    g.cancel = PF2_CANCEL;
    ok = PF2_OK;
    dlg_tree_end(&g);
    if (dlg_run(&g) != ok) return;
    for (i = 0; i < 3; i++) if (dlg_on(&g, kp[i])) lay_keep = i;
    pref_live = dlg_on(&g, PF2_LIVE);
    for (i = 0; i < 3; i++) if (dlg_on(&g, PF2_PDF1 + i)) pdf_scale = i + 1;
    for (i = 0; i < NLANG; i++)
        if (dlg_on(&g, PF2_DICT0 + i) && i != spell_lang) {     /* another dictionary: loaded when next needed */
            spell_unload_main();
            spell_lang = i;
        }
    if (strncmp(dd, pref_docdir, 40)) {
        strncpy(pref_docdir, dd, sizeof pref_docdir - 1);
        docdir_changed();
    }
}

int dlg_prefs(void)
{
    static const int order[NDATEFMT_ST] = { DT_SHORT, DT_DOTS, DT_US, DT_ISO, DT_LONG, DT_DAYLONG };
    static char dl[NDATEFMT_ST][32], mins[3];
    DLG g;
    short lang[NLANG], dr[NDATEFMT_ST], pp[NPAPER], land, tbar, col, dm[4], ason, asm1, ok, more, r;
    int i, v;

    for (i = 0; i < NDATEFMT_ST; i++) fmt_now(dl[i], order[i]);
    sprintf(mins, "%d", pref_asmin);
    if (!dlg_load(&g, RT_PREFS)) return 0;
    for (i = 0; i < NLANG; i++) {
        lang[i] = (short)(PF_LANG0 + i); dlg_set(&g, lang[i], ui_lang == i);
    }
    for (i = 0; i < NDATEFMT_ST; i++) { dr[i] = (short)(PF_DATE0 + i); dlg_str(&g, dr[i], dl[i]); dlg_set(&g, dr[i], pref_date == order[i]); }
    for (i = 0; i < NPAPER; i++) { pp[i] = (short)(PF_PAPER0 + i); dlg_set(&g, pp[i], pref_paper == i); }
    dlg_set(&g, PF_PORTRAIT, !pref_land);
    dlg_set(&g, land = PF_LAND, pref_land);
    dlg_set(&g, tbar = PF_TOOLBAR, pref_toolbar);
    dlg_set(&g, col = PF_COLOURS, colour_on);
    for (i = 0; i < 4; i++) { dm[i] = (short)(PF_DISP0 + i); dlg_set(&g, dm[i], pref_display == i); }
    dlg_set(&g, ason = PF_ASON, pref_autosave);
    dlg_edtext(&g, PF_ASMIN, mins);
    dlg_set(&g, PF_ASCOPY, !pref_asmode);
    dlg_set(&g, asm1 = PF_ASDOC, pref_asmode);
    more = PF_MORE; ok = PF_OK;
    g.cancel = PF_CANCEL;
    dlg_tree_end(&g);
    r = dlg_run(&g);
    if (r != ok && r != more) return 0;

    for (i = 0; i < 4; i++) if (dlg_on(&g, dm[i])) pref_display = i;
    pref_autosave = dlg_on(&g, ason);
    v = atoi(mins);
    pref_asmin = v < 1 ? 1 : v > 99 ? 99 : v;
    pref_asmode = dlg_on(&g, asm1);
    for (i = 0; i < NDATEFMT_ST; i++) if (dlg_on(&g, dr[i])) pref_date = order[i];
    for (i = 0; i < NPAPER; i++) if (dlg_on(&g, pp[i])) pref_paper = i;
    pref_land = dlg_on(&g, land);
    pref_toolbar = dlg_on(&g, tbar);
    colour_on = dlg_on(&g, col);
    for (i = 0; i < NLANG; i++) if (dlg_on(&g, lang[i]) && i != ui_lang) set_language(i);
    if (r == more) prefs_more();        /* this page's choices kept, then the next */
    return 1;
}

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
    HF(d).size = (unsigned char)(v < 4 ? 4 : v > 72 ? 72 : v);
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
    static DLG g;
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
    static DLG g;
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

/* the theme from the popup, or each colour by hand; Apply shows it
 * without closing. 1: changed (and kept) */
/* The colour theme. On a screen without a palette it can be for the top
 * document's window alone ("This document") or for every window that has
 * none of its own ("All documents"); on a palette screen it is for all.
 * While the dialog is up, theme_id and theme_rgb hold the theme being
 * edited (the picture shows them); the one for all is kept aside. */
int dlg_theme(void)
{
    static const short fo[TH_NROLES][3] = {
        { DTH_R0, DTH_G0, DTH_B0 }, { DTH_R1, DTH_G1, DTH_B1 }, { DTH_R2, DTH_G2, DTH_B2 },
        { DTH_R3, DTH_G3, DTH_B3 }, { DTH_R4, DTH_G4, DTH_B4 }, { DTH_R5, DTH_G5, DTH_B5 } };
    static char f[TH_NROLES][3][4], pick[24];
    static DLG g;
    static USERBLK pv;
    unsigned char grgb[TH_NROLES][3], wrgb[TH_NROLES][3], erg[TH_NROLES][3];
    DOC *d = top_doc();
    int gid = theme_id, per = theme_per_window() && d, slot = d ? d->slot : -1;
    int wid = per ? theme_window(slot, wrgb) : -1, shown = 0, i, k, v, eid, mine;
    short r, x, y;

    if (!theme_possible()) return 0;
    memcpy(grgb, theme_rgb, sizeof grgb);
    if (!dlg_load(&g, RT_THEME)) return 0;
    theme_userdef(&g.o[DTH_PREVIEW], &pv, 2);   /* the picture of it */
    if (wid >= 0) { theme_id = wid; memcpy(theme_rgb, wrgb, sizeof wrgb); }   /* this window's own */
    dlg_set(&g, DTH_THISDOC, wid >= 0);
    dlg_set(&g, DTH_ALLDOCS, wid < 0);
    if (!per) {                                  /* a palette: one theme for all */
        g.o[DTH_THISDOC].ob_state |= OS_DISABLED;
        g.o[DTH_ALLDOCS].ob_state |= OS_DISABLED;
    }
    theme_preview_pens();
    for (i = 0; i < TH_NROLES; i++)
        for (k = 0; k < 3; k++) dlg_edtext(&g, fo[i][k], f[i][k]);
    g.cancel = DTH_CANCEL;
    dlg_tree_end(&g);
    for (;;) {
        for (i = 0; i < TH_NROLES; i++)
            for (k = 0; k < 3; k++) sprintf(f[i][k], "%d", theme_rgb[i][k]);
        sprintf(pick, "%.17s", theme_id == THEME_CUSTOM ? L(S_THEME_CUSTOM) : theme_name(theme_id));
        dlg_str(&g, DTH_PICK, pick);
        r = dlg_run(&g);
        if (r == DTH_PICK) {
            objc_offset(g.o, DTH_PICK, &x, &y);
            v = popup_rsc(RT_POPTHEME, theme_id < NTHEMES ? theme_id : -1, x, (short)(y + g.o[DTH_PICK].ob_height));
            if (v >= 0 && v < NTHEMES) { theme_pick(v); theme_preview_pens(); shown = 1; }
            continue;
        }
        if (r != DTH_APPLY && r != DTH_OK) {
            /* Cancel: as it was, on screen too if Apply showed another */
            theme_id = gid;
            memcpy(theme_rgb, grgb, sizeof grgb);
            if (per) theme_set_window(slot, wid, wid >= 0 ? wrgb : NULL);
            theme_apply();
            if (shown) form_dial(FMD_FINISH, 0, 0, 0, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h);
            return 0;
        }
        /* the colours as typed: a theme of its own if they're not one */
        for (i = 0; i < TH_NROLES; i++)
            for (k = 0; k < 3; k++) {
                v = atoi(f[i][k]);
                v = v < 0 ? 0 : v > 255 ? 255 : v;
                if (v != theme_rgb[i][k]) { theme_rgb[i][k] = (unsigned char)v; theme_id = THEME_CUSTOM; }
            }
        /* this window's, or everyone's (and then this window follows it) */
        eid = theme_id;
        memcpy(erg, theme_rgb, sizeof erg);
        mine = per && dlg_on(&g, DTH_THISDOC);
        if (mine) {
            theme_set_window(slot, eid, erg);
            theme_id = gid;
            memcpy(theme_rgb, grgb, sizeof grgb);
        } else {
            if (per) theme_set_window(slot, -1, NULL);
            gid = eid;
            memcpy(grgb, erg, sizeof grgb);
        }
        theme_apply();
        if (r == DTH_OK) return 1;
        shown = 1;
        form_dial(FMD_FINISH, 0, 0, 0, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h);
        pump_redraws();
        theme_id = eid;                          /* still editing it */
        memcpy(theme_rgb, erg, sizeof erg);
        theme_preview_pens();
    }
}
