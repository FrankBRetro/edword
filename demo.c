/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * demo.c - the screenshot script for the manual ("make EDDEMO=1")
 *
 * At start-up EdWord looks for DEMO.SCR in its folder. Each timer tick
 * runs one line of it; screenshots are written to SHOTS\NAME.SCR as a
 * small header and the raw screen memory. Lines:
 *
 *   OPEN file            open an RTF (or .TXT) document
 *   CMD name             a menu command (see cmd_by_name in gem.c)
 *   SHOT name            screenshot once the windows are redrawn
 *   DSHOT name           screenshot of the next dialog or popup
 *   DSHOT2 name          ... and of the dialog that the next one opens
 *   DSHOTAT k name       ... of the kth dialog from now (1 the next)
 *   DFILE name           the next file selector answers name (in EdWord's folder)
 *   SAVE name            save the top document as RTF
 *   PAGETOP n y          scroll to y pixels below the top of page n
 *   DEXIT n              the next dialog leaves by its nth exit button
 *                        (0: the default one), a popup with item n; more
 *                        DEXIT lines: the dialogs after it, in turn
 *   MENU n name          drop down menu n and take a screenshot
 *   TYPE text            type text into the top document
 *   KEY scan ascii shift a key press
 *   FIND text            select the next occurrence of text
 *   GO START|END|n       move the caret
 *   WIN x y w h          move and size the top document window
 *   TOOLWIN x y          move the top tool window
 *   ZOOM n / VIEW n      zoom percent; view 0 page, 1 split, 2 codes
 *   SELOBJ n             select picture or drawing n (-1: none)
 *   PICTURE file         insert a GEM IMG picture
 *   SHAPE t x1 y1 x2 y2 fill lw colour   add a shape to the selected drawing
 *   FINDSET what|with    fill in the Find window
 *   LONGPRESS word       hold the button on the word (spelling popup)
 *   CLICK x y n          a click at screen x y
 *   REDRAW               redraw the whole screen
 *   THEMEDOC n           give the top window colour theme n of its own
 *   WAIT n               do nothing for n ticks
 *   PRTIME c             time printing page 1 at 300 dpi (c: in colour), in the log
 *   STROKE x y x y ...   a freehand stroke through those screen points (drawing window)
 *   QUIT
 *
 * The test build ("make EDTEST=1", EDWTEST.PRG) runs the same way, in
 * English, from TEST.SCR or, without one, the script built into it below,
 * and writes what happened to EDTEST.LOG; its files go in EDTEST\. More
 * lines for it:
 *
 *   LOG text             a line in the log
 *   ALLMENUS             every enabled menu command on the top document
 *   KEYS text            type text as keys (\n: Return), in any view
 *   MARK                 remember the top document's length
 *   CHECK TEXT s | NOTEXT s | LEN n | SAME | DOCS n | TABLES n | OBJS n |
 *         FILE name | VIEW n          pass or fail, in the log
 *   EXPORT fmt name      export the top document (0 RTF, 1 text, 2 Markdown,
 *                        3 1st Word Plus, 4 Protext, 5 PDF)
 *   MKDIR name           make a folder in EdWord's
 *   CLOSEALL             close every document, unsaved
 */
#ifdef EDDEMO
#include <gem.h>
#include <osbind.h>
#include <mint/falcon.h>
#include <mint/cookie.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gemui.h"

int demo_on;
static char  *script, *line_p;
#define NPEND 8
static char   dshot[NPEND][16];           /* the pictures of the next dialogs: this one's, the one after... */
static int    dexit[NPEND], ndexit, wait_ticks;   /* how the next dialogs are left, in turn */
static char   dfile[40];

/* the file selector's answer: the name from DFILE, or cancel */
int demo_file(char *out)
{
    if (!dfile[0]) return 0;
    sprintf(out, "%s%s", progdir, dfile);
    dfile[0] = 0;
    return 1;
}

/* the screen is system memory: under MiNT with memory protection it is
 * copied in supervisor mode */
static void *shot_src, *shot_dst;
static long  shot_len;
static long shot_copy(void) { memcpy(shot_dst, shot_src, shot_len); return 0; }

static void shot_file(const char *name)
{
    char path[300];
    long fh, n, vdo = 0;
    short hdr[4], ext[57];
    int planes, i;
    void *buf;
    vq_extnd(vhandle, 1, ext);                   /* Getrez() can't tell a Falcon's modes */
    planes = ext[4] > 0 ? ext[4] : 1;
    sprintf(path, "%sSHOTS", progdir);
    (void)Dcreate(path);
    sprintf(path, "%sSHOTS\\%s.SCR", progdir, name);
    n = (long)scr_w * scr_h / 8 * planes;
    buf = malloc(n);
    if (!buf) return;
    shot_src = Physbase(); shot_dst = buf; shot_len = n;
    Supexec(shot_copy);
    fh = Fcreate(path, 0);
    if (fh < 0) { free(buf); return; }
    hdr[0] = 0x4544;  /* "ED" */
    hdr[1] = scr_w; hdr[2] = scr_h; hdr[3] = (short)planes;
    Fwrite((short)fh, sizeof hdr, hdr);
    Fwrite((short)fh, n, buf);
    /* with a palette, its colours after the pixels: "PAL", then 0x00RRGGBB
     * for each pixel value (the Falcon's own, or the ST's 16) */
    if (planes >= 2 && planes <= 8) {
        long pal[256];
        int np = 1 << planes;
        (void)Getcookie(0x5F56444FL, &vdo);      /* '_VDO' */
        if ((vdo >> 16) >= 3) VgetRGB(0, np, pal);
        else
            for (i = 0; i < np && i < 16; i++) {
                int c = Setcolor(i, -1), r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
                r = ((r & 7) << 1) | (r >> 3); g = ((g & 7) << 1) | (g >> 3); b = ((b & 7) << 1) | (b >> 3);   /* STE order */
                pal[i] = ((long)(r * 17) << 16) | ((long)(g * 17) << 8) | (b * 17);
            }
        Fwrite((short)fh, 4, "PAL");
        Fwrite((short)fh, 4L * np, pal);
    }
    Fclose((short)fh);
    free(buf);
}

void demo_shot(const char *name)
{
    graf_mouse(M_OFF, NULL);
    shot_file(name);
    graf_mouse(M_ON, NULL);
}

#ifdef EDTEST
#include <stdarg.h>
/* ---- the test build ------------------------------------------------- */
static const char test_script[] =
    "LOG EdWord test run: menus, editing in each view, saving and loading\n"
    "MKDIR EDTEST\n"
    "CMD NEW\n"
    "CHECK DOCS 1\n"
    "TYPE The quick brown fox jumps over the lazy dog.\\nA second paragraph with a mispelt wurd in it.\\n\n"
    "CHECK TEXT quick brown\n"
    "MARK\n"
    "LOG -- page view\n"
    "VIEW 0\n"
    "CHECK VIEW 0\n"
    "GO END\n"
    "KEYS Typed in the page view.\\n\n"
    "CHECK TEXT Typed in the page view.\n"
    "KEY 0e 8 0\n"
    "KEY 0e 8 0\n"
    "CHECK NOTEXT Typed in the page view.\n"
    "FIND quick\n"
    "CMD BOLD\n"
    "CMD ITALIC\n"
    "CMD UNDER\n"
    "CMD JCENTRE\n"
    "CMD UNDO\n"
    "CMD REDO\n"
    "CMD ZOOMPAGE\n"
    "CMD ZOOMFIT\n"
    "CMD ZOOMPAGE\n"
    "CHECK VIEW 0\n"
    "LOG -- split view\n"
    "VIEW 1\n"
    "CHECK VIEW 1\n"
    "GO END\n"
    "KEYS Typed in the split view.\\n\n"
    "CHECK TEXT Typed in the split view.\n"
    "LOG -- quick edit\n"
    "VIEW 2\n"
    "CHECK VIEW 2\n"
    "GO END\n"
    "KEYS Typed in quick edit.\\n\n"
    "VIEW 0\n"
    "CHECK TEXT Typed in quick edit.\n"
    "LOG -- tables, pictures, clipboard\n"
    "GO END\n"
    "CMD TBLINS\n"
    "CHECK TABLES 1\n"
    "KEYS 12\n"
    "CMD ROWINS\n"
    "CMD COLINS\n"
    "CMD TBLPROP\n"
    "CMD COLDEL\n"
    "CMD ROWDEL\n"
    "CHECK TABLES 1\n"
    "GO END\n"
    "CMD SELALL\n"
    "CMD COPY\n"
    "GO END\n"
    "CMD PASTE\n"
    "CHECK LEN 200\n"
    "LOG -- every menu command\n"
    "GO START\n"
    "ALLMENUS\n"
    "LOG -- saving and loading\n"
    "VIEW 0\n"
    "MARK\n"
    "DFILE EDTEST\\TEST.EWR\n"
    "CMD SAVEAS\n"
    "CHECK FILE EDTEST\\TEST.EWR\n"
    "EXPORT 0 EDTEST\\TEST.RTF\n"
    "EXPORT 1 EDTEST\\TEST.TXT\n"
    "EXPORT 2 EDTEST\\TEST.MD\n"
    "EXPORT 3 EDTEST\\TEST.DOC\n"
    "EXPORT 4 EDTEST\\TEST.PRO\n"
    "EXPORT 5 EDTEST\\TEST.PDF\n"
    "CLOSEALL\n"
    "CHECK DOCS 0\n"
    "OPEN EDTEST\\TEST.EWR\n"
    "CHECK DOCS 1\n"
    "CHECK SAME\n"
    "CHECK TABLES 1\n"
    "CHECK TEXT Typed in the split view.\n"
    "OPEN EDTEST\\TEST.RTF\n"
    "CHECK DOCS 2\n"
    "CHECK TEXT Typed in quick edit.\n"
    "OPEN EDTEST\\TEST.TXT\n"
    "CHECK TEXT quick brown\n"
    "CLOSEALL\n"
    "QUIT\n";

static char *next_word(char **p);
static int  npass, nfail;
static long marked = -1;

void test_note(const char *fmt, ...)
{
    char path[300];
    FILE *f;
    va_list ap;
    sprintf(path, "%sEDTEST.LOG", progdir);
    f = fopen(path, "a");                 /* a line at a time: a crash leaves the last one */
    if (!f) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputs("\r\n", f);
    fclose(f);
}

static void result(int ok, const char *what)
{
    if (ok) npass++; else nfail++;
    test_note("%s %s", ok ? "pass   " : "FAIL   ", what);
}

void test_cmd_begin(const char *name) { test_note("run     %s", name); }

/* after each command: a document still there and laid out */
void test_cmd_end(const char *name)
{
    DOC *d = top_doc();
    char t[80];
    sprintf(t, "%s", name);
    if (!d) { result(0, t); test_note("        no document after it"); return; }
    if (d->len < 0 || d->cursor < 0 || d->cursor > d->len || (d->len > 0 && d->nlines <= 0)) {
        result(0, t);
        test_note("        len %ld, cursor %ld, %ld lines", d->len, d->cursor, d->nlines);
        return;
    }
    sprintf(t, "%s  (%ld characters, %d pages)", name, d->len, doc_npages(d));
    result(1, t);
}

static int ndocs(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_DOCS; i++) if (DOC_USED(i)) n++;
    return n;
}

static int has_text(DOC *d, const char *s)
{
    long e;
    return d && find_next(d, 0, s, 0, &e) >= 0;
}

static void check(char *s)
{
    char *what = next_word(&s), t[120];
    DOC *d = top_doc();
    int ok = 0;
    if (!strcmp(what, "TEXT")) ok = has_text(d, s);
    else if (!strcmp(what, "NOTEXT")) ok = d && !has_text(d, s);
    else if (!strcmp(what, "LEN")) ok = d && d->len >= atol(s);
    else if (!strcmp(what, "SAME")) ok = d && d->len == marked;
    else if (!strcmp(what, "DOCS")) ok = ndocs() == atoi(s);
    else if (!strcmp(what, "TABLES")) ok = d && d->a.ntbl >= atoi(s);
    else if (!strcmp(what, "OBJS")) ok = d && d->a.nobj >= atoi(s);
    else if (!strcmp(what, "VIEW")) ok = d && d->view == atoi(s);
    else if (!strcmp(what, "FILE")) {
        char path[300];
        long n;
        unsigned char *b;
        sprintf(path, "%s%s", progdir, s);
        b = load_file(path, &n);
        ok = b && n > 0;
        free(b);
        sprintf(t, "%s %s (%ld bytes)", what, s, ok ? n : 0L);
        result(ok, t);
        return;
    }
    sprintf(t, "%s %.80s", what, s);
    if (d && (!strcmp(what, "LEN") || !strcmp(what, "SAME")))
        sprintf(t + strlen(t), " (%ld characters, marked %ld)", d->len, marked);
    result(ok, t);
}

static void keys(const char *s)
{
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] == 'n') { handle_key(0, 0x1C0D); s++; }
        else handle_key(0, (short)(unsigned char)*s);
        pump_redraws();
    }
}

static void close_all(void)
{
    int i;
    tools_close_all();
    for (i = 0; i < MAX_DOCS; i++)
        if (DOC_USED(i)) { docs[i]->dirty = 0; docs[i]->cv.dirty = 0; close_doc(docs[i]); }
    pump_redraws();
}

static void test_line(char *cmd, char *s)
{
    DOC *d = top_doc();
    if (!strcmp(cmd, "LOG")) test_note("%s", s);
    else if (!strcmp(cmd, "CHECK")) check(s);
    else if (!strcmp(cmd, "KEYS")) keys(s);
    else if (!strcmp(cmd, "MARK")) marked = d ? d->len : -1;
    else if (!strcmp(cmd, "MKDIR")) { char path[300]; sprintf(path, "%s%s", progdir, s); (void)Dcreate(path); }
    else if (!strcmp(cmd, "CLOSEALL")) close_all();
    else if (!strcmp(cmd, "ALLMENUS")) {
        /* not these: they'd end the run, print on paper, or need the user */
        static const short skip[] = { C_QUIT, C_CLOSE, C_PRINT, C_NEW, C_TEMPLATE, C_ICONIFY, C_ICONALL, 0 };
        test_all_menus(skip);
    } else if (!strcmp(cmd, "EXPORT") && d) {
        char name[40], t[80];
        int fmt = atoi(next_word(&s));
        strncpy(dfile, s, 39); dfile[39] = 0;
        sprintf(t, "export %d %s", fmt, s);
        test_note("run     %s", t);
        result(export_doc(d, fmt, 1, CS_ATARI, name), t);
    }
}

static void test_end(void)
{
    test_note("%d passed, %d failed%s", npass, nfail, nfail ? "" : ": all well");
    test_note("DONE");
}
#endif

int demo_start(void)
{
    char path[300];
    long n;
#ifdef EDTEST
    sprintf(path, "%sEDTEST.LOG", progdir);
    remove(path);
    test_note("EdWord %s test build", ED_VERSION);
    sprintf(path, "%sTEST.SCR", progdir);
    script = (char *)load_file(path, &n);
    if (!script) {                            /* the script built in */
        n = (long)strlen(test_script);
        script = (char *)malloc(n + 1);
        if (!script) return 0;
        memcpy(script, test_script, n);
    }
#else
    sprintf(path, "%sDEMO.SCR", progdir);
    script = (char *)load_file(path, &n);
    if (!script) return 0;
#endif
    script = (char *)realloc(script, n + 1);
    script[n] = 0;
    line_p = script;
    /* the pointer out of the way */
    {
        short x, y, b, k;
        graf_mkstate(&x, &y, &b, &k);
    }
    return 1;
}

/* a dialog has been drawn: take its picture and say how to leave it */
int demo_dialog(OBJECT *tree, short x, short y, short w, short h)
{
    short i, def = -1, nexit = 0, want = (short)(ndexit ? dexit[0] : -1);
    (void)x; (void)y; (void)w; (void)h;
    if (dshot[0][0]) demo_shot(dshot[0]);
    memmove(dshot[0], dshot[1], sizeof dshot - sizeof dshot[0]);
    dshot[NPEND - 1][0] = 0;
    if (ndexit) { memmove(dexit, dexit + 1, sizeof dexit - sizeof dexit[0]); ndexit--; }
    for (i = 0; ; i++) {
        if ((tree[i].ob_flags & OF_DEFAULT) && want != -2) def = i;
        if (tree[i].ob_flags & OF_EXIT) {
            nexit++;
            if (want > 0 && nexit == want) def = i, want = -2;
        }
        if (tree[i].ob_flags & OF_LASTOB) break;
    }
    if (nexit == 0) return want > 0 ? want + 1 : 0;    /* a popup: item, or none */
    if (def < 0) {
        for (i = 0; ; i++) { if (tree[i].ob_flags & OF_EXIT) def = i; if (tree[i].ob_flags & OF_LASTOB) break; }
    }
    return def;
}

int demo_alert(void)
{
    if (dshot[0][0]) dshot[0][0] = 0;
    return 1;
}

static char *next_word(char **p)
{
    char *s = *p, *w;
    while (*s == ' ') s++;
    w = s;
    while (*s && *s != ' ') s++;
    if (*s) *s++ = 0;
    *p = s;
    return w;
}

static void menu_shot(int i, const char *name)
{
    OBJECT *t = menu_tree();
    short title = menu_title_obj(i), box = menu_box_obj(i), x, y;
    if (title < 0 || box < 0) return;
    pump_redraws();
    wind_update(BEG_UPDATE);
    objc_offset(t, box, &x, &y);
    form_dial(FMD_START, 0, 0, 0, 0, x - 2, y - 2, t[box].ob_width + 4, t[box].ob_height + 4);
    t[title].ob_state |= OS_SELECTED;
    objc_draw(t, 1, 8, 0, 0, scr_w, hchar + 3);
    objc_draw(t, box, 1, x - 1, y - 1, t[box].ob_width + 2, t[box].ob_height + 2);
    demo_shot(name);
    t[title].ob_state &= ~OS_SELECTED;
    objc_draw(t, 1, 8, 0, 0, scr_w, hchar + 3);
    form_dial(FMD_FINISH, 0, 0, 0, 0, x - 2, y - 2, t[box].ob_width + 4, t[box].ob_height + 4);
    wind_update(END_UPDATE);
}

static void type_text(DOC *d, const char *s)
{
    long from = d->cursor;
    while (*s) {
        const char *e = strstr(s, "\\n");
        char buf[200];
        long n = e ? e - s : (long)strlen(s);
        if (n > 199) n = 199;
        memcpy(buf, s, n);
        buf[n] = 0;
        doc_type_str(d, buf);
        if (!e) break;
        {
            CELL c = d->ins;
            c.ch = '\n';
            doc_type(d, &c, 1);
        }
        s = e + 2;
    }
    after_edit(d, from);
}

/* the screen position of document position pos */
static int pos_on_screen(DOC *d, long pos, short *x, short *y)
{
    GRECT tb, tp, cp;
    long li = doc_find_line(d, pos);
    LINE *ln = &d->lines[li];
    get_panes(d, &tb, &tp, &cp);
    *x = (short)(tp.g_x - d->scroll_x + view_page_left(d, &tp) + zx(d, doc_x_of(d, li, pos)));
    *y = (short)(tp.g_y - d->scroll_y + view_page_top(d, ln->page) + zx(d, ln->py + ln->asc));
    return 1;
}

static void run_line(char *s)
{
    char *cmd = next_word(&s);
    DOC *d = top_doc();
    edlog("demo: %s %s", cmd, s);
#ifdef EDTEST
    if (strcmp(cmd, "LOG") && strcmp(cmd, "CHECK") && strcmp(cmd, "ALLMENUS")) test_note("        %s %s", cmd, s);
    test_line(cmd, s);
#endif
    if (!strcmp(cmd, "OPEN")) { char path[300]; sprintf(path, "%s%s", progdir, s); open_path(path, strstr(s, ".TXT") != NULL); }
    else if (!strcmp(cmd, "CMD")) { int c = cmd_by_name(s); if (c) do_cmd(c); }
    else if (!strcmp(cmd, "LIVE")) { int c = cmd_by_name(s); demo_on = 0; if (c) do_cmd(c); }   /* as the user would: the script ends */
    else if (!strcmp(cmd, "SHOT")) { pump_redraws(); evnt_timer(50L); pump_redraws(); demo_shot(s); }
    else if (!strcmp(cmd, "DSHOT")) { strncpy(dshot[0], s, 15); dshot[0][15] = 0; }
    else if (!strcmp(cmd, "DSHOT2")) { strncpy(dshot[1], s, 15); dshot[1][15] = 0; }
    else if (!strcmp(cmd, "DSHOTAT")) {
        int k = atoi(next_word(&s)) - 1;
        if (k >= 0 && k < NPEND) { strncpy(dshot[k], s, 15); dshot[k][15] = 0; }
    }
    else if (!strcmp(cmd, "DEXIT")) { if (ndexit < NPEND) dexit[ndexit++] = atoi(s); }
    else if (!strcmp(cmd, "DRAWSEL")) draw_select_shape(atoi(s));
    else if (!strcmp(cmd, "DFILE")) { strncpy(dfile, s, 39); dfile[39] = 0; }
    else if (!strcmp(cmd, "MENU")) { int i = atoi(next_word(&s)); menu_shot(i, s); }
    else if (!strcmp(cmd, "WAIT")) wait_ticks = atoi(s);
    else if (!strcmp(cmd, "TYPETIME")) {                 /* type n keys as the user would; the time to TYPETIME.TXT */
        int n = atoi(s), k;
        clock_t t0 = clock();
        char path[300];
        FILE *f;
        for (k = 0; k < n; k++) {
            static const char txt[] = "The quick brown fox jumps over the lazy dog. ";
            int c = txt[k % (int)(sizeof txt - 1)];
            handle_key(0, (short)c);
            pump_redraws();
        }
        sprintf(path, "%sTYPETIME.TXT", progdir);
        f = fopen(path, "a");
        if (f) { fprintf(f, "%d keys: %ld ticks\r\n", n, (long)(clock() - t0)); fclose(f); }
    }
    else if (!strcmp(cmd, "PRTIME")) { void print_timing(DOC *, int); print_timing(d, atoi(s)); }
    else if (!strcmp(cmd, "QUIT")) {
        char path[300];
        long fh;
#ifdef EDTEST
        test_end();
#endif
        sprintf(path, "%sSHOTS", progdir);
        (void)Dcreate(path);
        sprintf(path, "%sSHOTS\\DONE", progdir);
        fh = Fcreate(path, 0);
        if (fh >= 0) Fclose((short)fh);
        quit_now = 1;
    }
    else if (!strcmp(cmd, "THEMEDOC") && d) {    /* THEMEDOC n: the top window's own theme n */
        unsigned char g[TH_NROLES][3];
        int gid = theme_id;
        memcpy(g, theme_rgb, sizeof g);
        theme_pick(atoi(s));
        theme_set_window(d->slot, theme_id, theme_rgb);
        theme_id = gid;
        memcpy(theme_rgb, g, sizeof g);
        form_dial(FMD_FINISH, 0, 0, 0, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h);
    }
    else if (!strcmp(cmd, "REDRAW")) { form_dial(FMD_FINISH, 0, 0, 0, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h); }
    else if (!strcmp(cmd, "KEY")) {
        int scan = (int)strtol(next_word(&s), NULL, 16), asc = atoi(next_word(&s)), ks = atoi(s);
        handle_key((short)ks, (short)((scan << 8) | asc));
    } else if (!strcmp(cmd, "CLICK")) {
        int x = atoi(next_word(&s)), y = atoi(next_word(&s)), n = atoi(s);
        handle_click((short)x, (short)y, 0, (short)(n ? n : 1));
    } else if (!d) return;
    else if (!strcmp(cmd, "TYPE")) type_text(d, s);
    else if (!strcmp(cmd, "SAVE")) { char path[300]; sprintf(path, "%s%s", progdir, s); rtf_export(d, path); }
    else if (!strcmp(cmd, "PAGETOP")) {
        int pg = atoi(next_word(&s));
        d->scroll_y = view_page_top(d, pg) - PGAP + atoi(s);
        clamp_scroll(d);
        d->vsz = d->vpos = -1;
        refresh_text(d);
        set_sliders(d);
    }
    else if (!strcmp(cmd, "FIND")) {
        long e, p = find_next(d, d->cursor, s, 0, &e);
        if (p < 0) p = find_next(d, 0, s, 0, &e);
        if (p >= 0) select_range(d, p, e);
    } else if (!strcmp(cmd, "GO")) {
        long p = !strcmp(s, "END") ? d->len : !strcmp(s, "START") ? 0 : atol(s);
        select_range(d, -1, p);
    } else if (!strcmp(cmd, "WIN")) {
        short x = (short)atoi(next_word(&s)), y = (short)atoi(next_word(&s)), w = (short)atoi(next_word(&s)), h = (short)atoi(s);
        wind_set(d->wh, WF_CURRXYWH, x, y, w, h);
        win_resized(d);
    } else if (!strcmp(cmd, "TOOLWIN")) {
        short h, x0, y0, w0, h0, d1;
        short x = (short)atoi(next_word(&s)), y = (short)atoi(s);
        wind_get(0, WF_TOP, &h, &d1, &d1, &d1);
        wind_get(h, WF_CURRXYWH, &x0, &y0, &w0, &h0);
        if (h != d->wh) {
            short m[8];
            m[0] = WM_MOVED; m[3] = h; m[4] = x; m[5] = y; m[6] = w0; m[7] = h0;
            tool_route_msg(m);
        }
    } else if (!strcmp(cmd, "ZOOM")) set_zoom(d, atoi(s));
    else if (!strcmp(cmd, "VIEW")) set_view(d, atoi(s));
    else if (!strcmp(cmd, "SELOBJ")) obj_select(d, atoi(s));
    else if (!strcmp(cmd, "PICTURE")) { char path[300]; sprintf(path, "%s%s", progdir, s); picture_insert(d, path); }
    else if (!strcmp(cmd, "SHAPE")) {
        OBJ *o = d->selobj >= 0 ? &d->a.objs[d->selobj] : NULL;
        if (o && o->type == OB_DRAW && o->nsh < MAX_SHAPES) {
            SHAPE *sh = &o->sh[o->nsh++];
            sh->type = (unsigned char)atoi(next_word(&s));
            sh->x1 = (short)atoi(next_word(&s)); sh->y1 = (short)atoi(next_word(&s));
            sh->x2 = (short)atoi(next_word(&s)); sh->y2 = (short)atoi(next_word(&s));
            sh->fill = (unsigned char)atoi(next_word(&s)); sh->lw = (unsigned char)atoi(next_word(&s));
            sh->color = (unsigned char)atoi(s);
            doc_changed(d);
            form_dial(FMD_FINISH, 0, 0, 0, 0, desk.g_x, desk.g_y, desk.g_w, desk.g_h);
        }
    } else if (!strcmp(cmd, "STROKE")) {          /* STROKE x y x y ...: freehand in the drawing window */
        static short xy[200];
        void draw_demo_stroke(int t, const short *xy, int n);
        int n = 0;
        while (*s && n < 100) { xy[2 * n] = (short)atoi(next_word(&s)); xy[2 * n + 1] = (short)atoi(next_word(&s)); n++; }
        draw_demo_stroke(6, xy, n);
    } else if (!strcmp(cmd, "FINDSET")) {
        char *bar = strchr(s, '|');
        if (bar) *bar++ = 0;
        find_set(s, bar ? bar : "");
    } else if (!strcmp(cmd, "LONGPRESS")) {
        long e, p = find_next(d, 0, s, FIND_WORD, &e);
        short x, y;
        spell_ensure(1);
        if (p >= 0 && pos_on_screen(d, p, &x, &y)) {
            select_range(d, p, e);
            pump_redraws();
            live_popup(d, p, e, x, y);
        }
    }
}

/* run the next line; 0 at the end */
int demo_tick(void)
{
    char *s, *e;
    if (!script) return 0;
    if (wait_ticks > 0) { wait_ticks--; return 1; }
    for (;;) {
        if (!*line_p) return 0;
        s = line_p;
        e = strchr(s, '\n');
        if (e) { *e = 0; line_p = e + 1; } else line_p = s + strlen(s);
        if ((e = strchr(s, '\r')) != NULL) *e = 0;
        if (*s && *s != '#') break;
    }
    run_line(s);
    return !quit_now;
}
#endif
