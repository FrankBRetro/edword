/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gem.c - EdWord GEM front end: start-up, preferences, menus, commands
 *         and the event loop
 */
#include <gem.h>
#include <gemx.h>
#include <osbind.h>
#include <mint/cookie.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "gemui.h"
#include "edrsc.h"

short vhandle, phys_handle, wchar, hchar, wbox, hbox, scr_w, scr_h, npens, sys_asc;
GRECT desk;
int   has_gdos, gdos_fsm, colour_on = 1, pdf_scale = 1;
int   native_iconify, native_bottom, quit_now;
int   safe_mode, no_arbpt;
DOC  *docs[MAX_DOCS];               /* made when first needed, then kept */
char  progdir[256];
const short sizes[NSIZES] = { 8, 9, 10, 12, 14, 18, 24, 36 };
const short zooms[NZOOM] = { 50, 75, 100, 125, 150, 200 };
short ap_id;
static int   untitled_no;

/* SpeedoGDOS does its work on our stack; give it room (MiNTLib) */
long  _stksize = 65536L;

/* ================================================================= */
/*  Start-up log: EDWORD.LOG next to the program. Every line is        */
/*  written and the file closed again, so it survives a crash.        */
/* ================================================================= */

#ifdef EDLOG
static char logpath[300];
static int  log_lines, log_max = 500;

void edlog_more(void) { log_max = log_lines + 4000; }

void edlog(const char *fmt, ...)
{
    FILE *f;
    va_list ap;
    if (!logpath[0] || log_lines > log_max) return;
    f = fopen(logpath, "a");
    if (!f) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputs("\r\n", f);
    fclose(f);
    log_lines++;
}

static void log_start(void)
{
    FILE *f;
    sprintf(logpath, "%sEDWORD.LOG", progdir);
    f = fopen(logpath, "w");
    if (!f) { logpath[0] = 0; return; }
    fprintf(f, "EdWord %s start-up log\r\n", ED_VERSION);
    fclose(f);
}
#else
#define log_start()
#endif

/* ================================================================= */
/*  Preferences, kept in EDWORD.INF next to the program               */
/* ================================================================= */

int pref_date = DT_SHORT, pref_toolbar = 1, pref_paper = P_A4, pref_land;
int pref_autosave, pref_asmin = 5, pref_asmode, pref_display, pref_live;
int pref_rulers, pref_guides = 1;
char pref_docdir[128] = "C:\\DATA\\DOCPROC\\EDWORD";
char tpl_name[48], tpl_addr[3][48], tpl_title[48];
static int pref_lang = -1, pref_spell = -1;

static void prefs_path(char *p) { sprintf(p, "%sEDWORD.INF", progdir); }

static void load_prefs(void)
{
    char path[300], line[200];
    FILE *f;
    prefs_path(path);
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *val;
        int v;
        if (!eq) continue;
        *eq = 0;
        val = eq + 1;
        val[strcspn(val, "\r\n")] = 0;
        v = atoi(val);
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
        else if (!strcmp(line, "keep") && v >= KEEP_ANY && v <= KEEP_TOGETHER) lay_keep = v;
        else if (!strcmp(line, "docdir")) { strncpy(pref_docdir, val, sizeof pref_docdir - 1); }
        else if (!strcmp(line, "tplname")) strncpy(tpl_name, val, sizeof tpl_name - 1);
        else if (!strcmp(line, "tpltitle")) strncpy(tpl_title, val, sizeof tpl_title - 1);
        else if (!strncmp(line, "tpladdr", 7) && line[7] >= '1' && line[7] <= '3' && !line[8])
            strncpy(tpl_addr[line[7] - '1'], val, sizeof tpl_addr[0] - 1);
    }
    fclose(f);
}

void save_prefs(void)
{
    char path[300];
    FILE *f;
    prefs_path(path);
    f = fopen(path, "w");
    if (!f) { alert(1, 3, AL_NO_INF, AB_OK); return; }
    fprintf(f, "date=%d\ntoolbar=%d\ncolour=%d\npdfscale=%d\npaper=%d\nlandscape=%d\n"
               "autosave=%d\nautosavemin=%d\nautosavemode=%d\ndisplay=%d\nlanguage=%d\nlivespell=%d\n"
               "rulers=%d\nguides=%d\nspelllang=%d\n",
            pref_date, pref_toolbar, colour_on, pdf_scale, pref_paper, pref_land,
            pref_autosave, pref_asmin, pref_asmode, pref_display, ui_lang, pref_live, pref_rulers, pref_guides, spell_lang);
    fprintf(f, "keep=%d\ndocdir=%s\ntplname=%s\ntpladdr1=%s\ntpladdr2=%s\ntpladdr3=%s\ntpltitle=%s\n",
            lay_keep, pref_docdir, tpl_name, tpl_addr[0], tpl_addr[1], tpl_addr[2], tpl_title);
    fclose(f);
}

/* the language TOS is set to, from the _AKP cookie (or the ROM header) */
static int tos_language(void)
{
    long akp;
    int c = -1;
    if (Getcookie(0x5F414B50L, &akp) == C_FOUND) c = (int)((akp >> 8) & 0xFF);   /* '_AKP' */
    switch (c) {
    case 1: case 8: return LANG_DE;     /* Germany, Swiss German */
    case 2: case 7: return LANG_FR;     /* France, Swiss French */
    case 4: return LANG_ES;
    case 5: return LANG_IT;
    }
    return LANG_EN;
}

/* ================================================================= */
/*  Menu bar, built at run time in the current language               */
/* ================================================================= */

#define MAXOBJ 260
#define NMENU  9
#define MAXFONTMENU 24

typedef struct { int text; const char *key; short cmd; } MI;    /* text < 0: separator */
#define SEP { -1, NULL, -1 }
#define DYN 0x4000                     /* text is an index into dyn[] */

static OBJECT mtree_own[MAXOBJ], *mtree = mtree_own;   /* the resource file's, or built here */
static short  mcmd[MAXOBJ], cmdobj[NCMD];
static short  nobj;
static char   items[MAXOBJ][44];
static char   titles[NMENU][16];
static char   dashes[NMENU][44];
static char   dyn[100][32];            /* fonts, sizes, colours, windows, dictionaries */
static int    font_items;

/* The menus follow the Atari conventions: the first menu is the
 * program's, Control+U closes and Control+W cycles the windows,
 * Control+F finds and Control+G finds again, Undo undoes. A shifted
 * shortcut is shown with the Shift arrow. */
static const MI m_desk[] = { { M_ABOUT, NULL, C_ABOUT }, SEP,
    { M_ACC, NULL, 0 }, { M_ACC, NULL, 0 }, { M_ACC, NULL, 0 }, { M_ACC, NULL, 0 }, { M_ACC, NULL, 0 },
    { M_ACC, NULL, 0 }, { 0, NULL, 0 } };
static const MI m_file[] = {
    { M_NEW, "^N", C_NEW }, { M_TEMPLATE, NULL, C_TEMPLATE }, { M_OPEN, "^O", C_OPEN }, { M_IMPORT, NULL, C_IMPORT }, { M_CLOSE, "^U", C_CLOSE }, SEP,
    { M_SAVEDOC, "^S", C_SAVE }, { M_SAVEAS, "^\001S", C_SAVEAS }, { M_EXPORT, NULL, C_EXPORT }, SEP,
    { M_PAGE, NULL, C_PAGE }, { M_HEADFOOT, NULL, C_HEADFOOT }, { M_PREVIEW, "^\001P", C_PREVIEW },
    { M_PRINT, "^P", C_PRINT }, SEP,
    { M_QUIT, "^Q", C_QUIT }, { 0, NULL, 0 } };
static const MI m_edit[] = {
    { M_UNDO, "Undo", C_UNDO }, { M_REDO, "\001Undo", C_REDO }, SEP,
    { M_CUT, "^X", C_CUT }, { M_COPY, "^C", C_COPY }, { M_PASTE, "^V", C_PASTE }, { M_SELALL, "^A", C_SELALL }, SEP,
    { M_FIND, "^F", C_FIND }, { M_FINDNEXT, "^G", C_FINDNEXT }, SEP,
    { M_PREVPAGE, "^\001", C_PREVPAGE }, { M_NEXTPAGE, "^\002", C_NEXTPAGE }, { M_GOTOPAGE, "^J", C_GOTOPAGE }, SEP,
    { M_REPAG, "F2", C_REPAG },
    { 0, NULL, 0 } };
static const MI m_insert[] = {
    { M_DATE, "^D", C_INSDATE }, { M_TIME, NULL, C_INSTIME }, SEP,
    { M_PGBRK, "^Ret", C_PGBRK }, { M_COLBRK, "^\001Ret", C_COLBRK }, { M_PAGENUM, NULL, C_PAGENUM }, SEP,
    { M_TBLINS, NULL, C_TBLINS }, { M_TBLPROP, NULL, C_TBLPROP }, { M_ROWINS, NULL, C_ROWINS },
    { M_ROWDEL, NULL, C_ROWDEL }, { M_COLINS, NULL, C_COLINS }, { M_COLDEL, NULL, C_COLDEL },
    { M_TBLDEL, NULL, C_TBLDEL }, { M_CHART, NULL, C_CHART }, SEP,
    { M_PICTURE, NULL, C_PICTURE }, { M_DRAWING, NULL, C_DRAWING }, SEP,
    { M_TOC, NULL, C_TOC }, SEP, { M_INSFILE, NULL, C_INSFILE }, { 0, NULL, 0 } };
static MI m_font[NSIZES + MAXFONTMENU + 8];
static MI m_style[NCOLORS + 8];
static const MI m_para[] = {
    { M_JLEFT, "^L", C_JLEFT }, { M_JCENTRE, "^E", C_JCENTRE }, { M_JRIGHT, "^R", C_JRIGHT },
    { M_JFULL, "^\001J", C_JFULL }, SEP,
    { M_SP1, NULL, C_SP1 }, { M_SP15, NULL, C_SP15 }, { M_SP2, NULL, C_SP2 }, SEP,
    { M_INDENTS, NULL, C_INDENTS }, { M_HANGING, NULL, C_HANGING }, { M_BULLETS, "F9", C_BULLETS },
    { M_NUMBERS, "F10", C_NUMBERS }, SEP,
    { M_HEAD1, NULL, C_HEAD1 }, { M_HEAD2, NULL, C_HEAD2 }, { M_HEAD3, NULL, C_HEAD3 }, { M_BODY, NULL, C_BODY }, SEP,
    { M_WRAPCOL, NULL, C_WRAPCOL }, { 0, NULL, 0 } };
static MI m_tools[] = {
    { M_SPELL, "^K", C_SPELL }, { M_LIVESPELL, NULL, C_LIVESPELL }, { M_THES, "\001F7", C_THES },
    { M_SUMMARY, NULL, C_SUMMARY }, SEP,
    { M_UDLOAD, NULL, C_UDLOAD }, { M_UDNEW, NULL, C_UDNEW }, SEP,
    { DYN + 60, NULL, C_UD0 }, { DYN + 61, NULL, C_UD0 + 1 }, { DYN + 62, NULL, C_UD0 + 2 }, { DYN + 63, NULL, C_UD0 + 3 },
    SEP, { M_PREFS, NULL, C_PREFS }, { 0, NULL, 0 } };
#define NWINFIX 20
static const MI m_window_fixed[NWINFIX] = {
    { M_VDOC, "F4", C_VDOC }, { M_SPLIT, "^Y", C_SPLIT }, { M_VCODES, "F6", C_VCODES }, SEP,
    { M_ZOOMIN, "F8", C_ZOOMIN }, { M_ZOOMOUT, "F7", C_ZOOMOUT }, SEP,
    { M_TOOLBAR, "^T", C_TOOLBAR }, { M_RULERS, "^\001R", C_RULERS }, { M_GUIDES, NULL, C_GUIDES }, SEP,
    { M_TILEV, NULL, C_TILEV }, { M_TILEH, NULL, C_TILEH }, { M_CASCADE, NULL, C_CASCADE }, SEP,
    { M_ICONIFY, "^Z", C_ICONIFY }, { M_ICONALL, NULL, C_ICONALL }, { M_BOTTOM, NULL, C_BOTTOM },
    { M_CYCLE, "^W", C_CYCLE }, SEP };
static MI m_window[NWINFIX + MAX_DOCS + 2];
static int win_items;                   /* document windows listed in the menu */
static short winmap[MAX_DOCS];          /* menu entry -> document slot */

/* the Window menu: as many documents as fit, then "More windows..." */
static void build_window_menu(void)
{
    int i, n = 0, rows = (scr_h - hchar - 4) / hchar - 1, room;
    for (i = 0; i < NWINFIX; i++) m_window[n++] = m_window_fixed[i];
    room = rows - n;
    if (room > MAX_DOCS) room = MAX_DOCS;
    if (room > 24) room = 24;
    win_items = room >= MAX_DOCS ? MAX_DOCS : room - 1;
    if (win_items < 1) win_items = 1;
    for (i = 0; i < win_items; i++) {
        m_window[n].text = DYN + 70 + i; m_window[n].key = NULL; m_window[n].cmd = (short)(C_WIN0 + i); n++;
    }
    if (win_items < MAX_DOCS) { m_window[n].text = M_WINMORE; m_window[n].key = NULL; m_window[n].cmd = C_WINMORE; n++; }
    m_window[n].text = 0; m_window[n].cmd = 0; m_window[n].key = NULL;
}

static short mk(short parent, short type, short x, short y, short w, short h)
{
    OBJECT *o = &mtree[nobj];
    memset(o, 0, sizeof *o);
    o->ob_next = o->ob_head = o->ob_tail = -1;
    o->ob_type = type;
    o->ob_x = x; o->ob_y = y; o->ob_width = w; o->ob_height = h;
    if (parent >= 0) objc_add(mtree, parent, nobj);
    return nobj++;
}

static void pad_item(char *dst, const char *src, int width)
{
    int n = (int)strlen(src);
    if (n > width) n = width;
    memcpy(dst, src, n);
    while (n < width) dst[n++] = ' ';
    dst[n] = 0;
}

static const char *mi_text(const MI *m)
{
    if (m->text & DYN) return dyn[m->text & 0xFF];
    return L(m->text);
}

/* the Font menu: the selector, sizes, then as many fonts as fit */
static void build_font_menu(void)
{
    int i, n = 0, rows = (scr_h - hchar - 4) / hchar - 1, room;
    m_font[n].text = M_FONTSEL; m_font[n].key = "^\001F"; m_font[n].cmd = C_FONTSEL; n++;
    m_font[n].text = M_SIZEOTHER; m_font[n].key = NULL; m_font[n].cmd = C_SIZEOTHER; n++;
    m_font[n].text = -1; m_font[n].cmd = -1; m_font[n].key = NULL; n++;
    for (i = 0; i < NSIZES; i++) {
        sprintf(dyn[i], "%d %s", sizes[i], L(S_POINT));
        m_font[n].text = DYN + i; m_font[n].key = NULL; m_font[n].cmd = (short)(C_SIZE0 + i); n++;
    }
    m_font[n].text = -1; m_font[n].cmd = -1; m_font[n].key = NULL; n++;
    room = rows - n;
    if (room > MAXFONTMENU) room = MAXFONTMENU;
    font_items = nfonts <= room ? nfonts : room - 1;
    if (font_items < 1) font_items = 1;
    for (i = 0; i < font_items; i++) {
        sprintf(dyn[10 + i], "%.24s", fonts[i].name);
        m_font[n].text = DYN + 10 + i; m_font[n].key = NULL; m_font[n].cmd = (short)(C_FONT0 + i); n++;
    }
    if (font_items < nfonts) { m_font[n].text = M_MOREFONTS; m_font[n].key = NULL; m_font[n].cmd = C_MOREFONTS; n++; }
    m_font[n].text = 0; m_font[n].cmd = 0;
}

static void build_style_menu(void)
{
    int i, n = 0;
    static const short cmds[4] = { C_PLAIN, C_BOLD, C_ITALIC, C_UNDER };
    static const int txt[4] = { M_PLAIN, M_BOLD, M_ITALIC, M_UNDER };
    static const char *keys[4] = { NULL, "^B", "^I", "^\001U" };
    for (i = 0; i < 4; i++) {
        m_style[n].text = txt[i]; m_style[n].key = keys[i]; m_style[n].cmd = cmds[i]; n++;
        if (i == 0) { m_style[n].text = -1; m_style[n].cmd = -1; m_style[n].key = NULL; n++; }
    }
    m_style[n].text = -1; m_style[n].cmd = -1; m_style[n].key = NULL; n++;
    for (i = 0; i < NCOLORS; i++) {
        sprintf(dyn[50 + i], "%s", L(S_COL_BLACK + i));
        m_style[n].text = DYN + 50 + i; m_style[n].key = NULL; m_style[n].cmd = (short)(C_COLOR0 + i); n++;
    }
    m_style[n].text = 0; m_style[n].cmd = 0;
}

/* the menu bar from the resource file: the fonts and windows that fit,
 * the screen's width; 0 without the file */
static void set_item(int cmd, const char *text);
static int build_menu_rsc(void)
{
    static const short cm[][2] = { RSC_MENU_CMDS };
    OBJECT *t;
    int i, rows = (scr_h - hchar - 4) / hchar - 1, room, nfix;
    short b;
    if (!rsc_load(ui_lang, 1) || !(t = rsc_tree(RT_MENU))) return 0;
    mtree = t;
    nobj = MN_COUNT;
    memset(mcmd, 0, sizeof mcmd);
    memset(cmdobj, 0, sizeof cmdobj);
    for (i = 0; i < (int)(sizeof cm / sizeof cm[0]); i++) {
        mcmd[cm[i][0]] = cm[i][1];
        if (cm[i][1] > 0 && cm[i][1] < NCMD) cmdobj[cm[i][1]] = cm[i][0];
    }
    t[MN_ROOT].ob_width = t[MN_BAR].ob_width = t[MN_SCREEN].ob_width = scr_w;
    t[MN_ROOT].ob_height = scr_h;
    t[MN_SCREEN].ob_height = (short)(scr_h - t[MN_SCREEN].ob_y);
    /* the fonts: as many as fit, then "More fonts..." */
    nfix = MN_FONT0 - (t[MN_BOX4].ob_head);
    room = rows - nfix;
    if (room > MAXFONTMENU) room = MAXFONTMENU;
    font_items = nfonts <= room ? nfonts : room - 1;
    if (font_items < 1) font_items = 1;
    for (i = 0; i < MAXFONTMENU; i++) {
        if (i < font_items) {
            char n[40];
            sprintf(n, "%.24s", i < nfonts ? fonts[i].name : "-");
            set_item(C_FONT0 + i, n);
        } else {
            t[MN_FONT0 + i].ob_flags |= OF_HIDETREE;
            mcmd[MN_FONT0 + i] = 0;
            cmdobj[C_FONT0 + i] = 0;
        }
    }
    if (font_items < nfonts) t[MN_MOREFONTS].ob_y = (short)(t[MN_FONT0].ob_y + font_items * hchar);
    else { t[MN_MOREFONTS].ob_flags |= OF_HIDETREE; cmdobj[C_MOREFONTS] = 0; }
    t[MN_BOX4].ob_height = (short)((nfix + font_items + (font_items < nfonts)) * hchar);
    /* the windows */
    nfix = MN_WIN0 - (t[MN_BOX8].ob_head);
    room = rows - nfix;
    if (room > MAX_DOCS) room = MAX_DOCS;
    if (room > 24) room = 24;
    win_items = room >= MAX_DOCS ? MAX_DOCS : room - 1;
    if (win_items < 1) win_items = 1;
    for (i = win_items; i < 24; i++) {
        t[MN_WIN0 + i].ob_flags |= OF_HIDETREE;
        mcmd[MN_WIN0 + i] = 0;
        if (C_WIN0 + i < NCMD) cmdobj[C_WIN0 + i] = 0;
    }
    if (win_items < MAX_DOCS) t[MN_WINMORE].ob_y = (short)(t[MN_WIN0].ob_y + win_items * hchar);
    else { t[MN_WINMORE].ob_flags |= OF_HIDETREE; cmdobj[C_WINMORE] = 0; }
    t[MN_BOX8].ob_height = (short)((nfix + win_items + (win_items < MAX_DOCS)) * hchar);
    for (i = 0; i < win_items; i++) set_item(C_WIN0 + i, "-");
    for (i = 0; i < MAX_UDICT; i++) set_item(C_UD0 + i, L(S_NO_UDICT));
    /* each menu on the screen */
    for (b = t[MN_SCREEN].ob_head; b > MN_SCREEN; b = t[b].ob_next)
        if (t[b].ob_x + t[b].ob_width > scr_w - 2) t[b].ob_x = (short)(scr_w - 2 - t[b].ob_width);
    return 1;
}

static void build_menu(void)
{
    static const int tid[NMENU] = { MT_DESK, MT_FILE, MT_EDIT, MT_INSERT, MT_FONT, MT_STYLE, MT_PARA, MT_TOOLS, MT_WINDOW };
    const MI *menus[NMENU];
    short root, bar, active, screen, tx[NMENU], x = 0, i, k, total = 0;

    if (build_menu_rsc()) return;
    mtree = mtree_own;
    build_font_menu();
    build_style_menu();
    build_window_menu();
    for (i = 0; i < win_items; i++) strcpy(dyn[70 + i], "-");
    for (i = 0; i < MAX_UDICT; i++) strcpy(dyn[60 + i], L(S_NO_UDICT));
    menus[0] = m_desk; menus[1] = m_file; menus[2] = m_edit; menus[3] = m_insert; menus[4] = m_font;
    menus[5] = m_style; menus[6] = m_para; menus[7] = m_tools; menus[8] = m_window;

    nobj = 0;
    memset(mcmd, 0, sizeof mcmd);
    memset(cmdobj, 0, sizeof cmdobj);
    for (i = 0; i < NMENU; i++) {
        sprintf(titles[i], " %.12s ", L(tid[i]));
        total += (short)strlen(titles[i]) * wchar;
    }
    root   = mk(-1, G_IBOX, 0, 0, scr_w, scr_h);
    bar    = mk(root, G_BOX, 0, 0, scr_w, hchar + 2);
    mtree[bar].ob_spec.index = 0x00001100L;
    active = mk(bar, G_IBOX, 2 * wchar, 0, total, hchar + 3);
    for (i = 0; i < NMENU; i++) {
        short w = (short)strlen(titles[i]) * wchar;
        short t = mk(active, G_TITLE, x, 0, w, hchar + 3);
        mtree[t].ob_spec.free_string = titles[i];
        tx[i] = x;
        x += w;
    }
    screen = mk(root, G_IBOX, 0, hchar + 3, scr_w, scr_h - hchar - 3);

    for (i = 0; i < NMENU; i++) {
        const MI *m = menus[i];
        short n = 0, maxl = 0, w, h, dx, box, first = (short)nobj;
        (void)first;
        for (k = 0; m[k].text; k++) {
            short l = 0;
            if (m[k].text >= 0) {
                l = (short)(strlen(mi_text(&m[k])) + 4);
                if (m[k].key) l = (short)(l + strlen(m[k].key) + 2);
            }
            if (l > maxl) maxl = l;
            n++;
        }
        if (maxl > 42) maxl = 42;
        if (maxl < 12) maxl = 12;
        memset(dashes[i], '-', maxl);
        dashes[i][maxl] = 0;
        w = maxl * wchar; h = n * hchar;
        dx = 2 * wchar + tx[i];
        if (dx + w > scr_w - 2) dx = scr_w - 2 - w;
        box = mk(screen, G_BOX, dx, 0, w, h);
        mtree[box].ob_spec.index = 0x00FF1100L;
        for (k = 0; k < n; k++) {
            short o = mk(box, G_STRING, 0, k * hchar, w, hchar);
            if (m[k].text < 0) {
                mtree[o].ob_spec.free_string = dashes[i];
                mtree[o].ob_state |= OS_DISABLED;
            } else {
                char *s = items[o];
                char line[48];
                int tl;
                sprintf(line, "  %s", mi_text(&m[k]));
                pad_item(s, line, maxl);
                if (m[k].key) {
                    tl = (int)strlen(m[k].key);
                    memcpy(s + maxl - tl - 1, m[k].key, tl);
                }
                mtree[o].ob_spec.free_string = s;
            }
            mcmd[o] = m[k].cmd < 0 ? 0 : m[k].cmd;
            if (m[k].cmd > 0 && m[k].cmd < NCMD) cmdobj[m[k].cmd] = o;
        }
    }
    mtree[nobj - 1].ob_flags |= OF_LASTOB;
}

static void check(int cmd, int on)  { if (cmd > 0 && cmd < NCMD && cmdobj[cmd]) menu_icheck(mtree, cmdobj[cmd], on ? 1 : 0); }
static void enable(int cmd, int on) { if (cmd > 0 && cmd < NCMD && cmdobj[cmd]) menu_ienable(mtree, cmdobj[cmd], on ? 1 : 0); }

/* set a dynamic item's text, keeping the menu width */
static void set_item(int cmd, const char *text)
{
    short o;
    char *s;
    int w;
    if (cmd <= 0 || cmd >= NCMD || !(o = cmdobj[cmd])) return;
    s = mtree[o].ob_spec.free_string;
    w = (int)strlen(s);
    s[0] = s[1] = ' ';
    pad_item(s + 2, text, w - 2);
}

void update_menu(DOC *d)
{
    int i, intbl = 0, hasdoc = d != NULL && !d->iconified;
    PARA *p = NULL;
    for (i = 0; i < NCOLORS; i++) enable(C_COLOR0 + i, colour_on);
    check(C_LIVESPELL, pref_live);
    for (i = 0; i < MAX_UDICT; i++) {
        if (i < spell_nuser) {
            const char *s = strrchr(spell_upath[i], '\\');
            char t[40];
            sprintf(t, "%.24s", s ? s + 1 : spell_upath[i]);
            set_item(C_UD0 + i, t);
        } else set_item(C_UD0 + i, L(S_NO_UDICT));
        enable(C_UD0 + i, i < spell_nuser);
        check(C_UD0 + i, i == spell_active);
    }
    for (i = C_UNDO; i <= C_WRAPCOL; i++) enable(i, hasdoc);
    for (i = C_SAVE; i <= C_CLOSE; i++) enable(i, d != NULL);
    enable(C_SPELL, hasdoc); enable(C_THES, hasdoc); enable(C_SUMMARY, hasdoc);
    xfer_refresh();
    if (!d) return;
    if (hasdoc) {
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
    for (i = 0; i < font_items; i++) check(C_FONT0 + i, i == d->ins.font);
    for (i = 0; i < NSIZES; i++) check(C_SIZE0 + i, sizes[i] == d->ins.size);
    for (i = 0; i < NCOLORS; i++) check(C_COLOR0 + i, i == d->ins.color);
    check(C_BOLD, d->ins.attr & A_BOLD);
    check(C_ITALIC, d->ins.attr & A_ITALIC);
    check(C_UNDER, d->ins.attr & A_UNDER);
    if (p) {
        check(C_JLEFT, p->just == J_LEFT);   check(C_JCENTRE, p->just == J_CENTRE);
        check(C_JRIGHT, p->just == J_RIGHT); check(C_JFULL, p->just == J_FULL);
        check(C_SP1, p->spacing == LS_SINGLE); check(C_SP15, p->spacing == LS_ONEHALF);
        check(C_SP2, p->spacing == LS_DOUBLE);
        check(C_HANGING, p->first < 0);
        check(C_BULLETS, p->list == LIST_BULLET); check(C_NUMBERS, p->list == LIST_NUMBER);
        check(C_HEAD1, p->toc == 1); check(C_HEAD2, p->toc == 2); check(C_HEAD3, p->toc == 3);
        check(C_BODY, p->toc == 0);
    }
    check(C_VDOC, d->view == VIEW_DOC); check(C_SPLIT, d->view == VIEW_SPLIT); check(C_VCODES, d->view == VIEW_CODES);
    check(C_TOOLBAR, d->toolbar);
    check(C_RULERS, pref_rulers);
    check(C_GUIDES, pref_guides);
    check(C_ICONIFY, d->iconified);
}

void update_winmenu(void)
{
    DOC *t = top_doc();
    int i, k = 0;
    /* the open documents, in order, in the first entries */
    for (i = 0; i < MAX_DOCS && k < win_items; i++) {
        char s[40];
        if (!DOC_USED(i)) continue;
        sprintf(s, "%d  %.18s%s", k + 1, docs[i]->base, docs[i]->iconified ? " *" : "");
        set_item(C_WIN0 + k, s);
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
    {
        int n = 0;
        for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i)) n++;
        enable(C_WINMORE, n > win_items);
    }
}

void rebuild_menu(void)
{
    menu_bar(mtree, 0);
    build_menu();
    menu_bar(mtree, 1);
    update_menu(top_doc());
    update_winmenu();
}

/* month and day names for dates, in the current language */
static const char *mnames[12], *dnames[7];

void set_language(int lang)
{
    int i;
    ui_lang = lang >= 0 && lang < NLANG ? lang : 0;
    for (i = 0; i < 12; i++) mnames[i] = L(S_MONTH1 + i);
    for (i = 0; i < 7; i++) dnames[i] = L(S_DAY0 + i);
    date_months = mnames;
    date_days = dnames;
    edlog("language %d", ui_lang);
}

/* ================================================================= */
/*  Tool windows                                                      */
/* ================================================================= */

static const TOOL *tools[] = { &spell_tool, &find_tool, &thes_tool, &import_tool, &export_tool, &fontsel_tool, &preview_tool, &draw_tool, NULL };

int tool_owns(short wh)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(wh)) return 1;
    return 0;
}

int tool_route_msg(short *msg)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(msg[3])) { tools[i]->msg(msg); return 1; }
    return 0;
}

int tool_route_click(short wh, short mx, short my, short clicks)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(wh)) { tools[i]->click(mx, my, clicks); return 1; }
    return 0;
}

int tool_route_key(short wh, short ks, short key)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(wh)) { tools[i]->key(ks, key); return 1; }
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

DOC *doc_of(short wh)
{
    int i;
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i) && docs[i]->wh == wh) return docs[i];
    return NULL;
}

static DOC *last_doc;

/* the document that menu commands act on: the top document window, or,
 * when a tool window is on top, the document window used last */
DOC *active_doc(void)
{
    short h, d1, d2, d3;
    DOC *d;
    int i;
    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    d = doc_of(h);
    if (d) return last_doc = d;
    if (spell_doc() && tool_owns(h)) return spell_doc();
    if (last_doc && last_doc->used) return last_doc;
    for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i)) return docs[i];
    return NULL;
}

DOC *top_doc(void)
{
    short h, d1, d2, d3;
    DOC *d;
    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    d = doc_of(h);
    if (d) return last_doc = d;
    if (tool_owns(h)) return active_doc();
    return NULL;
}

static void base_name(DOC *d, const char *path)
{
    const char *s = strrchr(path, '\\');
    if (!s) s = strrchr(path, ':');
    s = s ? s + 1 : path;
    strncpy(d->base, s, sizeof d->base - 1);
    d->base[sizeof d->base - 1] = 0;
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

/* a document has been created or loaded: lay it out, start its undo */
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
    if (!d) return;
    doc_prefs(d);
    sprintf(d->base, "%s %d", L(S_UNTITLED), ++untitled_no);
    doc_loaded(d);
    open_window(d);
}

static void release_doc(DOC *d);
/* File > New from Template: a letter or newsletter to fill in */
static void cmd_template(void)
{
#ifndef TPL_DEF
#define TPL_DEF TPL_UK
#endif
    static int kind = TPL_DEF;
    const char *w[NTW], *addr[3];
    DOC *d;
    int i, ok;
    if (!dlg_template(&kind)) return;
    save_prefs();                           /* the name and address for next time */
    if (kind == TPL_BLANK) { cmd_new(); return; }
    d = new_doc();
    if (!d) return;
    doc_prefs(d);
    sprintf(d->base, "%s %d", L(S_UNTITLED), ++untitled_no);
    for (i = 0; i < NTW; i++) w[i] = L(T_TO + i);
    for (i = 0; i < 3; i++) addr[i] = tpl_addr[i];
    busy_begin(L(S_OPENING));
    ok = doc_template(d, kind, tpl_name, addr, tpl_title, w);
    busy_end();
    if (!ok) { alert(1, 3, AL_NO_MEMORY, AB_OK); release_doc(d); return; }
    undo_reset(d);
    open_window(d);
    close_pristine(d);
}

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    size_t i;
    if (n < m) return 0;
    for (i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != ext[i]) return 0;
    }
    return 1;
}

static int file_type(const char *path, int text) { return file_detect(path, text); }

/* a document slot that wasn't used after all */
static void release_doc(DOC *d)
{
    doc_free(d);
    d->used = 0;
}

/* An Untitled window nobody has typed in goes when another document is
 * opened or made in its place. */
void close_pristine(DOC *keep)
{
    int i;
    for (i = 0; i < MAX_DOCS; i++) {
        DOC *o = docs[i];
        if (!DOC_USED(i) || o == keep || o->path[0] || o->len || o->dirty || o->cv.dirty) continue;
        close_doc(o);
    }
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

    if (type == FT_AUTO) type = file_type(path, 1);
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
    switch (type) {
    case FT_TEXT: ok = text_import_buf(d, b, n, cs, join); break;
    case FT_MD:   ok = md_import(d, path, cs); break;
    case FT_FWP:  ok = fwp_import(d, path); break;
    case FT_PT:   ok = pt_import(d, path); break;
    default:      ok = rtf_import(d, path); break;
    }
    free(b);
    if (into || !ok) busy_end();
    if (!ok) {
        alert(1, 3, AL_CANT_READ, AB_OK);
        if (into) doc_free(d); else release_doc(d);
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
    if (type != FT_RTF) d->dirty = 1;      /* saved as RTF, under a new name */
    open_window(d);
    close_pristine(d);
    return 1;
}

/* text: 0 open (RTF, or what the file turns out to be), 1 import (plain
 * text asks how to read it) */
void open_path(const char *path, int text)
{
    read_into(path, file_type(path, text), CS_AUTO, 0, NULL, 1);
}

static void default_name(DOC *d, const char *ext, char *def)
{
    char *s;
    strcpy(def, d->base);
    s = strchr(def, '.');
    if (s) *s = 0;
    def[8] = 0;
    for (s = def; *s; s++) if (*s == ' ') *s = '_';
    strcat(def, ext);
}

static int save_as(DOC *d)
{
    char path[256], def[40];
    default_name(d, ".RTF", def);
    if (!ask_file(L(S_FS_SAVE), "*.RTF", def, path)) return 0;
    strcpy(d->path, path);
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
    char path[256], def[40];
    int ok;
    default_name(d, ".PDF", def);
    if (!ask_file(L(S_FS_PDF), "*.PDF", def, path)) return;
#ifdef EDLOG
    edlog_more();
#endif
    edlog("export: file %s chosen, redrawing", path);
    pump_redraws();                    /* let the file selector's area redraw first */
    {
        GRECT over;
        gauge_open(L(S_EXPORTING_PDF), preview_area(&over) ? &over : NULL);
    }
    ok = export_pdf(d, path, gauge_set);
    gauge_close();
    edlog("export: finished (%s)", ok ? "ok" : "failed");
    if (!ok) alert(1, 3, AL_CANT_PDF, AB_OK);
}

/* Markdown and 1st Word Plus: the file selector, then write it */
static void cmd_export(DOC *d, int fmt)
{
    char name[40];
    if (!export_doc(d, fmt, 1, CS_ATARI, name) && name[0]) alert(1, 3, AL_CANT_WRITE, AB_OK);
}

/* The Export window: the file selector for the format, then write a copy
 * of the document (its own name and path don't change). written gets the
 * file's name; it stays empty if the file selector was cancelled. */
int export_doc(DOC *d, int fmt, int crlf, int cs, char *written)
{
    static const char *ext[] = { ".RTF", ".TXT", ".MD", ".DOC", ".PRO", ".PDF" };
    static const char *pat[] = { "*.RTF", "*.TXT", "*.MD", "*.DOC", "*.PRO", "*.PDF" };
    static const int title[] = { S_FS_EXPRTF, S_FS_EXPORT, S_FS_EXPMD, S_FS_EXPFWP, S_FS_EXPPT, S_FS_PDF };
    char path[256], def[40];
    const char *base;
    int ok;
    written[0] = 0;
    if (fmt < FT_RTF || fmt > FT_PDF) return 0;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    default_name(d, ext[fmt], def);
    if (!ask_file(L(title[fmt]), pat[fmt], def, path)) return 0;
    base = strrchr(path, '\\');
    strncpy(written, base ? base + 1 : path, 39);
    written[39] = 0;
    pump_redraws();
    if (fmt == FT_PDF) {
        GRECT over;
        gauge_open(L(S_EXPORTING_PDF), preview_area(&over) ? &over : NULL);
        ok = export_pdf(d, path, gauge_set);
        gauge_close();
        return ok;
    }
    busy_begin(L(S_EXPORTING));
    switch (fmt) {
    case FT_TEXT: ok = text_export(d, path, crlf, cs); break;
    case FT_MD:   ok = md_export(d, path, crlf, cs); break;
    case FT_FWP:  ok = fwp_export(d, path); break;
    case FT_PT:   ok = pt_export(d, path); break;
    default:      ok = rtf_export(d, path); break;
    }
    busy_end();
    return ok;
}

/* File > Print: through GDOS to its printer (device 21) */
void cmd_print(DOC *d)
{
    static int copies = 1;
    unsigned char *sel;
    char name[80];
    if (!d) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    doc_layout(d);
    if (!printer_name(name)) { alert(1, 3, AL_NO_PRINTER, AB_OK); return; }
    sel = (unsigned char *)malloc((size_t)doc_npages(d) + 1);
    if (!sel) { alert(1, 3, AL_NO_MEMORY, AB_OK); return; }
    if (dlg_print(d, name, sel, &copies) && d->used && !print_doc(d, sel, copies))   /* -1: cancelled */
        alert(1, 3, AL_PRINT_FAILED, AB_OK);
    free(sel);
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
    char path[256], def[40];
    int crlf, cs, ok;
    if (!dlg_textexport(&crlf, &cs)) return;
    default_name(d, ".TXT", def);
    if (!ask_file(L(S_FS_EXPORT), "*.TXT", def, path)) return;
    busy_begin(L(S_EXPORTING));
    ok = text_export(d, path, crlf, cs);
    busy_end();
    if (!ok) alert(1, 3, AL_CANT_WRITE, AB_OK);
}

int close_doc(DOC *d)
{
    int i;
    if (dlg_busy) { dlg_front(); return 0; }       /* finish the dialog first */
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    if (d->dirty) {
        int r = alert(1, 2, AL_SAVE_CHANGES, AB_SAVE_DISCARD, d->base);
        if (r == 3) return 0;
        if (r == 1 && !cmd_save(d, 0)) return 0;
    }
    for (i = 0; tools[i]; i++) tools[i]->doc_closed(d);
    if (last_doc == d) last_doc = NULL;
    close_window(d);
    img_cache_free(d);
    release_doc(d);
    update_menu(top_doc());
    update_winmenu();
    return 1;
}

static void cmd_quit(void)
{
    int i;
    if (dlg_busy) { dlg_front(); return; }
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

/* change a paragraph format of the selection or the caret's paragraph */
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
    unsigned short dt = (unsigned short)Tgetdate(), tm = (unsigned short)Tgettime();
    char s[48];
    long from;
    fmt_date(s, style, 1980 + (dt >> 9), (dt >> 5) & 15, dt & 31, tm >> 11, (tm >> 5) & 63);
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
            wind_set(d->wh, WF_TOP, 0, 0, 0, 0);
            update_menu(d);
            update_winmenu();
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
    char path[256];

    switch (cmd) {
    case C_ABOUT:  dlg_about(); return;
    case C_NEW:    cmd_new(); return;
    case C_TEMPLATE: cmd_template(); return;
    case C_OPEN:   if (ask_file(L(S_FS_OPEN), "*.RTF", NULL, path)) open_path(path, 0); return;
    case C_IMPORT: import_open(); return;
    case C_EXPORT: export_open(); return;
    case C_RULERS:
    case C_GUIDES:
        if (cmd == C_RULERS) pref_rulers = !pref_rulers; else pref_guides = !pref_guides;
        save_prefs();
        {
            int i;
            for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i) && !docs[i]->iconified) win_resized(docs[i]);
        }
        update_menu(d);
        return;
    case C_QUIT:   cmd_quit(); return;
    case C_CYCLE:  cycle_windows(); return;
    case C_FONTSEL: if (!font_protocol_select()) fontsel_open(); return;
    case C_MOREFONTS: fontsel_open(); return;
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
        int keep = lay_keep;
        if (dlg_prefs()) {
            if (lay_keep != keep) {                 /* the pages break differently */
                int i;
                busy_begin(L(S_REPAGINATING));
                for (i = 0; i < MAX_DOCS; i++)
                    if (DOC_USED(i)) { docs[i]->lay_full = 1; doc_layout(docs[i]); }
                busy_end();
            }
            if (pref_live) spell_ensure(0);
            save_prefs();
            display_apply();
            autosave_reset();
            if (lang != ui_lang) { tools_close_all(); rebuild_menu(); }
            update_menu(d);
            all_windows_refresh();
        }
        return;
    }
    case C_UDLOAD:
        if (ask_file(L(S_FS_UDLOAD), "*.UDC", NULL, path)) {
            if (spell_user_load(path, 0) < 0) alert(1, 3, AL_UD_LOAD, AB_OK);
            update_menu(d);
        }
        return;
    case C_UDNEW:
        if (ask_file(L(S_FS_UDNEW), "*.UDC", "PERSONAL.UDC", path)) {
            if (spell_user_load(path, 1) < 0) alert(1, 3, AL_UD_NEW, AB_OK);
            update_menu(d);
        }
        return;
    case C_ICONALL: {
        int i;
        for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i)) iconify(docs[i], NULL);
        return;
    }
    }
    if (cmd >= C_UD0 && cmd < C_UD0 + MAX_UDICT) { spell_active = cmd - C_UD0; update_menu(d); return; }
    if (cmd == C_WINMORE) {                 /* all the windows in a popup */
        static const char *items[MAX_DOCS];
        static char names[MAX_DOCS][28];
        short slot[MAX_DOCS], mx, my, mb, ks;
        int i, n = 0, cur = -1, sel;
        for (i = 0; i < MAX_DOCS; i++)
            if (DOC_USED(i)) {
                sprintf(names[n], "%.24s", docs[i]->base);
                items[n] = names[n];
                if (docs[i] == d) cur = n;
                slot[n++] = (short)i;
            }
        graf_mkstate(&mx, &my, &mb, &ks);
        sel = popup(items, n, cur, mx, my);
        if (sel >= 0) cmd = C_WIN0 + MAX_DOCS + slot[sel];        /* below */
        else return;
    }
    if (cmd >= C_WIN0 && cmd < C_WIN0 + 2 * MAX_DOCS) {
        int k = cmd - C_WIN0;
        DOC *w = k >= MAX_DOCS ? docs[k - MAX_DOCS] : winmap[k] >= 0 ? docs[winmap[k]] : NULL;
        if (!w || !w->used) return;
        if (w->iconified) uniconify(w, NULL);
        wind_set(w->wh, WF_TOP, 0, 0, 0, 0);
        update_menu(w);
        update_winmenu();
        return;
    }
    if (!d) return;

    /* window commands work on iconified windows too */
    switch (cmd) {
    case C_CLOSE:   close_doc(d); return;
    case C_ICONIFY: if (d->iconified) uniconify(d, NULL); else iconify(d, NULL); return;
    case C_BOTTOM:  send_to_back(d); return;
    case C_SAVE:    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d); cmd_save(d, 0); return;
    case C_SAVEAS:  if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d); cmd_save(d, 1); return;
    case C_PREVIEW: preview_open(d); return;
    case C_PRINT:   cmd_print(d); return;
    }
    if (d->iconified) return;

    /* the clipboard commands act on the codes text when it has the focus */
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
    case C_PDF:     cmd_pdf(d); break;
    case C_REPAG:   cmd_repag(d); break;
    case C_PAGENUM:
        if (dlg_pagenum(d)) doc_changed(d);
        break;
    case C_HEADFOOT:
        if (dlg_headfoot(d)) doc_changed(d);
        break;
    case C_CHART: {
        long e2 = doc_para_end(d, d->cursor < d->len ? d->cursor : d->len - 1);
        int t2 = d->len ? PARA_OF(d, &d->text[e2 < d->len ? e2 : d->len - 1])->tbl - 1 : -1;
        short mx, my, mb, mk;
        if (t2 < 0) { alert(1, 1, AL_CHART_TABLE, AB_OK); break; }
        graf_mkstate(&mx, &my, &mb, &mk);
        chart_popup(d, t2, mx, my);
        break;
    }
    case C_INSFILE:
        if (ask_file(L(S_FS_INSERT), "*.*", NULL, path)) insert_file(d, path);
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
    case C_TOC:
        busy_ptr(1);
        a = 0;
        if (toc_find(d, &a, &b)) a = toc_update(d);
        else a = toc_insert(d, d->cursor, L(S_CONTENTS));
        busy_ptr(0);
        if (a) doc_changed(d);
        else alert(1, 1, AL_NO_TOC_HERE, AB_OK);
        break;
    case C_COPY:   clip_copy(d); break;
    case C_CUT:
        if (d->selobj >= 0) { if (clip_copy(d)) do_cmd(C_OBJDEL); }
        else if (doc_sel(d, &a, &b)) { clip_copy(d); doc_delete_sel(d); after_edit(d, a); }
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
/*  Events                                                            */
/* ================================================================= */

#define FONT_CHANGED 0x7A18           /* the font protocol */
#define FONT_SELECT  0x7A19

/* a font chosen in an external font selector (or dropped from a font
 * palette): msg[3] window (or < 0), [4] VDI font id, [5] points, [7] effects */
static void font_changed(short *msg)
{
    DOC *d = msg[3] >= 0 ? doc_of(msg[3]) : NULL;
    int i, f = -1;
    if (!d) d = active_doc();
    if (!d || d->iconified) return;
    if (msg[4] > 0) for (i = 0; i < nfonts; i++) if (fonts[i].id == msg[4]) { f = i; break; }
    if (f >= 0) apply_style(d, AP_FONT, f);
    if (msg[5] > 0 && msg[5] <= 120) apply_style(d, AP_SIZE, msg[5]);
    if (msg[7] >= 0) {
        int a = 0;
        if (msg[7] & 1) a |= A_BOLD;
        if (msg[7] & 4) a |= A_ITALIC;
        if (msg[7] & 8) a |= A_UNDER;
        apply_style(d, AP_ATTRS, a);
    }
}

void handle_msg(short *msg)
{
    DOC *d;
    switch (msg[0]) {
    case MN_SELECTED:
        menu_tnormal(mtree, msg[3], 1);
        if (msg[4] >= 0 && msg[4] < MAXOBJ) do_cmd(mcmd[msg[4]]);
        return;
    case AP_TERM:
        cmd_quit();
        return;
    case FONT_CHANGED:
        font_changed(msg);
        return;
    }
    if (gauge_msg(msg)) return;
    if (tool_route_msg(msg)) return;
    d = doc_of(msg[3]);
    if (!d) return;
    if (msg[0] == WM_CLOSED) { close_doc(d); return; }
    win_msg(d, msg);
}

/* deal with pending redraws while EdWord is busy (exporting, printing):
 * the windows are redrawn and can be moved; menu choices wait */
/* While a job has a document half made (laying it out, say), its window
 * is not drawn: what it asks for is kept, and drawn when the job ends. */
int pump_defer;
static GRECT defer_r[MAX_DOCS];
static char defer_on[MAX_DOCS];

void pump_flush(void)
{
    int i;
    for (i = 0; i < MAX_DOCS; i++)
        if (defer_on[i]) {
            defer_on[i] = 0;
            if (DOC_USED(i) && docs[i]->wh >= 0) redraw_area(docs[i], &defer_r[i]);
        }
}

void pump_redraws(void)
{
    short msg[8], mx, my, mb, ks, key, br, ev;
    busy_ptr_track();
    for (;;) {
        ev = evnt_multi(MU_MESAG | MU_TIMER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        msg, 0L, &mx, &my, &mb, &ks, &key, &br);
        if (!(ev & MU_MESAG)) break;
        if (msg[0] == MN_SELECTED) { menu_tnormal(mtree, msg[3], 1); continue; }
        if (gauge_msg(msg)) continue;
        if (pump_defer && msg[0] == WM_REDRAW && doc_of(msg[3])) {
            DOC *d = doc_of(msg[3]);
            GRECT *r = &defer_r[d->slot];
            short x2 = msg[4] + msg[6], y2 = msg[5] + msg[7];
            if (!defer_on[d->slot]) { r->g_x = msg[4]; r->g_y = msg[5]; r->g_w = msg[6]; r->g_h = msg[7]; defer_on[d->slot] = 1; }
            else {
                if (r->g_x + r->g_w > x2) x2 = r->g_x + r->g_w;
                if (r->g_y + r->g_h > y2) y2 = r->g_y + r->g_h;
                if (msg[4] < r->g_x) r->g_x = msg[4];
                if (msg[5] < r->g_y) r->g_y = msg[5];
                r->g_w = x2 - r->g_x; r->g_h = y2 - r->g_y;
            }
            continue;
        }
        if (msg[0] == WM_REDRAW || msg[0] == WM_MOVED || msg[0] == WM_TOPPED) {
            DOC *d = doc_of(msg[3]);
            if (d) win_msg(d, msg);
            else tool_route_msg(msg);
        }
    }
}

/* ask an external font selector, named by the FONTSELECT environment
 * variable (the font protocol), for the active document's font;
 * returns 0 if there is none to ask */
int font_protocol_select(void)
{
    char *env = NULL, list[256], *p, *e;
    DOC *d = active_doc();
    short msg[8];
    if (!d) return 0;
    if (!shel_envrn(&env, "FONTSELECT=") || !env || !*env) env = getenv("FONTSELECT");
    if (!env || !*env) return 0;
    strncpy(list, env, sizeof list - 1);
    list[sizeof list - 1] = 0;
    for (p = list; p && *p; p = e) {
        char name[9], *b, *dot;
        short id;
        int k;
        e = strchr(p, ';');
        if (e) *e++ = 0;
        b = strrchr(p, '\\');
        b = b ? b + 1 : p;
        dot = strchr(b, '.');
        for (k = 0; k < 8; k++) {
            char c = (b + k < (dot ? dot : b + strlen(b))) ? b[k] : ' ';
            name[k] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
        }
        name[8] = 0;
        id = appl_find(name);
        if (id < 0 && strchr(p, '\\') && aes_global[1] != 1) {   /* not running: start it (multitasking) */
            int t;
            if (shel_write(1, 1, 100, p, "") > 0)
                for (t = 0; t < 30 && (id = appl_find(name)) < 0; t++) evnt_timer(100L);
        }
        if (id < 0) continue;
        memset(msg, 0, sizeof msg);
        msg[0] = FONT_SELECT;
        msg[1] = ap_id;
        msg[3] = d->wh;
        msg[4] = fonts[d->ins.font < nfonts ? d->ins.font : 0].id;
        msg[5] = d->ins.size;
        msg[6] = -1;
        msg[7] = (short)(((d->ins.attr & A_BOLD) ? 1 : 0) | ((d->ins.attr & A_ITALIC) ? 4 : 0) |
                         ((d->ins.attr & A_UNDER) ? 8 : 0));
        appl_write(id, 16, msg);
        return 1;
    }
    return 0;
}

/* function keys and other keys that work without Control */
static int special_key(short ks, short key)
{
    short scan = (key >> 8) & 0xff;
    int shift = (ks & 3) != 0;
    switch (scan) {
    case 0x61: do_cmd(shift ? C_REDO : C_UNDO); return 1;          /* Undo key */
    case 0x3C: do_cmd(C_REPAG); return 1;                           /* F2 */
    case 0x3D: do_cmd(C_FINDNEXT); return 1;                        /* F3 */
    case 0x3E: do_cmd(C_VDOC); return 1;                            /* F4 */
    case 0x3F: do_cmd(C_SPLIT); return 1;                           /* F5 */
    case 0x40: do_cmd(C_VCODES); return 1;                          /* F6 */
    case 0x41: do_cmd(C_ZOOMOUT); return 1;                         /* F7 */
    case 0x42: do_cmd(C_ZOOMIN); return 1;                          /* F8 */
    case 0x43: do_cmd(C_BULLETS); return 1;                         /* F9 */
    case 0x44: do_cmd(C_NUMBERS); return 1;                         /* F10 */
    case 0x5A: do_cmd(C_THES); return 1;                            /* Shift F7 */
    }
    return 0;
}

/* Control shortcuts, as the Atari guidelines have them; Shift with
 * Control gives the second set */
void handle_key(short ks, short key)
{
    short scan = (key >> 8) & 0xff, asc = key & 0xff, h, d1, d2, d3;
    int shift = (ks & 3) != 0;
    DOC *d = top_doc();

    wind_get(0, WF_TOP, &h, &d1, &d2, &d3);
    if (!(ks & 4) && tool_route_key(h, ks, key)) return;
    if (!(ks & 4) && special_key(ks, key)) return;
    if (ks & 4) {                               /* Control */
        if (scan == 0x0F) { do_cmd(C_CYCLE); return; }
        if (scan == 0x1C || scan == 0x72) { do_cmd(shift ? C_COLBRK : C_PGBRK); return; }
        if (scan == 0x61) { do_cmd(C_REDO); return; }
        if (scan == 0x48) { do_cmd(C_PREVPAGE); return; }           /* Control + up / down */
        if (scan == 0x50) { do_cmd(C_NEXTPAGE); return; }
        if (scan == 0x4B || scan == 0x4D || scan == 0x47) {      /* words, document ends */
            if (d) win_key(d, ks, key);
            return;
        }
        if (shift) {
            switch (asc) {
            case 0x13: do_cmd(C_SAVEAS); break;    case 0x10: do_cmd(C_PREVIEW); break;
            case 0x15: do_cmd(C_UNDER); break;     case 0x06: do_cmd(C_FONTSEL); break;
            case 0x0A: do_cmd(C_JFULL); break;     case 0x12: do_cmd(C_RULERS); break;
            }
            return;
        }
        switch (asc) {
        case 0x0E: do_cmd(C_NEW); break;     case 0x0F: do_cmd(C_OPEN); break;
        case 0x13: do_cmd(C_SAVE); break;    case 0x15: do_cmd(C_CLOSE); break;
        case 0x17: do_cmd(C_CYCLE); break;   case 0x11: do_cmd(C_QUIT); break;
        case 0x10: do_cmd(C_PRINT); break;   case 0x06: do_cmd(C_FIND); break;
        case 0x07: do_cmd(C_FINDNEXT); break;
        case 0x18: do_cmd(C_CUT); break;     case 0x03: do_cmd(C_COPY); break;
        case 0x16: do_cmd(C_PASTE); break;   case 0x01: do_cmd(C_SELALL); break;
        case 0x02: do_cmd(C_BOLD); break;    case 0x09: do_cmd(C_ITALIC); break;
        case 0x0C: do_cmd(C_JLEFT); break;   case 0x05: do_cmd(C_JCENTRE); break;
        case 0x12: do_cmd(C_JRIGHT); break;  case 0x0A: do_cmd(C_GOTOPAGE); break;
        case 0x04: do_cmd(C_INSDATE); break; case 0x0B: do_cmd(C_SPELL); break;
        case 0x19: do_cmd(C_SPLIT); break;   case 0x14: do_cmd(C_TOOLBAR); break;
        case 0x1A: do_cmd(C_ICONIFY); break;
        }
        return;
    }
    if (d) win_key(d, ks, key);
}

void handle_click(short mx, short my, short ks, short clicks)
{
    DOC *d = top_doc();
    short wh = wind_find(mx, my);
    if (tool_route_click(wh, mx, my, clicks)) return;
    if (!d || wh != d->wh) return;
    win_click(d, mx, my, ks, clicks);
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

static clock_t last_input;

#ifdef ED_CPU
void cpu_check(void);           /* cpucheck.c: on the wrong processor, an alert and quit */
#endif

int main(int argc, char *argv[])
{
    short work_in[11], work_out[57], i, dmy, o1, o2, o3, o4;
    short msg[8], mx, my, mb, ks, key, br, ev;
    char *s;

#ifdef ED_CPU
    cpu_check();                /* first: the rest is compiled for a bigger processor */
#endif
    ap_id = appl_init();
    if (ap_id < 0) return 1;

    /* where we were started from: the dictionary and the log live there */
    progdir[0] = (char)('A' + Dgetdrv());
    progdir[1] = ':';
    Dgetpath(progdir + 2, 0);
    strcat(progdir, "\\");
    log_start();
    edlog("AES version %04x, ap_id %d, folder %s", (unsigned short)aes_global[0], ap_id, progdir);

    /* hold Control while starting: run without GDOS fonts */
    safe_mode = (Kbshift(-1) & 4) != 0;
    if (safe_mode) edlog("Control held: safe mode, GDOS fonts will not be loaded");
    /* hold Alternate: size outline fonts with vst_point instead of vst_arbpt */
    no_arbpt = (Kbshift(-1) & 8) != 0;
    if (no_arbpt) edlog("Alternate held: outline fonts sized with vst_point");

    load_prefs();
    set_language(pref_lang >= 0 ? pref_lang : tos_language());
    spell_lang = pref_spell >= 0 ? pref_spell : ui_lang;    /* the dictionary: the menus' language at first */

    phys_handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    vhandle = phys_handle;
    edlog("graf_handle: handle %d, char %dx%d", phys_handle, wchar, hchar);
    for (i = 0; i < 10; i++) work_in[i] = 1;
    work_in[10] = 2;
    /* with GDOS loaded, the screen device is Getrez()+2 */
    has_gdos = vq_gdos() != 0;
    if (has_gdos) work_in[0] = (short)(Getrez() + 2);
    edlog("vq_gdos %d, opening workstation for device %d", has_gdos, work_in[0]);
    v_opnvwk(work_in, &vhandle, work_out);
    edlog("v_opnvwk: handle %d", vhandle);
    if (!vhandle) {
        alert(1, 3, AL_NO_VDI, AB_QUIT);
        appl_exit();
        return 1;
    }
    scr_w = work_out[0] + 1;
    scr_h = work_out[1] + 1;
    npens = work_out[13];
    edlog("screen %dx%d, %d pens, pixel %dx%d microns", scr_w, scr_h, npens, work_out[3], work_out[4]);
    if (scr_w < 640) {
        alert(1, 3, AL_NEED_RES, AB_QUIT);
        v_clsvwk(vhandle);
        appl_exit();
        return 1;
    }
    dev_pixw = work_out[3];
    dev_pixh = work_out[4];
    /* pictures keep their colours only on screens that can show them */
    if (tc_init()) {
        long fr = (long)Malloc(-1L);
        img_rgb_max = fr / 3 > 8000000L ? 8000000L : fr / 3;
    } else {
        /* a screen with a palette: colour when there's memory to spare
         * (2 MB free or more), black and white on smaller machines */
        long fr = (long)Malloc(-1L);
        img_rgb_max = 0;
        if (fr >= 2000000L && pal_init()) img_rgb_max = fr / 4 > 8000000L ? 8000000L : fr / 4;
    }
    edlog("colour pictures: up to %ld bytes", img_rgb_max);
    sys_asc = hchar >= 16 ? 13 : 6;

    if (aes_global[0] >= 0x0400 || appl_find("?AGI") >= 0) {
        if (appl_getinfo(AES_WINDOW, &o1, &o2, &o3, &o4)) {
            native_bottom = (o1 & 0x0040) != 0;
            native_iconify = (o1 & 0x0080) && (o3 & 0x0001);
        }
    }
    edlog("AES iconify %d, AES bottom %d", native_iconify, native_bottom);

    dlg_init();
    display_apply();
    busy_ptr(1);
    fonts_load();
    vst_alignment(vhandle, 0, 0, &dmy, &dmy);           /* left, baseline */
    vst_color(vhandle, 1);
    wind_get(0, WF_WORKXYWH, &desk.g_x, &desk.g_y, &desk.g_w, &desk.g_h);
    icons_init();
    build_menu();
    menu_bar(mtree, 1);
    update_menu(NULL);
    busy_ptr(0);
    edlog("menu bar up, desktop %d,%d %dx%d", desk.g_x, desk.g_y, desk.g_w, desk.g_h);

#ifdef EDDEMO
    demo_on = demo_start();
#endif
    if (!demo_on) {
        if (safe_mode) alert(1, 1, AL_SAFE_MODE, AB_OK);
        else if (!has_gdos) alert(1, 1, AL_NO_GDOS, AB_OK);
    }

    if (argc > 1) {
        s = argv[1];
        open_path(s, ends_with(s, ".TXT") || ends_with(s, ".ASC"));
    }
    if (pref_live) spell_ensure(1);
    if (!top_doc() && !demo_on) cmd_new();
    edlog("first window open, entering the event loop");

    while (!quit_now) {
        int need = idle_work(0), timer = need || pref_autosave;
        long tmo = need ? (demo_on ? 50L : 400L) : 5000L, bl = blink_wait(), since;
        if (demo_on) need = timer = 1;
        if (bl >= 0) { timer = 1; if (bl < tmo) tmo = bl; }
        ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON | (timer ? MU_TIMER : 0), 2, 1, 1,
                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        msg, tmo, &mx, &my, &mb, &ks, &key, &br);
        if (ev & (MU_MESAG | MU_KEYBD | MU_BUTTON)) { blink_wake(); last_input = clock(); }
        if (ev & MU_MESAG) handle_msg(msg);
        if (ev & MU_KEYBD) handle_key(ks, key);
        if (ev & MU_BUTTON) handle_click(mx, my, ks, br);
        since = (long)(clock() - last_input) * 1000L / CLOCKS_PER_SEC;
        if ((ev & MU_TIMER) && need && (demo_on || since >= 380)) idle_work(1);
        if ((ev & MU_TIMER) && !quit_now) blink_tick();
#ifdef EDDEMO
        if (demo_on && (ev & MU_TIMER) && !(ev & MU_MESAG)) if (!demo_tick()) quit_now = 1;
#endif
        autosave_tick();
    }

    edlog("quitting");
    tools_close_all();
    display_restore();
    menu_bar(mtree, 0);
    rsc_free();
    spell_free_all();
    thes_close();
    doc_clip_free();
    fonts_unload();
    v_clsvwk(vhandle);
    appl_exit();
    return 0;
}

/* the demo script draws menus itself */
OBJECT *menu_tree(void) { return mtree; }
short menu_title_obj(int i)
{
    short k, n = 0;
    for (k = 0; k < nobj; k++)
        if (mtree[k].ob_type == G_TITLE) { if (n == i) return k; n++; }
    return -1;
}
short menu_box_obj(int i)
{
    short k, n = 0, screen = -1;
    for (k = 0; k < nobj; k++) if (mtree[k].ob_type == G_IBOX && mtree[k].ob_y == hchar + 3) screen = k;
    if (screen < 0) return -1;
    for (k = mtree[screen].ob_head; k != screen && k >= 0; k = mtree[k].ob_next) {
        if (n == i) return k;
        n++;
    }
    return -1;
}
int cmd_by_name(const char *s)
{
    static const struct { const char *n; int c; } t[] = {
        { "ABOUT", C_ABOUT }, { "NEW", C_NEW }, { "TEMPLATE", C_TEMPLATE }, { "SAVE", C_SAVE }, { "PAGE", C_PAGE }, { "HEADFOOT", C_HEADFOOT },
        { "PREVIEW", C_PREVIEW }, { "PDF", C_PDF }, { "CLOSE", C_CLOSE }, { "UNDO", C_UNDO }, { "REDO", C_REDO },
        { "SELALL", C_SELALL }, { "FIND", C_FIND }, { "FINDNEXT", C_FINDNEXT }, { "DATE", C_INSDATE },
        { "PGBRK", C_PGBRK }, { "COLBRK", C_COLBRK }, { "TBLINS", C_TBLINS }, { "TBLPROP", C_TBLPROP },
        { "ROWINS", C_ROWINS }, { "COLINS", C_COLINS }, { "PICTURE", C_PICTURE }, { "DRAWING", C_DRAWING },
        { "TOC", C_TOC }, { "FONTSEL", C_FONTSEL }, { "SIZEOTHER", C_SIZEOTHER }, { "PLAIN", C_PLAIN },
        { "BOLD", C_BOLD }, { "ITALIC", C_ITALIC }, { "UNDER", C_UNDER }, { "JLEFT", C_JLEFT },
        { "JCENTRE", C_JCENTRE }, { "JRIGHT", C_JRIGHT }, { "JFULL", C_JFULL }, { "SP1", C_SP1 }, { "SP15", C_SP15 },
        { "SP2", C_SP2 }, { "INDENTS", C_INDENTS }, { "HANGING", C_HANGING }, { "BULLETS", C_BULLETS },
        { "NUMBERS", C_NUMBERS }, { "HEAD1", C_HEAD1 }, { "HEAD2", C_HEAD2 }, { "HEAD3", C_HEAD3 }, { "BODY", C_BODY },
        { "WRAPCOL", C_WRAPCOL }, { "SPELL", C_SPELL }, { "LIVESPELL", C_LIVESPELL }, { "THES", C_THES },
        { "SUMMARY", C_SUMMARY }, { "PREFS", C_PREFS }, { "VDOC", C_VDOC }, { "SPLIT", C_SPLIT }, { "VCODES", C_VCODES },
        { "ZOOMIN", C_ZOOMIN }, { "ZOOMOUT", C_ZOOMOUT }, { "TOOLBAR", C_TOOLBAR }, { "TILEV", C_TILEV },
        { "TILEH", C_TILEH }, { "CASCADE", C_CASCADE }, { "ICONIFY", C_ICONIFY }, { "OBJPROP", C_OBJPROP },
        { "OBJEDIT", C_OBJEDIT }, { "CHART", C_CHART }, { "EXPTXT", C_EXPTXT }, { "IMPORT", C_IMPORT }, { "INSFILE", C_INSFILE },
        { "CUT", C_CUT }, { "COPY", C_COPY }, { "PASTE", C_PASTE }, { "SAVEAS", C_SAVEAS }, { "OPEN", C_OPEN },
        { "ROWDEL", C_ROWDEL }, { "COLDEL", C_COLDEL }, { "TBLDEL", C_TBLDEL }, { "TIME", C_INSTIME },
        { "OBJDEL", C_OBJDEL }, { "PICTUREDLG", C_PICTURE }, { "UDLOAD", C_UDLOAD }, { "UDNEW", C_UDNEW },
        { "CYCLE", C_CYCLE }, { "BOTTOM", C_BOTTOM }, { "ICONALL", C_ICONALL }, { "MOREFONTS", C_MOREFONTS },
        { "PRINT", C_PRINT }, { "EXPMD", C_EXPMD }, { "EXPFWP", C_EXPFWP }, { "EXPPT", C_EXPPT }, { "EXPORT", C_EXPORT }, { "PREVPAGE", C_PREVPAGE },
        { "NEXTPAGE", C_NEXTPAGE }, { "GOTOPAGE", C_GOTOPAGE }, { "REPAG", C_REPAG }, { "PAGENUM", C_PAGENUM }, { "RULERS", C_RULERS }, { "GUIDES", C_GUIDES },
        { NULL, 0 } };
    int i;
    for (i = 0; t[i].n; i++) if (!strcmp(t[i].n, s)) return t[i].c;
    if (!strncmp(s, "COLOR", 5)) return C_COLOR0 + atoi(s + 5);
    if (!strncmp(s, "SIZE", 4)) return C_SIZE0 + atoi(s + 4);
    if (!strncmp(s, "FONT", 4)) return C_FONT0 + atoi(s + 4);
    return 0;
}
