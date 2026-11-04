/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * wintwin.c - tool windows that hold a dialog: Find and Replace,
 *             Spelling, Thesaurus, Import and Export; and the suggestions
 *             popup of spelling as you type (after amiga/amitwin.c)
 *
 * The documents stay usable while the windows are open: they act on the
 * document used last.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winui.h"

typedef struct {
    int    open;
    DLG    g;
    short  def;
    char   title[48];
    void (*act)(short item, int clicks);
    void (*build)(void);
    void (*changed)(void);              /* a cycle gadget or check box changed */
} TWIN;

static void tw_close(TWIN *t)
{
    if (!t->open) return;
    t->open = 0;
    dlg_close(&t->g);
}

/* open the window (or bring it to the front); at_bottom: under the
 * documents' text, else at the right */
static int tw_open(TWIN *t, int title, int at_bottom)
{
    long x, y, w, h;
    RECT fr;
    if (t->open) { SetActiveWindow(t->g.win); return 1; }
    t->build();
    GetWindowRect(frame, &fr);
    w = t->g.w + 2 * GetSystemMetrics(SM_CXFIXEDFRAME);
    h = t->g.h + GetSystemMetrics(SM_CYSMCAPTION) + 2 * GetSystemMetrics(SM_CYFIXEDFRAME);
    if (at_bottom) { x = fr.left + (fr.right - fr.left - w) / 2; y = fr.bottom - h - 24; }
    else { x = fr.right - w - 24; y = fr.top + 80; }
    sprintf(t->title, "%s", L(title));
    if (!dlg_window(&t->g, t->title, x, y)) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return 0; }
    t->open = 1;
    if (t->g.edit1 >= 0 && t->g.it[t->g.edit1].gad) {
        SetFocus(t->g.it[t->g.edit1].gad);
        SendMessageA(t->g.it[t->g.edit1].gad, EM_SETSEL, 0, -1);
    }
    return 1;
}

static void tw_redraw(TWIN *t) { if (t->open) dlg_rebuild(&t->g); }

static void tw_msg(TWIN *t, EVT *m)
{
    short it;
    int clicks = 1;
    ULONG cls = m->Class;
    if (!t->open) return;
    if (!dlg_event(&t->g, m, &it, &clicks)) {
        if (cls == IDCMP_GADGETUP && t->changed) { dlg_fetch(&t->g); t->changed(); }
        return;
    }
    {
        dlg_fetch(&t->g);
        if (it < 0) tw_close(t);
        else t->act(it, clicks);
    }
}

static void tw_key(TWIN *t, int key, int qual)
{
    (void)qual;
    if (!t->open) return;
    if (key == K_RETURN || key == K_ENTER) { dlg_fetch(&t->g); t->act(t->def, 1); }
    else if (key == K_ESC) tw_close(t);
}

static DOC *live_doc(DOC *d) { return d && d->used ? d : active_doc(); }

/* ================================================================= */
/*  Find and replace                                                  */
/* ================================================================= */

static TWIN fw;
static char f_what[41], f_with[41], f_status[48];
static short f_case, f_word, f_back, f_next, f_rep, f_all, f_close;
static const char f_tmpl[] = "______________________________";
static const char f_valid[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

static int f_flags(void)
{
    return (dlg_on(&fw.g, f_case) ? FIND_CASE : 0) | (dlg_on(&fw.g, f_word) ? FIND_WORD : 0) |
           (dlg_on(&fw.g, f_back) ? FIND_BACK : 0);
}

static void f_build(void)
{
    DLG *g = &fw.g;
    int oc = fw.g.nit ? dlg_on(g, f_case) : 0, ow = fw.g.nit ? dlg_on(g, f_word) : 0, ob = fw.g.nit ? dlg_on(g, f_back) : 0;
    dlg_begin(g, NULL);
    dlg_label(g, L(S_FIND_WHAT));
    dlg_edit(g, f_tmpl, f_what, f_valid);
    dlg_row(g);
    dlg_label(g, L(S_REPLACE_WITH));
    dlg_edit(g, f_tmpl, f_with, f_valid);
    dlg_row(g);
    dlg_label(g, "");
    f_case = dlg_check(g, L(S_MATCH_CASE), oc);
    f_word = dlg_check(g, L(S_WHOLE_WORDS), ow);
    f_back = dlg_check(g, L(S_BACKWARDS), ob);
    dlg_row(g);
    if (!f_status[0]) strcpy(f_status, " ");
    dlg_text(g, f_status);
    f_next = dlg_button(g, L(S_FIND_NEXT), OF_EXIT | OF_DEFAULT);
    f_rep = dlg_button(g, L(S_REPLACE), OF_EXIT);
    f_all = dlg_button(g, L(S_REPLACE_ALL), OF_EXIT);
    f_close = dlg_button(g, L(S_CLOSE), OF_EXIT);
    dlg_end(g);
    /* room for the status messages */
    {
        int i;
        for (i = 0; i < g->nit; i++) if (g->it[i].s == f_status) g->it[i].gw = (short)(g->w - 2 * 10);
    }
    fw.def = f_next;
}

static void f_say(int text, long n)
{
    sprintf(f_status, L(text), n);
    if (!f_status[0]) strcpy(f_status, " ");
    tw_redraw(&fw);
}

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
        busy(1);
        n = find_replace_all(d, f_what, f_with, f_flags(), 0);
        busy(0);
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
}

void find_again(DOC *d)
{
    if (!f_what[0]) { find_open(d); return; }
    if (!fw.g.nit) { fw.act = f_act; fw.build = f_build; f_build(); }
    f_find(d);
}

static int  f_owns(WIN w) { return fw.open && w == fw.g.win; }
static void f_msg(EVT *m) { tw_msg(&fw, m); }
static void f_key(int k, int q) { tw_key(&fw, k, q); }
static void f_close_tool(void) { tw_close(&fw); }
static void f_docgone(DOC *d) { (void)d; }
const TOOL find_tool = { f_owns, f_msg, f_key, f_close_tool, f_docgone };

/* ================================================================= */
/*  Spelling                                                          */
/* ================================================================= */

static TWIN sw;
static struct {
    int    ns, wrapped;
    DOC   *d;
    long   s, e, from, start;
    char   word[WORDMAX], sug[6][WORDMAX], sugl[6][WORDMAX], head[80], chg[WORDMAX];
    short  sb[6], rep, ign, iga, add, stp;
} sp;
static const char sp_tmpl[]  = "______________________________";
static const char sp_valid[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

int spell_ensure(int quiet)
{
    char path[300];
    if (!spell_main_loaded()) {
        int ok;
        sprintf(path, "%s%s", progdir, spell_dic_name(spell_lang));
        busy_begin(L(S_LOADING_DIC));
        ok = spell_load_main(path);
        busy_end();
        if (!ok) {
            if (!quiet) alert(1, 3, AL_NO_DIC, AB_OK);
            if (!spell_nuser) return 0;
        }
    }
    if (spell_nuser == 0) {
        sprintf(path, "%sUSER.UDC", datadir);
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
    dlg_text(g, sp.head);
    dlg_row(g);
    dlg_label(g, L(S_CHANGE_TO));
    dlg_edit(g, sp_tmpl, sp.chg, sp_valid);
    dlg_row(g);
    dlg_label(g, L(S_SUGGESTIONS));
    for (i = 0; i < 6; i++) {
        sp.sb[i] = dlg_bigbutton(g, sp.sugl[i], 18, OF_EXIT);
        dlg_show(g, sp.sb[i], i < sp.ns);
    }
    sp.rep = dlg_button(g, L(S_REPLACE), OF_EXIT | OF_DEFAULT);
    sp.ign = dlg_button(g, L(S_IGNORE), OF_EXIT);
    sp.iga = dlg_button(g, L(S_IGNORE_ALL), OF_EXIT);
    sp.add = dlg_button(g, L(S_ADD), OF_EXIT);
    sp.stp = dlg_button(g, L(S_STOP), OF_EXIT);
    dlg_end(g);
    for (i = 0; i < g->nit; i++) if (g->it[i].s == sp.head) g->it[i].gw = (short)(g->w - 20);
    sw.def = sp.rep;
}

void spell_close(void)
{
    tw_close(&sw);
    update_menu(top_doc());
}

DOC *spell_doc(void) { return sw.open ? sp.d : NULL; }

static void sp_find(void)
{
    DOC *d = sp.d;
    long s, e;
    int i;

    for (;;) {
        busy(1);
        s = spell_next(d, sp.from, &e, sp.word);
        busy(0);
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
    busy(1);
    sp.ns = spell_suggest(sp.word, sp.sug, 6);
    busy(0);
    sprintf(sp.head, "%s  %.31s", L(S_NOT_IN_DIC), lat(sp.word));
    strcpy(sp.chg, sp.ns > 0 ? sp.sug[0] : sp.word);
    for (i = 0; i < 6; i++) {
        if (i >= sp.ns) sp.sug[i][0] = 0;
        strcpy(sp.sugl[i], lat(sp.sug[i]));
        dlg_show(&sw.g, sp.sb[i], i < sp.ns);
    }
    dlg_disable(&sw.g, sp.add, spell_active < 0);
    /* the edit field shows the new suggestion */
    for (i = 0; i < sw.g.nit; i++)
        if (sw.g.it[i].buf == sp.chg && sw.g.it[i].gad) {
            SetWindowTextA(sw.g.it[i].gad, lat(sp.chg));
        }
    tw_redraw(&sw);
}

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
    sp.head[0] = ' '; sp.head[1] = 0; sp.chg[0] = 0; sp.ns = 6;
    {
        int i;
        for (i = 0; i < 6; i++) strcpy(sp.sugl[i], "------------------");
    }
    if (!tw_open(&sw, S_SPELL_TITLE, 1)) return;
    sp.d = d;
    sp.wrapped = 0;
    sp.start = sp.from = doc_sel(d, &a, &b) ? a : d->cursor;
    sp_find();
}

static int  s_owns(WIN w) { return sw.open && w == sw.g.win; }
static void s_msg(EVT *m) { tw_msg(&sw, m); if (m->Class == IDCMP_ACTIVEWINDOW && sw.open) update_menu(sp.d); }
static void s_key(int k, int q) { tw_key(&sw, k, q); }
static void s_close_tool(void) { tw_close(&sw); }
static void s_docgone(DOC *d) { if (sw.open && sp.d == d) spell_close(); }
const TOOL spell_tool = { s_owns, s_msg, s_key, s_close_tool, s_docgone };

/* ---- spelling as you type: the suggestions popup ------------------------ */

void live_popup(DOC *d, long a, long b, short mx, short my)
{
    char word[WORDMAX], sug[6][WORDMAX], sugl[6][WORDMAX];
    const char *items[10];
    int n, i, k = 0, sel, ign, add;
    long j;
    if (b - a >= WORDMAX) return;
    for (j = a; j < b; j++) word[j - a] = (char)d->text[j].ch;
    word[b - a] = 0;
    busy(1);
    n = spell_suggest(word, sug, 6);
    busy(0);
    for (i = 0; i < n; i++) { strcpy(sugl[i], lat(sug[i])); items[k++] = sugl[i]; }
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
    long   a, b;
    THSRES r;
    int    cur, built;
    char   word[WORDMAX], with[WORDMAX], mean[TH_MEAN][40], syn[TH_SYN][WORDMAX], synl[TH_SYN][WORDMAX], msg[64];
    short  look, mb[TH_MEAN], sb[TH_SYN], rep, cls;
} th;
static const char th_tmpl[] = "____________________________";
static const char th_valid[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

static void th_build(void)
{
    DLG *g = &tw.g;
    int i;
    dlg_begin(g, NULL);
    dlg_label(g, L(S_TH_WORD));
    dlg_edit(g, th_tmpl, th.word, th_valid);
    th.look = dlg_bigbutton(g, L(S_LOOK_UP), (int)strlen(L(S_LOOK_UP)) + 4, OF_EXIT);
    dlg_row(g);
    dlg_label(g, L(S_MEANINGS));
    for (i = 0; i < TH_MEAN; i++) {
        th.mb[i] = dlg_bigbutton(g, th.mean[i], 22, OF_EXIT);
        dlg_show(g, th.mb[i], i < th.r.nsense);
    }
    dlg_row(g);
    dlg_label(g, L(S_SYNONYMS));
    for (i = 0; i < TH_SYN; i++) {
        th.sb[i] = dlg_bigbutton(g, th.synl[i], 16, OF_EXIT);
        dlg_show(g, th.sb[i], th.cur < th.r.nsense && i < th.r.nsyn[th.cur]);
    }
    dlg_row(g);
    dlg_label(g, L(S_REPLACE_WITH));
    dlg_edit(g, th_tmpl, th.with, th_valid);
    dlg_row(g);
    if (!th.msg[0]) strcpy(th.msg, " ");
    dlg_text(g, th.msg);
    th.rep = dlg_button(g, L(S_REPLACE), OF_EXIT | OF_DEFAULT);
    th.cls = dlg_button(g, L(S_CLOSE), OF_EXIT);
    dlg_end(g);
    for (i = 0; i < g->nit; i++) if (g->it[i].s == th.msg) g->it[i].gw = (short)(g->w - 20);
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

/* the edit fields show new contents: set the gadgets too */
static void th_set_field(char *buf)
{
    int i;
    for (i = 0; i < tw.g.nit; i++)
        if (tw.g.it[i].buf == buf && tw.g.it[i].gad)
            SetWindowTextA(tw.g.it[i].gad, lat(buf));
}

static void th_show_sense(int m)
{
    int i;
    th.cur = m;
    for (i = 0; i < TH_SYN; i++) {
        int on = m < th.r.nsense && i < th.r.nsyn[m];
        if (on) strcpy(th.syn[i], th.r.syn[m][i]); else th.syn[i][0] = 0;
        strcpy(th.synl[i], lat(th.syn[i]));
        dlg_show(&tw.g, th.sb[i], on);
    }
    for (i = 0; i < TH_MEAN; i++)          /* the chosen meaning is marked */
        if (i < th.r.nsense) {
            char t[40];
            sprintf(t, "%s%s %.16s", i == m ? "\273" : "", pos_name(th.r.pos[i]), lat(th.r.gloss[i]));
            strcpy(th.mean[i], t);
        }
    if (m < th.r.nsense && th.r.nsyn[m] > 0) { strcpy(th.with, th.r.syn[m][0]); th_set_field(th.with); }
}

static void th_lookup(void)
{
    int i;
    char path[300];
    if (!thes_ready()) {
        sprintf(path, "%sEDWORD.THS", progdir);
        if (!thes_open(path)) { alert(1, 3, AL_NO_THS, AB_OK); return; }
    }
    busy(1);
    thes_lookup(th.word, &th.r);
    busy(0);
    for (i = 0; i < TH_MEAN; i++) {
        int on = i < th.r.nsense;
        if (!on) th.mean[i][0] = 0;
        dlg_show(&tw.g, th.mb[i], on);
    }
    if (th.r.nsense) {
        if (strcmp(th.r.word, th.word)) sprintf(th.msg, L(S_TH_BASE), lat(th.r.word));
        else strcpy(th.msg, " ");
    } else {
        sprintf(th.msg, L(S_TH_NONE), lat(th.word));
        th.with[0] = 0;
        th_set_field(th.with);
    }
    th_show_sense(0);
    th_set_field(th.word);
    tw_redraw(&tw);
}

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
            if (clicks >= 2) {
                strcpy(th.word, th.syn[i]);
                th_lookup();
                return;
            }
            strcpy(th.with, th.syn[i]);
            th_set_field(th.with);
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
    strcpy(th.msg, " ");
    if (!tw.open) {
        /* made with all buttons, so the window is big enough for any word */
        int i;
        memset(&th.r, 0, sizeof th.r);
        th.r.nsense = TH_MEAN;
        for (i = 0; i < TH_MEAN; i++) { th.r.nsyn[i] = TH_SYN; strcpy(th.mean[i], "----------------------"); }
        for (i = 0; i < TH_SYN; i++) strcpy(th.synl[i], "----------------");
        th.cur = 0;
    }
    if (b > a) select_range(d, a, b);
    if (!tw_open(&tw, S_THES_TITLE, 0)) return;
    if (w[0]) th_lookup();
    else {
        memset(&th.r, 0, sizeof th.r);
        th_show_sense(0);
        { int i; for (i = 0; i < TH_MEAN; i++) dlg_show(&tw.g, th.mb[i], 0); }
        th_set_field(th.word);
        tw_redraw(&tw);
    }
}

static int  t_owns(WIN w) { return tw.open && w == tw.g.win; }
static void t_msg(EVT *m) { tw_msg(&tw, m); }
static void t_key(int k, int q) { tw_key(&tw, k, q); }
static void t_close_tool(void) { tw_close(&tw); }
static void t_docgone(DOC *d) { if (tw.open && th.d == d) tw_close(&tw); }
const TOOL thes_tool = { t_owns, t_msg, t_key, t_close_tool, t_docgone };

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

static void fit_status(char *out, int text, const char *name)
{
    char s[120];
    sprintf(s, L(text), name);
    s[46] = 0;
    strcpy(out, s);
    if (!out[0]) strcpy(out, " ");
}

static void widen(DLG *g, const char *buf)
{
    int i;
    for (i = 0; i < g->nit; i++) if (g->it[i].s == buf) g->it[i].gw = (short)(g->w - 2 * 10);
}

/* ---- Import ------------------------------------------------------------- */

static TWIN iw;
static int ix_fmt = 0, ix_cs = CS_AUTO, ix_join = 0, ix_into = 0;   /* ix_fmt 0: automatic */
static short ix_f[6], ix_c[5], ix_j[2], ix_i[2], ix_go, ix_close;
static char ix_status[48] = " ";

static void ix_read(void)
{
    int i;
    for (i = 0; i < 6; i++) if (dlg_on(&iw.g, ix_f[i])) ix_fmt = i;
    for (i = 0; i < 5; i++) if (dlg_on(&iw.g, ix_c[i])) ix_cs = i;
    ix_join = dlg_on(&iw.g, ix_j[1]);
    ix_into = dlg_on(&iw.g, ix_i[1]) && live_doc(NULL);
}

static void ix_enable(void)
{
    int i, text = ix_fmt == 0 || ix_fmt == 1 + FT_TEXT, chars = text || ix_fmt == 1 + FT_MD;
    for (i = 0; i < 5; i++) dlg_disable(&iw.g, ix_c[i], !chars);
    for (i = 0; i < 2; i++) dlg_disable(&iw.g, ix_j[i], !text);
}

static void ix_build(void)
{
    DLG *g = &iw.g;
    int i;
    dlg_begin(g, NULL);
    dlg_label(g, L(S_FORMAT));
    ix_f[0] = dlg_radio(g, 1, L(S_AUTO), ix_fmt == 0);
    for (i = 0; i < 5; i++) ix_f[i + 1] = dlg_radio(g, 1, fmt_name(i), ix_fmt == i + 1);
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
    ix_i[1] = dlg_radio(g, 4, L(S_INTO_CURSOR), ix_into);
    dlg_row(g);
    {
        static char keep[48];
        strcpy(keep, ix_status);
        memset(ix_status, ' ', 34);
        ix_status[34] = 0;
        dlg_text(g, ix_status);
        ix_go = dlg_button(g, L(M_IMPORT), OF_EXIT | OF_DEFAULT);
        ix_close = dlg_button(g, L(S_CLOSE), OF_EXIT);
        ix_enable();
        dlg_end(g);
        strcpy(ix_status, keep);
    }
    widen(g, ix_status);
    iw.def = ix_go;
}

static void ix_changed(void)
{
    ix_read();
    ix_enable();
    tw_redraw(&iw);
}

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
    if (!ask_file(L(S_FS_IMPORT), "#?", NULL, path, 0)) return;
    base = strrchr(path, '/');
    if (!base) base = strrchr(path, ':');
    base = base ? base + 1 : path;
    if (read_into(path, ix_fmt ? ix_fmt - 1 : FT_AUTO, ix_cs, ix_join, into, 0)) fit_status(ix_status, S_XF_READ, base);
    else fit_status(ix_status, S_XF_FAILED, "");
    if (iw.open) { ix_enable(); tw_redraw(&iw); }
}

void import_open(void)
{
    iw.act = ix_act;
    iw.build = ix_build;
    iw.changed = ix_changed;
    if (iw.open) ix_changed();
    tw_open(&iw, S_IMPORT, 0);
}

static int  i_owns(WIN w) { return iw.open && w == iw.g.win; }
static void i_msg(EVT *m) { tw_msg(&iw, m); }
static void i_key(int k, int q) { tw_key(&iw, k, q); }
static void i_close_tool(void) { tw_close(&iw); }
static void i_docgone(DOC *d) { (void)d; }
const TOOL import_tool = { i_owns, i_msg, i_key, i_close_tool, i_docgone };

/* ---- Export ------------------------------------------------------------- */

static TWIN ew;
static int ex_fmt = FT_RTF, ex_crlf = 0, ex_cs = CS_WIN;
static short ex_f[6], ex_e[2], ex_c[4], ex_p[3], ex_go, ex_close;
static char ex_docline[48] = " ", ex_status[48] = " ", ex_doclabel[24];

static void ex_read(void)
{
    int i;
    for (i = 0; i < 6; i++) if (dlg_on(&ew.g, ex_f[i])) ex_fmt = i;
    ex_crlf = dlg_on(&ew.g, ex_e[1]);
    for (i = 0; i < 4; i++) if (dlg_on(&ew.g, ex_c[i])) ex_cs = i + 1;
    for (i = 0; i < 3; i++) if (dlg_on(&ew.g, ex_p[i])) pdf_scale = i + 1;
}

static void ex_name(void)
{
    DOC *d = live_doc(NULL);
    if (d) { strncpy(ex_docline, d->base, 46); ex_docline[46] = 0; }
    else fit_status(ex_docline, S_XF_NODOC, "");
}

static void ex_enable(void)
{
    int i;
    for (i = 0; i < 2; i++) dlg_disable(&ew.g, ex_e[i], ex_fmt != FT_TEXT && ex_fmt != FT_MD);
    for (i = 0; i < 4; i++) dlg_disable(&ew.g, ex_c[i], ex_fmt != FT_TEXT && ex_fmt != FT_MD);
    for (i = 0; i < 3; i++) dlg_disable(&ew.g, ex_p[i], ex_fmt != FT_PDF);
    dlg_disable(&ew.g, ex_go, !live_doc(NULL));
    if (ex_fmt == FT_TEXT && ex_status[0] == ' ') strcpy(ex_status, L(S_XF_STYLES));
    else if (ex_fmt != FT_TEXT && !strcmp(ex_status, L(S_XF_STYLES))) strcpy(ex_status, " ");
    ex_name();
}

static void ex_build(void)
{
    DLG *g = &ew.g;
    char *pc;
    int i;
    dlg_begin(g, NULL);
    strncpy(ex_doclabel, L(S_XF_DOC), sizeof ex_doclabel - 1);      /* "Document:" */
    pc = strchr(ex_doclabel, '%');
    if (pc) *pc = 0;
    for (pc = ex_doclabel + strlen(ex_doclabel); pc > ex_doclabel && pc[-1] == ' '; ) *--pc = 0;
    dlg_label(g, ex_doclabel);
    memset(ex_docline, ' ', 34);             /* room for long names and messages */
    ex_docline[34] = 0;
    dlg_text(g, ex_docline);
    dlg_row(g);
    dlg_label(g, L(S_FORMAT));
    for (i = 0; i < 6; i++) ex_f[i] = dlg_radio(g, 1, fmt_name(i), ex_fmt == i);
    dlg_row(g);
    dlg_label(g, L(S_LINE_ENDINGS));
    ex_e[0] = dlg_radio(g, 2, "Amiga/Unix LF", !ex_crlf);
    ex_e[1] = dlg_radio(g, 2, "Atari/DOS CR LF", ex_crlf);
    dlg_row(g);
    dlg_label(g, L(S_CHARACTERS));
    ex_c[0] = dlg_radio(g, 3, "Atari ST", ex_cs == CS_ATARI);
    ex_c[1] = dlg_radio(g, 3, "ISO-8859-1", ex_cs == CS_WIN);
    ex_c[2] = dlg_radio(g, 3, "UTF-8", ex_cs == CS_UTF8);
    ex_c[3] = dlg_radio(g, 3, "UTF-16", ex_cs == CS_UTF16);
    dlg_row(g);
    dlg_label(g, L(S_PDF_PICTURES));
    ex_p[0] = dlg_radio(g, 4, L(S_SCREEN), pdf_scale == 1);
    ex_p[1] = dlg_radio(g, 4, "2x", pdf_scale == 2);
    ex_p[2] = dlg_radio(g, 4, "3x", pdf_scale == 3);
    dlg_row(g);
    dlg_text(g, ex_status);
    ex_go = dlg_button(g, L(M_EXPORT), OF_EXIT | OF_DEFAULT);
    ex_close = dlg_button(g, L(S_CLOSE), OF_EXIT);
    ex_enable();
    dlg_end(g);
    ex_name();
    widen(g, ex_status);
    widen(g, ex_docline);
    ew.def = ex_go;
}

static void ex_changed(void)
{
    ex_read();
    ex_enable();
    tw_redraw(&ew);
}

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
    else return;                           /* the requester was cancelled */
    if (ew.open) { ex_enable(); tw_redraw(&ew); }
}

void export_open(void)
{
    ew.act = ex_act;
    ew.build = ex_build;
    ew.changed = ex_changed;
    if (ew.open) { ex_enable(); tw_redraw(&ew); }
    tw_open(&ew, S_EXPORT, 0);
}

static int  e_owns(WIN w) { return ew.open && w == ew.g.win; }
static void e_msg(EVT *m) { tw_msg(&ew, m); }
static void e_key(int k, int q) { tw_key(&ew, k, q); }
static void e_close_tool(void) { tw_close(&ew); }
static void e_docgone(DOC *d) { (void)d; }
const TOOL export_tool = { e_owns, e_msg, e_key, e_close_tool, e_docgone };

/* the document used last changed, or closed: keep the windows up to date */
void xfer_refresh(void)
{
    if (ew.open) {
        char old[48];
        int dis = ew.g.it[ex_go].disabled;
        strcpy(old, ex_docline);
        ex_enable();
        if (strcmp(old, ex_docline) || dis != ew.g.it[ex_go].disabled) tw_redraw(&ew);
    }
}
