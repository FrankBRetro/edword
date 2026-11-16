/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemfsel.c - EdWord's own file selector, in a window of its own
 *
 * Laid out as TOS's: the directory (path and pattern) and the selection
 * at the top, the drives, the list of folders and files, OK and Cancel.
 * It is a window like the others: while it is open the documents, their
 * menus and the other windows go on working (its caller waits for the
 * answer, as with the system's selector). Used when Preferences says so;
 * until it's chosen, the system's when a better one than TOS's is
 * installed.
 */
#include <gem.h>
#include <osbind.h>
#include <mint/cookie.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gemui.h"

#ifdef PCGEM
int fsel_window(char *dir, char *name, const char *title) { (void)dir; (void)name; (void)title; return -1; }
void fsel_follow(DOC *d) { (void)d; }
void fsel_top(DOC *d) { (void)d; }
int fsel_for(DOC *d) { (void)d; return 0; }
#else

#define FKIND   (NAME | CLOSER | MOVER | SIZER)  /* on its own; a sheet has only the sizer */
#define MAXENT  600
#define NAMEW   13              /* "FILENAME.EXT" and the folder mark */

typedef struct { char name[14]; char dir; long size; } FENT;

static struct {
    short wh;
    FENT *e;
    int   n, top, sel, rows;
    char  path[200], pat[20], name[64];
    int   field;                /* the field typed into: 0 directory, 1 selection */
    int   done;                 /* 1 OK, 2 Cancel */
    char  title[60];
    long  drives;
    DOC  *owner;                /* the document it is for: they move together */
    short tline;                /* a sheet: its title, in the window (no title bar) */
    short minw, minh;
} fs;

/* The layout, in the work area, as TOS 4's item selector: the directory
 * across the top; under it on the left the list in its box (a bar with
 * the closer, which goes up a folder, and the pattern; a scroll bar of
 * its own), on the right the selection, the drives A to U, OK and Cancel. */
#define LROWS 10                /* the list's lines when it opens */
#define RIGHTW 23               /* the right-hand part, characters */
typedef struct { GRECT w, dirf, self, list, bar, clo, items, sb, up, dn, track, drv, ok, can; short dw, dh; } FLAY;

static void lay(FLAY *L)
{
    short x, y, w, h, cw = wchar, ch = hchar, rx;
    wind_get(fs.wh, WF_WORKXYWH, &x, &y, &w, &h);
    L->w.g_x = x; L->w.g_y = y; L->w.g_w = w; L->w.g_h = h;
    /* Directory: and its field, the whole width */
    L->dirf.g_x = (short)(x + cw); L->dirf.g_y = (short)(y + fs.tline + ch + ch / 2 + 2);
    L->dirf.g_w = (short)(w - 3 * cw); L->dirf.g_h = (short)(ch + 4);
    /* the list box */
    L->list.g_x = (short)(x + cw); L->list.g_y = (short)(L->dirf.g_y + L->dirf.g_h + ch);
    /* (it takes what the window has to spare: the window can be sized) */
    /* (the right-hand part is laid out from the window's right edge, so it
     * always fits whatever the AES takes for the border and sizer) */
    L->list.g_w = (short)(w - 5 * cw - RIGHTW * cw);
    if (L->list.g_w < 10 * cw) L->list.g_w = (short)(10 * cw);
    L->list.g_h = (short)(y + h - ch - 6 - L->list.g_y);        /* room under it, clear of the sizer */
    if (L->list.g_h < 6 * ch) L->list.g_h = (short)(6 * ch);
    L->bar = L->list; L->bar.g_h = (short)(ch + 4);
    L->clo = L->bar; L->clo.g_w = (short)(2 * cw + 2);
    L->sb.g_w = (short)(2 * cw); L->sb.g_x = (short)(L->list.g_x + L->list.g_w - L->sb.g_w);
    L->sb.g_y = (short)(L->bar.g_y + L->bar.g_h - 1); L->sb.g_h = (short)(L->list.g_y + L->list.g_h - L->sb.g_y);
    L->up = L->sb; L->up.g_h = (short)(ch + 2);
    L->dn = L->up; L->dn.g_y = (short)(L->sb.g_y + L->sb.g_h - L->up.g_h);
    L->track = L->sb; L->track.g_y = (short)(L->up.g_y + L->up.g_h - 1);
    L->track.g_h = (short)(L->dn.g_y - L->track.g_y + 1);
    L->items.g_x = L->list.g_x; L->items.g_y = L->sb.g_y;
    L->items.g_w = (short)(L->list.g_w - L->sb.g_w + 1); L->items.g_h = L->sb.g_h;
    /* the right: Selection:, the drives, OK and Cancel */
    rx = (short)(x + w - 2 * cw - RIGHTW * cw);              /* a margin on the right too */
    L->self.g_x = rx; L->self.g_y = (short)(L->list.g_y + ch + 2);
    L->self.g_w = (short)(RIGHTW * cw); L->self.g_h = (short)(ch + 4);
    L->dw = (short)(L->self.g_w / 7);
    L->dh = (short)(ch + 12);
    L->drv.g_x = rx; L->drv.g_y = (short)(L->self.g_y + L->self.g_h + ch / 2);
    L->drv.g_w = (short)(7 * L->dw); L->drv.g_h = (short)(3 * L->dh);
    L->can.g_w = L->ok.g_w = (short)(L->self.g_w / 2 - cw / 2);
    L->ok.g_h = L->can.g_h = (short)(ch + 6);
    L->ok.g_x = rx; L->can.g_x = (short)(rx + L->self.g_w - L->can.g_w);
    L->ok.g_y = L->can.g_y = (short)(L->list.g_y + L->list.g_h - L->ok.g_h - 2);
    fs.rows = (L->items.g_h - 4) / ch;
    if (fs.rows < 1) fs.rows = 1;
}

static int in_g(const GRECT *r, short x, short y)
{
    return x >= r->g_x && y >= r->g_y && x < r->g_x + r->g_w && y < r->g_y + r->g_h;
}

static int ent_cmp(const void *a, const void *b)
{
    const FENT *p = (const FENT *)a, *q = (const FENT *)b;
    if (p->dir != q->dir) return q->dir - p->dir;     /* folders first */
    return strcmp(p->name, q->name);
}

/* does name match the pattern (* and ?, as TOS) */
static int match(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            p++;
            if (!*p) return 1;
            for (; *s; s++) if (match(p, s)) return 1;
            return match(p, s);
        }
        if (!*s) return 0;
        if (*p != '?' && st_upper((unsigned char)*p) != st_upper((unsigned char)*s)) return 0;
    }
    return !*s;
}

static void read_dir(void)
{
    char spec[220];
    _DTA dta, *old = Fgetdta();
    int r;
    fs.n = 0;
    fs.top = 0;
    fs.sel = -1;
    if (!fs.e) fs.e = (FENT *)malloc(MAXENT * sizeof(FENT));
    if (!fs.e) return;
    Fsetdta(&dta);
    sprintf(spec, "%s*.*", fs.path);
    busy_ptr(1);
    for (r = Fsfirst(spec, 0x10); r == 0 && fs.n < MAXENT; r = Fsnext()) {
        FENT *f = &fs.e[fs.n];
        if (dta.dta_name[0] == '.') continue;
        f->dir = (dta.dta_attribute & 0x10) != 0;
        if (!f->dir && !match(fs.pat, dta.dta_name)) continue;
        strncpy(f->name, dta.dta_name, 13);
        f->name[13] = 0;
        f->size = dta.dta_size;
        fs.n++;
    }
    busy_ptr(0);
    Fsetdta(old);
    qsort(fs.e, fs.n, sizeof(FENT), ent_cmp);
}

static void set_slider(void) { }

/* a button as an object (the AES's look, 3D where it has it) */
static void draw_btn(const GRECT *r, const char *text, int def, int on)
{
    OBJECT t[1];
    memset(t, 0, sizeof t);
    t[0].ob_next = t[0].ob_head = t[0].ob_tail = -1;
    t[0].ob_type = G_BUTTON;
    t[0].ob_flags = (short)(OF_LASTOB | OF_SELECTABLE | (def ? OF_DEFAULT : 0) | (aes_3d ? OF_FL3DACT : 0));
    t[0].ob_state = (short)(on ? OS_SELECTED : 0);
    t[0].ob_spec.free_string = (char *)text;
    t[0].ob_x = r->g_x; t[0].ob_y = r->g_y; t[0].ob_width = r->g_w; t[0].ob_height = r->g_h;
    objc_draw(t, 0, 1, r->g_x - 3, r->g_y - 3, r->g_w + 6, r->g_h + 6);
}

static void box(const GRECT *r, int fillc)
{
    short p[10];
    vsf_interior(vhandle, FIS_SOLID);
    vsf_color(vhandle, fillc);
    vswr_mode(vhandle, MD_REPLACE);
    p[0] = r->g_x; p[1] = r->g_y; p[2] = (short)(r->g_x + r->g_w - 1); p[3] = (short)(r->g_y + r->g_h - 1);
    v_bar(vhandle, p);
    vsl_color(vhandle, 1); vsl_type(vhandle, 1); vsl_width(vhandle, 1);
    p[0] = p[6] = p[8] = r->g_x; p[1] = p[3] = p[9] = r->g_y;
    p[2] = p[4] = (short)(r->g_x + r->g_w - 1); p[5] = p[7] = (short)(r->g_y + r->g_h - 1);
    v_pline(vhandle, 5, p);
}

static void text_at(short x, short y, const char *s)
{
    short d;
    vst_alignment(vhandle, 0, 5, &d, &d);
    vswr_mode(vhandle, MD_TRANS);
    v_gtext(vhandle, x, y, s);
    vst_alignment(vhandle, 0, 0, &d, &d);
    vswr_mode(vhandle, MD_REPLACE);
}

static void field(const GRECT *r, const char *s, int focus)
{
    char t[120];
    int cols = (r->g_w - 6) / wchar, n = (int)strlen(s);
    box(r, 0);
    /* the end of a long path is what matters */
    strcpy(t, n > cols ? s + n - cols : s);
    vst_color(vhandle, 1);
    text_at((short)(r->g_x + 3), (short)(r->g_y + 2), t);
    if (focus) {                                    /* the cursor, at the end */
        short p[4];
        int k = (int)strlen(t);
        p[0] = p[2] = (short)(r->g_x + 3 + k * wchar);
        p[1] = (short)(r->g_y + 2); p[3] = (short)(r->g_y + r->g_h - 3);
        vsl_color(vhandle, 1);
        v_pline(vhandle, 2, p);
    }
}

static void arrow(const GRECT *r, int up)
{
    short p[6], cx = (short)(r->g_x + r->g_w / 2), cy = (short)(r->g_y + r->g_h / 2), k = (short)(hchar / 4);
    box(r, 0);
    vsf_color(vhandle, 1);
    p[0] = (short)(cx - k); p[2] = (short)(cx + k); p[4] = cx;
    p[1] = p[3] = (short)(up ? cy + k / 2 : cy - k / 2);
    p[5] = (short)(up ? cy - k / 2 - 1 : cy + k / 2 + 1);
    v_fillarea(vhandle, 3, p);
}

static void draw(const GRECT *clip)
{
    FLAY Y;
    GRECT r = *clip, t;
    short p[4], i;
    char s[40];
    lay(&Y);
    if (!rc_intersect(&Y.w, &r)) return;
    p[0] = r.g_x; p[1] = r.g_y; p[2] = (short)(r.g_x + r.g_w - 1); p[3] = (short)(r.g_y + r.g_h - 1);
    vs_clip(vhandle, 1, p);
    set_sys_font();
    /* the background, as the AES's dialogs */
    vsf_interior(vhandle, FIS_SOLID);
    vsf_color(vhandle, aes_3d && npens >= 16 ? 8 : 0);
    vswr_mode(vhandle, MD_REPLACE);
    v_bar(vhandle, p);
    vst_color(vhandle, 1);
    if (fs.tline) {                     /* a sheet's title, centred, as TOS's selector has it */
        const char *t = fs.title;
        while (*t == ' ') t++;
        text_at((short)(Y.w.g_x + (Y.w.g_w - (short)strlen(t) * wchar) / 2), (short)(Y.w.g_y + hchar / 4), t);
    }
    text_at(Y.dirf.g_x, (short)(Y.dirf.g_y - hchar - 2), L(S_FS_DIR));
    {
        char full[230];
        sprintf(full, "%s%s", fs.path, fs.pat);
        field(&Y.dirf, full, fs.field == 0);
    }
    text_at(Y.self.g_x, (short)(Y.self.g_y - hchar - 2), L(S_FS_SELECTION));
    field(&Y.self, fs.name, fs.field == 1);
    /* the list: its bar (closer and pattern), the names, its scroll bar */
    box(&Y.list, 0);
    box(&Y.bar, 0);
    box(&Y.clo, 0);
    {   /* the closer: a cross, as a window's */
        short l[4], m = 3;
        l[0] = (short)(Y.clo.g_x + m); l[1] = (short)(Y.clo.g_y + m);
        l[2] = (short)(Y.clo.g_x + Y.clo.g_w - 1 - m); l[3] = (short)(Y.clo.g_y + Y.clo.g_h - 1 - m);
        v_pline(vhandle, 2, l);
        { short k = l[1]; l[1] = l[3]; l[3] = k; }
        v_pline(vhandle, 2, l);
    }
    text_at((short)(Y.bar.g_x + (Y.bar.g_w - (short)strlen(fs.pat) * wchar) / 2), (short)(Y.bar.g_y + 2), fs.pat);
    box(&Y.sb, 0);
    arrow(&Y.up, 1);
    arrow(&Y.dn, 0);
    t = Y.track; t.g_x++; t.g_w -= 2; t.g_y++; t.g_h -= 2;
    vsf_interior(vhandle, FIS_PATTERN); vsf_style(vhandle, 4); vsf_color(vhandle, 1);
    p[0] = t.g_x; p[1] = t.g_y; p[2] = (short)(t.g_x + t.g_w - 1); p[3] = (short)(t.g_y + t.g_h - 1);
    v_bar(vhandle, p);
    vsf_interior(vhandle, FIS_SOLID);
    {   /* the slider */
        GRECT sl = t;
        if (fs.n > fs.rows) {
            sl.g_h = (short)((long)t.g_h * fs.rows / fs.n);
            if (sl.g_h < hchar / 2) sl.g_h = (short)(hchar / 2);
            sl.g_y = (short)(t.g_y + (long)(t.g_h - sl.g_h) * fs.top / (fs.n - fs.rows));
        }
        box(&sl, 0);
    }
    for (i = 0; i < fs.rows && fs.top + i < fs.n; i++) {
        FENT *f = &fs.e[fs.top + i];
        short y = (short)(Y.items.g_y + 3 + i * hchar);
        char nm[14], *dot;
        strcpy(nm, f->name);
        dot = f->dir ? NULL : strchr(nm, '.');
        if (dot) { *dot = 0; sprintf(s, "%c %-8s %s", f->dir ? '\007' : ' ', nm, dot + 1); }
        else sprintf(s, "%c %s", f->dir ? '\007' : ' ', nm);
        vst_color(vhandle, 1);
        text_at((short)(Y.items.g_x + 4), y, s);
        if (fs.top + i == fs.sel) {
            short q[4];
            vswr_mode(vhandle, MD_XOR);
            vsf_color(vhandle, 1);
            q[0] = (short)(Y.items.g_x + 2); q[1] = y; q[2] = (short)(Y.items.g_x + Y.items.g_w - 4); q[3] = (short)(y + hchar - 1);
            v_bar(vhandle, q);
            vswr_mode(vhandle, MD_REPLACE);
        }
    }
    /* the drives, A to U (MiNT's U:, the whole system): those there aren't greyed */
    {
        int k;
        for (k = 0; k < 21; k++) {
            GRECT d;
            OBJECT o[1];
            d.g_x = (short)(Y.drv.g_x + (k % 7) * Y.dw + 4); d.g_y = (short)(Y.drv.g_y + (k / 7) * Y.dh + 4);
            d.g_w = (short)(Y.dw - 8); d.g_h = (short)(Y.dh - 8);
            s[0] = (char)('A' + k); s[1] = 0;
            memset(o, 0, sizeof o);
            o[0].ob_next = o[0].ob_head = o[0].ob_tail = -1;
            o[0].ob_type = G_BUTTON;
            o[0].ob_flags = (short)(OF_LASTOB | OF_SELECTABLE | OF_RBUTTON | (aes_3d ? OF_FL3DACT : 0));
            o[0].ob_state = (short)((fs.path[0] == 'A' + k ? OS_SELECTED : 0) | (fs.drives & (1L << k) ? 0 : OS_DISABLED));
            o[0].ob_spec.free_string = s;
            o[0].ob_x = d.g_x; o[0].ob_y = d.g_y; o[0].ob_width = d.g_w; o[0].ob_height = d.g_h;
            objc_draw(o, 0, 1, r.g_x, r.g_y, r.g_w, r.g_h);
        }
    }
    draw_btn(&Y.ok, L(S_BTN_OK), 1, 0);
    draw_btn(&Y.can, L(S_CANCEL), 0, 0);
    p[0] = r.g_x; p[1] = r.g_y; p[2] = (short)(r.g_x + r.g_w - 1); p[3] = (short)(r.g_y + r.g_h - 1);
    vs_clip(vhandle, 0, p);
    style_reset();
}

static void redraw(const GRECT *a)
{
    GRECT r, s = *a;
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    wind_get(fs.wh, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        GRECT c = r;
        if (rc_intersect(&s, &c)) draw(&c);
        wind_get(fs.wh, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
}

static void redraw_all(void)
{
    GRECT w;
    wind_get(fs.wh, WF_WORKXYWH, &w.g_x, &w.g_y, &w.g_w, &w.g_h);
    redraw(&w);
    set_slider();
}

static void go_dir(const char *sub)
{
    if (!strcmp(sub, "..")) {
        int n = (int)strlen(fs.path);
        if (n > 3) {                                     /* "C:\A\B\" -> "C:\A\" */
            fs.path[n - 1] = 0;
            {
                char *b = strrchr(fs.path, '\\');
                if (b) b[1] = 0;
            }
        }
    } else if (strlen(fs.path) + strlen(sub) < sizeof fs.path - 2) {
        strcat(fs.path, sub);
        strcat(fs.path, "\\");
    }
    read_dir();
    redraw_all();
}

static void clamp_top(void)
{
    if (fs.top > fs.n - fs.rows) fs.top = fs.n - fs.rows;
    if (fs.top < 0) fs.top = 0;
}

/* the path typed into the directory field: "C:\DOCS\*.RTF" */
static void take_dir(const char *full)
{
    const char *b = strrchr(full, '\\');
    if (!b) return;
    strncpy(fs.path, full, (size_t)(b - full + 1));
    fs.path[b - full + 1] = 0;
    strncpy(fs.pat, b[1] ? b + 1 : "*.*", sizeof fs.pat - 1);
    fs.pat[sizeof fs.pat - 1] = 0;
}

static void choose(int i, int dbl)
{
    FENT *f;
    if (i < 0 || i >= fs.n) return;
    f = &fs.e[i];
    if (f->dir) { if (dbl) go_dir(f->name); else { fs.sel = i; redraw_all(); } return; }
    fs.sel = i;
    strcpy(fs.name, f->name);
    fs.field = 1;
    redraw_all();
    if (dbl) fs.done = 1;
}

static void click(short mx, short my, short clicks)
{
    FLAY Y;
    short d1, d2, mb, ks;
    lay(&Y);
    if (in_g(&Y.dirf, mx, my)) { fs.field = 0; redraw_all(); return; }
    if (in_g(&Y.self, mx, my)) { fs.field = 1; redraw_all(); return; }
    if (in_g(&Y.ok, mx, my)) { fs.done = 1; return; }
    if (in_g(&Y.can, mx, my)) { fs.done = 2; return; }
    if (in_g(&Y.clo, mx, my)) { go_dir(".."); return; }
    if (in_g(&Y.up, mx, my) || in_g(&Y.dn, mx, my)) {     /* a line at a time, while held */
        int dir = in_g(&Y.up, mx, my) ? -1 : 1;
        do {
            int o = fs.top;
            fs.top += dir;
            clamp_top();
            if (fs.top != o) redraw_all();
            evnt_timer(60L);
            graf_mkstate(&d1, &d2, &mb, &ks);
        } while (mb & 1);
        return;
    }
    if (in_g(&Y.track, mx, my)) {                         /* a page, up or down */
        long sy = Y.track.g_y + (fs.n > fs.rows ? (long)Y.track.g_h * fs.top / fs.n : 0);
        fs.top += my < sy ? -fs.rows : fs.rows;
        clamp_top();
        redraw_all();
        do graf_mkstate(&d1, &d2, &mb, &ks); while (mb & 1);
        return;
    }
    if (in_g(&Y.drv, mx, my)) {
        int k = (mx - Y.drv.g_x) / Y.dw + 7 * ((my - Y.drv.g_y) / Y.dh);
        if (k >= 0 && k < 21 && (fs.drives & (1L << k))) {
            sprintf(fs.path, "%c:\\", 'A' + k);
            read_dir();
            redraw_all();
        }
        do graf_mkstate(&d1, &d2, &mb, &ks); while (mb & 1);
        return;
    }
    if (in_g(&Y.items, mx, my)) {
        int i = fs.top + (my - Y.items.g_y - 3) / hchar;
        if (i >= fs.top && i < fs.n) choose(i, clicks >= 2);
    }
    do graf_mkstate(&d1, &d2, &mb, &ks); while (mb & 1);
}

static void key(short ks, short k)
{
    short scan = (k >> 8) & 0xff, asc = k & 0xff;
    char *t;
    size_t max;
    (void)ks;
    if (scan == 0x1C || scan == 0x72) {                  /* Return: a typed directory, or OK */
        if (fs.field == 0) { char full[230]; sprintf(full, "%s%s", fs.path, fs.pat); take_dir(full); read_dir(); redraw_all(); return; }
        if (fs.sel >= 0 && fs.e[fs.sel].dir && !fs.name[0]) { go_dir(fs.e[fs.sel].name); return; }
        fs.done = 1;
        return;
    }
    if (scan == 0x01 || scan == 0x61) { fs.done = 2; return; }      /* Esc, Undo */
    if (scan == 0x0F) { fs.field = !fs.field; redraw_all(); return; }  /* Tab */
    if (scan == 0x48 || scan == 0x50) {                  /* up, down: through the list */
        int i = fs.sel + (scan == 0x48 ? -1 : 1);
        if (i < 0 || i >= fs.n) return;
        fs.sel = i;
        if (i < fs.top) fs.top = i;
        if (i >= fs.top + fs.rows) fs.top = i - fs.rows + 1;
        if (!fs.e[i].dir) strcpy(fs.name, fs.e[i].name);
        redraw_all();
        return;
    }
    if (fs.field == 0) {                                  /* the directory: path and pattern together */
        static char full[230];
        sprintf(full, "%s%s", fs.path, fs.pat);
        t = full; max = sizeof full - 1;
        if (scan == 0x0E) { size_t n = strlen(t); if (n) t[n - 1] = 0; }
        else if (asc >= 32 && strlen(t) < max) { size_t n = strlen(t); t[n] = (char)asc; t[n + 1] = 0; }
        else return;
        {   /* kept as typed: split where the last backslash is */
            const char *b = strrchr(full, '\\');
            if (b) take_dir(full);
        }
        redraw_all();
        return;
    }
    t = fs.name; max = 12;
    if (scan == 0x0E) { size_t n = strlen(t); if (n) t[n - 1] = 0; }
    else if (scan == 0x53) t[0] = 0;
    else if (asc > 32 && strlen(t) < max) { size_t n = strlen(t); t[n] = (char)st_upper((unsigned char)asc); t[n + 1] = 0; }
    else return;
    redraw_all();
}

int fsel_for(DOC *d) { return d && fs.wh > 0 && fs.owner == d; }

/* its document's window came to the front: the selector over it */
void fsel_top(DOC *d)
{
    if (fs.wh > 0 && d && fs.owner == d) wind_set(fs.wh, WF_TOP, 0, 0, 0, 0);
}

/* its document's window moved or changed size: the selector goes with it */
void fsel_follow(DOC *d)
{
    short x, y, w, h;
    if (fs.wh <= 0 || !d || fs.owner != d) return;
    wind_get(fs.wh, WF_CURRXYWH, &x, &y, &w, &h);
    sheet_pos(d, w, h, &x, &y);
    winpos_clamp(&x, &y, w, h);
    wind_set(fs.wh, WF_CURRXYWH, x, y, w, h);
}

/* dir: "C:\PATH\*.EWR" in and out; name: the file in and out; 1 OK, 0
 * Cancel, -1 no window to spare (the caller uses the system's) */
int fsel_window(char *dir, char *name, const char *title)
{
    short x, y, w, h, msg[8], mx, my, mb, ks, k, br, ev;
    short ww, wh;
    if (fs.wh > 0) return -1;                    /* one at a time: the system's for a second */
    memset(&fs.path, 0, sizeof fs.path);
    take_dir(dir);
    if (!fs.path[0]) { strcpy(fs.path, "C:\\"); strcpy(fs.pat, "*.*"); }
    strncpy(fs.name, name, sizeof fs.name - 1);
    fs.name[sizeof fs.name - 1] = 0;
    fs.field = 1;
    fs.done = 0;
    fs.drives = Drvmap();
    {   /* MiNT's U: (the whole system) isn't always in the map */
        long v;
        if (Getcookie(0x4D694E54L, &v) == C_FOUND) fs.drives |= 1L << 20;     /* 'MiNT' */
    }
    {   /* the work area the layout needs */
        short cw = wchar, ch = hchar;
        ww = (short)(cw + (NAMEW + 6) * cw + 2 * cw + RIGHTW * cw + 3 * cw);
        fs.owner = ui_doc && ui_doc->used && ui_doc->wh > 0 && !ui_doc->iconified ? ui_doc : NULL;
        fs.tline = (short)(fs.owner ? ch + ch / 2 : 0);
        wh = (short)(fs.tline + ch + ch / 2 + 2 + ch + 4 + ch + (LROWS + 1) * ch + 8 + ch + 6 + ch / 2);
        wind_calc(WC_BORDER, fs.owner ? SIZER : FKIND, 0, 0, ww, wh, &x, &y, &ww, &wh);
        fs.minw = ww; fs.minh = (short)(wh - 4 * ch);
    }
    if (ww > desk.g_w) ww = desk.g_w;
    if (wh > desk.g_h) wh = desk.g_h;
    x = (short)(desk.g_x + (desk.g_w - ww) / 2);
    y = (short)(desk.g_y + (desk.g_h - wh) / 2);
    if (fs.owner) sheet_pos(fs.owner, ww, wh, &x, &y);    /* a sheet on the document's window */
    winpos_clamp(&x, &y, ww, wh);
    fs.wh = wind_create(fs.owner ? SIZER : FKIND, x, y, ww, wh);
    if (fs.wh <= 0) { fs.wh = 0; return -1; }
    sprintf(fs.title, " %.40s ", title ? title : "");
    wind_set_str(fs.wh, WF_NAME, fs.title);
    wind_open(fs.wh, x, y, ww, wh);
    if (fs.owner) refresh_all(fs.owner);           /* greyed while the selector is open */
    read_dir();
    {
        FLAY Y;
        lay(&Y);
    }
    set_slider();
    while (!fs.done) {
        short top, d1, d2, d3;
        ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON, 0x102, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        msg, 0L, &mx, &my, &mb, &ks, &k, &br);
        wind_get(0, WF_TOP, &top, &d1, &d2, &d3);
        if (ev & MU_MESAG) {
            if (msg[3] == fs.wh && msg[0] >= 20 && msg[0] < 40) {
                GRECT a;
                switch (msg[0]) {
                case WM_REDRAW: a.g_x = msg[4]; a.g_y = msg[5]; a.g_w = msg[6]; a.g_h = msg[7]; redraw(&a); break;
                case WM_TOPPED: wind_set(fs.wh, WF_TOP, 0, 0, 0, 0); break;
                case WM_CLOSED: fs.done = 2; break;
                case WM_MOVED:                  /* (on its own only: a sheet has no mover) */
                    if (fs.owner) break;
                    winpos_clamp(&msg[4], &msg[5], msg[6], msg[7]);
                    wind_set(fs.wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
                    break;
                case WM_SIZED:
                    w = msg[6] < fs.minw ? fs.minw : msg[6];
                    h = msg[7] < fs.minh ? fs.minh : msg[7];
                    wind_set(fs.wh, WF_CURRXYWH, msg[4], msg[5], w, h);
                    if (fs.owner) fsel_follow(fs.owner);      /* a sheet stays in the middle of the window */
                    { FLAY Y; lay(&Y); }
                    clamp_top();
                    redraw_all();
                    break;
                case WM_ARROWED:
                    if (msg[4] == WA_UPLINE) fs.top--;
                    else if (msg[4] == WA_DNLINE) fs.top++;
                    else if (msg[4] == WA_UPPAGE) fs.top -= fs.rows;
                    else if (msg[4] == WA_DNPAGE) fs.top += fs.rows;
                    clamp_top();
                    redraw_all();
                    break;
                case WM_VSLID:
                    fs.top = fs.n > fs.rows ? (int)((long)msg[4] * (fs.n - fs.rows) / 1000L) : 0;
                    clamp_top();
                    redraw_all();
                    break;
                }
            } else if (!dlg_dispatch_msg(msg)) handle_msg(msg);
            if (quit_now) fs.done = 2;
        }
        if (ev & MU_KEYBD) {
            if (top == fs.wh) key(ks, k);
            else if (!dlg_dispatch_key(ks, k)) handle_key(ks, k);
        }
        if (ev & MU_BUTTON) {
            if (wind_find(mx, my) == fs.wh) {
                if (top != fs.wh) wind_set(fs.wh, WF_TOP, 0, 0, 0, 0);
                if (mb & 1) click(mx, my, br);
            } else if (mb & 2) { short d4; do graf_mkstate(&d1, &d2, &mb, &d4); while (mb & 2); }
            else if (!dlg_dispatch_click(mx, my, br)) handle_click(mx, my, ks, br);
        }
    }
    wind_close(fs.wh);
    wind_delete(fs.wh);
    fs.wh = 0;
    if (fs.owner && fs.owner->used) refresh_all(fs.owner);    /* not greyed any more */
    pump_redraws();
    if (fs.done != 1 || !fs.name[0]) return 0;
    sprintf(dir, "%s%s", fs.path, fs.pat);
    strcpy(name, fs.name);
    return 1;
}
#endif
