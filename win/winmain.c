/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * winmain.c - EdWord for Windows: start-up, preferences, the frame window
 *             and its menus, commands and the message loop (after
 *             amiga/ami.c)
 */
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "winui.h"

HINSTANCE hinst;
WIN   frame, mdiclient;
HFONT uifont, fixfont;
short wchar, hchar, sys_asc, scr_w, scr_h, npens = 16, small_icons;
GRECT desk;
int   colour_on = 1, pdf_scale = 1, quit_now;
DOC  *docs[MAX_DOCS];               /* made when first needed, then kept */
char  progdir[MAX_PATH], datadir[MAX_PATH];
const short sizes[NSIZES] = { 8, 9, 10, 12, 14, 18, 24, 36 };
const short zooms[NZOOM] = { 50, 75, 100, 125, 150, 200 };
static int  untitled_no;
static char appname[40];
static HMENU menubar;

/* ================================================================= */
/*  Preferences, kept in EdWord.ini (in Application Data\EdWord, or   */
/*  next to the program on Windows 95)                                */
/* ================================================================= */

int pref_date = DT_SHORT, pref_toolbar = 1, pref_paper = P_A4, pref_land;
int pref_autosave, pref_asmin = 5, pref_asmode, pref_display, pref_live;
int pref_rulers, pref_guides = 1;
static int pref_lang = -1, pref_spell = -1;
static int pref_maxim = 1, pref_fx = -1, pref_fy, pref_fw, pref_fh;   /* the frame window */

static void prefs_path(char *p) { sprintf(p, "%sEdWord.ini", datadir); }

static void load_prefs(void)
{
    char path[MAX_PATH + 20], line[80];
    FILE *f;
    prefs_path(path);
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        int v;
        if (!eq) continue;
        *eq = 0;
        v = atoi(eq + 1);
        if (!strcmp(line, "date") && v >= 0 && v < NDATEFMT) pref_date = v;
        else if (!strcmp(line, "toolbar")) pref_toolbar = v != 0;
        else if (!strcmp(line, "colour")) colour_on = v != 0;
        else if (!strcmp(line, "pdfscale") && v >= 1 && v <= 3) pdf_scale = v;
        else if (!strcmp(line, "paper") && v >= 0 && v < NPAPER) pref_paper = v;
        else if (!strcmp(line, "landscape")) pref_land = v != 0;
        else if (!strcmp(line, "autosave")) pref_autosave = v != 0;
        else if (!strcmp(line, "autosavemin") && v >= 1 && v <= 99) pref_asmin = v;
        else if (!strcmp(line, "autosavemode")) pref_asmode = v != 0;
        else if (!strcmp(line, "display") && v >= 0 && v <= 3) pref_display = v;
        else if (!strcmp(line, "language") && v >= 0 && v < NLANG) pref_lang = v;
        else if (!strcmp(line, "livespell")) pref_live = v != 0;
        else if (!strcmp(line, "spelllang") && v >= 0 && v < NLANG) pref_spell = v;
        else if (!strcmp(line, "rulers")) pref_rulers = v != 0;
        else if (!strcmp(line, "guides")) pref_guides = v != 0;
        else if (!strcmp(line, "maximised")) pref_maxim = v != 0;
        else if (!strcmp(line, "framex")) pref_fx = v;
        else if (!strcmp(line, "framey")) pref_fy = v;
        else if (!strcmp(line, "framew")) pref_fw = v;
        else if (!strcmp(line, "frameh")) pref_fh = v;
    }
    fclose(f);
}

static void frame_place(void)
{
    WINDOWPLACEMENT wp;
    if (!frame) return;
    memset(&wp, 0, sizeof wp);
    wp.length = sizeof wp;
    if (!GetWindowPlacement(frame, &wp)) return;
    pref_maxim = wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED);
    pref_fx = wp.rcNormalPosition.left;
    pref_fy = wp.rcNormalPosition.top;
    pref_fw = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    pref_fh = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
}

static void write_prefs(int quiet)
{
    char path[MAX_PATH + 20];
    FILE *f;
    frame_place();
    prefs_path(path);
    f = fopen(path, "w");
    if (!f) { if (!quiet) alert(1, 3, AL_NO_INF, AB_OK); return; }
    fprintf(f, "date=%d\ntoolbar=%d\ncolour=%d\npdfscale=%d\npaper=%d\nlandscape=%d\n"
               "autosave=%d\nautosavemin=%d\nautosavemode=%d\ndisplay=%d\nlanguage=%d\nlivespell=%d\n"
               "rulers=%d\nguides=%d\nspelllang=%d\nmaximised=%d\nframex=%d\nframey=%d\nframew=%d\nframeh=%d\n",
            pref_date, pref_toolbar, colour_on, pdf_scale, pref_paper, pref_land,
            pref_autosave, pref_asmin, pref_asmode, pref_display, ui_lang, pref_live, pref_rulers, pref_guides, spell_lang,
            pref_maxim, pref_fx, pref_fy, pref_fw, pref_fh);
    fclose(f);
}

void save_prefs(void) { write_prefs(0); }

/* the language Windows is set to */
static int system_language(void)
{
    switch (PRIMARYLANGID(GetUserDefaultLangID())) {
    case LANG_GERMAN:  return 1;
    case LANG_SPANISH: return 2;
    case LANG_ITALIAN: return 3;
    case LANG_FRENCH:  return 4;
    }
    return 0;
}

/* month and day names for dates, in the current language (ST characters,
 * as they go into the text) */
static char mnames_st[12][16], dnames_st[7][16];
static const char *mnames[12], *dnames[7];

void set_language(int lang)
{
    int i;
    ui_lang = lang >= 0 && lang < NLANG ? lang : 0;
    for (i = 0; i < 12; i++) { stc(mnames_st[i], L(S_MONTH1 + i), 16); mnames[i] = mnames_st[i]; }
    for (i = 0; i < 7; i++) { stc(dnames_st[i], L(S_DAY0 + i), 16); dnames[i] = dnames_st[i]; }
    date_months = mnames;
    date_days = dnames;
}

/* ================================================================= */
/*  Modal dialogs: the other windows take no input meanwhile          */
/* ================================================================= */

#define MAXOFF 64
static HWND offw[MAXOFF];
static int  offlev[MAXOFF], noff, blockn;

static BOOL CALLBACK disable_one(HWND h, LPARAM lp)
{
    (void)lp;
    if (IsWindowVisible(h) && IsWindowEnabled(h) && noff < MAXOFF &&
        (h == frame || GetWindow(h, GW_OWNER) == frame)) {
        EnableWindow(h, FALSE);
        offlev[noff] = blockn;
        offw[noff++] = h;
    }
    return TRUE;
}

void block_input(int on)
{
    if (on) {
        blockn++;
        EnumThreadWindows(GetCurrentThreadId(), disable_one, 0);
        busy(1);
    } else if (blockn > 0) {
        busy(0);
        while (noff > 0 && offlev[noff - 1] == blockn) {
            noff--;
            if (IsWindow(offw[noff])) EnableWindow(offw[noff], TRUE);
        }
        blockn--;
    }
}

int block_now(void) { return blockn > 0; }

void frame_enable(int on) { EnableWindow(frame, on ? TRUE : FALSE); }

/* ================================================================= */
/*  The menus, built at run time in the current language              */
/* ================================================================= */

#define MAXFONTMENU 30
enum { MI_TITLE, MI_ITEM, MI_CHECK, MI_BAR, MI_END };
typedef struct { int type; const char *label; const char *key; int cmd; } MITEM;
#define MAXMI 260
static MITEM mi[MAXMI];
static int   nmi;
static char  dyn[90][40];             /* sizes, fonts, colours, dictionaries, windows */
static int   font_items, win_items;
static short winmap[MAX_DOCS];        /* Window menu entry -> document slot */

/* Ctrl shortcuts: the key ("N", "+", "Shift+S") of each command */
static struct { int cmd, vk, shift; } keys[48];
static int nkeys;

static void mi_add(int type, const char *label, const char *key, int cmd)
{
    MITEM *m;
    if (nmi >= MAXMI - 1) return;
    m = &mi[nmi++];
    m->type = type;
    m->label = label;
    m->key = key;
    m->cmd = cmd;
    if (key && cmd && nkeys < (int)(sizeof keys / sizeof keys[0])) {
        int shift = !strncmp(key, "Shift+", 6);
        const char *k = shift ? key + 6 : key;
        if (k[1] == 0) {
            keys[nkeys].cmd = cmd;
            keys[nkeys].vk = (unsigned char)k[0];
            keys[nkeys].shift = shift;
            nkeys++;
        }
    }
}

#define TITLE(t)          mi_add(MI_TITLE, L(t), NULL, 0)
#define ITEM(t, k, c)     mi_add(MI_ITEM, L(t), k, c)
#define CITEM(t, k, c)    mi_add(MI_CHECK, L(t), k, c)
#define DITEM_(s, c)      mi_add(MI_CHECK, s, NULL, c)
#define BAR()             mi_add(MI_BAR, NULL, NULL, 0)

static const char *dyn_text(int slot, const char *s)
{
    lstrcpynA(dyn[slot], s, sizeof dyn[slot]);
    return dyn[slot];
}

static void build_menu(void)
{
    int i;
    static const int sty[4] = { M_PLAIN, M_BOLD, M_ITALIC, M_UNDER };
    static const char *styk[4] = { NULL, "B", "I", "U" };
    static const short styc[4] = { C_PLAIN, C_BOLD, C_ITALIC, C_UNDER };
    nmi = 0;
    nkeys = 0;

    /* the usual Windows menus and keys */
    TITLE(MT_FILE);
    ITEM(M_NEW, "N", C_NEW); ITEM(M_OPEN, "O", C_OPEN); ITEM(M_IMPORT, NULL, C_IMPORT); BAR();
    ITEM(M_SAVEDOC, "S", C_SAVE); ITEM(M_SAVEAS, "Shift+S", C_SAVEAS); ITEM(M_EXPORT, NULL, C_EXPORT); BAR();
    ITEM(M_PAGE, NULL, C_PAGE); ITEM(M_HEADFOOT, NULL, C_HEADFOOT); ITEM(M_PREVIEW, NULL, C_PREVIEW);
    ITEM(M_PRINT, "P", C_PRINT); BAR();
    ITEM(M_CLOSE, "W", C_CLOSE); BAR();
    mi_add(MI_ITEM, L(S_W_EXIT), "Alt+F4", C_QUIT);

    TITLE(MT_EDIT);
    ITEM(M_UNDO, "Z", C_UNDO); ITEM(M_REDO, "Y", C_REDO); BAR();
    ITEM(M_CUT, "X", C_CUT); ITEM(M_COPY, "C", C_COPY); ITEM(M_PASTE, "V", C_PASTE); ITEM(M_SELALL, "A", C_SELALL); BAR();
    ITEM(M_FIND, "F", C_FIND); ITEM(M_FINDNEXT, "F3", C_FINDNEXT); BAR();
    ITEM(M_PREVPAGE, NULL, C_PREVPAGE); ITEM(M_NEXTPAGE, NULL, C_NEXTPAGE); ITEM(M_GOTOPAGE, "G", C_GOTOPAGE); BAR();
    ITEM(M_REPAG, "F2", C_REPAG);

    TITLE(MT_INSERT);
    ITEM(M_DATE, "D", C_INSDATE); ITEM(M_TIME, NULL, C_INSTIME); BAR();
    mi_add(MI_ITEM, L(M_PGBRK), "Ctrl+Enter", C_PGBRK); mi_add(MI_ITEM, L(M_COLBRK), "Ctrl+Shift+Enter", C_COLBRK);
    ITEM(M_PAGENUM, NULL, C_PAGENUM); BAR();
    ITEM(M_TBLINS, NULL, C_TBLINS); ITEM(M_TBLPROP, NULL, C_TBLPROP); ITEM(M_ROWINS, NULL, C_ROWINS);
    ITEM(M_ROWDEL, NULL, C_ROWDEL); ITEM(M_COLINS, NULL, C_COLINS); ITEM(M_COLDEL, NULL, C_COLDEL);
    ITEM(M_TBLDEL, NULL, C_TBLDEL); ITEM(M_CHART, NULL, C_CHART); BAR();
    ITEM(M_PICTURE, NULL, C_PICTURE); ITEM(M_DRAWING, NULL, C_DRAWING); BAR();
    ITEM(M_TOC, NULL, C_TOC); BAR(); ITEM(M_INSFILE, NULL, C_INSFILE);

    TITLE(MT_FONT);
    ITEM(M_FONTSEL, "T", C_FONTSEL); ITEM(M_SIZEOTHER, NULL, C_SIZEOTHER); BAR();
    for (i = 0; i < NSIZES; i++) {
        char s[20];
        sprintf(s, "%d %s", sizes[i], L(S_POINT));
        DITEM_(dyn_text(i, s), C_SIZE0 + i);
    }
    BAR();
    font_items = nfonts <= MAXFONTMENU ? nfonts : MAXFONTMENU - 1;
    if (font_items < 1) font_items = 1;
    for (i = 0; i < font_items && i < nfonts; i++) DITEM_(dyn_text(10 + i, fonts[i].name), C_FONT0 + i);
    if (font_items < nfonts) ITEM(M_MOREFONTS, NULL, C_MOREFONTS);

    TITLE(MT_STYLE);
    for (i = 0; i < 4; i++) {
        if (i == 0) ITEM(sty[i], styk[i], styc[i]);
        else CITEM(sty[i], styk[i], styc[i]);
        if (i == 0) BAR();
    }
    BAR();
    for (i = 0; i < NCOLORS; i++) DITEM_(dyn_text(50 + i, L(S_COL_BLACK + i)), C_COLOR0 + i);

    TITLE(MT_PARA);
    CITEM(M_JLEFT, "L", C_JLEFT); CITEM(M_JCENTRE, "E", C_JCENTRE); CITEM(M_JRIGHT, "R", C_JRIGHT);
    CITEM(M_JFULL, "J", C_JFULL); BAR();
    CITEM(M_SP1, "1", C_SP1); CITEM(M_SP15, "5", C_SP15); CITEM(M_SP2, "2", C_SP2); BAR();
    ITEM(M_INDENTS, NULL, C_INDENTS); CITEM(M_HANGING, NULL, C_HANGING); CITEM(M_BULLETS, "F9", C_BULLETS);
    CITEM(M_NUMBERS, "F10", C_NUMBERS); BAR();
    CITEM(M_HEAD1, NULL, C_HEAD1); CITEM(M_HEAD2, NULL, C_HEAD2); CITEM(M_HEAD3, NULL, C_HEAD3); CITEM(M_BODY, NULL, C_BODY); BAR();
    ITEM(M_WRAPCOL, NULL, C_WRAPCOL);

    TITLE(MT_TOOLS);
    ITEM(M_SPELL, "K", C_SPELL); CITEM(M_LIVESPELL, NULL, C_LIVESPELL); ITEM(M_THES, "Shift+F7", C_THES);
    ITEM(M_SUMMARY, NULL, C_SUMMARY); BAR();
    ITEM(M_UDLOAD, NULL, C_UDLOAD); ITEM(M_UDNEW, NULL, C_UDNEW); BAR();
    for (i = 0; i < MAX_UDICT; i++) DITEM_(dyn_text(60 + i, L(S_NO_UDICT)), C_UD0 + i);
    BAR(); ITEM(M_PREFS, NULL, C_PREFS);

    TITLE(MT_WINDOW);
    CITEM(M_VDOC, "F4", C_VDOC); CITEM(M_SPLIT, "F5", C_SPLIT); CITEM(M_VCODES, "F6", C_VCODES); BAR();
    ITEM(M_ZOOMIN, "+", C_ZOOMIN); ITEM(M_ZOOMOUT, "-", C_ZOOMOUT); BAR();
    CITEM(M_TOOLBAR, NULL, C_TOOLBAR); CITEM(M_RULERS, NULL, C_RULERS); CITEM(M_GUIDES, NULL, C_GUIDES); BAR();
    ITEM(M_TILEV, NULL, C_TILEV); ITEM(M_TILEH, NULL, C_TILEH); ITEM(M_CASCADE, NULL, C_CASCADE); BAR();
    mi_add(MI_CHECK, L(S_W_MINIMISE), "M", C_ICONIFY); mi_add(MI_ITEM, L(S_W_MINALL), NULL, C_ICONALL);
    ITEM(M_BOTTOM, NULL, C_BOTTOM); mi_add(MI_ITEM, L(M_CYCLE), "Ctrl+Tab", C_CYCLE); BAR();
    win_items = MAX_DOCS < 9 ? MAX_DOCS : 9;
    for (i = 0; i < win_items; i++) DITEM_(dyn_text(70 + i, "-"), C_WIN0 + i);
    if (win_items < MAX_DOCS) ITEM(M_WINMORE, NULL, C_WINMORE);

    TITLE(S_W_HELP);
    mi_add(MI_ITEM, L(M_ABOUT), "F1", C_ABOUT);
    mi_add(MI_END, NULL, NULL, 0);
}

/* "&" marks the first letter of a menu item for Alt; a key goes after a tab */
static void item_text(char *out, const MITEM *m)
{
    char *o = out;
    const char *s = m->label;
    while (*s == ' ') s++;
    if (m->type == MI_TITLE) *o++ = '&';
    for (; *s && o < out + 70; s++) {
        if (*s == '&') *o++ = '&';
        *o++ = *s;
    }
    if (m->key) {
        const char *k = m->key;
        int plain = (k[0] == 'F' && k[1] >= '0' && k[1] <= '9') || !strncmp(k, "Alt+", 4) || !strncmp(k, "Ctrl+", 5) ||
                    !strncmp(k, "Shift+F", 7);
        o += sprintf(o, "\t%s%s", plain ? "" : "Ctrl+", k);
    }
    *o = 0;
}

static char shown[NCMD][72];

static HMENU make_menu(void)
{
    HMENU bar = CreateMenu(), pop = NULL;
    int i;
    char t[80];
    build_menu();
    memset(shown, 0, sizeof shown);
    for (i = 0; i < nmi && mi[i].type != MI_END; i++) {
        MITEM *m = &mi[i];
        switch (m->type) {
        case MI_TITLE:
            pop = CreatePopupMenu();
            item_text(t, m);
            AppendMenuA(bar, MF_POPUP | MF_STRING, (UINT_PTR)pop, t);
            break;
        case MI_BAR:
            if (pop) AppendMenuA(pop, MF_SEPARATOR, 0, NULL);
            break;
        default:
            if (!pop) break;
            item_text(t, m);
            AppendMenuA(pop, MF_STRING, (UINT_PTR)m->cmd, t);
            if (m->cmd > 0 && m->cmd < NCMD) lstrcpynA(shown[m->cmd], t, sizeof shown[0]);
        }
    }
    return bar;
}

static void check(int cmd, int on) { if (menubar) CheckMenuItem(menubar, (UINT)cmd, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); }
static void enable(int cmd, int on) { if (menubar) EnableMenuItem(menubar, (UINT)cmd, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); }
static void set_item(int cmd, const char *text)
{
    char t[72];
    UINT st;
    if (!menubar || cmd <= 0 || cmd >= NCMD) return;
    lstrcpynA(t, text, sizeof t);
    if (!strcmp(t, shown[cmd])) return;
    st = GetMenuState(menubar, (UINT)cmd, MF_BYCOMMAND);
    if (st == (UINT)-1) return;
    ModifyMenuA(menubar, (UINT)cmd, MF_BYCOMMAND | MF_STRING | (st & (MF_CHECKED | MF_GRAYED)), (UINT_PTR)cmd, t);
    strcpy(shown[cmd], t);
}

static int enabled(int cmd)
{
    UINT st = menubar ? GetMenuState(menubar, (UINT)cmd, MF_BYCOMMAND) : (UINT)-1;
    return st != (UINT)-1 && !(st & (MF_GRAYED | MF_DISABLED));
}

void update_menu(DOC *d)
{
    int i, intbl = 0, hasdoc = d != NULL && !d->iconified;
    PARA *p = NULL;
    if (!menubar) return;
    for (i = 0; i < NCOLORS; i++) enable(C_COLOR0 + i, colour_on);
    check(C_LIVESPELL, pref_live);
    for (i = 0; i < MAX_UDICT; i++) {
        if (i < spell_nuser) {
            const char *s = strrchr(spell_upath[i], '\\');
            if (!s) s = strrchr(spell_upath[i], '/');
            set_item(C_UD0 + i, s ? s + 1 : spell_upath[i]);
        } else set_item(C_UD0 + i, L(S_NO_UDICT));
        enable(C_UD0 + i, i < spell_nuser);
        check(C_UD0 + i, i == spell_active);
    }
    for (i = C_UNDO; i <= C_WRAPCOL; i++) enable(i, hasdoc);
    for (i = C_SAVE; i <= C_CLOSE; i++) enable(i, d != NULL);
    enable(C_SPELL, hasdoc); enable(C_THES, hasdoc); enable(C_SUMMARY, hasdoc);
    xfer_refresh();
    for (i = C_VDOC; i <= C_TOOLBAR; i++) enable(i, hasdoc);
    enable(C_RULERS, 1); enable(C_GUIDES, 1);
    enable(C_ICONIFY, d != NULL); enable(C_BOTTOM, d != NULL);
    for (i = 0; i < NSIZES; i++) enable(C_SIZE0 + i, hasdoc);
    for (i = 0; i < font_items; i++) enable(C_FONT0 + i, hasdoc);
    for (i = 0; i < NCOLORS; i++) if (!hasdoc) enable(C_COLOR0 + i, 0);
    if (d && hasdoc) {
        intbl = doc_in_table(d, d->cursor, NULL, NULL) != 0;
        p = doc_para_at(d, d->cursor);
        enable(C_UNDO, undo_can(d) || d->ch_lo >= 0);
        enable(C_REDO, redo_can(d));
        enable(C_TBLPROP, intbl); enable(C_ROWINS, intbl); enable(C_ROWDEL, intbl);
        enable(C_COLINS, intbl); enable(C_COLDEL, intbl); enable(C_TBLDEL, intbl);
        enable(C_TBLINS, !intbl);
        enable(C_ZOOMIN, d->view != VIEW_CODES && d->zoom < zooms[NZOOM - 1]);
        enable(C_ZOOMOUT, d->view != VIEW_CODES && d->zoom > zooms[0]);
    }
    for (i = 0; i < font_items; i++) check(C_FONT0 + i, d && i == d->ins.font);
    for (i = 0; i < NSIZES; i++) check(C_SIZE0 + i, d && sizes[i] == d->ins.size);
    for (i = 0; i < NCOLORS; i++) check(C_COLOR0 + i, d && i == d->ins.color);
    check(C_BOLD, d && (d->ins.attr & A_BOLD));
    check(C_ITALIC, d && (d->ins.attr & A_ITALIC));
    check(C_UNDER, d && (d->ins.attr & A_UNDER));
    check(C_JLEFT, p && p->just == J_LEFT);   check(C_JCENTRE, p && p->just == J_CENTRE);
    check(C_JRIGHT, p && p->just == J_RIGHT); check(C_JFULL, p && p->just == J_FULL);
    check(C_SP1, p && p->spacing == LS_SINGLE); check(C_SP15, p && p->spacing == LS_ONEHALF);
    check(C_SP2, p && p->spacing == LS_DOUBLE);
    check(C_HANGING, p && p->first < 0);
    check(C_BULLETS, p && p->list == LIST_BULLET); check(C_NUMBERS, p && p->list == LIST_NUMBER);
    check(C_HEAD1, p && p->toc == 1); check(C_HEAD2, p && p->toc == 2); check(C_HEAD3, p && p->toc == 3);
    check(C_BODY, p && p->toc == 0);
    check(C_VDOC, d && d->view == VIEW_DOC); check(C_SPLIT, d && d->view == VIEW_SPLIT);
    check(C_VCODES, d && d->view == VIEW_CODES);
    check(C_TOOLBAR, d && d->toolbar);
    check(C_ICONIFY, d && d->iconified);
    {
        DOC *t = top_doc();
        int k = 0, n = 0;
        for (i = 0; i < MAX_DOCS; i++) {
            char s[48];
            if (!DOC_USED(i)) continue;
            n++;
            if (k >= win_items) continue;
            sprintf(s, "&%d  %.30s", k + 1, lat(docs[i]->base));
            { /* a & in a name would mark a letter */
                char e[64], *o = e;
                const char *q;
                for (q = s; *q && o < e + 60; q++) { if (*q == '&' && q != s) *o++ = '&'; *o++ = *q; }
                *o = 0;
                set_item(C_WIN0 + k, e);
            }
            enable(C_WIN0 + k, 1);
            check(C_WIN0 + k, docs[i] == t);
            winmap[k++] = (short)i;
        }
        for (; k < win_items; k++) {
            set_item(C_WIN0 + k, "-");
            enable(C_WIN0 + k, 0);
            check(C_WIN0 + k, 0);
            winmap[k] = -1;
        }
        enable(C_WINMORE, n > win_items);
    }
    check(C_RULERS, pref_rulers);
    check(C_GUIDES, pref_guides);
}

void update_winmenu(void) { update_menu(top_doc()); }

void rebuild_menu(void)
{
    HMENU old = menubar;
    menubar = make_menu();
    SetMenu(frame, menubar);
    if (old) DestroyMenu(old);
    DrawMenuBar(frame);
    update_menu(top_doc());
}

/* a Ctrl key: the command it is the shortcut of */
int menu_key(int key, int qual)
{
    int i, shift = (qual & KQ_SHIFT) != 0;
    if (key >= 'a' && key <= 'z') key -= 32;
    for (i = 0; i < nkeys; i++)
        if (keys[i].vk == key && keys[i].shift == shift) {
            int c = keys[i].cmd;
            update_menu(top_doc());
            if (enabled(c)) do_cmd(c);
            if (!quit_now) update_menu(top_doc());
            return 1;
        }
    return 0;
}

/* ================================================================= */
/*  Tool windows                                                      */
/* ================================================================= */

const TOOL *const tools[] = { &spell_tool, &find_tool, &thes_tool, &import_tool, &export_tool, &fontsel_tool, &preview_tool, &draw_tool, NULL };

int tool_owns(WIN w)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(w)) return 1;
    return 0;
}

int tool_route(EVT *m)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(m->IDCMPWindow)) { tools[i]->msg(m); return 1; }
    return 0;
}

int tool_route_key(WIN w, int key, int qual)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(w)) { tools[i]->key(key, qual); return 1; }
    return 0;
}

static void tools_close_all(void)
{
    int i;
    for (i = 0; tools[i]; i++) tools[i]->close();
}

/* ================================================================= */
/*  Documents                                                         */
/* ================================================================= */

DOC *doc_of_win(WIN w)
{
    int i;
    if (!w) return NULL;
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i) && doc_win(docs[i]) == w) return docs[i];
    return NULL;
}

static DOC *last_doc;

/* the window of ours that is active (a top-level one), if any */
static WIN active_win(void)
{
    HWND a = GetActiveWindow();
    if (!a) return NULL;
    if (a == frame) {
        HWND c = mdiclient ? (HWND)SendMessageA(mdiclient, WM_MDIGETACTIVE, 0, 0) : NULL;
        return c;
    }
    return a;
}

/* the document that menu commands act on: the active document window, or,
 * when a tool window is active, the document window used last */
DOC *active_doc(void)
{
    WIN w = active_win();
    DOC *d = doc_of_win(w);
    int i;
    if (d) return last_doc = d;
    if (spell_doc() && w && tool_owns(w)) return spell_doc();
    if (last_doc && last_doc->used) return last_doc;
    for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i)) return docs[i];
    return NULL;
}

DOC *top_doc(void)
{
    WIN w = active_win();
    DOC *d = doc_of_win(w);
    if (d) return last_doc = d;
    if (w && tool_owns(w)) return active_doc();
    if (mdiclient) {
        HWND c = (HWND)SendMessageA(mdiclient, WM_MDIGETACTIVE, 0, 0);
        d = doc_of_win(c);
        if (d) return last_doc = d;
    }
    if (last_doc && last_doc->used) return last_doc;
    return NULL;
}

void activate_doc(DOC *d)
{
    WIN w = doc_win(d);
    if (!w) return;
    SetActiveWindow(frame);
    SendMessageA(mdiclient, WM_MDIACTIVATE, (WPARAM)w, 0);
    SetFocus(w);
    last_doc = d;
}

void window_closing(WIN w) { (void)w; }

/* the document's short name (ST characters) from a path */
static void base_name(DOC *d, const char *path)
{
    const char *s = strrchr(path, '\\');
    if (!s) s = strrchr(path, '/');
    if (!s) s = strrchr(path, ':');
    s = s ? s + 1 : path;
    stc(d->base, s, sizeof d->base);
}

static DOC *new_doc(void)
{
    int i;
    for (i = 0; i < MAX_DOCS; i++)
        if (!DOC_USED(i)) {
            if (!docs[i]) docs[i] = (DOC *)malloc(sizeof(DOC));
            if (!docs[i]) { alert(1, 3, AL_NO_MEMORY, AB_OK); return NULL; }
            doc_init(docs[i]);
            docs[i]->used = 1;
            docs[i]->wh = -1;
            docs[i]->slot = i;
            return docs[i];
        }
    alert(1, 1, AL_FOUR_DOCS, AB_OK, MAX_DOCS);
    return NULL;
}

/* a new document takes the page size and toolbar from the preferences */
static void doc_prefs(DOC *d)
{
    short ml = PS(d).ml, mr = PS(d).mr, mt = PS(d).mt, mb = PS(d).mb;
    page_set_paper(&PS(d), pref_paper, pref_land);
    PS(d).ml = ml; PS(d).mr = mr; PS(d).mt = mt; PS(d).mb = mb;
    d->toolbar = pref_toolbar;
}

void doc_loaded(DOC *d)
{
    busy_begin(L(S_OPENING));
    doc_layout(d);
    busy_end();
    d->cursor = 0;
    d->anchor = -1;
    doc_sync_ins(d);
    undo_reset(d);
}

static void cmd_new(void)
{
    DOC *d = new_doc();
    char s[40];
    if (!d) return;
    doc_prefs(d);
    sprintf(s, "%s %d", L(S_UNTITLED), ++untitled_no);
    stc(d->base, s, sizeof d->base);
    doc_loaded(d);
    open_window(d);
}

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext), i;
    if (n < m) return 0;
    for (i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != ext[i]) return 0;
    }
    return 1;
}

static int text_name(const char *s)
{
    return ends_with(s, ".TXT") || ends_with(s, ".ASC") || ends_with(s, ".TEXT");
}

/* Read a file as type (FT_AUTO: what it turns out to be, plain text if
 * nothing else) into a new window, or into 'into' at its cursor. Plain
 * text uses cs and join, or with ask asks how to read it. */
int read_into(const char *path, int type, int cs, int join, DOC *into, int ask)
{
    static DOC tmp;
    DOC *d;
    int ok;
    unsigned char *b = NULL;
    long n = 0;

    if (type == FT_AUTO) type = file_detect(path, 1);
    if (type == FT_TEXT) {
        int eol, det;
        b = load_file(path, &n);
        if (!b) { alert(1, 3, AL_CANT_READ, AB_OK); return 0; }
        if (ask) {
            text_detect(b, n, &eol, &det);
            if (!dlg_textimport(eol, det, &cs, &join)) { free(b); return 0; }
        }
    }
    if (into) {
        d = &tmp;
        doc_init(d);
        d->ins = into->ins;
    } else {
        d = new_doc();
        if (!d) { free(b); return 0; }
        doc_prefs(d);
    }
    busy_begin(L(into ? S_IMPORTING : S_OPENING));
    prog_part(0, into ? 1000 : 650);           /* then laying it out */
    ok = type == FT_TEXT ? text_import_buf(d, b, n, cs, join) : doc_read(d, path, type, cs, join);
    free(b);
    if (into || !ok) busy_end();
    if (!ok) {
        alert(1, 3, AL_CANT_READ, AB_OK);
        doc_free(d);
        if (!into) d->used = 0;
        return 0;
    }
    if (into) {
        ok = d->len > 0 && insert_loaded(into, d);
        doc_free(d);
        return ok;
    }
    base_name(d, path);
    if (type == FT_RTF) strncpy(d->path, path, sizeof d->path - 1);
    prog_part(650, 1000);
    doc_loaded(d);
    busy_end();
    if (type != FT_RTF) d->dirty = 1;
    open_window(d);
    return 1;
}

/* text: 0 open (RTF, or what the file turns out to be), 1 import (plain
 * text asks how to read it) */
void open_path(const char *path, int text)
{
    read_into(path, file_detect(path, text), CS_AUTO, 0, NULL, 1);
}

/* a file name for the document with another extension (Windows-1252) */
static void default_name(DOC *d, const char *ext, char *def)
{
    char *s, *sl;
    if (d->path[0] && strlen(d->path) < MAX_PATH - 8) strcpy(def, d->path);   /* in the document's folder */
    else strcpy(def, lat(d->base));
    s = strrchr(def, '.');
    sl = strrchr(def, '\\');
    if (s && s != def && (!sl || s > sl)) *s = 0;
    strcat(def, ext);
}

static int save_as(DOC *d)
{
    char path[MAX_PATH + 20], def[MAX_PATH + 20];
    default_name(d, ".rtf", def);
    if (!ask_file(L(S_FS_SAVE), "#?.rtf", def, path, 1)) return 0;
    strncpy(d->path, path, sizeof d->path - 1);
    d->path[sizeof d->path - 1] = 0;
    base_name(d, path);
    return 1;
}

static int cmd_save(DOC *d, int ask)
{
    int ok;
    if (ask || !d->path[0]) if (!save_as(d)) return 0;
    busy_begin(L(S_SAVING));
    ok = rtf_export(d, d->path);
    busy_end();
    if (!ok) { alert(1, 3, AL_CANT_SAVE, AB_OK); return 0; }
    set_name(d);
    update_winmenu();
    return 1;
}

void cmd_pdf(DOC *d)
{
    char path[MAX_PATH + 20], def[MAX_PATH + 20];
    int ok;
    default_name(d, ".pdf", def);
    if (!ask_file(L(S_FS_PDF), "#?.pdf", def, path, 1)) return;
    gauge_open(L(S_EXPORTING_PDF));
    ok = export_pdf(d, path, gauge_set);
    gauge_close();
    if (!ok) alert(1, 3, AL_CANT_PDF, AB_OK);
}

/* The Export window: the file dialog for the format, then write a copy of
 * the document (its own name and path don't change). written gets the
 * file's name; it stays empty if the dialog was cancelled. */
int export_doc(DOC *d, int fmt, int crlf, int cs, char *written)
{
    static const char *ext[] = { ".rtf", ".txt", ".md", ".doc", ".pro", ".pdf" };
    static const char *pat[] = { "#?.rtf", "#?.txt", "#?.md", "#?.doc", "#?", "#?.pdf" };
    static const int title[] = { S_FS_EXPRTF, S_FS_EXPORT, S_FS_EXPMD, S_FS_EXPFWP, S_FS_EXPPT, S_FS_PDF };
    char path[MAX_PATH + 20], def[MAX_PATH + 20];
    const char *base;
    int ok;
    written[0] = 0;
    if (fmt < FT_RTF || fmt > FT_PDF) return 0;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    default_name(d, ext[fmt], def);
    if (!ask_file(L(title[fmt]), pat[fmt], def, path, 1)) return 0;
    base = strrchr(path, '\\');
    if (!base) base = strrchr(path, '/');
    strncpy(written, base ? base + 1 : path, 39);
    written[39] = 0;
    if (fmt == FT_PDF) {
        gauge_open(L(S_EXPORTING_PDF));
        ok = export_pdf(d, path, gauge_set);
        gauge_close();
        return ok;
    }
    busy_begin(L(S_EXPORTING));
    ok = doc_write(d, path, fmt, crlf, cs);
    busy_end();
    return ok;
}

static void cmd_export(DOC *d, int fmt)
{
    char name[40];
    if (!export_doc(d, fmt, 1, CS_WIN, name) && name[0]) alert(1, 3, AL_CANT_WRITE, AB_OK);
}

/* File > Print: the Windows Print dialog chooses the printer, pages and copies */
void cmd_print(DOC *d)
{
    int from, to, copies;
    char name[160];
    if (!d) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    doc_layout(d);
    if (!printer_name(name)) { alert(1, 3, AL_W_NO_PRINTER, AB_OK); return; }
    if (!print_choose(d, &from, &to, &copies) || !d->used) return;
    {
        int np = doc_npages(d), k;
        unsigned char *sel = (unsigned char *)calloc((size_t)np + 1, 1);
        if (!sel) { alert(1, 3, AL_NO_MEMORY, AB_OK); return; }
        for (k = from; k <= to && k <= np; k++) if (k >= 1) sel[k - 1] = 1;
        if (!print_doc(d, sel, copies)) alert(1, 3, AL_PRINT_FAILED, AB_OK);
        free(sel);
    }
}

/* Edit > Repaginate: every page laid out again, the contents' page numbers
 * brought up to date */
static void cmd_repag(DOC *d)
{
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    busy_begin(L(S_REPAGINATING));
    doc_repaginate(d);
    busy_end();
    doc_changed(d);
}

static void cmd_exptxt(DOC *d)
{
    char path[MAX_PATH + 20], def[MAX_PATH + 20];
    int crlf, cs;
    if (!dlg_textexport(&crlf, &cs)) return;
    default_name(d, ".txt", def);
    if (!ask_file(L(S_FS_EXPORT), "#?.txt", def, path, 1)) return;
    busy_begin(L(S_EXPORTING));
    if (!text_export(d, path, crlf, cs)) { busy_end(); alert(1, 3, AL_CANT_WRITE, AB_OK); return; }
    busy_end();
}

int close_doc(DOC *d)
{
    int i;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    if (d->dirty) {
        int r;
        if (d->iconified) uniconify(d);
        activate_doc(d);
        r = alert(1, 2, AL_SAVE_CHANGES, AB_SAVE_DISCARD, lat(d->base));
        if (r == 3) return 0;
        if (r == 1 && !cmd_save(d, 0)) return 0;
    }
    for (i = 0; tools[i]; i++) tools[i]->doc_closed(d);
    if (last_doc == d) last_doc = NULL;
    close_window(d);
    img_cache_free(d);
    doc_free(d);
    d->used = 0;
    update_menu(top_doc());
    return 1;
}

static void cmd_quit(void)
{
    int i;
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i) && !close_doc(docs[i])) return;
    quit_now = 1;
}

/* ---- character and paragraph formats ------------------------------------- */

static const char *code_of(int what, int val, char *buf)
{
    switch (what) {
    case AP_FONT:  sprintf(buf, "[Font %.32s]", fonts[val < nfonts ? val : 0].name); break;
    case AP_SIZE:  sprintf(buf, "[Size %d]", val); break;
    case AP_COLOR: sprintf(buf, "[Colour %s]", color_name[val % NCOLORS]); break;
    case AP_PLAIN: strcpy(buf, "[/B][/I][/U]"); break;
    case AP_TOGGLE: strcpy(buf, val == A_BOLD ? "[B]" : val == A_ITALIC ? "[I]" : "[U]"); break;
    default: buf[0] = 0;
    }
    return buf;
}

void apply_style(DOC *d, int what, int val)
{
    long a, b;
    char code[48];
    if (d->iconified) return;
    if (codes_focus(d)) { codes_insert_code(d, code_of(what, val, code)); return; }
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    if (doc_sel(d, &a, &b)) {
        int on = doc_apply(d, a, b, what, val);
        switch (what) {
        case AP_FONT:   d->ins.font = (unsigned char)val; break;
        case AP_SIZE:   d->ins.size = (unsigned char)val; break;
        case AP_PLAIN:  d->ins.attr = 0; break;
        case AP_TOGGLE: if (on) d->ins.attr |= val; else d->ins.attr &= ~val; break;
        case AP_COLOR:  d->ins.color = (unsigned char)val; break;
        case AP_ATTRS:  d->ins.attr = (unsigned char)val; break;
        }
        after_edit(d, doc_para_start(d, a));
    } else {
        switch (what) {
        case AP_FONT:   d->ins.font = (unsigned char)val; break;
        case AP_SIZE:   d->ins.size = (unsigned char)val; break;
        case AP_PLAIN:  d->ins.attr = 0; break;
        case AP_TOGGLE: d->ins.attr ^= val; break;
        case AP_COLOR:  d->ins.color = (unsigned char)val; break;
        case AP_ATTRS:  d->ins.attr = (unsigned char)val; break;
        }
        if (d->len == 0) { doc_layout(d); refresh_text(d); }
        if (d->toolbar) refresh_toolbar(d);
    }
    if (d->view != VIEW_DOC) d->codes_stale = 1;
    update_menu(d);
    set_info(d);
    fontsel_sync();
}

static const char *para_code(int field, int val, char *buf)
{
    static const char *jn[4] = { "[Left]", "[Centre]", "[Right]", "[Justify]" };
    switch (field) {
    case PF_JUST: strcpy(buf, jn[val & 3]); break;
    case PF_SPACING: strcpy(buf, val == LS_DOUBLE ? "[Spacing 2]" : val == LS_ONEHALF ? "[Spacing 1.5]" : "[Spacing 1]"); break;
    case PF_LIST: strcpy(buf, val == LIST_BULLET ? "[Bullet]" : val == LIST_NUMBER ? "[Number]" : "[/List]"); break;
    case PF_TOC: if (val) sprintf(buf, "[Heading %d]", val); else strcpy(buf, "[/Heading]"); break;
    default: buf[0] = 0;
    }
    return buf;
}

void apply_para(DOC *d, int field, int val)
{
    long a, b;
    char code[32];
    if (d->iconified) return;
    if (codes_focus(d)) { codes_insert_code(d, para_code(field, val, code)); return; }
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    if (!doc_sel(d, &a, &b)) a = b = d->cursor;
    doc_para_set(d, a, b, field, val);
    if (d->len == 0) d->insp = d->a.paras[d->ins.para];
    after_edit(d, doc_para_start(d, a));
    if (d->view != VIEW_DOC) d->codes_stale = 1;
    update_menu(d);
}

static void insert_date(DOC *d, int style)
{
    char s[48];
    long from;
    fmt_now(s, style);
    if (codes_focus(d)) { codes_insert_code(d, s); return; }
    from = d->cursor;
    if (doc_type_str(d, s)) after_edit(d, from);
}

static void insert_break(DOC *d, unsigned char ch)
{
    CELL c = d->ins;
    long from = d->cursor;
    if (codes_focus(d)) { codes_insert_code(d, ch == CH_PAGE ? "[Page]\n" : "[Column]\n"); return; }
    if (doc_in_table(d, d->cursor, NULL, NULL)) { alert(1, 1, AL_NOT_IN_TABLE_BRK, AB_OK); return; }
    c.ch = ch;
    if (doc_type(d, &c, 1)) after_edit(d, from);
}

static void all_windows_refresh(void)
{
    int i;
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i) && !docs[i]->iconified) { refresh_all(docs[i]); set_info(docs[i]); }
}

static void cycle_windows(void)
{
    DOC *t = top_doc();
    int i, start = t ? t->slot : -1;
    for (i = 1; i <= MAX_DOCS; i++) {
        DOC *d = docs[(start + i + MAX_DOCS) % MAX_DOCS];
        if (d && d->used && d != t) {
            if (d->iconified) uniconify(d);
            activate_doc(d);
            update_menu(d);
            return;
        }
    }
}

/* ---- tables ---------------------------------------------------------- */

static void cmd_table(DOC *d, int cmd)
{
    long pos = d->cursor;
    int ok = 0;
    if (cmd != C_TBLINS && !doc_in_table(d, pos, NULL, NULL)) { alert(1, 1, AL_NOT_IN_TABLE, AB_OK); return; }
    switch (cmd) {
    case C_TBLINS: {
        static int rows = 3, cols = 3, border = 1;
        if (!dlg_table(d, 1, &rows, &cols, NULL, &border)) return;
        doc_delete_sel(d);
        ok = tbl_insert(d, d->cursor, rows, cols, border);
        break;
    }
    case C_TBLPROP: {
        int t = doc_in_table(d, pos, NULL, NULL), rows = tbl_rows(d, pos), cols, border;
        short w[MAX_COLS];
        TBL *T = &d->a.tbls[t - 1];
        cols = T->ncols;
        border = T->border;
        memcpy(w, T->w, sizeof w);
        if (!dlg_table(d, 0, &rows, &cols, w, &border)) return;
        ok = tbl_resize(d, pos, rows, cols, w, border);
        break;
    }
    case C_ROWINS: ok = tbl_row_insert(d, pos, 1); break;
    case C_ROWDEL: ok = tbl_row_delete(d, pos); break;
    case C_COLINS: ok = tbl_col_insert(d, pos, 1); break;
    case C_COLDEL: ok = tbl_col_delete(d, pos); break;
    case C_TBLDEL: ok = tbl_delete(d, pos); break;
    }
    if (ok) doc_changed(d);
    else if (cmd == C_COLINS) alert(1, 1, AL_MAX_COLS, AB_OK);
}

/* ---- paragraph commands -------------------------------------------------- */

static void cmd_para(DOC *d, int cmd)
{
    PARA *p = doc_para_at(d, d->cursor);
    switch (cmd) {
    case C_JLEFT:   apply_para(d, PF_JUST, J_LEFT); break;
    case C_JCENTRE: apply_para(d, PF_JUST, J_CENTRE); break;
    case C_JRIGHT:  apply_para(d, PF_JUST, J_RIGHT); break;
    case C_JFULL:   apply_para(d, PF_JUST, J_FULL); break;
    case C_SP1:     apply_para(d, PF_SPACING, LS_SINGLE); break;
    case C_SP15:    apply_para(d, PF_SPACING, LS_ONEHALF); break;
    case C_SP2:     apply_para(d, PF_SPACING, LS_DOUBLE); break;
    case C_BULLETS: apply_para(d, PF_LIST, p->list == LIST_BULLET ? LIST_NONE : LIST_BULLET); break;
    case C_NUMBERS: apply_para(d, PF_LIST, p->list == LIST_NUMBER ? LIST_NONE : LIST_NUMBER); break;
    case C_HEAD1:   apply_para(d, PF_TOC, 1); break;
    case C_HEAD2:   apply_para(d, PF_TOC, 2); break;
    case C_HEAD3:   apply_para(d, PF_TOC, 3); break;
    case C_BODY:    apply_para(d, PF_TOC, 0); break;
    case C_HANGING: {
        int hang = p->first >= 0;
        int left = hang ? (p->left > 0 ? p->left : 36) : 0;
        if (codes_focus(d)) { codes_insert_code(d, hang ? "[Indent 36 -36]" : "[Indent 0 0]"); break; }
        apply_para(d, PF_LEFT, left);
        apply_para(d, PF_FIRST, hang ? -left : 0);
        break;
    }
    case C_INDENTS: {
        int l = p->left, f = p->first;
        if (!dlg_indents(d, &l, &f)) break;
        if (codes_focus(d)) { char c[32]; sprintf(c, "[Indent %d %d]", l, f); codes_insert_code(d, c); break; }
        apply_para(d, PF_LEFT, l);
        apply_para(d, PF_FIRST, f);
        break;
    }
    }
}

static void cmd_undo(DOC *d, int redo)
{
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    if (redo ? redo_do(d) : undo_do(d)) {
        doc_layout(d);
        doc_sync_ins(d);
        if (d->view != VIEW_DOC) rebuild_codes(d);
        ensure_caret(d);
        refresh_all(d);
        set_name(d);
        set_sliders(d);
        set_info(d);
        update_menu(d);
        preview_changed(d);
    }
}

void do_cmd(int cmd)
{
    DOC *d = top_doc();
    long a, b;
    char path[MAX_PATH + 20];

    switch (cmd) {
    case C_ABOUT:  dlg_about(); return;
    case C_NEW:    cmd_new(); return;
    case C_OPEN:   if (ask_file(L(S_FS_OPEN), "#?.(rtf|txt|md|doc|pro|asc)", NULL, path, 0)) open_path(path, text_name(path)); return;
    case C_IMPORT: import_open(); return;
    case C_EXPORT: export_open(); return;
    case C_RULERS:
    case C_GUIDES: {
        int i;
        if (cmd == C_RULERS) pref_rulers = !pref_rulers; else pref_guides = !pref_guides;
        save_prefs();
        for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i) && !docs[i]->iconified) win_resized(docs[i]);
        update_menu(d);
        return;
    }
    case C_WINMORE: {
        static const char *items[MAX_DOCS];
        static char names[MAX_DOCS][36];
        short slot[MAX_DOCS];
        int i, n = 0, cur = -1, sel;
        POINT p;
        for (i = 0; i < MAX_DOCS; i++)
            if (DOC_USED(i)) {
                sprintf(names[n], "%.32s", lat(docs[i]->base));
                items[n] = names[n];
                if (docs[i] == d) cur = n;
                slot[n++] = (short)i;
            }
        GetCursorPos(&p);
        sel = popup(items, n, cur, (short)p.x, (short)p.y);
        if (sel < 0) return;
        if (docs[slot[sel]]->iconified) uniconify(docs[slot[sel]]);
        activate_doc(docs[slot[sel]]);
        update_menu(docs[slot[sel]]);
        return;
    }
    case C_QUIT:   cmd_quit(); return;
    case C_CYCLE:  cycle_windows(); return;
    case C_FONTSEL: case C_MOREFONTS: fontsel_open(); return;
    case C_TILEV:  tile_windows(0); return;
    case C_TILEH:  tile_windows(1); return;
    case C_CASCADE: tile_windows(2); return;
    case C_LIVESPELL:
        pref_live = !pref_live;
        if (pref_live) spell_ensure(0);
        save_prefs();
        update_menu(d);
        all_windows_refresh();
        return;
    case C_PREFS: {
        int lang = ui_lang;
        if (dlg_prefs()) {
            if (pref_live) spell_ensure(0);
            save_prefs();
            autosave_reset();
            if (lang != ui_lang) { tools_close_all(); rebuild_menu(); }
            update_menu(d);
            all_windows_refresh();
        }
        return;
    }
    case C_UDLOAD:
        if (ask_file(L(S_FS_UDLOAD), "#?.udc", NULL, path, 0)) {
            if (spell_user_load(path, 0) < 0) alert(1, 3, AL_UD_LOAD, AB_OK);
            update_menu(d);
        }
        return;
    case C_UDNEW:
        if (ask_file(L(S_FS_UDNEW), "#?.udc", "Personal.udc", path, 1)) {
            if (spell_user_load(path, 1) < 0) alert(1, 3, AL_UD_NEW, AB_OK);
            update_menu(d);
        }
        return;
    case C_ICONALL: {
        int i;
        for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i)) iconify(docs[i]);
        return;
    }
    }
    if (cmd >= C_UD0 && cmd < C_UD0 + MAX_UDICT) { spell_active = cmd - C_UD0; update_menu(d); return; }
    if (cmd >= C_WIN0 && cmd < C_WIN0 + MAX_DOCS) {
        int k = cmd - C_WIN0;
        DOC *w = k < win_items && winmap[k] >= 0 ? docs[winmap[k]] : NULL;
        if (!w || !w->used) return;
        if (w->iconified) uniconify(w);
        activate_doc(w);
        update_menu(w);
        return;
    }
    if (!d) return;

    /* window commands work on minimised windows too */
    switch (cmd) {
    case C_CLOSE:   close_doc(d); return;
    case C_ICONIFY: if (d->iconified) uniconify(d); else iconify(d); return;
    case C_BOTTOM:  send_to_back(d); return;
    case C_SAVE:    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d); cmd_save(d, 0); return;
    case C_SAVEAS:  if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d); cmd_save(d, 1); return;
    case C_PREVIEW: preview_open(d); return;
    case C_PRINT:   cmd_print(d); return;
    }
    if (d->iconified) return;

    if (codes_focus(d) && (cmd == C_CUT || cmd == C_COPY || cmd == C_PASTE || cmd == C_SELALL)) {
        codes_clip(d, cmd);
        return;
    }
    if (cmd == C_UNDO || cmd == C_REDO) { cmd_undo(d, cmd == C_REDO); return; }
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);

    if (cmd >= C_FONT0 && cmd < C_FONT0 + MAXFONTMENU) { apply_style(d, AP_FONT, cmd - C_FONT0); return; }
    if (cmd >= C_SIZE0 && cmd < C_SIZE0 + NSIZES) { apply_style(d, AP_SIZE, sizes[cmd - C_SIZE0]); return; }
    if (cmd >= C_COLOR0 && cmd < C_COLOR0 + NCOLORS) { apply_style(d, AP_COLOR, cmd - C_COLOR0); return; }
    if (cmd >= C_JLEFT && cmd <= C_BODY) { cmd_para(d, cmd); return; }
    if (cmd >= C_TBLINS && cmd <= C_TBLDEL) { cmd_table(d, cmd); return; }

    switch (cmd) {
    case C_INSDATE: insert_date(d, pref_date); break;
    case C_INSTIME: insert_date(d, DT_TIME); break;
    case C_PGBRK:   insert_break(d, CH_PAGE); break;
    case C_COLBRK:  insert_break(d, CH_COLBRK); break;
    case C_EXPTXT:  cmd_exptxt(d); break;
    case C_EXPMD:   cmd_export(d, FT_MD); break;
    case C_EXPFWP:  cmd_export(d, FT_FWP); break;
    case C_EXPPT:   cmd_export(d, FT_PT); break;
    case C_PDF:     cmd_pdf(d); break;
    case C_PREVPAGE: case C_NEXTPAGE: {
        int pg = d->nlines ? d->lines[doc_find_line(d, d->cursor)].page : 0;
        goto_page(d, pg + (cmd == C_NEXTPAGE ? 1 : -1));
        break;
    }
    case C_GOTOPAGE: {
        int pg = d->nlines ? d->lines[doc_find_line(d, d->cursor)].page : 0;
        if (dlg_goto(d, &pg) && d->used) goto_page(d, pg);
        break;
    }
    case C_REPAG:   cmd_repag(d); break;
    case C_PAGENUM:
        if (dlg_pagenum(d)) doc_changed(d);
        break;
    case C_HEADFOOT:
        if (dlg_headfoot(d)) doc_changed(d);
        break;
    case C_CHART: {
        long e2 = d->len ? doc_para_end(d, d->cursor < d->len ? d->cursor : d->len - 1) : 0;
        int t2 = d->len ? PARA_OF(d, &d->text[e2 < d->len ? e2 : d->len - 1])->tbl - 1 : -1;
        if (t2 < 0) { alert(1, 1, AL_CHART_TABLE, AB_OK); break; }
        chart_menu(d, t2);
        break;
    }
    case C_INSFILE:
        if (ask_file(L(S_FS_INSERT), "#?.(rtf|txt|asc)", NULL, path, 0)) insert_file(d, path);
        break;
    case C_PAGE:
    case C_WRAPCOL:
        if (cmd == C_PAGE ? dlg_page(d) : dlg_wrapcol(d)) doc_changed(d);
        break;
    case C_PICTURE: cmd_picture(d); break;
    case C_DRAWING: cmd_drawing(d); break;
    case C_OBJPROP: obj_props(d); break;
    case C_OBJDEL:  obj_delete(d); break;
    case C_OBJEDIT: if (d->selobj >= 0) draw_edit(d, d->selobj); break;
    case C_TOC: {
        char title[40];
        busy(1);
        a = 0;
        stc(title, L(S_CONTENTS), sizeof title);
        if (toc_find(d, &a, &b)) a = toc_update(d);
        else a = toc_insert(d, d->cursor, title);
        busy(0);
        if (a) doc_changed(d);
        else alert(1, 1, AL_NO_TOC_HERE, AB_OK);
        break;
    }
    case C_COPY:   clip_copy(d); break;
    case C_CUT:
        if (d->selobj >= 0 && d->selobj < d->a.nobj) { if (clip_copy(d)) obj_delete(d); break; }
        if (doc_sel(d, &a, &b)) { clip_copy(d); doc_delete_sel(d); after_edit(d, a); }
        break;
    case C_PASTE:  clip_paste(d); break;
    case C_SELALL:
        select_range(d, 0, d->len);
        refresh_text(d);
        break;
    case C_FIND:     find_open(d); break;
    case C_FINDNEXT: find_again(d); break;
    case C_PLAIN:   apply_style(d, AP_PLAIN, 0); break;
    case C_BOLD:    apply_style(d, AP_TOGGLE, A_BOLD); break;
    case C_ITALIC:  apply_style(d, AP_TOGGLE, A_ITALIC); break;
    case C_UNDER:   apply_style(d, AP_TOGGLE, A_UNDER); break;
    case C_SIZEOTHER: {
        int sz = d->ins.size;
        if (dlg_size(&sz)) apply_style(d, AP_SIZE, sz);
        break;
    }
    case C_VDOC:    set_view(d, VIEW_DOC); break;
    case C_SPLIT:   set_view(d, d->view == VIEW_SPLIT ? VIEW_DOC : VIEW_SPLIT); break;
    case C_VCODES:  set_view(d, d->view == VIEW_CODES ? VIEW_DOC : VIEW_CODES); break;
    case C_ZOOMIN:
    case C_ZOOMOUT: {
        int i, z = d->zoom;
        for (i = 0; i < NZOOM; i++) if (zooms[i] == d->zoom) break;
        if (i == NZOOM) i = 2;
        i += cmd == C_ZOOMIN ? 1 : -1;
        if (i >= 0 && i < NZOOM) z = zooms[i];
        set_zoom(d, z);
        break;
    }
    case C_TOOLBAR: set_toolbar(d, !d->toolbar); break;
    case C_SPELL:   spell_run(d); break;
    case C_THES:    thes_run(d); break;
    case C_SUMMARY: dlg_summary(d); break;
    }
}

/* ================================================================= */
/*  Keys                                                              */
/* ================================================================= */

int key_from_vk(WPARAM vk)
{
    switch (vk) {
    case VK_LEFT: return K_LEFT;
    case VK_RIGHT: return K_RIGHT;
    case VK_UP: return K_UP;
    case VK_DOWN: return K_DOWN;
    case VK_RETURN: return K_RETURN;
    case VK_TAB: return K_TAB;
    case VK_BACK: return K_BS;
    case VK_DELETE: return K_DEL;
    case VK_ESCAPE: return K_ESC;
    case VK_HOME: return K_HOME;
    case VK_END: return K_END;
    case VK_PRIOR: return K_PGUP;
    case VK_NEXT: return K_PGDN;
    case VK_INSERT: return K_INS;
    case VK_HELP: return K_HELP;
    }
    if (vk >= VK_F1 && vk <= VK_F10) return K_F1 + (int)(vk - VK_F1);
    return 0;
}

/* function keys and other keys that work without Ctrl */
static int special_key(int key, int qual)
{
    int shift = (qual & KQ_SHIFT) != 0;
    switch (key) {
    case K_F1:  case K_HELP: do_cmd(C_ABOUT); return 1;
    case K_F2:  do_cmd(C_REPAG); return 1;
    case K_F3:  do_cmd(C_FINDNEXT); return 1;
    case K_F4:  if (qual & KQ_ALT) return 0; do_cmd(C_VDOC); return 1;
    case K_F5:  do_cmd(C_SPLIT); return 1;
    case K_F6:  do_cmd(C_VCODES); return 1;
    case K_F7:  do_cmd(shift ? C_THES : C_ZOOMOUT); return 1;
    case K_F8:  do_cmd(C_ZOOMIN); return 1;
    case K_F9:  do_cmd(C_BULLETS); return 1;
    case K_F10: do_cmd(C_NUMBERS); return 1;
    }
    return 0;
}

void handle_key(WIN w, int key, int qual)
{
    DOC *d;
    if (!(qual & KQ_CTRL) && tool_route_key(w, key, qual)) return;
    if (special_key(key, qual)) { update_menu(top_doc()); return; }
    if (qual & KQ_CTRL) {
        if (key == K_TAB) { do_cmd(C_CYCLE); return; }
        if (key == K_RETURN || key == K_ENTER) { do_cmd((qual & KQ_SHIFT) ? C_COLBRK : C_PGBRK); return; }
        if (key < K_LEFT) return;
    }
    d = doc_of_win(w);
    if (d) win_key(d, key, qual);
}

/* ================================================================= */
/*  The timer: idle work after a pause                                */
/* ================================================================= */

#define IDLE_TIMER 1
static int tbusy, tdue;

#ifdef EDLOG
/* a log of how long things take, in EdWord.log ("make EDLOG=1") */
void edlog(const char *fmt, ...)
{
    static FILE *f;
    static DWORD t0;
    char buf[200];
    va_list ap;
    if (!f) {
        char path[MAX_PATH + 20];
        sprintf(path, "%sEdWord.log", datadir);
        f = fopen(path, "w");
        t0 = GetTickCount();
    }
    if (!f) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(f, "%8lu ms  %s\n", (unsigned long)(GetTickCount() - t0), buf);
    fflush(f);
}
#endif

void timer_after(long ms)
{
    if (!frame) return;
    SetTimer(frame, IDLE_TIMER, (UINT)ms, NULL);
    tbusy = 1;
    tdue = 0;
}

int timer_pending(void) { return tbusy; }

int timer_due(void)
{
    if (!tdue) return 0;
    tdue = 0;
    return 1;
}

/* split view and quick edit: apply edited codes, or refresh stale ones,
 * when idle; a stale preview is redrawn too */
static int idle_work(int run)
{
    int i, need = 0;
    if (preview_idle(run)) need = 1;
    for (i = 0; i < MAX_DOCS; i++) {
        DOC *d = docs[i];
        if (!d || !d->used || d->view == VIEW_DOC || d->iconified) continue;
        if (d->cv.dirty || d->codes_stale) {
            need = 1;
            if (run) { if (d->cv.dirty) apply_codes(d); else rebuild_codes(d); }
        }
    }
    return need;
}

/* ================================================================= */
/*  Messages                                                          */
/* ================================================================= */

ULONG upd_id;
long  upd_top;

/* While a job has a document half made (laying it out, say), its window
 * is not drawn: it is drawn again when the job ends. */
int pump_defer;
static char defer_on[MAX_DOCS];

void pump_flush(void)
{
    int i;
    for (i = 0; i < MAX_DOCS; i++)
        if (defer_on[i]) { defer_on[i] = 0; if (DOC_USED(i)) refresh_all(docs[i]); }
}

/* deal with waiting messages from inside long jobs: windows are drawn,
 * the keyboard and mouse wait */
void pump_messages(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, WM_PAINT, WM_PAINT, PM_REMOVE)) {
        DOC *d = doc_of_win(m.hwnd);
        if (d && pump_defer) {
            defer_on[d->slot] = 1;
            ValidateRect(m.hwnd, NULL);
        } else DispatchMessageA(&m);
    }
}

/* files dropped on the frame from Explorer */
static void dropped(HDROP h)
{
    UINT n = DragQueryFileA(h, 0xFFFFFFFF, NULL, 0), i;
    char path[MAX_PATH];
    for (i = 0; i < n; i++)
        if (DragQueryFileA(h, i, path, sizeof path)) open_path(path, text_name(path));
    DragFinish(h);
}

static void desk_size(void)
{
    RECT r;
    if (!mdiclient) return;
    GetClientRect(mdiclient, &r);
    desk.g_x = 0;
    desk.g_y = 0;
    desk.g_w = (short)r.right;
    desk.g_h = (short)r.bottom;
}

static LRESULT CALLBACK frame_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        CLIENTCREATESTRUCT cc;
        cc.hWindowMenu = NULL;
        cc.idFirstChild = 50000;
        mdiclient = CreateWindowExA(WS_EX_CLIENTEDGE, "MDICLIENT", NULL,
                                    WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE,
                                    0, 0, 0, 0, h, (HMENU)1, hinst, &cc);
        DragAcceptFiles(h, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (lp == 0 && LOWORD(wp) > 0 && LOWORD(wp) < NCMD) {
            if (block_now()) return 0;
            do_cmd(LOWORD(wp));
            if (!quit_now) update_menu(top_doc());
            return 0;
        }
        break;
    case WM_INITMENUPOPUP:
        if (!HIWORD(lp)) update_menu(top_doc());
        break;
    case WM_SIZE: {
        LRESULT r = DefFrameProcA(h, mdiclient, msg, wp, lp);
        desk_size();
        return r;
    }
    case WM_TIMER:
        if (wp == IDLE_TIMER) {
            KillTimer(h, IDLE_TIMER);
            tbusy = 0;
            tdue = 1;
            return 0;
        }
        break;
    case 0x020A: {                     /* WM_MOUSEWHEEL over the frame: to the document */
        DOC *d = top_doc();
        if (d && doc_win(d)) return SendMessageA(doc_win(d), msg, wp, lp);
        break;
    }
    case WM_DROPFILES:
        if (!block_now()) dropped((HDROP)wp);
        else DragFinish((HDROP)wp);
        return 0;
    case WM_CLOSE:
        if (block_now()) return 0;
        cmd_quit();
        return 0;
    case WM_QUERYENDSESSION:
        cmd_quit();
        return quit_now ? TRUE : FALSE;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) {
            DOC *d = top_doc();
            if (d) SetFocus(doc_win(d));
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefFrameProcA(h, mdiclient, msg, wp, lp);
}

/* the top-level window a message is for */
static HWND root_of(HWND h)
{
    while (h && (GetWindowLongA(h, GWL_STYLE) & WS_CHILD)) h = GetParent(h);
    return h;
}

/* one message from the queue */
static void dispatch(MSG *m)
{
    HWND r = root_of(m->hwnd);
    char cls[16];
    if (m->message == WM_KEYDOWN && r == frame && !block_now() && (GetKeyState(VK_CONTROL) & 0x8000) &&
        !(GetKeyState(VK_MENU) & 0x8000)) {
        int k = (int)m->wParam;
        if (k == VK_ADD || k == VK_OEM_PLUS) k = '+';
        else if (k == VK_SUBTRACT || k == VK_OEM_MINUS) k = '-';
        if (((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9') || k == '+' || k == '-') && menu_key(k, key_qual())) return;
    }
    if (r && r != frame) {
        GetClassNameA(r, cls, sizeof cls);
        if ((!lstrcmpA(cls, "EdWordDlg") || !lstrcmpA(cls, "EdWordTool")) && IsDialogMessageA(r, m)) return;
    }
    if (mdiclient && TranslateMDISysAccel(mdiclient, m)) return;
    TranslateMessage(m);
    DispatchMessageA(m);
}

static void frame_register(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconA(hinst, MAKEINTRESOURCEA(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = "EdWordFrame";
    RegisterClassA(&wc);
}

/* the folder a path is in, with a \ at the end */
static void folder_of(char *out, const char *path)
{
    char *s;
    lstrcpynA(out, path, MAX_PATH - 20);
    s = strrchr(out, '\\');
    if (s) s[1] = 0; else strcpy(out, ".\\");
}

/* where the settings go: Application Data\EdWord, or the program's folder */
static void find_datadir(void)
{
    char ad[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("APPDATA", ad, sizeof ad);
    strcpy(datadir, progdir);
    if (n > 0 && n < MAX_PATH - 20) {
        DWORD at;
        strcat(ad, "\\EdWord");
        CreateDirectoryA(ad, NULL);
        at = GetFileAttributesA(ad);
        if (at != 0xFFFFFFFF && (at & FILE_ATTRIBUTE_DIRECTORY)) {
            strcpy(datadir, ad);
            strcat(datadir, "\\");
        }
    }
}

/* the files named on the command line (quoted or not) */
static void open_args(const char *cmd)
{
    char path[MAX_PATH];
    while (*cmd) {
        int n = 0;
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        if (!*cmd) break;
        if (*cmd == '"') {
            cmd++;
            while (*cmd && *cmd != '"') { if (n < MAX_PATH - 1) path[n++] = *cmd; cmd++; }
            if (*cmd) cmd++;
        } else
            while (*cmd && *cmd != ' ' && *cmd != '\t') { if (n < MAX_PATH - 1) path[n++] = *cmd; cmd++; }
        path[n] = 0;
        if (n) open_path(path, text_name(path));
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG m;
    HDC dc;
    TEXTMETRICA tm;
    char exe[MAX_PATH];
    int dpi, i;
    (void)prev;

    hinst = inst;
    sprintf(appname, "EdWord %s", ED_VERSION);
    GetModuleFileNameA(NULL, exe, sizeof exe);
    folder_of(progdir, exe);
    find_datadir();
    load_prefs();
    set_language(pref_lang >= 0 ? pref_lang : system_language());
    spell_lang = pref_spell >= 0 ? pref_spell : ui_lang;
    img_rgb_max = 16000000L;              /* pictures keep their colours */

    /* the screen: its size, how big a pixel is, the fonts */
    scr_w = (short)GetSystemMetrics(SM_CXSCREEN);
    scr_h = (short)GetSystemMetrics(SM_CYSCREEN);
    dc = GetDC(NULL);
    dpi = GetDeviceCaps(dc, LOGPIXELSY);
    if (dpi < 48) dpi = 96;
    dev_pixw = (short)(25400 / GetDeviceCaps(dc, LOGPIXELSX));
    dev_pixh = (short)(25400 / dpi);
    npens = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES) >= 8 ? 16 : 16;
    uifont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!uifont) uifont = (HFONT)GetStockObject(ANSI_VAR_FONT);
    fixfont = CreateFontA(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    if (!fixfont) fixfont = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    {
        HFONT of = (HFONT)SelectObject(dc, uifont);
        GetTextMetricsA(dc, &tm);
        hchar = (short)tm.tmHeight;
        sys_asc = (short)tm.tmAscent;
        SelectObject(dc, of);
    }
    ReleaseDC(NULL, dc);

    gfx_init();
    wchar = (short)((sys_width("abcdefghijklmnopqrstuvwxyz ABCDEFGHIJ0123456789") + 23) / 47);
    if (wchar < 4) wchar = 8;
    InitCommonControls();
    dlg_register();
    doc_register();
    frame_register();

    busy(1);
    fonts_load();
    menubar = make_menu();
    frame = CreateWindowExA(0, "EdWordFrame", appname, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            pref_fx >= 0 && pref_fw >= 320 ? pref_fx : CW_USEDEFAULT, pref_fx >= 0 && pref_fw >= 320 ? pref_fy : CW_USEDEFAULT,
                            pref_fx >= 0 && pref_fw >= 320 ? pref_fw : CW_USEDEFAULT, pref_fx >= 0 && pref_fh >= 240 ? pref_fh : CW_USEDEFAULT,
                            NULL, menubar, inst, NULL);
    busy(0);
    if (!frame) return 1;
    {   /* keep the frame on the screen */
        RECT r;
        GetWindowRect(frame, &r);
        if (r.left >= scr_w - 40 || r.top >= scr_h - 40 || r.right < 40 || r.bottom < 40)
            SetWindowPos(frame, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(frame, (show == SW_SHOWDEFAULT || show == SW_SHOWNORMAL) && pref_maxim ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(frame);
    desk_size();
    update_menu(NULL);

    open_args(cmdline);
    if (pref_live) spell_ensure(1);
    if (!top_doc()) cmd_new();

    while (!quit_now) {
        int input;
        if (!GetMessageA(&m, NULL, 0, 0)) break;
        input = m.message != WM_TIMER && m.message != WM_PAINT;
        dispatch(&m);
        if (quit_now) break;
        if (timer_due()) idle_work(1);
        autosave_tick();
        /* idle work runs once the user has paused for 0.4 seconds */
        if (input || !timer_pending()) {
            if (idle_work(0)) timer_after(400);
            else if (pref_autosave) timer_after(5000);
        }
    }

    write_prefs(1);
    tools_close_all();
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i)) { close_window(docs[i]); doc_free(docs[i]); docs[i]->used = 0; }
    spell_free_all();
    thes_close();
    doc_clip_free();
    img_cache_free(NULL);
    fonts_unload();
    gfx_free();
    if (frame && IsWindow(frame)) DestroyWindow(frame);
    return 0;
}
