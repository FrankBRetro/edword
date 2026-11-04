/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * winui.h - shared state of the EdWord Windows front end (Windows 95 to 11)
 *
 * The front end follows the Amiga one (amiga/), which follows the GEM
 * one: the same commands, dialogs, tool windows and drawing, done with
 * the plain Win32 API and GDI. A frame window holds the menus and the
 * document windows (MDI); the tool windows float above it. Window
 * messages are turned into events in the shape the Amiga code uses
 * (EVT), so the editing code is the same.
 *
 * Character sets: everything in a DOC (text, names) is in the Atari ST
 * character set, as on the ST; the user interface strings are
 * Windows-1252. lat() and stc() convert at the edges.
 */
#ifndef WINUI_H
#define WINUI_H

#include <windows.h>
#include "../wp.h"
#include "lang.h"

#ifndef ED_VERSION
#define ED_VERSION "3.0"
#endif
#ifndef ED_ABOUT
#define ED_ABOUT ""
#endif

typedef HWND WIN;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

/* a rectangle, as in GEM, in window pixels */
typedef struct { short g_x, g_y, g_w, g_h; } GRECT;

#define PGAP    12          /* gap around and between pages in the view, pixels */
#define DIVH    4           /* split view divider height */

/* menu commands (the same numbers as the GEM version) */
enum {
    C_NONE, C_ABOUT,
    C_NEW, C_OPEN, C_IMPORT, C_SAVE, C_SAVEAS, C_EXPTXT, C_EXPMD, C_EXPFWP, C_EXPPT, C_EXPORT, C_PAGE, C_HEADFOOT,
    C_PREVIEW, C_PRINT, C_PDF, C_CLOSE, C_QUIT,
    C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE, C_SELALL, C_FIND, C_FINDNEXT,
    C_PREVPAGE, C_NEXTPAGE, C_GOTOPAGE, C_REPAG,
    C_INSDATE, C_INSTIME, C_PGBRK, C_COLBRK, C_PAGENUM, C_TBLINS, C_TBLPROP, C_ROWINS, C_ROWDEL, C_COLINS,
    C_COLDEL, C_TBLDEL, C_CHART, C_PICTURE, C_DRAWING, C_TOC, C_INSFILE,
    C_FONTSEL, C_SIZEOTHER, C_MOREFONTS,
    C_PLAIN, C_BOLD, C_ITALIC, C_UNDER,
    C_JLEFT, C_JCENTRE, C_JRIGHT, C_JFULL, C_SP1, C_SP15, C_SP2, C_INDENTS, C_HANGING,
    C_BULLETS, C_NUMBERS, C_HEAD1, C_HEAD2, C_HEAD3, C_BODY, C_WRAPCOL,
    C_SPELL, C_LIVESPELL, C_THES, C_SUMMARY, C_UDLOAD, C_UDNEW, C_PREFS,
    C_VDOC, C_SPLIT, C_VCODES, C_ZOOMIN, C_ZOOMOUT, C_TOOLBAR, C_RULERS, C_GUIDES, C_TILEV, C_TILEH,
    C_CASCADE, C_ICONIFY, C_ICONALL, C_BOTTOM, C_CYCLE, C_WINMORE,
    C_OBJPROP, C_OBJDEL, C_OBJEDIT,
    C_LAST,
    C_FONT0 = 100, C_SIZE0 = 200, C_COLOR0 = 250, C_UD0 = 260, C_WIN0 = 270
};
#define NCMD (C_WIN0 + MAX_DOCS)

/* ---- events: window messages in the shape the Amiga code uses ---------- */
enum {
    IDCMP_CLOSEWINDOW = 1, IDCMP_NEWSIZE, IDCMP_REFRESHWINDOW, IDCMP_MOUSEBUTTONS, IDCMP_MOUSEMOVE,
    IDCMP_MENUPICK, IDCMP_ACTIVEWINDOW, IDCMP_INACTIVEWINDOW, IDCMP_GADGETUP, IDCMP_IDCMPUPDATE,
    IDCMP_INTUITICKS, IDCMP_CHANGEWINDOW
};
#define SELECTDOWN 0x68
#define SELECTUP   0xE8
#define MENUDOWN   0x69
typedef struct {
    ULONG Class;
    UWORD Code;
    UWORD Qualifier;                /* KQ_* */
    void *IAddress;                 /* gadget events: the item number (as a pointer) */
    short MouseX, MouseY;           /* window client coordinates */
    ULONG Seconds, Micros;          /* the message time */
    WIN   IDCMPWindow;
} EVT;

/* ---- the display ---------------------------------------------------------- */
extern HINSTANCE hinst;
extern WIN   frame, mdiclient;
extern HFONT uifont;                      /* the dialog and toolbar font      */
extern HFONT fixfont;                     /* fixed width, for the codes pane  */
extern short wchar, hchar, sys_asc, scr_w, scr_h, npens, small_icons;
extern GRECT desk;                        /* the frame's client area           */
extern int   colour_on, pdf_scale, quit_now;
extern DOC  *docs[MAX_DOCS];
#define DOC_USED(i) (docs[i] != NULL && docs[i]->used)
extern char  progdir[MAX_PATH], datadir[MAX_PATH];    /* the program's folder; where its settings go */
extern const short sizes[];
extern int   pref_date, pref_toolbar, pref_paper, pref_land;
extern int   pref_autosave, pref_asmin, pref_asmode, pref_display, pref_live, pref_rulers, pref_guides;
enum { DISP_NORMAL, DISP_DARK, DISP_GREEN, DISP_AMBER };
#define NSIZES 8
#define NZOOM  6
extern const short zooms[NZOOM];

/* EdWord's sixteen colours (pens), as on the Amiga */
enum { P_GREY, P_BLACK, P_WHITE, P_BLUE, P_RED, P_GREEN, P_DBLUE, P_CYAN, P_MAGENTA, P_YELLOW,
       P_TGREY, P_AMBER, P_PHOSPHOR, P_SHADOW, P_WHITE2, P_LIGHT };
#define P_DESK P_TGREY
extern COLORREF pal[16];
#define PEN(p) (pal[(p) & 15])

/* dialog item flags, as in GEM */
#define OF_EXIT     0x0004
#define OF_DEFAULT  0x0002

/* winmain.c */
DOC  *doc_of_win(WIN w);
DOC  *top_doc(void);
DOC  *active_doc(void);
void  do_cmd(int cmd);
void  update_menu(DOC *d);
void  update_winmenu(void);
void  rebuild_menu(void);
void  apply_style(DOC *d, int what, int val);
void  apply_para(DOC *d, int field, int val);
int   close_doc(DOC *d);
void  save_prefs(void);
void  cmd_pdf(DOC *d);
void  cmd_print(DOC *d);
void  set_language(int lang);
void  doc_loaded(DOC *d);
void  open_path(const char *path, int text);
int   dlg_chart(int *kind, int *pos);
void  chart_menu(DOC *d, int tbl);
int   read_into(const char *path, int type, int cs, int join, DOC *into, int ask);
int   export_doc(DOC *d, int fmt, int crlf, int cs, char *written);
int   insert_loaded(DOC *d, DOC *t);
void  import_open(void);
void  export_open(void);
void  xfer_refresh(void);
void  handle_key(WIN w, int key, int qual);
void  pump_messages(void);
void  pump_flush(void);
extern int pump_defer;
void  window_closing(WIN w);
void  activate_doc(DOC *d);
void  block_input(int on);
int   menu_key(int key, int qual);          /* a Ctrl shortcut: 1 when a command took it */
void  frame_enable(int on);
int   block_now(void);                     /* a dialog is open: the documents take no input */

/* keys, as handle_key and the windows get them: a Windows-1252 character, or one of these */
enum { K_LEFT = 0x100, K_RIGHT, K_UP, K_DOWN, K_RETURN, K_ENTER, K_TAB, K_BS, K_DEL, K_ESC, K_HELP,
       K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10,
       K_HOME, K_END, K_PGUP, K_PGDN, K_INS };
#define KQ_SHIFT 1
#define KQ_ALT   2
#define KQ_CTRL  4
#define KQ_AMIGA 8
int   key_from_vk(WPARAM vk);               /* a WM_KEYDOWN key we handle, or 0 */
int   key_qual(void);                       /* the shift keys held now          */

/* tool windows: each module answers for its own windows */
typedef struct {
    int  (*owns)(WIN w);
    void (*msg)(EVT *m);                    /* everything but keys */
    void (*key)(int key, int qual);
    void (*close)(void);
    void (*doc_closed)(DOC *d);
} TOOL;
extern const TOOL import_tool, export_tool;
int   tool_route(EVT *m);
int   tool_route_key(WIN w, int key, int qual);
int   tool_owns(WIN w);
extern const TOOL *const tools[];

/* winwin.c */
#define TBH (24)
void  work_area(DOC *d, GRECT *g);
void  get_panes(DOC *d, GRECT *tb, GRECT *tp, GRECT *cp);
void  redraw_area(DOC *d, GRECT *area);
void  refresh_all(DOC *d);
void  refresh_text(DOC *d);
void  refresh_codes(DOC *d);
void  refresh_toolbar(DOC *d);
void  set_sliders(DOC *d);
void  set_name(DOC *d);
void  set_info(DOC *d);
void  clamp_scroll(DOC *d);
void  after_edit(DOC *d, long from);
void  after_typing(DOC *d, long from);
void  after_move(DOC *d, long oc, long oa);
void  doc_changed(DOC *d);
void  apply_codes(DOC *d);
void  rebuild_codes(DOC *d);
void  set_view(DOC *d, int view);
void  set_toolbar(DOC *d, int on);
void  set_zoom(DOC *d, int zoom);
int   open_window(DOC *d);
void  close_window(DOC *d);
void  iconify(DOC *d);
void  uniconify(DOC *d);
void  send_to_back(DOC *d);
void  tile_windows(int how);
void  win_key(DOC *d, int key, int qual);
void  win_msg(DOC *d, EVT *m);
void  win_resized(DOC *d);
void  select_range(DOC *d, long a, long b);
void  codes_clip(DOC *d, int cmd);
int   codes_focus(DOC *d);
int   codes_insert_code(DOC *d, const char *code);
int   ed_bg(void);
int   ed_fg(void);
long  view_page_top(DOC *d, int page);
long  view_page_left(DOC *d, GRECT *tp);
int   view_to_page(DOC *d, GRECT *tp, short mx, short my, long *px, long *py);
long  zx(DOC *d, long v);
void  ensure_caret(DOC *d);
void  goto_page(DOC *d, int page);
WIN   doc_win(DOC *d);
void  doc_block(int on);
LRESULT CALLBACK doc_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
void  doc_register(void);

/* wingfx.c: drawing helpers and the mouse */
void  gfx_begin(WIN w);                    /* following calls draw into w        */
void  gfx_dc(HDC dc, int ox, int oy, int w, int h);  /* ... or into a device context */
void  gfx_end(void);
extern HDC   gdc;                          /* the device context being drawn into */
extern WIN   gwin;
void  clip_set(const GRECT *r);            /* clip drawing to r (window coords)  */
void  clip_off(void);
int   rc_inter(const GRECT *a, GRECT *b);
int   in_rect(const GRECT *r, short x, short y);
void  fill_rect(long x1, long y1, long x2, long y2, int pen);   /* inclusive */
void  fill_pat(long x1, long y1, long x2, long y2, int pen, int grey);
void  fill_g(const GRECT *r, int pen);
void  frame_rect(long x1, long y1, long x2, long y2, int pen);
void  xor_rect(long x1, long y1, long x2, long y2);
void  xor_frame(long x1, long y1, long x2, long y2);
void  xor_line(long x1, long y1, long x2, long y2);
void  hline(long x1, long x2, long y, int pen, int dotted);
void  vline_dotted(long x, long y1, long y2, int pen);
void  line(long x1, long y1, long x2, long y2, int pen);
void  thick_line(long x1, long y1, long x2, long y2, int pen, int w);
void  poly_line(const long *xy, int n, int pen, int w);
void  poly_fill(const long *xy, int n, int pen, int grey);
void  disc(long cx, long cy, long r, int pen);
void  sys_text(long x, long top, const char *s, int pen);       /* Windows-1252, UI font */
void  sys_text_n(long x, long top, const char *s, long n, int pen);
void  fix_text(long x, long top, const char *s, long n, int pen); /* codes pane font */
long  sys_width(const char *s);
void  bevel(const GRECT *r, int recessed);
void  button_box(const GRECT *r, const char *label, int sel, int disabled);
void  busy(int on);
int   icon_h(void);
void  draw_tbicon(int ic, long x, long y, int pen);
void  blit_bits(const unsigned char *bits, long rb, long w, long h, long x, long y, int pen);
void  blit_rgb(const unsigned char *rgb, long iw, long ih, long x, long y, long w, long h);
/* tracking the mouse while a button is held (from a SELECTDOWN) */
void  track_begin(WIN w);
int   track_next(short *mx, short *my, int *qual);   /* 0 when the button is released */
void  track_end(void);
int   long_press(WIN w, short mx, short my);
int   double_click(ULONG secs, ULONG micros);
void  gfx_init(void);
void  gfx_free(void);
enum { IC_BOLD, IC_ITALIC, IC_UNDER, IC_LEFT, IC_CENTRE, IC_RIGHT, IC_JUSTIFY,
       IC_FONT, IC_SIZE, IC_COLOUR, IC_SPELL, IC_CODES, IC_BULLETS, IC_NUMBERS,
       IC_UNDO, IC_REDO, IC_ZOOM, IC_TABLE, IC_RULER, IC_GUIDES, IC_PGBRK, IC_COUNT };

/* character sets */
const char *lat(const char *st);           /* ST -> Windows-1252, into a ring of buffers */
void  lat_n(char *out, const char *st, long n);
void  stc(char *out, const char *win, int max);    /* Windows-1252 -> ST */
unsigned char lat_ch(unsigned char st);
unsigned char st_ch(unsigned char win);

/* winrend.c */
void  fonts_load(void);
void  fonts_unload(void);
HFONT font_for(int font, int size_px, int attr);
int   color_pen(int c);
void  draw_page(DOC *d, int page, long ox, long oy, long n, long den, const GRECT *clip, int pen, int live);
void  draw_preview_page(DOC *d, int page, long ox, long oy, long n, long den);
int   export_pdf(DOC *d, const char *path, void (*gauge)(long done, long total));
int   printer_name(char *out);
int   print_doc(DOC *d, const unsigned char *sel, int copies);   /* sel: a flag per page, NULL all */
int   print_choose(DOC *d, int *from, int *to, int *copies);   /* the Print dialog */
int   word_misspelt(DOC *d, long a, long b);
void  img_cache_free(DOC *d);
void  text_at(long x, long base, const char *st, int n, int font, int size_px, int attr, int pen);

/* windlg.c: a dialog builder on Windows controls that lays itself out */
#define MAXDITEM 120
typedef struct {
    short type, group, flags, wc;
    short row, col;                 /* layout: row, x in pixels                     */
    short gx, gy, gw;               /* control position and width (pixels)          */
    short lead, nopt;               /* radio: first item of its group; options      */
    short hidden, disabled;
    int   on;                       /* check: state; radio leader: chosen option    */
    const char *s, *tmpl, *valid;
    char *buf;
    HWND  gad, gad2, gad3;          /* the control, and edit fields' labels          */
    char  label[40], after[40];     /* edit: text before and after the field        */
    short n;                        /* edit: characters                             */
} DITEM;
typedef struct DLG {
    DITEM it[MAXDITEM];
    short nit, row, nrows, framed, edit1, def;
    short w, h;                     /* inner size, pixels                           */
    const char *title;
    WIN   win;
    short lastitem;
    ULONG lsecs, lmics;
    int   modal, done, result;      /* dlg_run */
    int   tool;                     /* a tool window: events go to tool_route       */
} DLG;
void  dlg_begin(DLG *g, const char *title);
void  dlg_row(DLG *g);
void  dlg_label(DLG *g, const char *s);
short dlg_text(DLG *g, const char *s);
short dlg_radio(DLG *g, int group, const char *s, int on);
short dlg_check(DLG *g, const char *s, int on);
short dlg_edit(DLG *g, const char *tmpl, char *buf, const char *valid);
short dlg_button(DLG *g, const char *s, int flags);
short dlg_bigbutton(DLG *g, const char *s, int wc, int flags);
void  dlg_end(DLG *g);
short dlg_run(DLG *g);
int   dlg_on(DLG *g, short item);
void  dlg_show(DLG *g, short item, int show);
void  dlg_disable(DLG *g, short item, int dis);
int   dlg_gadgets(DLG *g);              /* (re)make the controls in g->win           */
void  dlg_fetch(DLG *g);                /* copy edit fields and states back           */
void  dlg_free_gadgets(DLG *g);
int   dlg_event(DLG *g, EVT *m, short *item, int *clicks);   /* 1: item chosen */
void  dlg_rebuild(DLG *g);              /* after texts or visibility changed          */
void  dlg_settext(DLG *g, short item, const char *s);
WIN   dlg_window(DLG *g, const char *title, long x, long y);   /* a tool window */
void  dlg_close(DLG *g);
int   popup(const char **items, int n, int cur, short x, short y);  /* screen coords */
int   ask_file(const char *title, const char *pattern, const char *defname, char *out, int save);
int   alert(int def, int icon, int text, int buttons, ...);
int   dlg_page(DOC *d);
int   dlg_wrapcol(DOC *d);
int   dlg_size(int *size);
int   dlg_textimport(int eol, int det, int *cs, int *join);
int   dlg_textexport(int *crlf, int *cs);
int   dlg_prefs(void);
int   dlg_headfoot(DOC *d);
int   dlg_pagenum(DOC *d);
int   dlg_indents(DOC *d, int *left, int *first);
int   dlg_table(DOC *d, int insert, int *rows, int *cols, short *w, int *border);
int   dlg_objprops(DOC *d, OBJ *o);
void  dlg_summary(DOC *d);
void  dlg_about(void);
int   dlg_goto(DOC *d, int *page);
int   dlg_print(DOC *d, const char *printer, unsigned char *sel, int *copies);
void  gauge_open(const char *title);
void  gauge_set(long done, long total);
void  gauge_close(void);
void  busy_begin(const char *title);   /* a long job: a gauge if it takes a while */
void  busy_end(void);
void  dlg_register(void);

/* wintwin.c: find and replace, spelling, thesaurus windows */
extern const TOOL find_tool, spell_tool, thes_tool;
void  find_open(DOC *d);
void  find_again(DOC *d);
void  spell_run(DOC *d);
void  spell_close(void);
DOC  *spell_doc(void);
void  thes_run(DOC *d);
void  live_popup(DOC *d, long a, long b, short mx, short my);
int   spell_ensure(int quiet);

/* wintool.c: the font selector and print preview windows */
extern const TOOL fontsel_tool, preview_tool;
void  fontsel_open(void);
void  fontsel_sync(void);
void  preview_open(DOC *d);
void  preview_changed(DOC *d);
int   preview_idle(int run);

/* winobj.c: pictures and drawings in the document view */
extern const TOOL draw_tool;
int   obj_at(DOC *d, int page, long px, long py);
int   obj_click(DOC *d, GRECT *tp, short mx, short my, int qual, int clicks);
void  obj_select(DOC *d, int i);
void  obj_draw_handles(DOC *d, GRECT *tp);
int   obj_key(DOC *d, int key, int qual);
void  cmd_picture(DOC *d);
int   picture_insert(DOC *d, const char *path);
int   picture_insert_img(DOC *d, IMG *im, const char *name);
void  cmd_drawing(DOC *d);
void  obj_props(DOC *d);
void  obj_delete(DOC *d);
void  draw_edit(DOC *d, int i);
void  draw_close_for(DOC *d, int i);
void  draw_obj(DOC *d, const OBJ *o, long ox, long oy, long n, long den);

/* winclip.c */
int   clip_copy(DOC *d);
int   clip_paste(DOC *d);
int   clip_put_text(const char *s, long n);
char *clip_get_text(long *n);
int   insert_file(DOC *d, const char *path);
void  autosave_tick(void);
void  autosave_reset(void);

/* the timer, for idle work */
void  timer_after(long ms);
int   timer_due(void);
int   timer_pending(void);
extern ULONG upd_id;                       /* GA_ID and PGA_Top of the last scroll event */
extern long  upd_top;

#define WM_EDCLOSE (WM_USER + 1)           /* a tool window's close box */

#ifdef EDLOG
void edlog(const char *fmt, ...);
#else
#define edlog(...) ((void)0)
#endif

#endif
