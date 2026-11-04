/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amiui.h - shared state of the EdWord Amiga front end (AmigaOS 2.04+)
 *
 * The front end follows the GEM one (gem*.c) closely: the same commands,
 * dialogs, tool windows and drawing, done with Intuition, GadTools and
 * graphics.library. EdWord opens a screen of its own, cloned from the
 * Workbench's display mode, with 16 colours for the text colours.
 *
 * Character sets: everything in a DOC (text, names) is in the Atari ST
 * character set, as on the ST; the user interface strings are ISO-8859-1.
 * lat() and stc() convert at the edges.
 */
#ifndef AMIUI_H
#define AMIUI_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <libraries/gadtools.h>
#include "../wp.h"
#include "lang.h"

#ifndef ED_VERSION
#define ED_VERSION "3.0"
#endif
#ifndef ED_ABOUT
#define ED_ABOUT ""
#endif

/* a rectangle, as in GEM, in window (or screen) pixels */
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

/* the screen and its pens */
extern struct Screen   *scr;
extern struct DrawInfo *dri;
extern APTR             vi;               /* GadTools visual info            */
extern struct TextFont *sysfont;          /* the screen's font, for the UI  */
extern struct TextFont *fixfont;          /* fixed width, for the codes pane */
extern struct TextAttr  sysattr;
extern struct Window   *backdrop;         /* behind the documents: menus, requesters */
extern struct MsgPort  *idcmp_port;       /* shared by all EdWord windows   */
extern short wchar, hchar, sys_asc, scr_w, scr_h, npens, depth, small_icons;
extern GRECT desk;                        /* the screen below its title bar */
extern int   colour_on, pdf_scale, quit_now;
extern DOC  *docs[MAX_DOCS];
#define DOC_USED(i) (docs[i] != NULL && docs[i]->used)
extern char  progdir[256];
extern const short sizes[];
extern int   pref_date, pref_toolbar, pref_paper, pref_land;
extern int   pref_autosave, pref_asmin, pref_asmode, pref_display, pref_live, pref_rulers, pref_guides;
enum { DISP_NORMAL, DISP_DARK, DISP_GREEN, DISP_AMBER };
#define NSIZES 8
#define NZOOM  6
extern const short zooms[NZOOM];

/* pens of EdWord's screen */
/* Pens i and 15 - i are each other's complement: selections are drawn
 * by complementing the two lowest planes (black and white swap), and
 * where only full complement works, white becomes 13 and black 14. */
enum { P_GREY, P_BLACK, P_WHITE, P_BLUE, P_RED, P_GREEN, P_DBLUE, P_CYAN, P_MAGENTA, P_YELLOW,
       P_TGREY, P_AMBER, P_PHOSPHOR, P_SHADOW, P_WHITE2, P_LIGHT };
#define P_DESK (npens >= 16 ? P_TGREY : P_GREY)

/* The screen: our own (high res by default, or another mode) or the
 * Workbench's, where EdWord's colours are the nearest pens it has */
enum { SCR_HIRES, SCR_LACE, SCR_LIKEWB, SCR_OTHER, SCR_WB, NSCR };
extern int pref_screen, pref_scrw, pref_scrh, pref_scrdepth, on_wb;
extern unsigned long pref_modeid;
extern UBYTE penmap[16];
#define PEN(p) ((ULONG)penmap[(p) & 15])
#define SCR_TAG (on_wb ? WA_PubScreen : WA_CustomScreen)

/* dialog item flags, as in GEM */
#define OF_EXIT     0x0004
#define OF_DEFAULT  0x0002

/* ami.c */
DOC  *doc_of_win(struct Window *w);
DOC  *top_doc(void);
DOC  *active_doc(void);
void  do_cmd(int cmd);
void  update_menu(DOC *d);
void  update_winmenu(void);
void  rebuild_menu(void);
void  menu_attach(struct Window *w);
void  menu_detach(struct Window *w);
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
void  handle_key(struct Window *w, int key, int qual);
void  pump_messages(void);
void  pump_flush(void);
extern int pump_defer;
void  window_closing(struct Window *w);
struct Window *open_ewin(struct TagItem *tags, ULONG idcmp);
void  close_ewin(struct Window *w);
void  activate_doc(DOC *d);
void  block_input(int on);

/* keys, as handle_key and the windows get them: a Latin-1 character, or one of these */
enum { K_LEFT = 0x100, K_RIGHT, K_UP, K_DOWN, K_RETURN, K_ENTER, K_TAB, K_BS, K_DEL, K_ESC, K_HELP,
       K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10 };
#define KQ_SHIFT 1
#define KQ_ALT   2
#define KQ_CTRL  4
#define KQ_AMIGA 8

/* tool windows: each module answers for its own windows */
typedef struct {
    int  (*owns)(struct Window *w);
    void (*msg)(struct IntuiMessage *m);    /* everything but keys */
    void (*key)(int key, int qual);
    void (*close)(void);
    void (*doc_closed)(DOC *d);
} TOOL;
extern const TOOL import_tool, export_tool;
int   tool_route(struct IntuiMessage *m);
int   tool_route_key(struct Window *w, int key, int qual);
int   tool_owns(struct Window *w);

/* amiwin.c */
#define TBH (small_icons ? 15 : 24)
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
void  win_msg(DOC *d, struct IntuiMessage *m);
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
struct Window *doc_win(DOC *d);
void  doc_block(int on);

/* amigfx.c: drawing helpers and the mouse */
struct RastPort *rp_of(struct Window *w);
void  gfx_begin(struct Window *w);         /* following calls draw into w        */
void  gfx_end(void);
extern struct RastPort *grp;               /* the RastPort being drawn into      */
extern struct Window   *gwin;
void  clip_set(const GRECT *r);            /* clip drawing to r (window coords)  */
void  clip_off(void);
int   rc_inter(const GRECT *a, GRECT *b);
int   in_rect(const GRECT *r, short x, short y);
void  fill_rect(long x1, long y1, long x2, long y2, int pen);   /* inclusive */
void  fill_pat(long x1, long y1, long x2, long y2, int pen, int grey);
void  fill_g(const GRECT *r, int pen);
void  frame(long x1, long y1, long x2, long y2, int pen);
void  xor_rect(long x1, long y1, long x2, long y2);
void  xor_frame(long x1, long y1, long x2, long y2);
void  xor_line(long x1, long y1, long x2, long y2);
void  hline(long x1, long x2, long y, int pen, int dotted);
void  line(long x1, long y1, long x2, long y2, int pen);
void  thick_line(long x1, long y1, long x2, long y2, int pen, int w);
void  poly_line(const long *xy, int n, int pen, int w);
void  poly_fill(const long *xy, int n, int pen, int grey);
void  disc(long cx, long cy, long r, int pen);
void  sys_text(long x, long top, const char *s, int pen);       /* Latin-1, screen font */
void  sys_text_n(long x, long top, const char *s, long n, int pen);
void  fix_text(long x, long top, const char *s, long n, int pen); /* codes pane font */
long  sys_width(const char *s);
void  bevel(const GRECT *r, int recessed);
void  button_box(const GRECT *r, const char *label, int sel, int disabled);
void  busy(int on);
void  icons_init(void);
void  icons_free(void);
int   icon_h(void);
void  draw_tbicon(int ic, long x, long y, int pen);
void  blit_bits(const unsigned char *bits, long rb, long w, long h, long x, long y, int pen);
/* tracking the mouse while a button is held (from a SELECTDOWN) */
void  track_begin(struct Window *w);
int   track_next(short *mx, short *my, int *qual);   /* 0 when the button is released */
void  track_end(void);
int   long_press(struct Window *w, short mx, short my);
int   double_click(ULONG secs, ULONG micros);
void  gfx_init(void);
void  gfx_free(void);
enum { IC_BOLD, IC_ITALIC, IC_UNDER, IC_LEFT, IC_CENTRE, IC_RIGHT, IC_JUSTIFY,
       IC_FONT, IC_SIZE, IC_COLOUR, IC_SPELL, IC_CODES, IC_BULLETS, IC_NUMBERS,
       IC_UNDO, IC_REDO, IC_ZOOM, IC_TABLE, IC_RULER, IC_GUIDES, IC_PGBRK, IC_COUNT };

/* character sets */
const char *lat(const char *st);           /* ST -> Latin-1, into a ring of buffers */
void  lat_n(char *out, const char *st, long n);
void  stc(char *out, const char *latin, int max);    /* Latin-1 -> ST */
unsigned char lat_ch(unsigned char st);
unsigned char st_ch(unsigned char latin);

/* amirend.c */
void  fonts_load(void);
void  fonts_unload(void);
struct TextFont *font_for(int font, int size_px, int attr);
int   color_pen(int c);
void  draw_page(DOC *d, int page, long ox, long oy, long n, long den, const GRECT *clip, int pen, int live);
void  draw_preview_page(DOC *d, int page, long ox, long oy, long n, long den);
int   export_pdf(DOC *d, const char *path, void (*gauge)(long done, long total));
int   printer_name(char *out);
int   print_doc(DOC *d, const unsigned char *sel, int copies);   /* sel: a flag per page, NULL all */
int   word_misspelt(DOC *d, long a, long b);
void  img_cache_free(DOC *d);
void  text_at(long x, long base, const char *st, int n, int font, int size_px, int attr, int pen);

/* amidlg.c: a dialog builder on GadTools that lays itself out */
#define MAXDITEM 120
typedef struct {
    short type, group, flags, wc;
    short row, col;                 /* layout: row, x in pixels                     */
    short gx, gy, gw;               /* gadget position and width (pixels)           */
    short lead, nopt;               /* radio: first item of its group; options      */
    short hidden, disabled;
    int   on;                       /* check: state; radio leader: chosen option    */
    const char *s, *tmpl, *valid;
    char *buf;
    struct Gadget *gad;
    char  label[40], after[40];     /* edit: text before and after the field        */
    short n;                        /* edit: characters                             */
} DITEM;
typedef struct {
    DITEM it[MAXDITEM];
    short nit, row, nrows, framed, edit1, def;
    short w, h;                     /* inner size, pixels                           */
    const char *title;
    struct Window *win;
    struct Gadget *glist;
    const char *cyc[MAXDITEM + 24];  /* cycle gadget labels, NULL after each group */
    short ncyc, lastitem;
    ULONG lsecs, lmics;
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
int   dlg_gadgets(DLG *g);              /* (re)make the gadgets in g->win            */
void  dlg_fetch(DLG *g);                /* copy edit fields and states back           */
void  dlg_free_gadgets(DLG *g);
short dlg_item_of(DLG *g, struct Gadget *gad);
int   dlg_event(DLG *g, struct IntuiMessage *m, short *item, int *clicks);   /* 1: item chosen */
void  dlg_rebuild(DLG *g);              /* after texts or visibility changed          */
struct Window *dlg_window(DLG *g, const char *title, long x, long y, struct MsgPort *port, ULONG idcmp);
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

/* amitwin.c: find and replace, spelling, thesaurus windows */
extern const TOOL find_tool, spell_tool, thes_tool;
void  find_open(DOC *d);
void  find_again(DOC *d);
void  spell_run(DOC *d);
void  spell_close(void);
DOC  *spell_doc(void);
void  thes_run(DOC *d);
void  live_popup(DOC *d, long a, long b, short mx, short my);
int   spell_ensure(int quiet);

/* amitool.c: the font selector and print preview windows */
extern const TOOL fontsel_tool, preview_tool;
void  fontsel_open(void);
void  fontsel_sync(void);
void  preview_open(DOC *d);
void  preview_changed(DOC *d);
int   preview_idle(int run);

/* amiobj.c: pictures and drawings in the document view */
extern const TOOL draw_tool;
int   obj_at(DOC *d, int page, long px, long py);
int   obj_click(DOC *d, GRECT *tp, short mx, short my, int qual, int clicks);
void  obj_select(DOC *d, int i);
void  obj_draw_handles(DOC *d, GRECT *tp);
int   obj_key(DOC *d, int key, int qual);
void  cmd_picture(DOC *d);
int   picture_insert(DOC *d, const char *path);
void  cmd_drawing(DOC *d);
void  obj_props(DOC *d);
void  obj_delete(DOC *d);
void  draw_edit(DOC *d, int i);
void  draw_close_for(DOC *d, int i);
void  draw_obj(DOC *d, const OBJ *o, long ox, long oy, long n, long den);

/* amiclip.c */
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
extern ULONG upd_id;                       /* GA_ID and PGA_Top of the last IDCMPUPDATE */
extern LONG  upd_top;
extern ULONG timer_sig;

#ifdef EDLOG
void edlog(const char *fmt, ...);
#else
#define edlog(...) ((void)0)
#endif

#ifdef AMI13
#include "ami13.h"
#endif

#endif
