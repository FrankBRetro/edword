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
 *   DFILE name           the next file selector answers name (in EdWord's folder)
 *   SAVE name            save the top document as RTF
 *   PAGETOP n y          scroll to y pixels below the top of page n
 *   DEXIT n              the next dialog leaves by its nth exit button
 *                        (0: the default one), a popup with item n
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
 *   WAIT n               do nothing for n ticks
 *   PRTIME c             time printing page 1 at 300 dpi (c: in colour), in the log
 *   STROKE x y x y ...   a freehand stroke through those screen points (drawing window)
 *   QUIT
 */
#ifdef EDDEMO
#include <gem.h>
#include <osbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gemui.h"

int demo_on;
static char  *script, *line_p;
static char   dshot[16], dshot2[16];      /* this dialog's picture, the one after it */
static int    dexit = -1, wait_ticks;
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
    long fh, n;
    short hdr[4];
    short rez = Getrez();
    int planes = rez == 2 || rez == 6 ? 1 : rez == 1 ? 2 : 4;
    void *buf;
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
    Fclose((short)fh);
    free(buf);
}

void demo_shot(const char *name)
{
    graf_mouse(M_OFF, NULL);
    shot_file(name);
    graf_mouse(M_ON, NULL);
}

int demo_start(void)
{
    char path[300];
    long n;
    sprintf(path, "%sDEMO.SCR", progdir);
    script = (char *)load_file(path, &n);
    if (!script) return 0;
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
    short i, def = -1, nexit = 0, want = (short)dexit;
    (void)x; (void)y; (void)w; (void)h;
    if (dshot[0]) { demo_shot(dshot); dshot[0] = 0; }
    strcpy(dshot, dshot2);
    dshot2[0] = 0;
    dexit = -1;
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
    if (dshot[0]) dshot[0] = 0;
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
    if (!strcmp(cmd, "OPEN")) { char path[300]; sprintf(path, "%s%s", progdir, s); open_path(path, strstr(s, ".TXT") != NULL); }
    else if (!strcmp(cmd, "CMD")) { int c = cmd_by_name(s); if (c) do_cmd(c); }
    else if (!strcmp(cmd, "SHOT")) { pump_redraws(); evnt_timer(50L); pump_redraws(); demo_shot(s); }
    else if (!strcmp(cmd, "DSHOT")) { strncpy(dshot, s, 15); dshot[15] = 0; }
    else if (!strcmp(cmd, "DSHOT2")) { strncpy(dshot2, s, 15); dshot2[15] = 0; }
    else if (!strcmp(cmd, "DEXIT")) dexit = atoi(s);
    else if (!strcmp(cmd, "DRAWSEL")) draw_select_shape(atoi(s));
    else if (!strcmp(cmd, "DFILE")) { strncpy(dfile, s, 39); dfile[39] = 0; }
    else if (!strcmp(cmd, "MENU")) { int i = atoi(next_word(&s)); menu_shot(i, s); }
    else if (!strcmp(cmd, "WAIT")) wait_ticks = atoi(s);
    else if (!strcmp(cmd, "PRTIME")) { void print_timing(DOC *, int); print_timing(d, atoi(s)); }
    else if (!strcmp(cmd, "QUIT")) {
        char path[300];
        long fh;
        sprintf(path, "%sSHOTS", progdir);
        (void)Dcreate(path);
        sprintf(path, "%sSHOTS\\DONE", progdir);
        fh = Fcreate(path, 0);
        if (fh >= 0) Fclose((short)fh);
        quit_now = 1;
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
