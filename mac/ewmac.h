/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ewmac.h - the macOS front end of EdWord
 *
 * The portable core (doc, layout, edit, undo, RTF, codes, spelling,
 * thesaurus...) is shared with the Atari version unchanged. This front end
 * uses AppKit: one NSDocument per document, a page view drawn with
 * Quartz, the formatting codes in an NSTextView, panels for the tools.
 *
 * Layout units: the core lays text out in "pixels" of dev_pixw microns.
 * Here dev_pixw is 88, about a quarter of a point, so widths rounded to
 * whole units stay accurate; the view scales them to the zoom.
 */
#import <Cocoa/Cocoa.h>
#include "../wp.h"
#include "lang.h"

#ifndef ED_VERSION
#define ED_VERSION "3.0"
#endif
#ifndef ED_ABOUT
#define ED_ABOUT ""
#endif

/* commands, as menu item tags */
enum {
    C_NONE, C_ABOUT, C_PREFS,
    C_IMPORT, C_EXPORT, C_EXPTXT, C_EXPMD, C_EXPFWP, C_EXPPT, C_PAGE, C_HEADFOOT, C_PDF, C_HELP,
    C_UNDO, C_REDO, C_FIND, C_FINDNEXT, C_PREVPAGE, C_NEXTPAGE, C_GOTOPAGE, C_REPAG,
    C_INSDATE, C_INSTIME, C_PGBRK, C_COLBRK, C_PAGENUM, C_TBLINS, C_TBLPROP, C_ROWINS, C_ROWDEL, C_COLINS,
    C_COLDEL, C_TBLDEL, C_PICTURE, C_DRAWING, C_TOC, C_INSFILE,
    C_FONTSEL, C_SIZEOTHER,
    C_PLAIN, C_BOLD, C_ITALIC, C_UNDER,
    C_JLEFT, C_JCENTRE, C_JRIGHT, C_JFULL, C_SP1, C_SP15, C_SP2, C_INDENTS, C_HANGING,
    C_BULLETS, C_NUMBERS, C_HEAD1, C_HEAD2, C_HEAD3, C_BODY, C_WRAPCOL,
    C_SPELL, C_LIVESPELL, C_THES, C_SUMMARY, C_UDLOAD, C_UDNEW,
    C_VDOC, C_SPLIT, C_VCODES, C_ZOOMIN, C_ZOOMOUT, C_ZOOM100, C_TOOLBAR, C_RULERS, C_GUIDES,
    C_TILEV, C_TILEH, C_CASCADE,
    C_OBJPROP, C_OBJDEL, C_OBJEDIT,
    C_CH0, C_CP0 = C_CH0 + NCHARTS,              /* chart kind; chart place (+4) */
    C_CPLAST = C_CP0 + 3,
    C_LAST,
    C_FONT0 = 1000, C_SIZE0 = 1300, C_COLOR0 = 1350, C_UD0 = 1380
};
#define NSIZES 10
extern const short sizes[NSIZES];
#define NZOOM 7
extern const short zooms[NZOOM];

/* preferences (NSUserDefaults) */
extern int pref_spell;
extern int pref_date, pref_live, pref_toolbar, pref_paper, pref_land, pref_rulers, pref_guides;
void prefs_load(void);
void prefs_save(void);
void set_language(int lang);

/* strings */
NSString *LS(int id);                       /* a user interface string    */
NSString *LSX(int id);                      /* ... without "..." or ":"   */
NSString *st2ns(const unsigned char *s, long n);
NSString *cells2ns(DOC *d, long a, long b);
int ns2st(NSString *s, char *out, int max); /* returns the length         */
unsigned char ch2st(unichar c);

/* fonts and drawing: ewdraw.m */
double units_per_pt(void);
void fonts_init(void);
NSFont *ew_font(int font, double size_units, int attr);
int font_index(NSString *family);          /* find or add; -1 if full     */
NSColor *ew_color(int c);
typedef struct {
    int  live;                  /* the editing view                       */
    long sa, sb;                /* selection, or sa < 0                   */
    long caret;                 /* caret position, or < 0                 */
    int  selobj;
} DRAWOPT;
/* draw a page in the current context, in layout units from the page's
 * top left corner (the caller scales and translates) */
void ew_draw_page(DOC *d, int page, NSRect clip, const DRAWOPT *o);
void ew_draw_obj(DOC *d, const OBJ *o);
void ew_add_chart_items(NSMenu *m);
NSData *ew_img_tiff(DOC *d, int obj);     /* a picture for the clipboard */       /* the chart's kinds and places */
void ew_draw_shape(const SHAPE *s, double ox, double oy);
void ew_img_cache_clear(void);
int  word_misspelt(DOC *d, long a, long b);

/* dialogs: ewforms.m */
typedef void (^EWAction)(void);
@interface EWForm : NSObject
@property (strong) NSWindow *win;
+ (EWForm *)formWithTitle:(NSString *)title;
- (void)row:(NSString *)label;
- (NSTextField *)field:(NSString *)value width:(int)chars;
- (NSTextField *)number:(long)value width:(int)chars;
- (NSTextField *)text:(NSString *)s;
- (NSButton *)check:(NSString *)title on:(BOOL)on;
- (NSPopUpButton *)popup:(NSArray *)items selected:(NSInteger)sel;
- (NSButton *)button:(NSString *)title action:(EWAction)a;
- (void)note:(NSString *)s;
- (void)addView:(NSView *)v;
/* call a when the control's value changes */
- (void)onChange:(NSControl *)c do:(EWAction)a;
/* modal: returns the index of the button pressed (buttons right to left
 * as given; the first is the default, Escape picks the last) */
- (NSInteger)runWithButtons:(NSArray *)titles;
/* a floating panel with these buttons; tapped calls back with the index */
- (NSPanel *)panelWithButtons:(NSArray *)titles tapped:(void (^)(NSInteger))tapped;
@end
void ew_alert(int text, ...);              /* an alert from a string id   */
int  mm_of(int pt);
int  pt_of_mm(NSTextField *f, int oldpt);  /* unchanged fields keep pt   */

int dlg_page(DOC *d, int page);
int dlg_wrapcol(DOC *d);
int dlg_headfoot(DOC *d);
int dlg_pagenum(DOC *d);
int dlg_indents(int *left, int *first);
int dlg_table(int insert, int *rows, int *cols, short *w, int *border);
int dlg_objprops(DOC *d, OBJ *o);          /* 0 cancel 1 ok 2 delete 3 edit */
int dlg_size(int *size);
void dlg_summary(DOC *d, NSString *name);
int dlg_prefs(void);
int dlg_textimport(int eol, int det, int *cs, int *join);
int dlg_textexport(int *crlf, int *cs);
int dlg_goto(DOC *d, int *page);

/* the document and its window */
@class EWPageView;
@interface EWDocument : NSDocument
{
@public
    DOC d;
}
@property (strong) EWPageView *page;
@property (strong) NSScrollView *pageScroll;
@property (strong) NSTextView *codes;
@property (strong) NSScrollView *codesScroll;
@property (strong) NSSplitView *split;
@property (strong) NSTextField *info;
@property (strong) NSWindow *win;
- (DOC *)doc;
- (void)edited:(long)from typing:(BOOL)typing;   /* after a change to the text */
- (void)changed;                                  /* after any change           */
- (void)moved;                                    /* the caret or selection     */
- (void)setView:(int)view;
- (void)setZoom:(int)zoom;
- (void)applyCodes;
- (void)rebuildCodes;
- (void)updateInfo;
- (void)updateToolbar;
- (void)command:(int)cmd;
- (BOOL)validate:(int)cmd checked:(BOOL *)checked;
- (void)selectFrom:(long)a to:(long)b;
- (void)showRulers;                               /* after Rulers or Guides     */
- (void)updateRulers;                             /* the markers for the caret  */
- (int)caretTable;                                /* the caret's table, or -1   */
- (BOOL)insertLoaded:(DOC *)t;                    /* at the caret, undoable     */
- (BOOL)exportPDFTo:(NSURL *)url;
- (NSString *)baseName;
@end
EWDocument *ew_active_doc(void);

/* the page view: ewpage.m */
@interface EWPageView : NSView <NSTextInputClient>
@property (weak) EWDocument *owner;
- (void)resize;                             /* the document's size changed */
- (void)showCaret;                          /* scroll the caret into view  */
- (NSRect)caretRect;
- (NSRect)pageRect:(int)page;               /* in view coordinates         */
- (double)scale;                            /* view points per layout unit */
- (int)pageAt:(NSPoint)p x:(long *)px y:(long *)py;
- (void)redrawLines:(long)a to:(long)b;
@end

/* printing and PDF: ewprint.m */
NSPrintOperation *ew_print_op(EWDocument *doc, NSURL *pdf);   /* pdf: save instead of print */

/* tool panels: ewpanels.m */
void find_open(EWDocument *doc);
void find_again(EWDocument *doc);
void spell_run(EWDocument *doc);
void thes_run(EWDocument *doc);
void live_popup(EWDocument *doc, long a, long b, NSEvent *ev, NSView *view);
void draw_edit(EWDocument *doc, int obj);
void panels_doc_closed(EWDocument *doc);
void import_open(void);
void export_open(void);
void xfer_refresh(void);
EWDocument *ew_open_import(const char *path, int type, int cs, int join);   /* nil if unreadable */
int  spell_ensure(int quiet);
NSString *support_dir(void);

/* the application: main.m */
int  app_command(int cmd);                  /* 1 if it was the application's */
int  app_validate(int cmd, BOOL *checked);  /* -1: not an application command */
void menus_build(void);
void tile_windows(int how);
int  insert_picture(EWDocument *doc, NSURL *url);
