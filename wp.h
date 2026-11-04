/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * wp.h - EdWord: a GDOS word processor for the Atari ST
 *
 * Shared types and prototypes. Everything except the gem*.c files is
 * portable C and can be built and tested on a host machine.
 */
#ifndef WP_H
#define WP_H

#ifndef MAX_DOCS
#define MAX_DOCS   32           /* open documents (the platform may allow fewer) */
#endif
#define MAX_FONTS  255

/* character attributes (CELL.attr) */
#define A_BOLD     1
#define A_ITALIC   2
#define A_UNDER    4

/* paragraph justification (PARA.just) */
enum { J_LEFT, J_CENTRE, J_RIGHT, J_FULL };

/* text colours (CELL.color); 0 is the default black */
#define NCOLORS    8
extern const char *color_name[NCOLORS];
extern const unsigned char color_rgb[NCOLORS][3];

/* special characters in the text */
#define CH_PAGE    0x0C        /* page break, ends a paragraph            */
#define CH_COLBRK  0x0B        /* column break, ends a paragraph          */
#define CH_CELL    0x1F        /* ends a table cell (not the row's last)  */
#define ISBRK(c)   ((c) == '\n' || (c) == CH_PAGE || (c) == CH_COLBRK)
#define ISEND(c)   (ISBRK(c) || (c) == CH_CELL)    /* ends a laid-out line */

/* One character plus its formatting (6 bytes). ch is in the Atari ST
 * character set. para indexes the document's paragraph formats; every
 * cell of a paragraph has the para of the cell that ends it. */
typedef struct {
    unsigned char ch, font, size, attr, color, para;
} CELL;

/* ---- paragraph formats -------------------------------------------------- */

enum { LS_SINGLE, LS_ONEHALF, LS_DOUBLE };
enum { LIST_NONE, LIST_BULLET, LIST_NUMBER };
enum { TOCE_NONE, TOCE_ENTRY, TOCE_TITLE };

typedef struct {
    unsigned char just;         /* J_*                                     */
    unsigned char spacing;      /* LS_*                                    */
    unsigned char list;         /* LIST_*                                  */
    unsigned char toc;          /* 0, or heading level 1-3 for the TOC     */
    unsigned char tocent;       /* TOCE_*: generated table of contents     */
    unsigned char tbl;          /* 0, or table number + 1: a table row     */
    short left, first;          /* indents in points; first is relative to
                                   left and may be negative (hanging)      */
} PARA;
#define MAX_PARAS 255

/* fields for doc_para_set() */
enum { PF_JUST, PF_SPACING, PF_LIST, PF_TOC, PF_TOCENT, PF_TBL, PF_LEFT, PF_FIRST };

/* ---- tables ------------------------------------------------------------- */

#define MAX_COLS 12
#define MAX_TBLS 48
enum { CH_NONE, CH_PIE, CH_BAR, CH_PIE3D, CH_BAR3D, NCHARTS };
enum { CP_TOP, CP_BOTTOM, CP_LEFT, CP_RIGHT };
typedef struct {
    short ncols, border;
    short w[MAX_COLS];          /* column widths, points                   */
    unsigned char chart, cpos;  /* CH_*: a chart of the table's numbers; CP_* */
    short cw, ch;               /* the chart's size in points, 0: automatic */
} TBL;

/* where a table's chart was laid out (page pixels; page -1: nowhere) */
typedef struct { short page, x, y, w, h, pos; } CHBOX;   /* pos: where it went (CP_*) */

/* ---- page set-up, all in PostScript points ------------------------------ */

enum { P_A4, P_LETTER, P_A5, P_LEGAL, NPAPER };
typedef struct {
    short paper, landscape;
    short w, h;                 /* page size after orientation             */
    short ml, mr, mt, mb;       /* margins                                 */
    short wrapcol;              /* >0: also wrap after this many columns   */
    short cols, colgap;         /* text columns and the gap between them   */
} PAGESET;
extern const char *paper_name[NPAPER];
void page_default(PAGESET *p);
void page_set_paper(PAGESET *p, int paper, int landscape);

/* margins and columns of one page (or that page and the ones after it) */
#define MAX_PGOVR 32
typedef struct {
    short page, following;      /* 0-based page; also the pages after it   */
    short ml, mr, mt, mb, cols, colgap;
} PGOVR;

/* the format a page actually uses */
typedef struct { short ml, mr, mt, mb, cols, colgap; } PGFMT;

/* ---- pictures and drawings: floating objects on a page ---------------------- */

enum { OB_NONE, OB_IMG, OB_DRAW };
enum { WR_NONE, WR_BOX, WR_SHAPE };
enum { SH_RECT, SH_RRECT, SH_ELLIPSE, SH_LINE, SH_TRI, NSHAPES };
enum { FL_NONE, FL_WHITE, FL_GREY, FL_SOLID, NFILLS };
/* more fill patterns, for charts in black and white */
enum { FP_DIAG1 = 8, FP_HLINE, FP_DIAG2, FP_CROSS, FP_DOTS, FP_VLINE };

typedef struct {
    unsigned char type, fill, color, lw;    /* lw: line width in pixels, 0 none */
    short x1, y1, x2, y2;                   /* points, inside the drawing box  */
} SHAPE;
#define MAX_SHAPES 16

typedef struct {
    short type, page;           /* OB_*, 0-based page                      */
    short x, y, w, h;           /* points from the page's top left corner  */
    short wrap, border;         /* WR_*; frame around the object           */
    short img;                  /* picture: index into the image list      */
    short nsh;
    SHAPE sh[MAX_SHAPES];       /* drawing: its shapes                     */
} OBJ;
#define MAX_OBJS 32

/* a picture, kept as a 1-bit bitmap (1 = black), rows of rb bytes, and
 * (when there was room) its colours, R G B for each pixel */
typedef struct {
    short w, h, rb;
    unsigned char *bits;
    unsigned char *rgb;         /* NULL: black and white only              */
    char name[14];
} IMG;
#define MAX_IMGS 32

/* ---- layout ----------------------------------------------------------------- */

/* One laid-out line. Device units are screen pixels at 100%; x and py are
 * measured from the top left corner of the page. */
#define LF_CELL   1             /* inside a table cell                     */
#define LF_FIRST  2             /* first line of its paragraph             */
#define LF_LAST   4             /* last line of its paragraph              */
typedef struct {
    long  start, len;           /* cells covered by this line              */
    short page, col;            /* page and text column                    */
    short x, w;                 /* where the line's space starts; width    */
    short tx0;                  /* x minus the column's left edge (tabs)   */
    short py;                   /* top of the line                         */
    short asc, desc, lh;        /* ascent, descent; lh: advance (spacing)  */
    short xoff;                 /* centre / right offset within w          */
    short sp_first;             /* first stretchable cell (rel. start)     */
    short nsp, sp_add, sp_rem;  /* full justification: stretched spaces    */
    short tabk, tabw;           /* dot-leader tab: cell (rel.) and width   */
    short hang;                 /* hanging indent: a tab before this x goes to it */
    short mark, markx;          /* list marker: 0, -1 bullet, n number; x  */
    unsigned char flags, cell;  /* LF_*; table cell column                 */
} LINE;

/* one table row as laid out, for drawing its grid */
typedef struct {
    long  start;                /* first cell of the row                   */
    short page, y, h, x;        /* top, height, left edge (page pixels)    */
    short tbl;
} TROW;

typedef struct {
    short id;                   /* VDI font id                             */
    char  name[34];
    short outline;              /* Speedo / FSM outline font               */
} FONTDEF;

typedef struct {
    short w[256];               /* advance width per character, pixels     */
    short asc, desc;
} METRICS;

/* Header and footer. The text may contain {page} {pages} {date} {time}
 * and {file}. They use the document's default font. */
typedef struct {
    char text[2][82];               /* 0 header, 1 footer (ST charset)     */
    unsigned char just[2];          /* J_LEFT, J_CENTRE or J_RIGHT         */
    unsigned char size;             /* point size                          */
    unsigned char first;            /* also on the first page              */
} HEADFOOT;

/* the editable "formatting codes" view of a document */
typedef struct {
    char *t;     long len, cap;     /* codes text                          */
    long *ls;    long nl, lcap;     /* start of each wrapped line          */
    long  cols;                     /* wrap width in characters            */
    long  cursor, anchor, want_col;
    long  top;                      /* first visible line                  */
    int   dirty;                    /* edited, not yet applied             */
} CODES;

/* Everything about a document except its text that undo has to restore.
 * Kept together so a snapshot is one copy. */
typedef struct {
    PAGESET  ps;
    HEADFOOT hf;
    short    npara, ntbl, novr, nobj;
    PARA     paras[MAX_PARAS];
    TBL      tbls[MAX_TBLS];
    PGOVR    ovr[MAX_PGOVR];
    OBJ      objs[MAX_OBJS];
} DOCAUX;

/* one undo step: cells [pos, pos+nold) were replaced by nnew cells */
typedef struct {
    long  pos, nold, nnew;
    CELL *cold, *cnew;
    long  cur0, cur1;           /* caret before and after                  */
    DOCAUX *aux0, *aux1;        /* non-text state before and after, or NULL */
    int   typing;               /* may merge with the next typed character */
} UREC;

typedef struct {
    UREC *r;  int n, cap, pos;  /* pos: number of steps that can be undone */
    CELL *shadow; long slen, scap;  /* the text as of the last step         */
    DOCAUX *saux;               /* the rest as of the last step            */
    long  bytes;                /* memory held by the steps                */
    int   ready, full;         /* full: compare the whole text next time */
} UNDO;

enum { VIEW_DOC, VIEW_SPLIT, VIEW_CODES };

typedef struct {
    int   used, dirty, shown_dirty;
    CELL *text;  long len, cap;
    LINE *lines; long nlines, lcap;
    TROW *rows;  long nrows, rcap;
    long *pgline; int npages, pgcap;    /* first line of each page          */
    DOCAUX a;                   /* page set-up, header/footer, paragraphs,
                                   tables, page formats, objects            */
    IMG   imgs[MAX_IMGS];  short nimg;
    CHBOX cbox[MAX_TBLS];       /* the tables' charts, as laid out         */
    long  ch_lo, ch_hi;         /* range changed since the last undo step  */
    UNDO  u;
    long  cursor, anchor;       /* anchor < 0: no selection                */
    long  want_x;               /* column memory for up/down               */
    CELL  ins;                  /* style used for newly typed text         */
    PARA  insp;                 /* paragraph format for an empty document  */
    char  path[256];
    char  base[40];             /* short title, e.g. "LETTER.RTF"          */
    char  name[80], info[120];
    /* front end state */
    short wh;                   /* GEM window handle, -1 if none           */
    short vsz, vpos, hsz, hpos;
    long  scroll_x, scroll_y;
    int   view, focus;          /* VIEW_*; focus 0 text, 1 codes           */
    int   zoom;                 /* percent                                 */
    int   toolbar, iconified, codes_stale;
    int   selobj;               /* selected picture/drawing, -1 none       */
    int   slot;                 /* its place in the front end's list       */
    int   caret_off;            /* the blinking caret is hidden just now   */
    short ic_x, ic_y, ic_w, ic_h;   /* window rect before iconify          */
    CODES cv;
    /* what changed since the last layout, so the next one can redo just
     * that part: [lay_lo, lay_hi) in current positions, the text grew by
     * lay_shift; lay_full: lay out everything */
    long  lay_lo, lay_hi, lay_shift;
    int   lay_full;
    DOCAUX *lay_aux;            /* page set-up, formats... as laid out     */
} DOC;

/* shorthands for the document's settings */
#define PS(d)   ((d)->a.ps)
#define HF(d)   ((d)->a.hf)
#define PARA_OF(d, c)  (&(d)->a.paras[(c)->para < (d)->a.npara ? (c)->para : 0])

/* walks the cells of one laid-out line, giving each its x position
 * (relative to the line's x) */
typedef struct {
    DOC  *d;
    LINE *ln;
    long  k, end, x, tab, key;
    int   gi;
    const METRICS *m;
} LWALK;

enum { AP_FONT, AP_SIZE, AP_PLAIN, AP_TOGGLE, AP_COLOR, AP_ATTRS };

extern short   dev_pixw, dev_pixh;      /* pixel size in microns */
extern FONTDEF fonts[MAX_FONTS];
extern short   nfonts;

/* supplied by the platform layer (gem*.c, or a test stub) */
const METRICS *get_metrics(int font, int size, int attr);

/* doc.c - storage, paragraph formats, tables, page formats */
long pt2px_x(long pt);
long pt2px_y(long pt);
long px2pt_x(long px);
long px2pt_y(long px);
long tab_px(void);
int  default_font(void);
int  default_size(void);
void doc_init(DOC *d);
void doc_free(DOC *d);
int  doc_insert(DOC *d, long pos, const CELL *c, long n);
void doc_delete(DOC *d, long pos, long n);
void doc_touch(DOC *d, long a, long b);
void doc_touch_all(DOC *d);
int  para_find(DOC *d, const PARA *p);
void para_default(PARA *p);
void doc_norm_para(DOC *d);
void doc_compact_paras(DOC *d);
long doc_para_start(DOC *d, long pos);
long doc_para_end(DOC *d, long pos);
PARA *doc_para_at(DOC *d, long pos);
int  doc_para_set(DOC *d, long a, long b, int field, int val);
int  doc_tbl_new(DOC *d, int ncols, const short *w, int border);
void page_fmt(DOC *d, int page, PGFMT *f);
int  page_ovr_set(DOC *d, int page, int following, const PGFMT *f);
void page_ovr_clear(DOC *d);
long page_w_px(DOC *d);
long page_h_px(DOC *d);
long text_w_px(DOC *d, int page);
int  doc_obj_add(DOC *d, const OBJ *o);
void doc_obj_delete(DOC *d, int i);
int  doc_img_add(DOC *d, const IMG *im);

/* layout.c - lines, pages, hit testing, caret movement */
void doc_layout(DOC *d);
/* how paragraphs break across pages (a preference) */
enum { KEEP_ANY, KEEP_WIDOWS, KEEP_TOGETHER };
extern int lay_keep;
int  doc_npages(DOC *d);
long doc_find_line(DOC *d, long pos);
void lw_init(LWALK *w, DOC *d, long li);
long lw_adv(LWALK *w);
void lw_step(LWALK *w);
long doc_x_of(DOC *d, long li, long pos);
long doc_pos_in_line(DOC *d, long li, long x);
long doc_hit(DOC *d, int page, long x, long y);
long doc_line_vert(DOC *d, long li, long x, int dir);
void doc_marker(DOC *d, long li, char *buf);
long doc_marker_x(DOC *d, long li);

/* edit.c - selection, typing, styles, clipboard, tables, lists */
int  doc_sel(DOC *d, long *a, long *b);
int  doc_delete_range(DOC *d, long a, long b);
int  doc_delete_sel(DOC *d);
int  doc_type(DOC *d, const CELL *c, long n);
int  doc_type_str(DOC *d, const char *s);
int  doc_apply(DOC *d, long a, long b, int what, int val);
void doc_sync_ins(DOC *d);
void doc_copy(DOC *d);
int  doc_paste(DOC *d);
int  doc_insert_doc(DOC *d, DOC *src, long a, long b);
void doc_clip_free(void);
int  doc_in_table(DOC *d, long pos, long *row_start, int *cell);
long doc_cell_start(DOC *d, long pos);
long doc_cell_end(DOC *d, long pos);
long doc_next_cell(DOC *d, long pos, int dir);
int  tbl_insert(DOC *d, long pos, int rows, int cols, int border);
int  tbl_row_insert(DOC *d, long pos, int below);
int  tbl_row_delete(DOC *d, long pos);
int  tbl_col_insert(DOC *d, long pos, int right);
int  tbl_col_delete(DOC *d, long pos);
int  tbl_delete(DOC *d, long pos);
int  tbl_range(DOC *d, long pos, long *a, long *b);
int  tbl_resize(DOC *d, long pos, int rows, int cols, const short *w, int border);
int  tbl_rows(DOC *d, long pos);

/* docops.c - table of contents, find and replace, statistics */
int  toc_insert(DOC *d, long pos, const char *title);
int  toc_update(DOC *d);
int  doc_repaginate(DOC *d);      /* lay out all pages again, update the contents' page numbers */
/* page numbers in the header (where 0) or footer (1): style 0 "1", 1 "Page 1",
 * 2 "Page 1 of 3", 3 "- 1 -"; page and of are the words (ST charset) */
#define NPNSTYLES 4
void hf_pagenum_set(DOC *d, int where, int just, int style, const char *page, const char *of);
void hf_pagenum_clear(DOC *d);
int  hf_pagenum_find(DOC *d, int *where, int *style);   /* 1 if a header or footer has {page} */
/* "1-3, 5, 8-": sel[0..np-1] gets 1 for each page named; the number of pages */
int  page_list(const char *s, int np, unsigned char *sel);
/* new documents from a template; w[] are the template's words (ST charset) */
enum { TPL_BLANK, TPL_UK, TPL_US, TPL_NEWS, NTPL };
enum { TW_TO, TW_TOADDR, TW_DEAR_UK, TW_DEAR_US, TW_SUBJECT, TW_BODY, TW_YOURS_UK,
       TW_YOURS_US, TW_ISSUE, TW_MAIN, TW_TEXT, TW_NEWS, TW_DATES, TW_WHAT, TW_WHEN,
       TW_CONTENTS, TW_ORG, TW_PAGE, NTW };
int  doc_template(DOC *d, int kind, const char *name, const char *const *addr,
                  const char *title, const char *const *w);
int  toc_find(DOC *d, long *a, long *b);
#define FIND_CASE  1
#define FIND_WORD  2
#define FIND_BACK  4
long find_next(DOC *d, long from, const char *what, int flags, long *end);
int  find_replace_all(DOC *d, const char *what, const char *with, int flags, int in_sel);
void doc_replace(DOC *d, long a, long b, const char *with);
typedef struct { long pages, paras, words, chars, chars_ns, lines, tables, objects; } DSTATS;
void doc_stats(DOC *d, DSTATS *s);
int  word_at(DOC *d, long pos, long *a, long *b, char *word, int max);

/* undo.c */
void undo_reset(DOC *d);
void undo_free(DOC *d);
int  undo_commit(DOC *d, int typing);
int  undo_can(DOC *d);
int  redo_can(DOC *d);
int  undo_do(DOC *d);
int  redo_do(DOC *d);

/* charset.c */
unsigned char st_to_win(unsigned char c);
unsigned char win_to_st(unsigned char c);
unsigned char uni_to_st(long u);
long st_to_uni(unsigned char c);
unsigned char st_lower(unsigned char c);
unsigned char st_upper(unsigned char c);
int  st_isalpha(unsigned char c);
int  nearest_color(int r, int g, int b);

/* rtf.c */
int rtf_import(DOC *d, const char *path);
int rtf_export(DOC *d, const char *path);

/* text.c */
enum { CS_AUTO, CS_ATARI, CS_WIN, CS_UTF8, CS_UTF16 };
/* UTF-16 (with a byte order mark, or little/big endian found by its zero
 * bytes) as UTF-8, malloc'ed, with its length; NULL if b isn't UTF-16
 * (force: take it as UTF-16 anyway, little endian without a mark) */
unsigned char *utf16_to_utf8(const unsigned char *b, long n, long *out, int force);
int  is_utf16(const unsigned char *b, long n);
int  file_transcode(const char *path, int cs, int crlf);   /* a UTF-8 LF file to cs, CR LF */
enum { EOL_NONE, EOL_LF, EOL_CRLF, EOL_CR, EOL_MIXED };
unsigned char *load_file(const char *path, long *n);

/* Progress of a long job (reading, saving, laying out). The front end
 * sets prog_hook to show a bar, which gets 0..1000; prog_part(lo, hi)
 * gives the next step that part of the bar. PROG(done, total) is cheap
 * enough for an inner loop: it does nothing until the bar would move. */
extern void (*prog_hook)(long done, long total);
extern long prog_next;
void prog_part(int lo, int hi);
void prog_split(int per_mille);      /* the next step gets that much of what is left */
void prog_step_done(void);           /* ...and that step has finished */
void prog(long done, long total);
#define PROG(done, total) do { if (prog_hook && (long)(done) >= prog_next) prog((long)(done), (long)(total)); } while (0)
void text_detect(const unsigned char *b, long n, int *eol, int *cs);
int  text_import_buf(DOC *d, const unsigned char *b, long n, int cs, int join);
int  text_import(DOC *d, const char *path, int cs, int join);
int  text_export(DOC *d, const char *path, int crlf, int cs);
long text_to_st(const unsigned char *b, long n, int cs, unsigned char *out);
#ifdef EOF
void put_utf8(FILE *f, unsigned char ch);
void put_cs(FILE *f, unsigned char ch, int cs);     /* one ST character in cs */
#endif

/* md.c - Markdown; fwp.c - 1st Word Plus */
int  md_import(DOC *d, const char *path, int cs);
int  md_export(DOC *d, const char *path, int crlf, int cs);
int  fwp_import(DOC *d, const char *path);
int  fwp_export(DOC *d, const char *path);
int  fwp_detect(const unsigned char *b, long n);

/* file formats, in the order of the Import and Export windows */
enum { FT_RTF, FT_TEXT, FT_MD, FT_FWP, FT_PT, FT_PDF };
#define FT_AUTO (-1)
int  file_detect(const char *path, int text);  /* FT_*; text: plain text if unknown */
int  doc_read(DOC *d, const char *path, int type, int cs, int join);
int  doc_write(DOC *d, const char *path, int type, int crlf, int cs);   /* not PDF */

/* chart.c - charts of a table's numbers */
#define CH_MAXROW 24
#define CH_MAXCOL MAX_COLS
#define CH_TXT    24
#define CH_MAXCAT CH_MAXROW
#define CH_MAXSER 6
#define CH_MAXPTS 56
#define CHART_PT  8              /* the labels' point size */
typedef struct {
    int    ncat, nser;
    char   cat[CH_MAXCAT][CH_TXT], ser[CH_MAXSER][CH_TXT];   /* ST charset */
    long   v[CH_MAXCAT][CH_MAXSER];  /* hundredths */
} CHDATA;
enum { CO_POLY, CO_LINE, CO_TEXT };
typedef struct {
    unsigned char op;            /* CO_*                                         */
    unsigned char series;        /* a polygon's fill: chart_color() / chart_pattern() */
    unsigned char shade;         /* 3D: 1 top, 2 side (a little darker)          */
    unsigned char align;         /* text: 0 left, 1 centre, 2 right of xy[0]     */
    short n;                     /* points; text: its baseline at xy[0], xy[1]   */
    short xy[2 * CH_MAXPTS];     /* the chart's pixels from its top left corner  */
    char  text[CH_TXT];          /* ST charset                                   */
} CHOP;
#define CH_MAXOPS 160
int  chart_number(const char *s, long *v);    /* in hundredths */
int  chart_data(DOC *d, int tbl, CHDATA *cd);
int  chart_ops(DOC *d, int tbl, long w, long h, CHOP *op, int max);
int  chart_color(int series);
int  chart_pattern(int series);
void chart_size(const TBL *T, long tw, long colw, long *w, long *h);

/* protext.c */
int  pt_import(DOC *d, const char *path);
int  pt_export(DOC *d, const char *path);
int  pt_detect(const unsigned char *b, long n);
int  font_family_find(int family);      /* rtf.c: 0 sans, 1 serif, 2 mono */
int  font_is_mono(int font);
enum { DT_SHORT, DT_US, DT_LONG, DT_DAYLONG, DT_ISO, DT_TIME, NDATEFMT = DT_TIME };
extern const char **date_months, **date_days;   /* set for the UI language */
void fmt_date(char *out, int style, int y, int m, int day, int hh, int mm);
void fmt_now(char *out, int style);
int  hf_visible(DOC *d, int which, int page);
void hf_expand(DOC *d, int which, int page, int npages, int datefmt, char *out, int max);

/* codes.c */
int  codes_build(DOC *d);
int  codes_apply(DOC *d);
void codes_wrap(CODES *c, long cols);
long codes_line_of(CODES *c, long pos);
long codes_line_len(CODES *c, long li);
long codes_pos(CODES *c, long li, long col);
int  codes_insert(CODES *c, long pos, const char *s, long n);
void codes_delete(CODES *c, long pos, long n);
int  codes_sel(CODES *c, long *a, long *b);
void codes_free(CODES *c);

/* img.c - GEM IMG pictures */
extern long img_rgb_max;        /* the largest colour copy kept (bytes)    */
int  img_load(const char *path, IMG *im);      /* GEM IMG, PNG, IFF ILBM/PBM */
int  img_detect(const unsigned char *b, long n);
int  img_from_rgb(IMG *im, long w, long h, const unsigned char *rgb, long stride, int bpp);
int  img_save(const IMG *im, const char *path); /* GEM IMG, 24 planes if colour */
unsigned char *img_scale_rgb(const IMG *im, long w, long h);
int  img_scale(const IMG *im, long w, long h, IMG *out);
void img_free(IMG *im);

/* raster.c - drawing into 1-bit bands (PDF pages) */
typedef struct {
    unsigned char *bits;
    long rb, w, y0, h;          /* row bytes, width, first row, rows        */
} BAND;
void rs_begin(void);
void rs_line(long x0, long y0, long x1, long y1);      /* 1/16 pixel units */
void rs_poly(const long *xy, int n);                   /* closed, 1/16 px  */
void rs_thick(long x0, long y0, long x1, long y1, long wid);
void rs_fill(BAND *b, int pattern);                    /* non-zero winding */
void rs_end(void);
void rs_rect(BAND *b, long x0, long y0, long x1, long y1, int pattern);
void rs_ellipse_path(long cx, long cy, long rx, long ry);
void rs_rrect_path(long x0, long y0, long x1, long y1, long r);
void rs_blit(BAND *b, const IMG *im, long x, long y);
void rs_shape(BAND *b, const SHAPE *s, long ox, long oy, long num, long den, int fill_only);
void rs_obj(BAND *b, DOC *d, const OBJ *o, int color, long num, long den);
extern int rs_skip_rgb;
void shape_extent(const SHAPE *s, long y0, long y1, long *x0, long *x1);
int  shape_pts(int type, long x0, long y0, long x1, long y1, long *xy);   /* up to 52 points */

/* pagedraw.c - one page into a band, for the PDF export; glyphs come from
 * the platform layer (outline or bitmap fonts) */
typedef struct {
    short w, h, ox, oy;         /* bitmap size; offset from pen and baseline */
    unsigned char *bits;        /* rows of (w + 7) / 8 bytes                 */
} GLYPHBM;
typedef const GLYPHBM *(*GLYPHFN)(void *ctx, int font, int size, int attr, int ch, int scale);
int page_render(DOC *d, int page, int color, BAND *b, int scale, GLYPHFN glyph, void *ctx, int datefmt);

/* pdf.c - each page is a bitmap image, drawn by the platform layer */
typedef struct {
    int  scale;                 /* image pixels per screen pixel           */
    long max_band;              /* rows the renderer can do at once        */
    int  colour;                /* keep text colours                       */
    /* draw rows [y0,y0+h) of a page; only things with this colour
     * (or everything if color < 0). Fill bits (1 = ink, MSB first). */
    int (*render)(void *ctx, DOC *d, int page, int color,
                  long y0, long h, long w, unsigned char *bits, long rowbytes);
    void *ctx;
    void (*progress)(void *ctx, int done, int total);   /* in bands */
    void (*log)(void *ctx, const char *msg);             /* may be NULL */
} PDFOUT;
int  pdf_export(DOC *d, const char *path, PDFOUT *o);

/* prt.c - a page to a printer as bitmap bands at its resolution */
typedef struct {
    long w, h;                  /* printable area in printer pixels (0: any) */
    long pixw, pixh;            /* printer pixel size, microns             */
    long band;                  /* printer rows at a time                  */
    int (*render)(void *ctx, DOC *d, int page, int color,
                  long y0, long h, long w, unsigned char *bits, long rowbytes);
    int (*out)(void *ctx, long y0, long h, long w, const unsigned char *bits, long rowbytes);
    void (*progress)(void *ctx, int done, int total);   /* rows of the page */
    void *ctx;
    int  scale;                 /* set by print_page: render at this scale */
} PRTOUT;
int  print_page(DOC *d, int page, PRTOUT *o);
unsigned page_colours(DOC *d, int page);

/* spell.c */
#define MAX_UDICT 4
#define WORDMAX   32
extern int  spell_nuser, spell_active;
extern char spell_upath[MAX_UDICT][256];
int  spell_load_main(const char *path);
void spell_unload_main(void);
const char *spell_dic_name(int lang);   /* EDWORD.DIC, EDWORDDE.DIC... */
extern int spell_lang;                  /* 0 English, 1 German, 2 Spanish, 3 Italian, 4 French */
int  spell_main_loaded(void);
void spell_free_all(void);
int  spell_user_load(const char *path, int create);
void spell_user_unload(int slot);
int  spell_user_add(const char *word);
int  spell_check(const char *word);
void spell_ignore(const char *word);
int  spell_suggest(const char *word, char out[][WORDMAX], int max);
long spell_next(DOC *d, long from, long *end, char *word);

/* thes.c - thesaurus */
#define THS_SENSES 10
#define THS_SYN    10
typedef struct {
    char word[WORDMAX];                 /* the headword that was found     */
    int  nsense;
    char pos[THS_SENSES];               /* 'n' 'v' 'a' 'r'                 */
    char gloss[THS_SENSES][40];
    int  nsyn[THS_SENSES];
    char syn[THS_SENSES][THS_SYN][WORDMAX];
} THSRES;
int  thes_open(const char *path);
int  thes_ready(void);
void thes_close(void);
int  thes_lookup(const char *word, THSRES *r);

#endif
