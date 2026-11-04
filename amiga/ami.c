/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ami.c - EdWord Amiga front end: start-up, the screen, preferences,
 *         menus, commands and the event loop (after gem.c)
 */
#include <exec/memory.h>
#include <devices/timer.h>
#include <graphics/gfxbase.h>
#include <graphics/displayinfo.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <libraries/locale.h>
#include <utility/tagitem.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/utility.h>
#include <proto/locale.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "amiui.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase, *LayersBase, *DiskfontBase, *AslBase;
struct UtilityBase *UtilityBase;
struct LocaleBase *LocaleBase;
extern struct WBStartup *_WBenchMsg;

struct Screen   *scr;
struct DrawInfo *dri;
APTR             vi;
struct TextFont *sysfont, *fixfont;
struct TextAttr  sysattr;
struct Window   *backdrop;
struct MsgPort  *idcmp_port;
short wchar, hchar, sys_asc, scr_w, scr_h, npens, depth, small_icons;
GRECT desk;
int   colour_on = 1, pdf_scale = 1, quit_now;
DOC  *docs[MAX_DOCS];               /* made when first needed, then kept */
char  progdir[256] = "PROGDIR:";
const short sizes[NSIZES] = { 8, 9, 10, 12, 14, 18, 24, 36 };
const short zooms[NZOOM] = { 50, 75, 100, 125, 150, 200 };
static int  untitled_no;
static char sysname[40];

/* ================================================================= */
/*  Preferences, kept in EdWord.prefs next to the program              */
/* ================================================================= */

int pref_date = DT_SHORT, pref_toolbar = 1, pref_paper = P_A4, pref_land;
int pref_autosave, pref_asmin = 5, pref_asmode, pref_display, pref_live;
int pref_rulers, pref_guides = 1;
static int pref_lang = -1, pref_spell = -1;
int pref_screen = SCR_HIRES;                /* the screen EdWord opens on */
unsigned long pref_modeid;                  /* SCR_OTHER: its display mode... */
int pref_scrw, pref_scrh, pref_scrdepth;    /* ...size and depth */
int on_wb;                                  /* running on the Workbench screen */
UBYTE penmap[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static int pens_obtained;

static void prefs_path(char *p) { sprintf(p, "%sEdWord.prefs", progdir); }

static void load_prefs(void)
{
    char path[300], line[80];
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
        else if (!strcmp(line, "screen") && v >= 0 && v < NSCR) pref_screen = v;
        else if (!strcmp(line, "screenmode")) pref_modeid = strtoul(eq + 1, NULL, 16);
        else if (!strcmp(line, "screenw") && v >= 0 && v <= 4096) pref_scrw = v;
        else if (!strcmp(line, "screenh") && v >= 0 && v <= 4096) pref_scrh = v;
        else if (!strcmp(line, "screendepth") && v >= 0 && v <= 8) pref_scrdepth = v;
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
               "rulers=%d\nguides=%d\nspelllang=%d\nscreen=%d\nscreenmode=%lx\nscreenw=%d\nscreenh=%d\nscreendepth=%d\n",
            pref_date, pref_toolbar, colour_on, pdf_scale, pref_paper, pref_land,
            pref_autosave, pref_asmin, pref_asmode, pref_display, ui_lang, pref_live, pref_rulers, pref_guides, spell_lang,
            pref_screen, pref_modeid, pref_scrw, pref_scrh, pref_scrdepth);
    fclose(f);
}

/* the language Locale prefers (locale.library is V38, Workbench 2.1) */
static int system_language(void)
{
    struct Locale *lo;
    int i, r = LANG_EN;
    if (!LocaleBase) return LANG_EN;
    lo = OpenLocale(NULL);
    if (!lo) return LANG_EN;
    for (i = 0; i < 10 && lo->loc_PrefLanguages[i]; i++) {
        const char *s = (const char *)lo->loc_PrefLanguages[i];
        if (!strncmp(s, "deutsch", 7)) { r = LANG_DE; break; }
        if (!strncmp(s, "espa", 4)) { r = LANG_ES; break; }
        if (!strncmp(s, "italiano", 8)) { r = LANG_IT; break; }
        if (!strncmp(s, "fran", 4)) { r = LANG_FR; break; }
        if (!strncmp(s, "english", 7)) break;
    }
    CloseLocale(lo);
    return r;
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
/*  Windows: all share one message port                               */
/* ================================================================= */

#define MAXWIN 24
static struct Window *wins[MAXWIN];
static struct Menu   *strip;
static struct Requester blockreq[MAXWIN];
static char blockon[MAXWIN];

struct Window *open_ewin(struct TagItem *tags, ULONG idcmp)
{
    struct Window *w = OpenWindowTags(NULL, SCR_TAG, (ULONG)scr, WA_IDCMP, 0,
                                      WA_NewLookMenus, TRUE, TAG_MORE, (ULONG)tags);
    int i;
    if (!w) return NULL;
    w->UserPort = idcmp_port;
    ModifyIDCMP(w, idcmp);
    for (i = 0; i < MAXWIN; i++) if (!wins[i]) { wins[i] = w; break; }
    if (strip) SetMenuStrip(w, strip);
    return w;
}

/* close a window that shares the port: its waiting messages go first */
void close_ewin(struct Window *w)
{
    struct IntuiMessage *msg;
    struct Node *succ;
    int i;
    if (!w) return;
    window_closing(w);
    for (i = 0; i < MAXWIN; i++)
        if (wins[i] == w) {
            if (blockon[i]) { EndRequest(&blockreq[i], w); blockon[i] = 0; }
            wins[i] = NULL;
        }
    if (w->MenuStrip) ClearMenuStrip(w);
    Forbid();
    msg = (struct IntuiMessage *)w->UserPort->mp_MsgList.lh_Head;
    while ((succ = msg->ExecMessage.mn_Node.ln_Succ) != NULL) {
        if (msg->IDCMPWindow == w) {
            Remove((struct Node *)msg);
            ReplyMsg((struct Message *)msg);
        }
        msg = (struct IntuiMessage *)succ;
    }
    w->UserPort = NULL;
    ModifyIDCMP(w, 0);
    Permit();
    CloseWindow(w);
}

/* modal dialogs: the other windows take no input meanwhile */
static int  blockn;

void block_input(int on)
{
    int i;
    if (on) {
        if (blockn++ == 0)
            for (i = 0; i < MAXWIN; i++)
                if (wins[i]) {
                    InitRequester(&blockreq[i]);
                    blockon[i] = Request(&blockreq[i], wins[i]) ? 1 : 0;
                }
        busy(1);
    } else if (blockn > 0) {
        busy(0);
        if (--blockn == 0)
            for (i = 0; i < MAXWIN; i++)
                if (blockon[i] && wins[i]) { EndRequest(&blockreq[i], wins[i]); blockon[i] = 0; }
    }
}

void menu_attach(struct Window *w) { if (strip && !w->MenuStrip) SetMenuStrip(w, strip); }
void menu_detach(struct Window *w) { if (w->MenuStrip) ClearMenuStrip(w); }

static void strip_off(void)
{
    int i;
    for (i = 0; i < MAXWIN; i++) if (wins[i] && wins[i]->MenuStrip) ClearMenuStrip(wins[i]);
}

static void strip_on(void)
{
    int i;
    if (!strip) return;
    for (i = 0; i < MAXWIN; i++) if (wins[i] && !wins[i]->MenuStrip) ResetMenuStrip(wins[i], strip);
}

/* ================================================================= */
/*  The menu strip, built at run time in the current language          */
/* ================================================================= */

#define MAXNM 230
#define MAXFONTMENU 24
static struct NewMenu nm[MAXNM];
static int   nnm;
static struct MenuItem *cmditem[NCMD];
static char  dyn[80][34];             /* fonts, sizes, colours, windows, dictionaries */
static int   font_items, win_items;
static short winmap[MAX_DOCS];             /* Window menu entry -> document slot */
static char  titlebuf[10][24];

static void nm_add(UBYTE type, const char *label, const char *key, UWORD flags, int cmd)
{
    struct NewMenu *n;
    if (nnm >= MAXNM - 1) return;
    n = &nm[nnm++];
    memset(n, 0, sizeof *n);
    n->nm_Type = type;
    n->nm_Label = (STRPTR)label;
    n->nm_CommKey = (STRPTR)key;
    n->nm_Flags = flags;
    n->nm_UserData = (APTR)(long)cmd;
}

#define TITLE(t)          nm_add(NM_TITLE, L(t), NULL, 0, 0)
/* in menus with check marks, the other items move right to line up */
static int  indent;
static char ind[40][44];
static int  nind;
static const char *ind_text(const char *s)
{
    if (!indent || nind >= 40) return s;
    sprintf(ind[nind], "  %.40s", s);
    return ind[nind++];
}
#define ITEM(t, k, c)     nm_add(NM_ITEM, ind_text(L(t)), k, 0, c)
#define CITEM(t, k, c)    nm_add(NM_ITEM, L(t), k, CHECKIT | MENUTOGGLE, c)
#define DITEM_(s, c)      nm_add(NM_ITEM, s, NULL, CHECKIT | MENUTOGGLE, c)
#define BAR()             nm_add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0)

/* a dynamic item's text, padded so it can change later */
static const char *pad(int slot, const char *s, int width)
{
    int n;
    strncpy(dyn[slot], s, 33);
    dyn[slot][33] = 0;
    for (n = (int)strlen(dyn[slot]); n < width && n < 33; n++) dyn[slot][n] = ' ';
    dyn[slot][n] = 0;
    return dyn[slot];
}

static void build_menu(void)
{
    int i, rows, room;
    static const int sty[4] = { M_PLAIN, M_BOLD, M_ITALIC, M_UNDER };
    static const char *styk[4] = { NULL, "B", "I", "U" };
    static const short styc[4] = { C_PLAIN, C_BOLD, C_ITALIC, C_UNDER };
    nnm = 0;

    nind = 0;
    indent = 0;
    /* the Amiga User Interface Style Guide: Project first, with New,
     * Open, Save, Save As, Print, About and Quit on their standard keys */
    TITLE(S_AMI_PROJECT);
    ITEM(M_NEW, "N", C_NEW); ITEM(M_OPEN, "O", C_OPEN); ITEM(M_IMPORT, NULL, C_IMPORT); BAR();
    ITEM(M_SAVEDOC, "S", C_SAVE); ITEM(M_SAVEAS, "A", C_SAVEAS); ITEM(M_EXPORT, NULL, C_EXPORT); BAR();
    ITEM(M_PAGE, NULL, C_PAGE); ITEM(M_HEADFOOT, NULL, C_HEADFOOT); ITEM(M_PREVIEW, NULL, C_PREVIEW);
    ITEM(M_PRINT, "P", C_PRINT); BAR();
    ITEM(M_CLOSE, "W", C_CLOSE); BAR();
    ITEM(M_ABOUT, "?", C_ABOUT); BAR();
    ITEM(M_QUIT, "Q", C_QUIT);

    indent = 0;
    TITLE(MT_EDIT);
    ITEM(M_UNDO, "Z", C_UNDO); ITEM(M_REDO, "Y", C_REDO); BAR();
    ITEM(M_CUT, "X", C_CUT); ITEM(M_COPY, "C", C_COPY); ITEM(M_PASTE, "V", C_PASTE); ITEM(M_SELALL, NULL, C_SELALL); BAR();
    ITEM(M_FIND, "F", C_FIND); ITEM(M_FINDNEXT, "G", C_FINDNEXT); BAR();
    ITEM(M_PREVPAGE, NULL, C_PREVPAGE); ITEM(M_NEXTPAGE, NULL, C_NEXTPAGE); ITEM(M_GOTOPAGE, NULL, C_GOTOPAGE); BAR();
    ITEM(M_REPAG, NULL, C_REPAG);

    indent = 0;
    TITLE(MT_INSERT);
    ITEM(M_DATE, "D", C_INSDATE); ITEM(M_TIME, NULL, C_INSTIME); BAR();
    ITEM(M_PGBRK, NULL, C_PGBRK); ITEM(M_COLBRK, NULL, C_COLBRK); ITEM(M_PAGENUM, NULL, C_PAGENUM); BAR();
    ITEM(M_TBLINS, NULL, C_TBLINS); ITEM(M_TBLPROP, NULL, C_TBLPROP); ITEM(M_ROWINS, NULL, C_ROWINS);
    ITEM(M_ROWDEL, NULL, C_ROWDEL); ITEM(M_COLINS, NULL, C_COLINS); ITEM(M_COLDEL, NULL, C_COLDEL);
    ITEM(M_TBLDEL, NULL, C_TBLDEL); ITEM(M_CHART, NULL, C_CHART); BAR();
    ITEM(S_AMI_PICTURE, NULL, C_PICTURE); ITEM(M_DRAWING, NULL, C_DRAWING); BAR();
    ITEM(M_TOC, NULL, C_TOC); BAR(); ITEM(M_INSFILE, NULL, C_INSFILE);

    indent = 1;
    TITLE(MT_FONT);
    ITEM(M_FONTSEL, "T", C_FONTSEL); ITEM(M_SIZEOTHER, NULL, C_SIZEOTHER); BAR();
    for (i = 0; i < NSIZES; i++) {
        char s[20];
        sprintf(s, "%d %s", sizes[i], L(S_POINT));
        DITEM_(pad(i, s, 0), C_SIZE0 + i);
    }
    BAR();
    rows = (scr_h - desk.g_y) / (hchar + 1) - 1;
    room = rows - (NSIZES + 5);
    if (room > MAXFONTMENU) room = MAXFONTMENU;
    font_items = nfonts <= room ? nfonts : room - 1;
    if (font_items < 1) font_items = 1;
    for (i = 0; i < font_items; i++) DITEM_(pad(10 + i, fonts[i].name, 0), C_FONT0 + i);
    if (font_items < nfonts) ITEM(M_MOREFONTS, NULL, C_MOREFONTS);

    indent = 1;
    TITLE(MT_STYLE);
    for (i = 0; i < 4; i++) {
        if (i == 0) ITEM(sty[i], styk[i], styc[i]);
        else CITEM(sty[i], styk[i], styc[i]);
        if (i == 0) BAR();
    }
    BAR();
    for (i = 0; i < NCOLORS; i++) DITEM_(pad(50 + i, L(S_COL_BLACK + i), 0), C_COLOR0 + i);

    indent = 1;
    TITLE(MT_PARA);
    CITEM(M_JLEFT, "L", C_JLEFT); CITEM(M_JCENTRE, "E", C_JCENTRE); CITEM(M_JRIGHT, "R", C_JRIGHT);
    CITEM(M_JFULL, "J", C_JFULL); BAR();
    CITEM(M_SP1, NULL, C_SP1); CITEM(M_SP15, NULL, C_SP15); CITEM(M_SP2, NULL, C_SP2); BAR();
    ITEM(M_INDENTS, NULL, C_INDENTS); CITEM(M_HANGING, NULL, C_HANGING); CITEM(M_BULLETS, NULL, C_BULLETS);
    CITEM(M_NUMBERS, NULL, C_NUMBERS); BAR();
    CITEM(M_HEAD1, NULL, C_HEAD1); CITEM(M_HEAD2, NULL, C_HEAD2); CITEM(M_HEAD3, NULL, C_HEAD3); CITEM(M_BODY, NULL, C_BODY); BAR();
    ITEM(M_WRAPCOL, NULL, C_WRAPCOL);

    indent = 1;
    TITLE(MT_TOOLS);
    ITEM(M_SPELL, "K", C_SPELL); CITEM(M_LIVESPELL, NULL, C_LIVESPELL); ITEM(M_THES, NULL, C_THES);
    ITEM(M_SUMMARY, NULL, C_SUMMARY); BAR();
    ITEM(M_UDLOAD, NULL, C_UDLOAD); ITEM(M_UDNEW, NULL, C_UDNEW); BAR();
    for (i = 0; i < MAX_UDICT; i++) DITEM_(pad(60 + i, L(S_NO_UDICT), 26), C_UD0 + i);
    BAR(); ITEM(M_PREFS, NULL, C_PREFS);

    indent = 1;
    TITLE(MT_WINDOW);
    CITEM(M_VDOC, NULL, C_VDOC); CITEM(M_SPLIT, NULL, C_SPLIT); CITEM(M_VCODES, NULL, C_VCODES); BAR();
    ITEM(M_ZOOMIN, NULL, C_ZOOMIN); ITEM(M_ZOOMOUT, NULL, C_ZOOMOUT); BAR();
    CITEM(M_TOOLBAR, NULL, C_TOOLBAR); CITEM(M_RULERS, NULL, C_RULERS); CITEM(M_GUIDES, NULL, C_GUIDES); BAR();
    ITEM(M_TILEV, NULL, C_TILEV); ITEM(M_TILEH, NULL, C_TILEH); ITEM(M_CASCADE, NULL, C_CASCADE); BAR();
    CITEM(M_ICONIFY, "M", C_ICONIFY); ITEM(M_ICONALL, NULL, C_ICONALL); ITEM(M_BOTTOM, NULL, C_BOTTOM);
    ITEM(M_CYCLE, NULL, C_CYCLE); BAR();
    /* as many documents as fit, then "More windows..." */
    rows = (scr_h - desk.g_y) / (hchar + 1) - 1;
    win_items = rows - 21;
    if (win_items > MAX_DOCS) win_items = MAX_DOCS;
    if (win_items > 9) win_items = 9;
    if (win_items < 1) win_items = 1;
    for (i = 0; i < win_items; i++) DITEM_(pad(70 + i, "-", 24), C_WIN0 + i);
    if (win_items < MAX_DOCS) ITEM(M_WINMORE, NULL, C_WINMORE);
    nm_add(NM_END, NULL, NULL, 0, 0);
    (void)titlebuf;
}

static void map_items(void)
{
    struct Menu *m;
    struct MenuItem *it;
    memset(cmditem, 0, sizeof cmditem);
    for (m = strip; m; m = m->NextMenu)
        for (it = m->FirstItem; it; it = it->NextItem) {
            long c = (long)GTMENUITEM_USERDATA(it);
            if (c > 0 && c < NCMD) cmditem[c] = it;
        }
}

static int make_strip(void)
{
    build_menu();
    strip = CreateMenus(nm, GTMN_FrontPen, 0, TAG_END);
    if (!strip) return 0;
    if (!LayoutMenus(strip, vi, GTMN_NewLookMenus, TRUE, TAG_END)) {
        FreeMenus(strip);
        strip = NULL;
        return 0;
    }
    map_items();
    return 1;
}

/* the wanted state of every item, applied in one go */
static UWORD want_flags[NCMD];
static int   want_set[NCMD];

static void want(int cmd, UWORD bit, int on)
{
    if (cmd <= 0 || cmd >= NCMD || !cmditem[cmd]) return;
    if (!want_set[cmd]) { want_flags[cmd] = cmditem[cmd]->Flags; want_set[cmd] = 1; }
    if (on) want_flags[cmd] |= bit; else want_flags[cmd] &= (UWORD)~bit;
}

static void check(int cmd, int on)  { want(cmd, CHECKED, on); }
static void enable(int cmd, int on) { want(cmd, ITEMENABLED, on); }

static char want_text[NCMD][34];
static int  want_tset[NCMD];
static void set_item(int cmd, const char *text)
{
    if (cmd <= 0 || cmd >= NCMD) return;
    strncpy(want_text[cmd], text, 33);
    want_text[cmd][33] = 0;
    want_tset[cmd] = 1;
}

static void menu_commit(void)
{
    int c, change = 0;
    for (c = 1; c < NCMD; c++) {
        if (want_set[c] && cmditem[c] && cmditem[c]->Flags != want_flags[c]) change = 1;
        if (want_tset[c] && cmditem[c]) {
            struct IntuiText *t = (struct IntuiText *)cmditem[c]->ItemFill;
            char *s = (char *)t->IText;
            int w = (int)strlen(s), k;
            char nt[40];
            strncpy(nt, want_text[c], 33);
            nt[33] = 0;
            for (k = (int)strlen(nt); k < w; k++) nt[k] = ' ';
            nt[w] = 0;
            if (strcmp(nt, s)) change = 1;
        }
    }
    if (change) {
        strip_off();
        for (c = 1; c < NCMD; c++) {
            if (want_set[c] && cmditem[c]) cmditem[c]->Flags = want_flags[c];
            if (want_tset[c] && cmditem[c]) {
                struct IntuiText *t = (struct IntuiText *)cmditem[c]->ItemFill;
                char *s = (char *)t->IText;
                int w = (int)strlen(s), k;
                strncpy(s, want_text[c], (size_t)w);
                for (k = (int)strlen(want_text[c]); k < w; k++) s[k] = ' ';
            }
        }
        strip_on();
    }
    memset(want_set, 0, sizeof want_set);
    memset(want_tset, 0, sizeof want_tset);
}

void update_menu(DOC *d)
{
    int i, intbl = 0, hasdoc = d != NULL && !d->iconified;
    PARA *p = NULL;
    if (!strip) return;
    for (i = 0; i < NCOLORS; i++) enable(C_COLOR0 + i, colour_on);
    check(C_LIVESPELL, pref_live);
    for (i = 0; i < MAX_UDICT; i++) {
        if (i < spell_nuser) {
            const char *s = strrchr(spell_upath[i], '/');
            if (!s) s = strrchr(spell_upath[i], ':');
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
            char s[40];
            if (!DOC_USED(i)) continue;
            n++;
            if (k >= win_items) continue;
            sprintf(s, "%d  %.18s%s", k + 1, lat(docs[i]->base), docs[i]->iconified ? " *" : "");
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
        enable(C_WINMORE, n > win_items);
    }
    check(C_RULERS, pref_rulers);
    check(C_GUIDES, pref_guides);
    menu_commit();
}

void update_winmenu(void) { update_menu(top_doc()); }

void rebuild_menu(void)
{
    strip_off();
    if (strip) FreeMenus(strip);
    strip = NULL;
    make_strip();
    strip_on();
    update_menu(top_doc());
}

/* ================================================================= */
/*  Tool windows                                                      */
/* ================================================================= */

static const TOOL *tools[] = { &spell_tool, &find_tool, &thes_tool, &import_tool, &export_tool, &fontsel_tool, &preview_tool, &draw_tool, NULL };

int tool_owns(struct Window *w)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(w)) return 1;
    return 0;
}

int tool_route(struct IntuiMessage *m)
{
    int i;
    for (i = 0; tools[i]; i++) if (tools[i]->owns(m->IDCMPWindow)) { tools[i]->msg(m); return 1; }
    return 0;
}

int tool_route_key(struct Window *w, int key, int qual)
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

DOC *doc_of_win(struct Window *w)
{
    int i;
    if (!w) return NULL;
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i) && doc_win(docs[i]) == w) return docs[i];
    return NULL;
}

static DOC *last_doc;

/* the active window, if it is one of ours */
static struct Window *active_win(void)
{
    struct Window *w = IntuitionBase->ActiveWindow;
    int i;
    for (i = 0; i < MAXWIN; i++) if (wins[i] && wins[i] == w) return w;
    return NULL;
}

/* the document that menu commands act on: the active document window, or,
 * when a tool window is active, the document window used last */
DOC *active_doc(void)
{
    struct Window *w = active_win();
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
    struct Window *w = active_win();
    DOC *d = doc_of_win(w);
    if (d) return last_doc = d;
    if (w && tool_owns(w)) return active_doc();
    if (last_doc && last_doc->used) return last_doc;
    return NULL;
}

void activate_doc(DOC *d)
{
    struct Window *w = doc_win(d);
    if (!w) return;
    WindowToFront(w);
    ActivateWindow(w);
    last_doc = d;
}

void window_closing(struct Window *w)
{
    (void)w;
}

/* the document's short name (ST characters) from a path */
static void base_name(DOC *d, const char *path)
{
    const char *s = strrchr(path, '/');
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

/* a file name for the document with another extension (Latin-1) */
static void default_name(DOC *d, const char *ext, char *def)
{
    char *s;
    strcpy(def, lat(d->base));
    s = strrchr(def, '.');
    if (s && s != def) *s = 0;
    strcat(def, ext);
}

static int save_as(DOC *d)
{
    char path[256], def[60];
    default_name(d, ".rtf", def);
    if (!ask_file(L(S_FS_SAVE), "#?.rtf", def, path, 1)) return 0;
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
    char path[256], def[60];
    int ok;
    default_name(d, ".pdf", def);
    if (!ask_file(L(S_FS_PDF), "#?.pdf", def, path, 1)) return;
    gauge_open(L(S_EXPORTING_PDF));
    ok = export_pdf(d, path, gauge_set);
    gauge_close();
    if (!ok) alert(1, 3, AL_CANT_PDF, AB_OK);
}

/* The Export window: the file requester for the format, then write a
 * copy of the document (its own name and path don't change). written
 * gets the file's name; it stays empty if the requester was cancelled. */
int export_doc(DOC *d, int fmt, int crlf, int cs, char *written)
{
    static const char *ext[] = { ".rtf", ".txt", ".md", ".doc", ".pro", ".pdf" };
    static const char *pat[] = { "#?.rtf", "#?.txt", "#?.md", "#?.doc", "#?", "#?.pdf" };
    static const int title[] = { S_FS_EXPRTF, S_FS_EXPORT, S_FS_EXPMD, S_FS_EXPFWP, S_FS_EXPPT, S_FS_PDF };
    char path[256], def[60];
    const char *base;
    int ok;
    written[0] = 0;
    if (fmt < FT_RTF || fmt > FT_PDF) return 0;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    default_name(d, ext[fmt], def);
    if (!ask_file(L(title[fmt]), pat[fmt], def, path, 1)) return 0;
    base = strrchr(path, '/');
    if (!base) base = strrchr(path, ':');
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
    if (!export_doc(d, fmt, 0, CS_WIN, name) && name[0]) alert(1, 3, AL_CANT_WRITE, AB_OK);
}

/* Project > Print: through printer.device, as set in Printer preferences */
void cmd_print(DOC *d)
{
    static int copies = 1;
    unsigned char *sel;
    char name[80];
    if (!d) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    doc_layout(d);
    if (!printer_name(name)) { alert(1, 3, AL_AMI_NO_PRINTER, AB_OK); return; }
    sel = (unsigned char *)malloc((size_t)doc_npages(d) + 1);
    if (!sel) { alert(1, 3, AL_NO_MEMORY, AB_OK); return; }
    if (dlg_print(d, name, sel, &copies) && d->used && !print_doc(d, sel, copies))
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
    char path[256], def[60];
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
        int r = alert(1, 2, AL_SAVE_CHANGES, AB_SAVE_DISCARD, lat(d->base));
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
    /* on the Workbench screen there is no screen of our own to keep: the
     * last document closed is the end of EdWord */
    if (on_wb) {
        for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i)) break;
        if (i == MAX_DOCS) quit_now = 1;
    }
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
    char path[256];

    switch (cmd) {
    case C_ABOUT:  dlg_about(); return;
    case C_NEW:    cmd_new(); return;
    case C_OPEN:   if (ask_file(L(S_FS_OPEN), "#?", NULL, path, 0)) open_path(path, text_name(path)); return;
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
        static char names[MAX_DOCS][28];
        short slot[MAX_DOCS];
        int i, n = 0, cur = -1, sel;
        for (i = 0; i < MAX_DOCS; i++)
            if (DOC_USED(i)) {
                sprintf(names[n], "%.24s", lat(docs[i]->base));
                items[n] = names[n];
                if (docs[i] == d) cur = n;
                slot[n++] = (short)i;
            }
        sel = popup(items, n, cur, scr->MouseX, scr->MouseY);
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

    /* window commands work on iconified windows too */
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
        if (ask_file(L(S_FS_INSERT), "#?", NULL, path, 0)) insert_file(d, path);
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

/* function keys and other keys that work without a qualifier */
static int special_key(int key, int qual)
{
    int shift = (qual & KQ_SHIFT) != 0;
    switch (key) {
    case K_F2:  do_cmd(C_REPAG); return 1;
    case K_F3:  do_cmd(C_FINDNEXT); return 1;
    case K_F4:  do_cmd(C_VDOC); return 1;
    case K_F5:  do_cmd(C_SPLIT); return 1;
    case K_F6:  do_cmd(C_VCODES); return 1;
    case K_F7:  do_cmd(shift ? C_THES : C_ZOOMOUT); return 1;
    case K_F8:  do_cmd(C_ZOOMIN); return 1;
    case K_F9:  do_cmd(C_BULLETS); return 1;
    case K_F10: do_cmd(C_NUMBERS); return 1;
    case K_HELP: do_cmd(C_ABOUT); return 1;
    }
    return 0;
}

void handle_key(struct Window *w, int key, int qual)
{
    DOC *d;
    if (!(qual & KQ_CTRL) && tool_route_key(w, key, qual)) return;
    if (special_key(key, qual)) return;
    if (qual & KQ_CTRL) {
        /* the menus have the Right Amiga shortcuts; Control is for the
         * cursor keys (the document's ends) and these two */
        if (key == K_TAB) { do_cmd(C_CYCLE); return; }
        if (key == K_RETURN || key == K_ENTER) { do_cmd((qual & KQ_SHIFT) ? C_COLBRK : C_PGBRK); return; }
        if (key < K_LEFT || key > K_DOWN) return;
    }
    d = doc_of_win(w);
    if (d) win_key(d, key, qual);
}

static int key_qual(UWORD q)
{
    return ((q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? KQ_SHIFT : 0) |
           ((q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? KQ_ALT : 0) |
           ((q & IEQUALIFIER_CONTROL) ? KQ_CTRL : 0) |
           ((q & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) ? KQ_AMIGA : 0);
}

/* VANILLAKEY and RAWKEY to our key codes; 0 to ignore */
static int map_key(ULONG cl, UWORD code, UWORD qual)
{
    if (cl == IDCMP_VANILLAKEY) {
        switch (code) {
        case 8: return K_BS;
        case 127: return K_DEL;
        case 13: return (qual & IEQUALIFIER_NUMERICPAD) ? K_ENTER : K_RETURN;
        case 10: return K_RETURN;
        case 9: return K_TAB;
        case 27: return K_ESC;
        }
        if ((qual & IEQUALIFIER_CONTROL) && code < 32) return code;
        if (code < 32) return 0;
        return code;
    }
    if (code & IECODE_UP_PREFIX) return 0;
    switch (code) {
    case 0x4C: return K_UP;
    case 0x4D: return K_DOWN;
    case 0x4E: return K_RIGHT;
    case 0x4F: return K_LEFT;
    case 0x5F: return K_HELP;
    case 0x46: return K_DEL;
    case 0x41: return K_BS;
    case 0x44: return K_RETURN;
    case 0x43: return K_ENTER;
    case 0x42: return K_TAB;
    case 0x45: return K_ESC;
    }
    if (code >= 0x50 && code <= 0x59) return K_F1 + (code - 0x50);
    return 0;
}

/* ================================================================= */
/*  The timer: idle work after a pause                                */
/* ================================================================= */

static struct MsgPort *tport;
static struct timerequest *treq;
static int tbusy, topen;
ULONG timer_sig;

#ifdef EDLOG
/* a log of how long things take, in PROGDIR:EdWord.log ("make EDLOG=1") */
struct Device *TimerBase;
static ULONG efreq;
static struct EClockVal e0;
void edlog(const char *fmt, ...)
{
    static FILE *f;
    struct EClockVal e;
    char buf[200];
    va_list ap;
    if (!TimerBase) return;
    if (!f) { f = fopen("PROGDIR:EdWord.log", "w"); ReadEClock(&e0); }
    if (!f) return;
    efreq = ReadEClock(&e);
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(f, "%8lu ms  %s\n", (unsigned long)((e.ev_lo - e0.ev_lo) / (efreq / 1000)), buf);
    fflush(f);
}
#endif

static int timer_init(void)
{
    tport = CreateMsgPort();
    if (!tport) return 0;
    treq = (struct timerequest *)CreateIORequest(tport, sizeof(struct timerequest));
    if (!treq) return 0;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)treq, 0)) return 0;
    topen = 1;
#ifdef EDLOG
    TimerBase = treq->tr_node.io_Device;
#endif
    timer_sig = 1UL << tport->mp_SigBit;
    return 1;
}

static void timer_stop(void)
{
    if (!tbusy) return;
    AbortIO((struct IORequest *)treq);
    WaitIO((struct IORequest *)treq);
    tbusy = 0;
}

void timer_after(long ms)
{
    if (!topen) return;
    timer_stop();
    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs = ms / 1000;
    treq->tr_time.tv_micro = (ms % 1000) * 1000;
    SendIO((struct IORequest *)treq);
    tbusy = 1;
}

int timer_pending(void) { return tbusy; }

int timer_due(void)
{
    if (!tbusy) return 0;
    if (!CheckIO((struct IORequest *)treq)) return 0;
    WaitIO((struct IORequest *)treq);
    tbusy = 0;
    return 1;
}

static void timer_free(void)
{
    timer_stop();
    if (topen) CloseDevice((struct IORequest *)treq);
    if (treq) DeleteIORequest((struct IORequest *)treq);
    if (tport) DeleteMsgPort(tport);
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

static void menu_pick(UWORD code)
{
    while (code != MENUNULL && !quit_now) {
        struct MenuItem *it = ItemAddress(strip, code);
        long c;
        if (!it) break;
        c = (long)GTMENUITEM_USERDATA(it);
        code = it->NextSelect;
        if (c > 0 && c < NCMD) do_cmd((int)c);
        if (!strip) break;
    }
    update_menu(top_doc());
}

ULONG upd_id;
LONG  upd_top;

/* every message is copied and replied to at once; the handlers get the copy */
static void dispatch(struct IntuiMessage *m)
{
    struct IntuiMessage cm = *m;
    struct Window *w = cm.IDCMPWindow;
    DOC *d;
    int k;
    if (m->Class == IDCMP_IDCMPUPDATE) {
        upd_id = GetTagData(GA_ID, 0, (struct TagItem *)m->IAddress);
        upd_top = (LONG)GetTagData(PGA_Top, (ULONG)-1L, (struct TagItem *)m->IAddress);
    }
    GT_ReplyIMsg(m);
    switch (cm.Class) {
    case IDCMP_MENUPICK:
        menu_pick(cm.Code);
        return;
    case IDCMP_VANILLAKEY:
    case IDCMP_RAWKEY:
        k = map_key(cm.Class, cm.Code, cm.Qualifier);
        if (k) handle_key(w, k, key_qual(cm.Qualifier));
        return;
    }
    if (tool_route(&cm)) return;
    d = doc_of_win(w);
    if (d) { win_msg(d, &cm); return; }
    if (cm.Class == IDCMP_REFRESHWINDOW) {
        GT_BeginRefresh(w);
        GT_EndRefresh(w, TRUE);
    }
}

/* deal with waiting messages (from inside long operations): refreshes only */
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

void pump_messages(void)
{
    struct IntuiMessage *m;
    while ((m = GT_GetIMsg(idcmp_port)) != NULL) {
        if (m->Class == IDCMP_REFRESHWINDOW && pump_defer && doc_of_win(m->IDCMPWindow)) {
            struct Window *w = m->IDCMPWindow;
            defer_on[doc_of_win(w)->slot] = 1;
            GT_ReplyIMsg(m);
            GT_BeginRefresh(w);
            GT_EndRefresh(w, TRUE);
        }
        else if (m->Class == IDCMP_REFRESHWINDOW) dispatch(m);
        else GT_ReplyIMsg(m);
    }
}

/* ================================================================= */
/*  The screen                                                        */
/* ================================================================= */

static const UWORD palette[16] = {
    0xAAA, 0x000, 0xFFF, 0x68B,  0xC00, 0x080, 0x00C, 0x088,
    0xA0A, 0xB80, 0x888, 0xFA0,  0x4F4, 0x222, 0xEEE, 0xDDD
};

/* the nearest of the screen's colours to a 12-bit one (no ObtainBestPen
 * before Workbench 3.0) */
static int nearest_pen(struct ColorMap *cm, int n, UWORD rgb)
{
    int i, best = 0;
    long bd = 0x7fffffffL;
    for (i = 0; i < n; i++) {
        UWORD c = (UWORD)GetRGB4(cm, i);
        long dr = ((c >> 8) & 15) - ((rgb >> 8) & 15), dg = ((c >> 4) & 15) - ((rgb >> 4) & 15), db = (c & 15) - (rgb & 15);
        long dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (dd < bd) { bd = dd; best = i; }
    }
    return best;
}

/* EdWord's 16 colours as pens of a screen that isn't ours */
static void map_pens(void)
{
    struct ColorMap *cm = scr->ViewPort.ColorMap;
    int i, n = 1 << depth;
    if (n > 256) n = 256;
    npens = (short)(n > 16 ? 16 : n);
    if (GfxBase->LibNode.lib_Version >= 39 && n >= 8) {
        for (i = 0; i < 16; i++) {
            ULONG r = (palette[i] >> 8) & 15, g = (palette[i] >> 4) & 15, b = palette[i] & 15;
            LONG p = ObtainBestPen(cm, r * 0x11111111UL, g * 0x11111111UL, b * 0x11111111UL,
                                   OBP_Precision, PRECISION_IMAGE, TAG_END);
            penmap[i] = (UBYTE)(p >= 0 ? p : nearest_pen(cm, n, palette[i]));
        }
        pens_obtained = 1;
        npens = 16;
    } else
        for (i = 0; i < 16; i++) penmap[i] = (UBYTE)(i < n && i < 4 ? i : nearest_pen(cm, n, palette[i]));
}

static void free_pens(void)
{
    int i;
    if (!pens_obtained) return;
    for (i = 0; i < 16; i++) ReleasePen(scr->ViewPort.ColorMap, penmap[i]);
    pens_obtained = 0;
}

static int open_screen(void)
{
    struct Screen *wb;
    ULONG mode = HIRES_KEY;
    UWORD pens[] = { (UWORD)~0 };
    long w = STDSCREENWIDTH, h = STDSCREENHEIGHT;
    struct TextAttr *wf;
    static char fname[40];
    int dpt, maxd = 4;
    DisplayInfoHandle dh;
    struct DisplayInfo di;

    wb = LockPubScreen((CONST_STRPTR)"Workbench");
    if (wb) {
        ULONG m = GetVPModeID(&wb->ViewPort);
        wf = wb->Font;
        if (wf && wf->ta_Name) {
            strncpy(fname, (const char *)wf->ta_Name, sizeof fname - 1);
            sysattr = *wf;
            sysattr.ta_Name = (STRPTR)fname;
        }
        if (pref_screen == SCR_WB) {
            /* keep it locked: the documents open on it */
            scr = wb;
            on_wb = 1;
        } else {
            if (pref_screen == SCR_LIKEWB && m != INVALID_ID) { mode = m; w = wb->Width; h = wb->Height; }
            UnlockPubScreen(NULL, wb);
        }
    }
    if (!sysattr.ta_Name) { sysattr.ta_Name = (STRPTR)"topaz.font"; sysattr.ta_YSize = 8; }
    if (on_wb) {
        depth = scr->RastPort.BitMap->Depth;
        map_pens();
    } else {
        if (pref_screen == SCR_LACE) mode = HIRESLACE_KEY;
        else if (pref_screen == SCR_OTHER && pref_modeid && pref_modeid != INVALID_ID && !ModeNotAvailable(pref_modeid)) {
            mode = pref_modeid;
            if (pref_scrw >= 640) w = pref_scrw;
            if (pref_scrh >= 200) h = pref_scrh;
            if (pref_scrdepth >= 2 && pref_scrdepth < 4) maxd = pref_scrdepth;
        }
        /* a non-interlaced screen keeps to a font that fits its lines */
        if (pref_screen != SCR_LACE && pref_screen != SCR_OTHER && mode == HIRES_KEY && sysattr.ta_YSize > 11) {
            sysattr.ta_Name = (STRPTR)"topaz.font";
            sysattr.ta_YSize = 8;
        }
        if (w != STDSCREENWIDTH && w < 640) w = 640;
        if (h != STDSCREENHEIGHT && h < 200) h = 200;
        for (dpt = maxd; dpt >= 2 && !scr; dpt--)
            scr = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, w, SA_Height, h, SA_Depth, dpt,
                                 SA_Title, (ULONG)sysname, SA_Pens, (ULONG)pens, SA_Font, (ULONG)&sysattr,
                                 SA_Type, CUSTOMSCREEN, SA_AutoScroll, TRUE, SA_FullPalette, TRUE, TAG_END);
        if (!scr)
            scr = OpenScreenTags(NULL, SA_DisplayID, HIRES_KEY, SA_Width, 640, SA_Height, STDSCREENHEIGHT, SA_Depth, 2,
                                 SA_Title, (ULONG)sysname, SA_Pens, (ULONG)pens, SA_Type, CUSTOMSCREEN, TAG_END);
        if (!scr) return 0;
        depth = scr->RastPort.BitMap->Depth;
        npens = (short)(1 << depth);
        if (npens > 16) npens = 16;
        LoadRGB4(&scr->ViewPort, (UWORD *)palette, npens);
    }
    scr_w = scr->Width;
    scr_h = scr->Height;

    /* the pixel shape: a hires pixel is taken as 0.35 mm wide */
    dev_pixw = 350;
    dev_pixh = 350;
    mode = GetVPModeID(&scr->ViewPort);
    dh = FindDisplayInfo(mode);
    if (dh && GetDisplayInfoData(dh, (UBYTE *)&di, sizeof di, DTAG_DISP, INVALID_ID) && di.Resolution.x > 0)
        dev_pixh = (short)(350L * di.Resolution.y / di.Resolution.x);
    else if (scr_h * 2 < scr_w)
        dev_pixh = 700;
    small_icons = dev_pixh >= dev_pixw * 3 / 2;
    return 1;
}

static int open_libs(void)
{
    static const struct { struct Library **base; const char *name; long ver; int need; } libs[] = {
#ifdef AMI13
        /* Workbench 1.2/1.3: ami13.c does what GadTools, asl and utility would */
        { (struct Library **)&IntuitionBase, "intuition.library", 33, 1 },
        { (struct Library **)&GfxBase, "graphics.library", 33, 1 },
        { &LayersBase, "layers.library", 33, 1 },
        { &DiskfontBase, "diskfont.library", 33, 0 },
#else
        { (struct Library **)&IntuitionBase, "intuition.library", 37, 1 },
        { (struct Library **)&GfxBase, "graphics.library", 37, 1 },
        { &GadToolsBase, "gadtools.library", 37, 1 },
        { &LayersBase, "layers.library", 37, 1 },
        { (struct Library **)&UtilityBase, "utility.library", 37, 1 },
        { &DiskfontBase, "diskfont.library", 36, 0 },
        { &AslBase, "asl.library", 37, 0 },
#endif
        { (struct Library **)&LocaleBase, "locale.library", 38, 0 },
        { NULL, NULL, 0, 0 } };
    int i;
    for (i = 0; libs[i].base; i++) {
        *libs[i].base = OpenLibrary((CONST_STRPTR)libs[i].name, libs[i].ver);
        if (!*libs[i].base && libs[i].need) {
            if (IntuitionBase) {
                char t[120];
                struct EasyStruct es;
                sprintf(t, L(AL_AMI_NEED_LIB), libs[i].name, libs[i].ver);
                { char *p; for (p = t; *p; p++) if (*p == '|') *p = '\n'; }
                es.es_StructSize = sizeof es;
                es.es_Flags = 0;
                es.es_Title = (UBYTE *)"EdWord";
                es.es_TextFormat = (UBYTE *)t;
                es.es_GadgetFormat = (UBYTE *)L(AB_OK);
                EasyRequestArgs(NULL, &es, NULL, NULL);
            }
            return 0;
        }
    }
    return 1;
}

static void close_libs(void)
{
    if (LocaleBase) CloseLibrary((struct Library *)LocaleBase);
#ifdef AMI13
    AslBase = NULL;
#endif
    if (AslBase) CloseLibrary(AslBase);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (UtilityBase) CloseLibrary((struct Library *)UtilityBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
}

/* a file from Workbench (a project icon, or one dropped on EdWord's) */
static void open_wbarg(struct WBArg *a)
{
    char path[300];
    if (!a->wa_Lock || !a->wa_Name || !a->wa_Name[0]) return;
    if (!NameFromLock(a->wa_Lock, (STRPTR)path, sizeof path)) return;
    if (!AddPart((STRPTR)path, a->wa_Name, sizeof path)) return;
    open_path(path, text_name(path));
}

int main(int argc, char *argv[])
{
    struct IntuiMessage *m;
    int rc = 20;

    sprintf(sysname, "EdWord %s", ED_VERSION);
    if (!open_libs()) { close_libs(); return 20; }
#ifdef AMI13
    c13_init();
    AslBase = &c13_asl;
    c13_progdir(progdir, sizeof progdir, argc == 0 ? _WBenchMsg : NULL);
#endif
    load_prefs();
    set_language(pref_lang >= 0 ? pref_lang : system_language());
    spell_lang = pref_spell >= 0 ? pref_spell : ui_lang;
    /* the screen has few colours: pictures keep theirs (for PDFs) only with
     * plenty of memory to spare */
    {
        long fr = (long)AvailMem(MEMF_ANY | MEMF_LARGEST);
        img_rgb_max = fr > 8000000L ? 4000000L : fr > 3000000L ? fr / 8 : 0;
    }
    idcmp_port = CreateMsgPort();
    if (!idcmp_port || !timer_init()) goto out;
    if (!open_screen()) {
        struct EasyStruct es;
        char t[160], *p;
        strcpy(t, L(AL_AMI_NO_SCREEN));
        for (p = t; *p; p++) if (*p == '|') *p = '\n';
        es.es_StructSize = sizeof es; es.es_Flags = 0; es.es_Title = (UBYTE *)"EdWord";
        es.es_TextFormat = (UBYTE *)t; es.es_GadgetFormat = (UBYTE *)L(AB_OK);
        EasyRequestArgs(NULL, &es, NULL, NULL);
        goto out;
    }
    dri = GetScreenDrawInfo(scr);
    vi = GetVisualInfo(scr, TAG_END);
    sysfont = OpenFont(&sysattr);
    if (!sysfont) sysfont = OpenFont(scr->Font);
    {
        struct TextAttr fa;
        fa.ta_Name = (STRPTR)GfxBase->DefaultFont->tf_Message.mn_Node.ln_Name;
        fa.ta_YSize = GfxBase->DefaultFont->tf_YSize;
        fa.ta_Style = 0; fa.ta_Flags = 0;
        fixfont = OpenFont(&fa);
    }
    if (!vi || !sysfont || !fixfont) goto out;
    hchar = sysfont->tf_YSize;
    sys_asc = sysfont->tf_Baseline;
    desk.g_x = 0;
    desk.g_y = (short)(scr->BarHeight + 1);
    desk.g_w = scr_w;
    desk.g_h = (short)(scr_h - desk.g_y);
    gfx_init();
    wchar = (short)((sys_width("abcdefghijklmnopqrstuvwxyz ABCDEFGHIJ0123456789") + 23) / 47);
    if (wchar < 4) wchar = 8;

    busy(1);
    fonts_load();
    if (!make_strip()) goto out;
    if (!on_wb) backdrop = open_ewin((struct TagItem *)(ULONG[]){ WA_Left, 0, WA_Top, (ULONG)desk.g_y, WA_Width, (ULONG)scr_w,
                         WA_Height, (ULONG)desk.g_h, WA_Backdrop, TRUE, WA_Borderless, TRUE,
                         WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE, WA_Activate, TRUE, TAG_END },
                         IDCMP_MENUPICK | IDCMP_VANILLAKEY | IDCMP_RAWKEY);
    busy(0);
    if (!backdrop && !on_wb) goto out;
    update_menu(NULL);

    if (argc == 0 && _WBenchMsg) {
        int i;
        for (i = 1; i < _WBenchMsg->sm_NumArgs; i++) open_wbarg(&_WBenchMsg->sm_ArgList[i]);
    } else if (argc > 1) {
        int i;
        for (i = 1; i < argc; i++) open_path(argv[i], text_name(argv[i]));
    }
    if (pref_live) spell_ensure(1);
    if (!top_doc()) cmd_new();

    rc = 0;
    while (!quit_now) {
        ULONG sig;
        int input = 0;
        sig = Wait((1UL << idcmp_port->mp_SigBit) | timer_sig | SIGBREAKF_CTRL_C);
        if (sig & SIGBREAKF_CTRL_C) cmd_quit();
        while (!quit_now && (m = GT_GetIMsg(idcmp_port)) != NULL) {
            if (m->Class != IDCMP_INTUITICKS) input = 1;
            dispatch(m);
        }
        if (quit_now) break;
        if (timer_due()) idle_work(1);
        autosave_tick();
        /* idle work runs once the user has paused for 0.4 seconds */
        if (input || !timer_pending()) {
            if (idle_work(0)) timer_after(400);
            else if (pref_autosave) timer_after(5000);
        }
    }

out:
    tools_close_all();
    {
        int i;
        for (i = 0; i < MAX_DOCS; i++)
            if (DOC_USED(i)) { close_window(docs[i]); doc_free(docs[i]); docs[i]->used = 0; }
    }
    if (backdrop) close_ewin(backdrop);
    if (strip) FreeMenus(strip);
    spell_free_all();
    thes_close();
    doc_clip_free();
    img_cache_free(NULL);
    fonts_unload();
    gfx_free();
    if (fixfont) CloseFont(fixfont);
    if (sysfont) CloseFont(sysfont);
    if (vi) FreeVisualInfo(vi);
    if (dri) FreeScreenDrawInfo(scr, dri);
    if (scr && on_wb) { free_pens(); UnlockPubScreen(NULL, scr); }
    else if (scr) CloseScreen(scr);
    timer_free();
    if (idcmp_port) DeleteMsgPort(idcmp_port);
    close_libs();
    return rc;
}
