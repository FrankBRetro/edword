/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemui.h - shared state of the EdWord GEM front end
 */
#ifndef GEMUI_H
#define GEMUI_H

#include <gem.h>
#include "wp.h"
#include "lang.h"

#ifndef ED_VERSION
#define ED_VERSION "3.0.11"   /* one more on every commit */
#endif
#ifndef ED_ABOUT
#define ED_ABOUT ""
#endif

#define WKIND  (NAME|CLOSER|FULLER|MOVER|SIZER|INFO|UPARROW|DNARROW|VSLIDE|LFARROW|RTARROW|HSLIDE)
#define PGAP    12          /* gap around and between pages in the view, pixels */
#define DIVH    4           /* split view divider height */

/* menu commands */
enum {
    C_NONE, C_ABOUT,
    /* File */
    C_NEW, C_OPEN, C_IMPORT, C_SAVE, C_SAVEAS, C_EXPTXT, C_EXPMD, C_EXPFWP, C_EXPPT, C_EXPORT, C_PAGE, C_HEADFOOT,
    C_PREVIEW, C_PRINT, C_PDF, C_CLOSE, C_QUIT,
    /* Edit */
    C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE, C_SELALL, C_FIND, C_FINDNEXT,
    C_PREVPAGE, C_NEXTPAGE, C_GOTOPAGE, C_REPAG,
    /* Insert */
    C_INSDATE, C_INSTIME, C_PGBRK, C_COLBRK, C_PAGENUM, C_TBLINS, C_TBLPROP, C_ROWINS, C_ROWDEL, C_COLINS,
    C_COLDEL, C_TBLDEL, C_CHART, C_PICTURE, C_DRAWING, C_TOC, C_INSFILE,
    /* Font */
    C_FONTSEL, C_SIZEOTHER, C_MOREFONTS,
    /* Style */
    C_PLAIN, C_BOLD, C_ITALIC, C_UNDER,
    /* Para */
    C_JLEFT, C_JCENTRE, C_JRIGHT, C_JFULL, C_SP1, C_SP15, C_SP2, C_INDENTS, C_HANGING,
    C_BULLETS, C_NUMBERS, C_HEAD1, C_HEAD2, C_HEAD3, C_BODY, C_WRAPCOL,
    /* Tools */
    C_SPELL, C_LIVESPELL, C_THES, C_SUMMARY, C_UDLOAD, C_UDNEW, C_PREFS,
    /* Window */
    C_VDOC, C_SPLIT, C_VCODES, C_ZOOMIN, C_ZOOMOUT, C_ZOOMFIT, C_TOOLBAR, C_RULERS, C_GUIDES, C_TILEV, C_TILEH,
    C_CASCADE, C_ICONIFY, C_ICONALL, C_BOTTOM, C_CYCLE, C_WINMORE,
    /* pictures and drawings */
    C_OBJPROP, C_OBJDEL, C_OBJEDIT,
    C_TEMPLATE,
    C_LAST,
    C_FONT0 = 100, C_SIZE0 = 200, C_COLOR0 = 250, C_UD0 = 260, C_WIN0 = 270
};
#define NCMD (C_WIN0 + MAX_DOCS)


extern short vhandle, phys_handle, wchar, hchar, scr_w, scr_h, npens, sys_asc;
extern GRECT desk;
extern int   has_gdos, gdos_fsm, colour_on, pdf_scale;
extern int   native_iconify, native_bottom, quit_now;
extern DOC  *docs[MAX_DOCS];
#define DOC_USED(i) (docs[i] != NULL && docs[i]->used)
extern char  progdir[256];
extern const short sizes[];
extern int   pref_date, pref_toolbar, pref_paper, pref_land;
extern int   pref_autosave, pref_asmin, pref_asmode, pref_display, pref_live;
extern char  pref_docdir[128], tpl_name[48], tpl_addr[3][48], tpl_title[48];
void  close_pristine(DOC *keep);
int   dlg_template(int *kind);
extern int   pref_rulers, pref_guides;
enum { DISP_NORMAL, DISP_DARK, DISP_GREEN, DISP_AMBER };
#define NSIZES 8
#define NZOOM  6
extern const short zooms[NZOOM];

/* gem.c */
DOC  *doc_of(short wh);
DOC  *top_doc(void);
DOC  *active_doc(void);
void  do_cmd(int cmd);
void  update_menu(DOC *d);
void  update_winmenu(void);
void  rebuild_menu(void);
void  apply_style(DOC *d, int what, int val);
void  apply_para(DOC *d, int field, int val);
int   close_doc(DOC *d);
extern int safe_mode, no_arbpt;
void  save_prefs(void);
void  cmd_pdf(DOC *d);
int   read_into(const char *path, int type, int cs, int join, DOC *into, int ask);
int   export_doc(DOC *d, int fmt, int crlf, int cs, char *written);
void  cmd_print(DOC *d);
void  set_language(int lang);
void  pump_redraws(void);
void  pump_flush(void);
extern int pump_defer;
void  handle_msg(short *msg);
int   font_protocol_select(void);
void  doc_loaded(DOC *d);
void  open_path(const char *path, int text);
void  handle_key(short ks, short key);
void  handle_click(short mx, short my, short ks, short clicks);
OBJECT *menu_tree(void);
short menu_title_obj(int i);
short menu_box_obj(int i);
int   cmd_by_name(const char *s);

/* a few facts to DIAG.TXT, only with "make EDDIAG=1" (no new variables:
 * the program's memory is laid out as in the normal build) */
#ifdef EDDIAG
void  eddiag(const char *fmt, ...);
#else
#define eddiag(...) ((void)0)
#endif

/* start-up log, only with "make EDLOG=1" */
#ifdef EDLOG
void  edlog(const char *fmt, ...);
void  edlog_more(void);
#else
#define edlog(...) ((void)0)
#endif

/* tool windows: each module answers for its own windows */
typedef struct {
    int  (*owns)(short wh);
    void (*msg)(short *msg);
    void (*click)(short mx, short my, short clicks);
    void (*key)(short ks, short key);
    void (*close)(void);
    void (*doc_closed)(DOC *d);
} TOOL;
int   tool_route_msg(short *msg);
int   tool_route_click(short wh, short mx, short my, short clicks);
int   tool_route_key(short wh, short ks, short key);
int   tool_owns(short wh);

/* gemwin.c */
#define TBH (hchar >= 16 ? 24 : 15)
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
void  set_fit_width(DOC *d, int on);   /* zoom so the page fills the window's width */
#define ZOOM_MIN 25
#define ZOOM_MAX 400
int   open_window(DOC *d);
void  close_window(DOC *d);
void  iconify(DOC *d, short *rect);
void  uniconify(DOC *d, short *rect);
void  send_to_back(DOC *d);
void  tile_windows(int how);
void  win_key(DOC *d, short ks, short key);
void  win_click(DOC *d, short mx, short my, short ks, short clicks);
void  win_msg(DOC *d, short *msg);
void  win_resized(DOC *d);
void  select_range(DOC *d, long a, long b);
void  codes_clip(DOC *d, int cmd);
int   codes_focus(DOC *d);
int   codes_insert_code(DOC *d, const char *code);
int   ed_bg(void);
int   ed_fg(void);
void  display_apply(void);
void  display_restore(void);
long  view_page_top(DOC *d, int page);
long  view_page_left(DOC *d, GRECT *tp);
int   view_to_page(DOC *d, GRECT *tp, short mx, short my, long *px, long *py);
long  zx(DOC *d, long v);
void  ensure_caret(DOC *d);
void  goto_page(DOC *d, int page);

/* gemclip.c */
int   clip_copy(DOC *d);
int   clip_paste(DOC *d);
int   clip_put_text(const char *s, long n);
char *clip_get_text(long *n);
int   insert_file(DOC *d, const char *path);
int   insert_loaded(DOC *d, DOC *t);
int   tc_init(void);                 /* gemobj.c: full colour pictures */
int   pal_init(void);                /* gemobj.c: colour pictures on 4-256 pens */
void  autosave_tick(void);
void  autosave_reset(void);
void  autosave_force(void);

/* gemicon.c */
enum { IC_BOLD, IC_ITALIC, IC_UNDER, IC_LEFT, IC_CENTRE, IC_RIGHT, IC_JUSTIFY,
       IC_FONT, IC_SIZE, IC_COLOUR, IC_SPELL, IC_CODES, IC_BULLETS, IC_NUMBERS,
       IC_UNDO, IC_REDO, IC_ZOOM, IC_TABLE, IC_RULER, IC_GUIDES, IC_PGBRK, IC_COUNT };
void  icons_init(void);
int   icon_h(void);
void  draw_tbicon(int ic, short x, short y);
short tb_item_x(DOC *d, int cmd);

/* gemrsc.c: the resource files */
int   rsc_load(int lang, int force);
void  rsc_free(void);
OBJECT *rsc_tree(int tree);
short rsc_title(int tree);
const char *rsc_entry(int tree, short obj);

/* gemrend.c */
void  fonts_load(void);
void  fonts_unload(void);
void  set_text_style(int font, int size, int attr, int color);
void  set_sys_font(void);
void  style_reset(void);
int   color_pen(int c);
void  draw_page(DOC *d, int page, long ox, long oy, long n, long den, const GRECT *clip, int pen, int live);
void  draw_preview_page(DOC *d, int page, long ox, long oy, long n, long den);
int   export_pdf(DOC *d, const char *path, void (*gauge)(long done, long total));
int   printer_name(char *out);
int   print_doc(DOC *d, const unsigned char *sel, int copies, int colour);   /* sel: a flag per page, NULL all */
int   word_misspelt(DOC *d, long a, long b);
void  img_cache_free(DOC *d);

/* gemdlg.c: a dialog builder that lays itself out, so every language fits */
#define MAXDOBJ 140
#define MAXTED  20
#define MAXDITEM 120
typedef struct {
    short type, group, flags, wc;   /* item kind, radio group, extra flags, width   */
    short row, col;                 /* after layout: row and column (characters)    */
    short obj;                      /* its object                                   */
    int   on;
    const char *s, *tmpl, *valid;
    char *buf;
    short ul, key;                  /* shortcut: letter position, the letter    */
} DITEM;
typedef struct {
    OBJECT  o[MAXDOBJ];
    TEDINFO te[MAXTED];
    short   n, nte;
    DITEM   it[MAXDITEM];
    short   nit, row, nrows, cols, framed, edit1;
    short   x, y, w, h;             /* on screen                                    */
    short   cancel;                 /* from a resource file: the Cancel button      */
    char    tx[MAXTED][3][48];      /* edit fields split for the 3D look            */
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
int   dlg_tree(DLG *g, int tree);   /* a dialog from the resource file: items are objects */
void  dlg_tree_end(DLG *g);
void  dlg_set(DLG *g, short obj, int on);
void  dlg_str(DLG *g, short obj, char *s);
void  dlg_edtext(DLG *g, short obj, char *buf);
int   popup_rsc(int tree, int cur, short x, short y);
short dlg_obj(DLG *g, short item);
int   popup(const char **items, int n, int cur, short x, short y);
int   popup2(const char **items, int n, int cur, int cur2, short x, short y);
void  chart_popup(DOC *d, int tbl, short x, short y);
int   ask_file(const char *title, const char *pattern, const char *defname, char *out);
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
int   dlg_print(DOC *d, const char *printer, unsigned char *sel, int *copies, int *colour);
void  dlg_init(void);
void  dlg_front(void);
extern int dlg_busy, aes_3d, aes_magic, win_dialogs;
int   winpos_get(const char *key, short *x, short *y, short *w, short *h);
void  winpos_set(const char *key, short x, short y, short w, short h);
void  winpos_save(const char *key, short wh);
void  winpos_clamp(short *x, short *y, short w, short h);
int   gauge_msg(short *msg);
void  gauge_open(const char *title, const GRECT *over);
void  gauge_set(long done, long total);
void  gauge_close(void);
void  gauge_cancelable(void);           /* the next gauge has a Cancel button */
int   gauge_cancelled(void);
void  blink_wake(void);                /* the blinking caret: show it again  */
long  blink_wait(void);                /* ms to the next blink, -1: none      */
void  blink_tick(void);
extern short ap_id;
void  busy_ptr(int on);                /* the busy bee, over EdWord's windows */
void  busy_ptr_track(void);
void  busy_begin(const char *title);   /* a long job: a gauge if it takes a while */
void  busy_end(void);

/* gemtwin.c: find and replace, spelling, thesaurus windows */
extern const TOOL find_tool, spell_tool, thes_tool;
void  find_open(DOC *d);
extern const TOOL import_tool, export_tool;
void  import_open(void);
void  export_open(void);
void  xfer_refresh(void);
void  find_again(DOC *d);
void  spell_run(DOC *d);
void  spell_close(void);
DOC  *spell_doc(void);
void  thes_run(DOC *d);
void  live_popup(DOC *d, long a, long b, short mx, short my);
int   spell_ensure(int quiet);
void  find_set(const char *what, const char *with);

/* gemtool.c: the font selector and print preview windows */
extern const TOOL fontsel_tool, preview_tool;
void  fontsel_open(void);
void  fontsel_sync(void);
void  preview_open(DOC *d);
void  preview_changed(DOC *d);
int   preview_idle(int run);
int   preview_area(GRECT *r);

/* gemobj.c: pictures and drawings in the document view */
extern const TOOL draw_tool;
int   obj_at(DOC *d, int page, long px, long py);
int   obj_click(DOC *d, GRECT *tp, short mx, short my, short ks, short clicks);
void  obj_select(DOC *d, int i);
void  obj_draw_handles(DOC *d, GRECT *tp);
int   obj_key(DOC *d, short ks, short key);
void  cmd_picture(DOC *d);
int   picture_insert(DOC *d, const char *path);
void  cmd_drawing(DOC *d);
void  obj_props(DOC *d);
void  obj_delete(DOC *d);
void  draw_edit(DOC *d, int i);
void  draw_close_for(DOC *d, int i);
void  draw_select_shape(int k);
void  draw_vdi_obj(DOC *d, const OBJ *o, long ox, long oy, long n, long den);

/* demo.c: the screenshot script, only in "make EDDEMO=1" builds */
#ifdef EDDEMO
int   demo_start(void);
int   demo_tick(void);
int   demo_dialog(OBJECT *tree, short x, short y, short w, short h);
int   demo_alert(void);
int   demo_file(char *out);
void  demo_shot(const char *name);
extern int demo_on;
#else
#define demo_on 0
#endif

#endif
