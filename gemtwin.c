/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemtwin.c - tool windows that hold a dialog: Find and Replace,
 *             Spelling, Thesaurus; and the suggestions popup of
 *             spelling as you type
 *
 * Each window holds an ordinary object tree made by the dialog builder,
 * drawn through the window's rectangle list. Edit fields are edited with
 * objc_edit(); Tab moves between them. The documents stay usable while
 * the windows are open: they act on the document used last.
 */
#include <gem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gemui.h"

#define TWKIND (NAME | CLOSER | MOVER)

typedef struct {
    int    open, curs;
    short  wh, ed, idx, def;            /* window, edit field with the cursor */
    DLG    g;
    char   name[48];
    void (*act)(short item, int clicks);
    void (*build)(void);
    void (*changed)(short item);        /* a radio button or check box changed */
    void (*topped)(void);               /* the window came to the top          */
} TWIN;

static int tw_top(TWIN *t)
{
    short h, d1, d2, d3;
    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    return t->open && h == t->wh;
}

static void tw_place(TWIN *t)
{
    GRECT w;
    wind_get(t->wh, WF_WORKXYWH, &w.g_x, &w.g_y, &w.g_w, &w.g_h);
    t->g.o[0].ob_x = w.g_x;
    t->g.o[0].ob_y = w.g_y;
}

static void tw_cursor(TWIN *t, int on)
{
    if (t->ed < 0 || !tw_top(t) || on == t->curs) return;
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    if (on) objc_edit(t->g.o, t->ed, 0, &t->idx, ED_INIT);
    else objc_edit(t->g.o, t->ed, 0, &t->idx, ED_END);
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
    t->curs = on;
}

static void tw_draw(TWIN *t, GRECT *area)
{
    GRECT r, a = *area;
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    wind_get(t->wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        short x1 = r.g_x > a.g_x ? r.g_x : a.g_x, y1 = r.g_y > a.g_y ? r.g_y : a.g_y;
        short x2 = (r.g_x + r.g_w < a.g_x + a.g_w) ? r.g_x + r.g_w : a.g_x + a.g_w;
        short y2 = (r.g_y + r.g_h < a.g_y + a.g_h) ? r.g_y + r.g_h : a.g_y + a.g_h;
        if (x2 > x1 && y2 > y1) objc_draw(t->g.o, 0, 8, x1, y1, x2 - x1, y2 - y1);
        wind_get(t->wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
    t->curs = 0;                     /* drawing the field removed the cursor */
    tw_cursor(t, 1);
}

static void tw_redraw(TWIN *t)
{
    GRECT w;
    if (!t->open) return;
    wind_get(t->wh, WF_WORKXYWH, &w.g_x, &w.g_y, &w.g_w, &w.g_h);
    tw_draw(t, &w);
}

static void tw_close(TWIN *t)
{
    if (!t->open) return;
    winpos_save(t->name, t->wh);
    wind_close(t->wh);
    wind_delete(t->wh);
    t->open = 0;
    t->wh = -1;
}

/* open the window (or bring it to the top); at_bottom: under the
 * documents' text, else at the right */
static int tw_open(TWIN *t, int title, int at_bottom)
{
    short x, y, w, h;
    if (t->open) { wind_set(t->wh, WF_TOP, 0, 0, 0, 0); return 1; }
    t->build();
    t->g.framed = 0;
    tw_place(t);
    wind_calc(WC_BORDER, TWKIND, 0, 0, t->g.w, t->g.h, &x, &y, &w, &h);
    if (at_bottom) { x = (short)(desk.g_x + (desk.g_w - w) / 2); y = (short)(desk.g_y + desk.g_h - h - 2); }
    else { x = (short)(desk.g_x + desk.g_w - w - 4); y = (short)(desk.g_y + 20); }
    sprintf(t->name, " %s ", L(title));
    winpos_get(t->name, &x, &y, NULL, NULL);          /* where it was last time */
    winpos_clamp(&x, &y, w, h);
    t->wh = wind_create(TWKIND, x, y, w, h);
    if (t->wh < 0) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return 0; }
    wind_set_str(t->wh, WF_NAME, t->name);
    t->open = 1;
    t->curs = 0;
    t->ed = t->g.edit1;
    t->idx = 0;
    wind_open(t->wh, x, y, w, h);
    tw_place(t);
    return 1;
}

static void tw_msg(TWIN *t, short *msg)
{
    GRECT r;
    switch (msg[0]) {
    case WM_REDRAW:
        r.g_x = msg[4]; r.g_y = msg[5]; r.g_w = msg[6]; r.g_h = msg[7];
        tw_draw(t, &r);
        break;
    case WM_TOPPED:
        wind_set(t->wh, WF_TOP, 0, 0, 0, 0);
        if (t->topped) t->topped();
        tw_cursor(t, 1);
        break;
    case WM_CLOSED:
        tw_close(t);
        break;
    case WM_MOVED:
        wind_set(t->wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
        tw_place(t);
        break;
    case WM_BOTTOMED:
        wind_set(t->wh, WF_BOTTOM, 0, 0, 0, 0);
        break;
    }
}

static short item_of(TWIN *t, short obj)
{
    short i;
    for (i = 0; i < t->g.nit; i++) if (t->g.it[i].obj == obj) return i;
    return -1;
}

static void tw_click(TWIN *t, short mx, short my, short clicks)
{
    short o, mb, ks, x, y;
    OBJECT *tr = t->g.o;
    if (!t->open) return;
    if (!tw_top(t)) { wind_set(t->wh, WF_TOP, 0, 0, 0, 0); if (t->topped) t->topped(); tw_cursor(t, 1); return; }
    o = objc_find(tr, 0, 8, mx, my);
    if (o <= 0 || (tr[o].ob_state & OS_DISABLED)) return;
    if (tr[o].ob_flags & OF_EDITABLE) {
        if (o != t->ed) { tw_cursor(t, 0); t->ed = o; tw_cursor(t, 1); }
        return;
    }
    if (!(tr[o].ob_flags & OF_SELECTABLE)) return;
    if (!(tr[o].ob_flags & OF_EXIT)) {
        /* check boxes and radio buttons */
        wind_update(BEG_UPDATE);
        if (tr[o].ob_flags & OF_RBUTTON) {
            short p = o, k;
            while (tr[p].ob_next >= 0 && tr[tr[p].ob_next].ob_tail != p) p = tr[p].ob_next;
            p = tr[p].ob_next;      /* the parent */
            for (k = tr[p].ob_head; k != p && k >= 0; k = tr[k].ob_next)
                if (k != o && (tr[k].ob_state & OS_SELECTED)) objc_change(tr, k, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h, 0, 1);
            objc_change(tr, o, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h, OS_SELECTED, 1);
        } else
            objc_change(tr, o, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h, tr[o].ob_state ^ OS_SELECTED, 1);
        do graf_mkstate(&x, &y, &mb, &ks); while (mb & 1);
        wind_update(END_UPDATE);
        if (t->changed) t->changed(item_of(t, o));
        return;
    }
    /* press feedback, act on release over the button */
    wind_update(BEG_UPDATE);
    objc_change(tr, o, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h, OS_SELECTED, 1);
    do graf_mkstate(&x, &y, &mb, &ks); while (mb & 1);
    objc_change(tr, o, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h, 0, 1);
    wind_update(END_UPDATE);
    if (objc_find(tr, 0, 8, x, y) == o) t->act(item_of(t, o), clicks);
}

static void tw_key(TWIN *t, short ks, short key)
{
    short scan = (key >> 8) & 0xff;
    (void)ks;
    if (!t->open) return;
    if (scan == 0x1C || scan == 0x72) { t->act(t->def, 1); return; }    /* Return: default button */
    if (scan == 0x01) { tw_close(t); return; }                         /* Esc */
    if (t->ed < 0) return;
    if (scan == 0x0F || scan == 0x50 || scan == 0x48) {                 /* Tab, arrows: next field */
        short i, first = -1, next = -1, prev = -1, last = -1;
        for (i = 1; i < t->g.n; i++)
            if (t->g.o[i].ob_flags & OF_EDITABLE) {
                if (first < 0) first = i;
                if (i > t->ed && next < 0) next = i;
                if (i < t->ed) prev = i;
                last = i;
            }
        tw_cursor(t, 0);
        t->ed = scan == 0x48 ? (prev >= 0 ? prev : last) : (next >= 0 ? next : first);
        tw_cursor(t, 1);
        return;
    }
    tw_cursor(t, 1);
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    objc_edit(t->g.o, t->ed, key, &t->idx, ED_CHAR);
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
}

static void show_obj(TWIN *t, short item, int show)
{
    short o = t->g.it[item].obj;
    if (show) t->g.o[o].ob_flags &= ~OF_HIDETREE;
    else t->g.o[o].ob_flags |= OF_HIDETREE;
}

static void set_disabled(TWIN *t, short item, int dis)
{
    short o = t->g.it[item].obj;
    if (dis) t->g.o[o].ob_state |= OS_DISABLED; else t->g.o[o].ob_state &= ~OS_DISABLED;
}

static DOC *live_doc(DOC *d) { return d && d->used ? d : active_doc(); }

/* ================================================================= */
/*  Find and replace                                                  */
/* ================================================================= */

static TWIN fw;
static char f_what[41], f_with[41], f_status[48];
static short f_ewhat, f_ewith, f_case, f_word, f_back, f_next, f_rep, f_all, f_close, f_stat;
static const char f_tmpl[] = "________________________________________";
static const char f_valid[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

static int f_flags(void)
{
    return (dlg_on(&fw.g, f_case) ? FIND_CASE : 0) | (dlg_on(&fw.g, f_word) ? FIND_WORD : 0) |
           (dlg_on(&fw.g, f_back) ? FIND_BACK : 0);
}

static void f_build(void)
{
    DLG *g = &fw.g;
    dlg_begin(g, NULL);
    dlg_label(g, L(S_FIND_WHAT));
    f_ewhat = dlg_edit(g, f_tmpl + 10, f_what, f_valid + 10);
    dlg_row(g);
    dlg_label(g, L(S_REPLACE_WITH));
    f_ewith = dlg_edit(g, f_tmpl + 10, f_with, f_valid + 10);
    dlg_row(g);
    dlg_label(g, "");
    f_case = dlg_check(g, L(S_MATCH_CASE), 0);
    f_word = dlg_check(g, L(S_WHOLE_WORDS), 0);
    f_back = dlg_check(g, L(S_BACKWARDS), 0);
    dlg_row(g);
    strcpy(f_status, "                                        ");
    f_stat = dlg_text(g, f_status);
    f_next = dlg_button(g, L(S_FIND_NEXT), OF_EXIT | OF_DEFAULT);
    f_rep = dlg_button(g, L(S_REPLACE), OF_EXIT);
    f_all = dlg_button(g, L(S_REPLACE_ALL), OF_EXIT);
    f_close = dlg_button(g, L(S_CLOSE), OF_EXIT);
    dlg_end(g);
    fw.def = f_next;
}

static void f_say(int text, long n)
{
    sprintf(f_status, L(text), n);
    tw_redraw(&fw);
}

/* find from the caret (or after the selection); wraps once */
static int f_find(DOC *d)
{
    long from, e, s, a, b;
    int flags = f_flags();
    if (!f_what[0]) return 0;
    if (doc_sel(d, &a, &b)) from = flags & FIND_BACK ? a : b;
    else from = d->cursor;
    s = find_next(d, from, f_what, flags, &e);
    if (s < 0) s = find_next(d, flags & FIND_BACK ? d->len : 0, f_what, flags, &e);
    if (s < 0) { f_say(S_NOT_FOUND, 0); return 0; }
    select_range(d, s, e);
    f_say(S_BLANK, 0);
    return 1;
}

static void f_act(short it, int clicks)
{
    DOC *d = live_doc(NULL);
    long a, b, e;
    (void)clicks;
    if (it == f_close) { tw_close(&fw); return; }
    if (!d) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    if (it == f_next) f_find(d);
    else if (it == f_rep) {
        int flags = f_flags() & ~FIND_BACK;
        if (doc_sel(d, &a, &b) && find_next(d, a, f_what, flags, &e) == a && e == b) {
            doc_replace(d, a, b, f_with);
            d->cursor = a + (long)strlen(f_with);
            d->anchor = -1;
            after_edit(d, a);
        }
        f_find(d);
    } else if (it == f_all) {
        int n;
        if (!f_what[0]) return;
        busy_ptr(1);
        n = find_replace_all(d, f_what, f_with, f_flags(), 0);
        busy_ptr(0);
        if (n) doc_changed(d);
        f_say(S_N_REPLACED, n);
    }
}

void find_open(DOC *d)
{
    long a, b, k;
    fw.act = f_act;
    fw.build = f_build;
    if (!fw.open && doc_sel(d, &a, &b) && b - a < 40) {
        for (k = a; k < b && !ISEND(d->text[k].ch); k++) f_what[k - a] = (char)d->text[k].ch;
        f_what[k - a] = 0;
    }
    tw_open(&fw, S_FIND_TITLE, 1);
    (void)f_ewhat; (void)f_ewith; (void)f_stat;
}

void find_again(DOC *d)
{
    if (!f_what[0]) { find_open(d); return; }
    if (!fw.g.nit) { fw.act = f_act; fw.build = f_build; f_build(); }
    f_find(d);
}

static int  f_owns(short wh) { return fw.open && wh == fw.wh; }
static void f_msg(short *m) { tw_msg(&fw, m); }
static void f_click(short x, short y, short c) { tw_click(&fw, x, y, c); }
static void f_key(short ks, short k) { tw_key(&fw, ks, k); }
static void f_close_tool(void) { tw_close(&fw); }
static void f_docgone(DOC *d) { (void)d; }
const TOOL find_tool = { f_owns, f_msg, f_click, f_key, f_close_tool, f_docgone };

/* ================================================================= */
/*  Spelling                                                          */
/* ================================================================= */

/* The spelling check runs in its own window. Before acting on a word it
 * checks the word is still where it was, in case the text was edited. */

static TWIN sw;
static struct {
    int    ns, wrapped;
    DOC   *d;
    long   s, e, from, start;
    char   word[WORDMAX], sug[6][WORDMAX], head[80], chg[WORDMAX];
    short  sb[6], ech, rep, ign, iga, add, stp, head_i;
} sp;
static const char sp_tmpl[]  = "_______________________________";
static const char sp_valid[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

/* load the dictionaries when first needed */
int spell_ensure(int quiet)
{
    char path[300];
    if (!spell_main_loaded()) {
        int ok;
        sprintf(path, "%s%s", progdir, spell_dic_name(spell_lang));
        edlog("spelling: loading %s", path);
        busy_begin(L(S_LOADING_DIC));
        ok = spell_load_main(path);
        busy_end();
        if (!ok) {
            if (!quiet) alert(1, 3, AL_NO_DIC, AB_OK);
            if (!spell_nuser) return 0;
        }
    }
    if (spell_nuser == 0) {
        sprintf(path, "%sUSER.UDC", progdir);
        spell_user_load(path, 1);
        update_menu(top_doc());
    }
    return 1;
}

static void sp_build(void)
{
    DLG *g = &sw.g;
    int i;
    dlg_begin(g, NULL);
    sp.head_i = dlg_text(g, sp.head);
    dlg_row(g);
    dlg_label(g, L(S_CHANGE_TO));
    sp.ech = dlg_edit(g, sp_tmpl, sp.chg, sp_valid);
    dlg_row(g);
    dlg_label(g, L(S_SUGGESTIONS));
    for (i = 0; i < 6; i++) sp.sb[i] = dlg_bigbutton(g, sp.sug[i], 18, OF_EXIT);
    sp.rep = dlg_button(g, L(S_REPLACE), OF_EXIT | OF_DEFAULT);
    sp.ign = dlg_button(g, L(S_IGNORE), OF_EXIT);
    sp.iga = dlg_button(g, L(S_IGNORE_ALL), OF_EXIT);
    sp.add = dlg_button(g, L(S_ADD), OF_EXIT);
    sp.stp = dlg_button(g, L(S_STOP), OF_EXIT);
    dlg_end(g);
    sw.def = sp.rep;
}

void spell_close(void)
{
    tw_close(&sw);
    update_menu(top_doc());
    update_winmenu();
}

DOC *spell_doc(void) { return sw.open ? sp.d : NULL; }

/* show the next misspelt word, or finish */
static void sp_find(void)
{
    DOC *d = sp.d;
    long s, e;
    int i;

    for (;;) {
        busy_ptr(1);
        s = spell_next(d, sp.from, &e, sp.word);
        busy_ptr(0);
        if (sp.wrapped && (s < 0 || s >= sp.start)) s = -1;
        else if (s < 0 && !sp.wrapped && sp.start > 0) {
            if (alert(1, 2, AL_SPELL_WRAP, AB_CONT_STOP) == 1) {
                sp.wrapped = 1;
                sp.from = 0;
                continue;
            }
        }
        break;
    }
    if (s < 0) {
        spell_close();
        alert(1, 1, AL_SPELL_DONE, AB_OK);
        return;
    }
    sp.s = s;
    sp.e = e;
    select_range(d, s, e);
    busy_ptr(1);
    sp.ns = spell_suggest(sp.word, sp.sug, 6);
    busy_ptr(0);
    sprintf(sp.head, "%s  %.31s", L(S_NOT_IN_DIC), sp.word);
    strcpy(sp.chg, sp.ns > 0 ? sp.sug[0] : sp.word);
    for (i = 0; i < 6; i++) {
        if (i >= sp.ns) sp.sug[i][0] = 0;
        show_obj(&sw, sp.sb[i], i < sp.ns);
    }
    set_disabled(&sw, sp.add, spell_active < 0);
    sw.idx = (short)strlen(sp.chg);
    tw_redraw(&sw);
}

/* is the word still where we found it? */
static int sp_still_there(void)
{
    DOC *d = sp.d;
    long n = (long)strlen(sp.word), i;
    if (!d->used || sp.e > d->len || sp.e - sp.s != n) return 0;
    for (i = 0; i < n; i++) if (d->text[sp.s + i].ch != (unsigned char)sp.word[i]) return 0;
    return 1;
}

static void sp_act(short it, int clicks)
{
    DOC *d = sp.d;
    const char *with = NULL;
    int i;
    (void)clicks;
    if (it == sp.stp) { spell_close(); return; }
    if (!sp_still_there()) {
        /* the text changed under us: look again from there */
        sp.from = sp.s <= d->len ? sp.s : d->len;
        sp_find();
        return;
    }
    for (i = 0; i < sp.ns; i++) if (it == sp.sb[i]) with = sp.sug[i];
    if (it == sp.rep) with = sp.chg;
    if (with) {
        if (strcmp(with, sp.word)) {
            long delta = (long)strlen(with) - (sp.e - sp.s);
            doc_replace(d, sp.s, sp.e, with);
            d->cursor = sp.s + (long)strlen(with);
            d->anchor = -1;
            after_edit(d, sp.s);
            if (sp.s < sp.start) sp.start += delta;
            sp.e += delta;
        }
    } else if (it == sp.iga) { spell_ignore(sp.word); refresh_text(d); }
    else if (it == sp.add) { spell_user_add(sp.word); update_menu(d); refresh_text(d); }
    else if (it != sp.ign) return;
    sp.from = sp.e;
    sp_find();
}

void spell_run(DOC *d)
{
    long a, b;
    if (!spell_ensure(0)) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    sw.act = sp_act;
    sw.build = sp_build;
    sp.head[0] = 0; sp.chg[0] = 0;
    if (!tw_open(&sw, S_SPELL_TITLE, 1)) return;
    sp.d = d;
    sp.wrapped = 0;
    sp.start = sp.from = doc_sel(d, &a, &b) ? a : d->cursor;
    sp_find();
}

static int  s_owns(short wh) { return sw.open && wh == sw.wh; }
static void s_msg(short *m) { tw_msg(&sw, m); if (m[0] == WM_TOPPED) update_menu(sp.d); }
static void s_click(short x, short y, short c) { tw_click(&sw, x, y, c); }
static void s_key(short ks, short k) { tw_key(&sw, ks, k); }
static void s_close_tool(void) { tw_close(&sw); }
static void s_docgone(DOC *d) { if (sw.open && sp.d == d) spell_close(); }
const TOOL spell_tool = { s_owns, s_msg, s_click, s_key, s_close_tool, s_docgone };

/* ---- spelling as you type: the suggestions popup ------------------------ */

void live_popup(DOC *d, long a, long b, short mx, short my)
{
    char word[WORDMAX], sug[6][WORDMAX];
    const char *items[10];
    int n, i, k = 0, sel, ign, add;
    long j;
    if (b - a >= WORDMAX) return;
    for (j = a; j < b; j++) word[j - a] = (char)d->text[j].ch;
    word[b - a] = 0;
    busy_ptr(1);
    n = spell_suggest(word, sug, 6);
    busy_ptr(0);
    for (i = 0; i < n; i++) items[k++] = sug[i];
    if (!n) items[k++] = L(S_NO_SUGGESTIONS);
    items[k++] = "-";
    ign = k; items[k++] = L(S_IGNORE_ALL);
    add = k; items[k++] = L(S_ADD_TO_DIC);
    sel = popup(items, k, -1, mx, (short)(my + hchar));
    if (sel >= 0 && sel < n) {
        doc_replace(d, a, b, sug[sel]);
        d->cursor = a + (long)strlen(sug[sel]);
        d->anchor = -1;
        after_edit(d, a);
    } else if (sel == ign) { spell_ignore(word); refresh_text(d); }
    else if (sel == add) {
        if (spell_active < 0) spell_ensure(1);
        spell_user_add(word);
        refresh_text(d);
        update_menu(d);
    }
}

/* ================================================================= */
/*  Thesaurus                                                         */
/* ================================================================= */

#define TH_MEAN 6
#define TH_SYN  10
static TWIN tw;
static struct {
    DOC   *d;
    long   a, b;                       /* the word in the document */
    THSRES r;
    int    cur;
    char   word[WORDMAX], with[WORDMAX], mean[TH_MEAN][36], syn[TH_SYN][WORDMAX], msg[60];
    short  eword, look, mb[TH_MEAN], sb[TH_SYN], ewith, rep, cls, msg_i;
} th;
static const char th_tmpl[] = "______________________________";
static const char th_valid[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

static void th_build(void)
{
    DLG *g = &tw.g;
    int i;
    dlg_begin(g, NULL);
    dlg_label(g, L(S_TH_WORD));
    th.eword = dlg_edit(g, th_tmpl, th.word, th_valid);
    th.look = dlg_bigbutton(g, L(S_LOOK_UP), (int)strlen(L(S_LOOK_UP)) + 4, OF_EXIT);
    dlg_row(g);
    dlg_label(g, L(S_MEANINGS));
    for (i = 0; i < TH_MEAN; i++) th.mb[i] = dlg_bigbutton(g, th.mean[i], 21, OF_EXIT);
    dlg_row(g);
    dlg_label(g, L(S_SYNONYMS));
    for (i = 0; i < TH_SYN; i++) th.sb[i] = dlg_bigbutton(g, th.syn[i], 16, OF_EXIT);
    dlg_row(g);
    dlg_label(g, L(S_REPLACE_WITH));
    th.ewith = dlg_edit(g, th_tmpl, th.with, th_valid);
    dlg_row(g);
    th.msg_i = dlg_text(g, th.msg);
    th.rep = dlg_button(g, L(S_REPLACE), OF_EXIT | OF_DEFAULT);
    th.cls = dlg_button(g, L(S_CLOSE), OF_EXIT);
    dlg_end(g);
    tw.def = th.rep;
}

static const char *pos_name(char p)
{
    switch (p) {
    case 'n': return L(S_POS_N);
    case 'v': return L(S_POS_V);
    case 'a': return L(S_POS_A);
    case 'r': return L(S_POS_R);
    }
    return "";
}

/* show the synonyms of meaning m */
static void th_show_sense(int m)
{
    int i;
    th.cur = m;
    for (i = 0; i < TH_SYN; i++) {
        int on = m < th.r.nsense && i < th.r.nsyn[m];
        if (on) strcpy(th.syn[i], th.r.syn[m][i]); else th.syn[i][0] = 0;
        show_obj(&tw, th.sb[i], on);
    }
    for (i = 0; i < TH_MEAN; i++) {
        short o = tw.g.it[th.mb[i]].obj;
        if (i == m) tw.g.o[o].ob_state |= OS_CHECKED | OS_SELECTED; else tw.g.o[o].ob_state &= ~(OS_CHECKED | OS_SELECTED);
    }
    if (m < th.r.nsense && th.r.nsyn[m] > 0) strcpy(th.with, th.r.syn[m][0]);
}

static void th_lookup(void)
{
    int i;
    char path[300];
    if (!thes_ready()) {
        sprintf(path, "%sEDWORD.THS", progdir);
        if (!thes_open(path)) { alert(1, 3, AL_NO_THS, AB_OK); return; }
    }
    busy_ptr(1);
    thes_lookup(th.word, &th.r);
    busy_ptr(0);
    for (i = 0; i < TH_MEAN; i++) {
        int on = i < th.r.nsense;
        if (on) sprintf(th.mean[i], "%s %.16s", pos_name(th.r.pos[i]), th.r.gloss[i]);
        else th.mean[i][0] = 0;
        show_obj(&tw, th.mb[i], on);
    }
    if (th.r.nsense) {
        if (strcmp(th.r.word, th.word)) sprintf(th.msg, L(S_TH_BASE), th.r.word);
        else th.msg[0] = 0;
    } else {
        sprintf(th.msg, L(S_TH_NONE), th.word);
        th.with[0] = 0;
    }
    th_show_sense(0);
    tw_redraw(&tw);
}

/* keep the word's capital letter */
static void match_case(const char *orig, char *rep)
{
    if (orig[0] && st_upper((unsigned char)orig[0]) == (unsigned char)orig[0] && st_isalpha((unsigned char)orig[0]))
        rep[0] = (char)st_upper((unsigned char)rep[0]);
}

static void th_act(short it, int clicks)
{
    DOC *d = th.d;
    int i;
    if (it == th.cls) { tw_close(&tw); return; }
    if (it == th.look) { th_lookup(); return; }
    for (i = 0; i < TH_MEAN; i++) if (it == th.mb[i]) { th_show_sense(i); tw_redraw(&tw); return; }
    for (i = 0; i < TH_SYN; i++)
        if (it == th.sb[i]) {
            if (clicks >= 2) {                  /* look up the synonym itself */
                strcpy(th.word, th.syn[i]);
                th_lookup();
                return;
            }
            strcpy(th.with, th.syn[i]);
            tw_redraw(&tw);
            return;
        }
    if (it == th.rep && d && d->used && th.with[0] && th.b > th.a && th.b <= d->len) {
        char orig[WORDMAX], w[WORDMAX];
        long k;
        for (k = th.a; k < th.b && k - th.a < WORDMAX - 1; k++) orig[k - th.a] = (char)d->text[k].ch;
        orig[k - th.a] = 0;
        strcpy(w, th.with);
        match_case(orig, w);
        doc_replace(d, th.a, th.b, w);
        d->cursor = th.a + (long)strlen(w);
        th.b = d->cursor;
        d->anchor = th.a;
        after_edit(d, th.a);
        tw_close(&tw);
    }
}

void thes_run(DOC *d)
{
    long a, b, pos;
    char w[WORDMAX];
    tw.act = th_act;
    tw.build = th_build;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    pos = doc_sel(d, &a, &b) ? a : d->cursor;
    if (!word_at(d, pos, &a, &b, w, WORDMAX) && !(pos > 0 && word_at(d, pos - 1, &a, &b, w, WORDMAX))) {
        a = b = pos;
        w[0] = 0;
    }
    th.d = d; th.a = a; th.b = b;
    strcpy(th.word, w);
    th.with[0] = 0;
    th.msg[0] = 0;
    if (b > a) select_range(d, a, b);
    if (!tw_open(&tw, S_THES_TITLE, 0)) return;
    if (w[0]) th_lookup();
    else tw_redraw(&tw);
}

static int  t_owns(short wh) { return tw.open && wh == tw.wh; }
static void t_msg(short *m) { tw_msg(&tw, m); }
static void t_click(short x, short y, short c) { tw_click(&tw, x, y, c); }
static void t_key(short ks, short k) { tw_key(&tw, ks, k); }
static void t_close_tool(void) { tw_close(&tw); }
static void t_docgone(DOC *d) { if (tw.open && th.d == d) tw_close(&tw); }
const TOOL thes_tool = { t_owns, t_msg, t_click, t_key, t_close_tool, t_docgone };

/* for the demo script */
void find_set(const char *what, const char *with)
{
    strncpy(f_what, what, 40); f_what[40] = 0;
    strncpy(f_with, with, 40); f_with[40] = 0;
    tw_redraw(&fw);
}

/* ================================================================= */
/*  Import and Export                                                 */
/* ================================================================= */

/* Two windows that stay open while you work: pick the format and its
 * options, then Import... or Export... asks for the file. Export acts on
 * the document used last; its name is shown in the window. */

static const char *fmt_name(int f)
{
    switch (f) {
    case FT_RTF:  return "RTF";
    case FT_TEXT: return L(S_FMT_TEXT);
    case FT_MD:   return "Markdown";
    case FT_FWP:  return "1st Word Plus";
    case FT_PT:   return "Protext";
    default:      return "PDF";
    }
}

static void set_on(TWIN *t, short item, int on)
{
    short o = t->g.it[item].obj;
    if (on) t->g.o[o].ob_state |= OS_SELECTED; else t->g.o[o].ob_state &= ~OS_SELECTED;
}

static void fit_status(char *out, int text, const char *name)
{
    char s[120];
    sprintf(s, L(text), name);
    s[46] = 0;
    strcpy(out, s);
}

/* ---- Import ------------------------------------------------------------- */

static TWIN iw;
static int ix_fmt = 0, ix_cs = CS_AUTO, ix_join = 0, ix_into = 0;   /* ix_fmt 0: automatic */
static short ix_f[6], ix_c[5], ix_j[2], ix_i[2], ix_go, ix_close, ix_stat;
static char ix_status[48];

static void ix_read(void)
{
    int i;
    for (i = 0; i < 6; i++) if (dlg_on(&iw.g, ix_f[i])) ix_fmt = i;
    for (i = 0; i < 5; i++) if (dlg_on(&iw.g, ix_c[i])) ix_cs = i;
    ix_join = dlg_on(&iw.g, ix_j[1]);
    ix_into = dlg_on(&iw.g, ix_i[1]);
}

static void ix_enable(void)
{
    int i, text = ix_fmt == 0 || ix_fmt == 1 + FT_TEXT, chars = text || ix_fmt == 1 + FT_MD;
    for (i = 0; i < 5; i++) set_disabled(&iw, ix_c[i], !chars);
    for (i = 0; i < 2; i++) set_disabled(&iw, ix_j[i], !text);
    set_disabled(&iw, ix_i[1], !live_doc(NULL));
}

static void ix_build(void)
{
    DLG *g = &iw.g;
    int i;
    dlg_begin(g, NULL);
    dlg_label(g, L(S_FORMAT));
    ix_f[0] = dlg_radio(g, 1, L(S_AUTO), ix_fmt == 0);
    for (i = 0; i < 5; i++) {
        if (i == 2) { dlg_row(g); dlg_label(g, ""); }
        ix_f[i + 1] = dlg_radio(g, 1, fmt_name(i), ix_fmt == i + 1);
    }
    dlg_row(g);
    dlg_label(g, L(S_CHARACTERS));
    ix_c[0] = dlg_radio(g, 2, L(S_AUTO), ix_cs == CS_AUTO);
    ix_c[1] = dlg_radio(g, 2, "Atari ST", ix_cs == CS_ATARI);
    ix_c[2] = dlg_radio(g, 2, "Windows", ix_cs == CS_WIN);
    ix_c[3] = dlg_radio(g, 2, "UTF-8", ix_cs == CS_UTF8);
    ix_c[4] = dlg_radio(g, 2, "UTF-16", ix_cs == CS_UTF16);
    dlg_row(g);
    dlg_label(g, L(S_LINES));
    ix_j[0] = dlg_radio(g, 3, L(S_ONE_PARA), !ix_join);
    ix_j[1] = dlg_radio(g, 3, L(S_JOIN_LINES), ix_join);
    dlg_row(g);
    dlg_label(g, L(S_INTO));
    ix_i[0] = dlg_radio(g, 4, L(S_INTO_NEW), !ix_into);
    dlg_row(g);
    dlg_label(g, "");
    ix_i[1] = dlg_radio(g, 4, L(S_INTO_CURSOR), ix_into);
    dlg_row(g);
    memset(ix_status, ' ', 46);
    ix_status[46] = 0;
    ix_stat = dlg_text(g, ix_status);
    ix_close = dlg_button(g, L(S_CLOSE), OF_EXIT);
    ix_go = dlg_button(g, L(M_IMPORT), OF_EXIT | OF_DEFAULT);
    dlg_end(g);
    iw.def = ix_go;
    ix_status[0] = 0;
    ix_enable();
    (void)ix_stat;
}

static void ix_changed(short it)
{
    (void)it;
    ix_read();
    if (!live_doc(NULL)) { ix_into = 0; set_on(&iw, ix_i[0], 1); set_on(&iw, ix_i[1], 0); }
    ix_enable();
    tw_redraw(&iw);
}

static void ix_topped(void) { ix_changed(-1); }

static void ix_act(short it, int clicks)
{
    char path[256];
    const char *base;
    DOC *into;
    (void)clicks;
    if (it == ix_close) { tw_close(&iw); return; }
    if (it != ix_go) return;
    ix_read();
    into = ix_into ? live_doc(NULL) : NULL;
    if (!ask_file(L(S_FS_IMPORT), "*.*", NULL, path)) return;
    pump_redraws();
    base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    if (read_into(path, ix_fmt ? ix_fmt - 1 : FT_AUTO, ix_cs, ix_join, into, 0))
        fit_status(ix_status, S_XF_READ, base);
    else fit_status(ix_status, S_XF_FAILED, "");
    if (iw.open) { ix_enable(); tw_redraw(&iw); }
}

void import_open(void)
{
    iw.act = ix_act;
    iw.build = ix_build;
    iw.changed = ix_changed;
    iw.topped = ix_topped;
    if (iw.open) { ix_changed(-1); tw_open(&iw, S_IMPORT, 0); return; }
    tw_open(&iw, S_IMPORT, 0);
}

static int  i_owns(short wh) { return iw.open && wh == iw.wh; }
static void i_msg(short *m) { tw_msg(&iw, m); }
static void i_click(short x, short y, short c) { tw_click(&iw, x, y, c); }
static void i_key(short ks, short k) { tw_key(&iw, ks, k); }
static void i_close_tool(void) { tw_close(&iw); }
static void i_docgone(DOC *d) { (void)d; if (iw.open) { ix_changed(-1); } }
const TOOL import_tool = { i_owns, i_msg, i_click, i_key, i_close_tool, i_docgone };

/* ---- Export ------------------------------------------------------------- */

static TWIN ew;
static int ex_fmt = FT_RTF, ex_crlf = 1, ex_cs = CS_ATARI;
static short ex_f[6], ex_e[2], ex_c[4], ex_p[3], ex_go, ex_close, ex_doc, ex_stat;
static char ex_docline[48], ex_status[48];

static void ex_read(void)
{
    int i;
    for (i = 0; i < 6; i++) if (dlg_on(&ew.g, ex_f[i])) ex_fmt = i;
    ex_crlf = dlg_on(&ew.g, ex_e[0]);
    for (i = 0; i < 4; i++) if (dlg_on(&ew.g, ex_c[i])) ex_cs = i + 1;
    for (i = 0; i < 3; i++) if (dlg_on(&ew.g, ex_p[i])) pdf_scale = i + 1;
}

static char ex_doclabel[24];

static void ex_name(void)
{
    DOC *d = live_doc(NULL);
    if (d) { strncpy(ex_docline, d->base, 46); ex_docline[46] = 0; }
    else fit_status(ex_docline, S_XF_NODOC, "");
}

static void ex_enable(void)
{
    int i;
    DOC *d = live_doc(NULL);
    for (i = 0; i < 2; i++) set_disabled(&ew, ex_e[i], ex_fmt != FT_TEXT && ex_fmt != FT_MD);
    for (i = 0; i < 4; i++) set_disabled(&ew, ex_c[i], ex_fmt != FT_TEXT && ex_fmt != FT_MD);
    for (i = 0; i < 3; i++) set_disabled(&ew, ex_p[i], ex_fmt != FT_PDF);
    set_disabled(&ew, ex_go, !d);
    if (ex_fmt == FT_TEXT && !ex_status[0]) strcpy(ex_status, L(S_XF_STYLES));
    else if (ex_fmt != FT_TEXT && !strcmp(ex_status, L(S_XF_STYLES))) ex_status[0] = 0;
    ex_name();
}

static void ex_build(void)
{
    DLG *g = &ew.g;
    int i;
    char *pc;
    dlg_begin(g, NULL);
    strncpy(ex_doclabel, L(S_XF_DOC), sizeof ex_doclabel - 1);      /* "Document:" */
    pc = strchr(ex_doclabel, '%');
    if (pc) *pc = 0;
    for (pc = ex_doclabel + strlen(ex_doclabel); pc > ex_doclabel && pc[-1] == ' '; ) *--pc = 0;
    dlg_label(g, ex_doclabel);
    memset(ex_docline, ' ', 36);
    ex_docline[36] = 0;
    ex_doc = dlg_text(g, ex_docline);
    dlg_row(g);
    dlg_label(g, L(S_FORMAT));
    for (i = 0; i < 6; i++) {
        if (i == 3) { dlg_row(g); dlg_label(g, ""); }
        ex_f[i] = dlg_radio(g, 1, fmt_name(i), ex_fmt == i);
    }
    dlg_row(g);
    dlg_label(g, L(S_LINE_ENDINGS));
    ex_e[0] = dlg_radio(g, 2, "Atari/DOS CR LF", ex_crlf);
    ex_e[1] = dlg_radio(g, 2, "Unix LF", !ex_crlf);
    dlg_row(g);
    dlg_label(g, L(S_CHARACTERS));
    ex_c[0] = dlg_radio(g, 3, "Atari ST", ex_cs == CS_ATARI);
    ex_c[1] = dlg_radio(g, 3, "Windows", ex_cs == CS_WIN);
    ex_c[2] = dlg_radio(g, 3, "UTF-8", ex_cs == CS_UTF8);
    ex_c[3] = dlg_radio(g, 3, "UTF-16", ex_cs == CS_UTF16);
    dlg_row(g);
    dlg_label(g, L(S_PDF_PICTURES));
    ex_p[0] = dlg_radio(g, 4, L(S_SCREEN), pdf_scale == 1);
    ex_p[1] = dlg_radio(g, 4, "2x", pdf_scale == 2);
    ex_p[2] = dlg_radio(g, 4, "3x", pdf_scale == 3);
    dlg_row(g);
    memset(ex_status, ' ', 46);
    ex_status[46] = 0;
    ex_stat = dlg_text(g, ex_status);
    ex_close = dlg_button(g, L(S_CLOSE), OF_EXIT);
    ex_go = dlg_button(g, L(M_EXPORT), OF_EXIT | OF_DEFAULT);
    dlg_end(g);
    ew.def = ex_go;
    ex_status[0] = 0;
    ex_enable();
    (void)ex_doc; (void)ex_stat;
}

static void ex_changed(short it)
{
    (void)it;
    ex_read();
    ex_enable();
    tw_redraw(&ew);
}

static void ex_topped(void) { ex_enable(); tw_redraw(&ew); }

static void ex_act(short it, int clicks)
{
    char name[40];
    DOC *d = live_doc(NULL);
    (void)clicks;
    if (it == ex_close) { tw_close(&ew); return; }
    if (it != ex_go) return;
    ex_read();
    if (!d) { ex_enable(); tw_redraw(&ew); return; }
    name[0] = 0;
    if (export_doc(d, ex_fmt, ex_crlf, ex_cs, name)) fit_status(ex_status, S_XF_DONE, name);
    else if (name[0]) fit_status(ex_status, S_XF_FAILED, "");
    else return;                           /* the file selector was cancelled */
    if (ew.open) { ex_enable(); tw_redraw(&ew); }
}

void export_open(void)
{
    ew.act = ex_act;
    ew.build = ex_build;
    ew.changed = ex_changed;
    ew.topped = ex_topped;
    if (ew.open) { ex_enable(); tw_redraw(&ew); }
    tw_open(&ew, S_EXPORT, 0);
}

static int  e_owns(short wh) { return ew.open && wh == ew.wh; }
static void e_msg(short *m) { tw_msg(&ew, m); }
static void e_click(short x, short y, short c) { tw_click(&ew, x, y, c); }
static void e_key(short ks, short k) { tw_key(&ew, ks, k); }
static void e_close_tool(void) { tw_close(&ew); }
static void e_docgone(DOC *d) { (void)d; if (ew.open) { ex_enable(); tw_redraw(&ew); } }
const TOOL export_tool = { e_owns, e_msg, e_click, e_key, e_close_tool, e_docgone };

/* the document used last changed, or closed: keep the windows up to date */
void xfer_refresh(void)
{
    if (ew.open) {
        char old[48];
        int dis = (ew.g.o[ew.g.it[ex_go].obj].ob_state & OS_DISABLED) != 0;
        strcpy(old, ex_docline);
        ex_enable();
        if (strcmp(old, ex_docline) || dis != ((ew.g.o[ew.g.it[ex_go].obj].ob_state & OS_DISABLED) != 0))
            tw_redraw(&ew);
    }
    if (iw.open) {
        int dis = (iw.g.o[iw.g.it[ix_i[1]].obj].ob_state & OS_DISABLED) != 0;
        if (dis != !live_doc(NULL)) ix_changed(-1);
    }
}
