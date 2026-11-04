/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amitool.c - the font selector and print preview windows (after
 *             gemtool.c). Both act on the active document: the document
 *             window used last.
 */
#include <exec/lists.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiui.h"

/* ================================================================= */
/*  Font selector                                                     */
/* ================================================================= */

static const short fsz[] = { 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 60, 72 };
#define NFSZ ((int)(sizeof fsz / sizeof fsz[0]))
enum { FG_FONTS = 1, FG_SIZES, FG_BOLD, FG_ITALIC, FG_UNDER, FG_APPLY, FG_CLOSE };

static struct {
    int open, font, size, attr;
    struct Window *w;
    struct Gadget *glist, *g[8];
    struct List flist, slist;
    struct Node fnodes[MAX_FONTS], snodes[NFSZ];
    char snames[NFSZ][8];
    GRECT pv;
    ULONG lsecs, lmics;
    int   lsel, lgad;
    char  title[40];
} fs;

static void new_list(struct List *l)
{
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_Tail = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}

static void fs_lists(void)
{
    int i;
    new_list(&fs.flist);
    for (i = 0; i < nfonts; i++) { fs.fnodes[i].ln_Name = fonts[i].name; AddTail(&fs.flist, &fs.fnodes[i]); }
    new_list(&fs.slist);
    for (i = 0; i < NFSZ; i++) {
        sprintf(fs.snames[i], "%3d", fsz[i]);
        fs.snodes[i].ln_Name = fs.snames[i];
        AddTail(&fs.slist, &fs.snodes[i]);
    }
}

static int size_index(int sz)
{
    int i;
    for (i = 0; i < NFSZ; i++) if (fsz[i] == sz) return i;
    return -1;
}

static void fs_sample(void)
{
    char st[80];
    long base, x;
    int px;
    const char *sample = L(S_FONT_SAMPLE);
    if (!fs.open) return;
    gfx_begin(fs.w);
    fill_g(&fs.pv, P_WHITE);
    bevel(&fs.pv, 1);
    {
        GRECT c = fs.pv;
        c.g_x += 2; c.g_y += 2; c.g_w -= 4; c.g_h -= 4;
        clip_set(&c);
    }
    stc(st, sample, sizeof st);
    px = (int)pt2px_y(fs.size);
    base = fs.pv.g_y + fs.pv.g_h * 2 / 3;
    x = fs.pv.g_x + 6;
    text_at(x, base, st, (int)strlen(st), fs.font, px, fs.attr & (A_BOLD | A_ITALIC), P_BLACK);
    if (fs.attr & A_UNDER) {
        const METRICS *m = get_metrics(fs.font, fs.size, fs.attr);
        long w = 0;
        int k;
        for (k = 0; st[k]; k++) w += m->w[(unsigned char)st[k]];
        line(x, base + 2, x + w, base + 2, P_BLACK);
    }
    clip_off();
    gfx_end();
}

/* set the gadgets from fs */
static void fs_show(void)
{
    int si = size_index(fs.size);
    if (!fs.open) return;
    GT_SetGadgetAttrs(fs.g[FG_FONTS], fs.w, NULL, GTLV_Selected, (ULONG)fs.font, GTLV_Top, (ULONG)(fs.font > 3 ? fs.font - 3 : 0), TAG_END);
    GT_SetGadgetAttrs(fs.g[FG_SIZES], fs.w, NULL, GTLV_Selected, (ULONG)(si >= 0 ? si : ~0UL),
                      GTLV_Top, (ULONG)(si > 3 ? si - 3 : 0), TAG_END);
    GT_SetGadgetAttrs(fs.g[FG_BOLD], fs.w, NULL, GTCB_Checked, (ULONG)((fs.attr & A_BOLD) != 0), TAG_END);
    GT_SetGadgetAttrs(fs.g[FG_ITALIC], fs.w, NULL, GTCB_Checked, (ULONG)((fs.attr & A_ITALIC) != 0), TAG_END);
    GT_SetGadgetAttrs(fs.g[FG_UNDER], fs.w, NULL, GTCB_Checked, (ULONG)((fs.attr & A_UNDER) != 0), TAG_END);
    fs_sample();
}

void fontsel_sync(void)
{
    DOC *d = active_doc();
    CELL c;
    long a0, b0;
    int f, sz, a;
    if (!fs.open || !d) return;
    c = doc_sel(d, &a0, &b0) ? d->text[a0] : d->ins;
    f = c.font < nfonts ? c.font : 0;
    sz = c.size;
    a = c.attr & (A_BOLD | A_ITALIC | A_UNDER);
    if (f == fs.font && sz == fs.size && a == fs.attr) return;
    fs.font = f; fs.size = sz; fs.attr = a;
    fs_show();
}

static void fontsel_close(void)
{
    if (!fs.open) return;
    RemoveGList(fs.w, fs.glist, -1);
    close_ewin(fs.w);
    FreeGadgets(fs.glist);
    fs.glist = NULL;
    fs.w = NULL;
    fs.open = 0;
}

void fontsel_open(void)
{
    struct NewGadget ng;
    struct Gadget *gad;
    long lh = 9 * (hchar + 1) + 4, top = hchar + 8, W = 420, H, x, bt, bl;
    int i;
    if (fs.open) { WindowToFront(fs.w); ActivateWindow(fs.w); fs.font = -1; fontsel_sync(); return; }
    H = top + lh + 10 + (small_icons ? 36 : 56) + 10 + hchar + 6 + 10;
    sprintf(fs.title, "%s", L(S_FONT));
    fs.w = open_ewin((struct TagItem *)(ULONG[]){ WA_Left, (ULONG)(scr_w - W - 30), WA_Top, (ULONG)(desk.g_y + 8),
        WA_InnerWidth, (ULONG)W, WA_InnerHeight, (ULONG)H, WA_Title, (ULONG)fs.title,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SmartRefresh, TRUE, WA_Activate, TRUE, TAG_END },
        LISTVIEWIDCMP | BUTTONIDCMP | CHECKBOXIDCMP | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_ACTIVEWINDOW |
        IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MENUPICK);
    if (!fs.w) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    bl = fs.w->BorderLeft; bt = fs.w->BorderTop;
    fs_lists();
    memset(fs.g, 0, sizeof fs.g);
    gad = CreateContext(&fs.glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &sysattr;
    ng.ng_VisualInfo = vi;
    ng.ng_LeftEdge = (WORD)(bl + 10); ng.ng_TopEdge = (WORD)(bt + top); ng.ng_Width = 200; ng.ng_Height = (WORD)lh;
    ng.ng_GadgetText = (UBYTE *)L(S_FONT); ng.ng_Flags = PLACETEXT_ABOVE | NG_HIGHLABEL; ng.ng_GadgetID = FG_FONTS;
    gad = fs.g[FG_FONTS] = CreateGadget(LISTVIEW_KIND, gad, &ng, GTLV_Labels, (ULONG)&fs.flist, GTLV_ShowSelected, 0, TAG_END);
    ng.ng_LeftEdge = (WORD)(bl + 220); ng.ng_Width = 64;
    ng.ng_GadgetText = (UBYTE *)L(S_SIZE); ng.ng_GadgetID = FG_SIZES;
    gad = fs.g[FG_SIZES] = CreateGadget(LISTVIEW_KIND, gad, &ng, GTLV_Labels, (ULONG)&fs.slist, GTLV_ShowSelected, 0, TAG_END);
    for (i = 0; i < 3; i++) {
        static const int sty[3] = { S_BOLD, S_ITALIC, S_UNDERLINE };
        ng.ng_LeftEdge = (WORD)(bl + 300); ng.ng_TopEdge = (WORD)(bt + top + i * (hchar + 10)); ng.ng_Width = 26; ng.ng_Height = 11;
        ng.ng_GadgetText = (UBYTE *)L(sty[i]); ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_GadgetID = (UWORD)(FG_BOLD + i);
        gad = fs.g[FG_BOLD + i] = CreateGadget(CHECKBOX_KIND, gad, &ng, TAG_END);
    }
    fs.pv.g_x = (short)(bl + 10); fs.pv.g_y = (short)(bt + top + lh + 10);
    fs.pv.g_w = (short)(W - 20); fs.pv.g_h = (short)(small_icons ? 36 : 56);
    x = W - 10;
    for (i = 1; i >= 0; i--) {
        const char *t = L(i ? S_CLOSE : S_APPLY);
        long bw = sys_width(t) + 24;
        if (bw < 70) bw = 70;
        x -= bw;
        ng.ng_LeftEdge = (WORD)(bl + x); ng.ng_TopEdge = (WORD)(fs.pv.g_y + fs.pv.g_h + 10); ng.ng_Width = (WORD)bw;
        ng.ng_Height = (WORD)(hchar + 6); ng.ng_GadgetText = (UBYTE *)t; ng.ng_Flags = PLACETEXT_IN;
        ng.ng_GadgetID = (UWORD)(i ? FG_CLOSE : FG_APPLY);
        gad = fs.g[i ? FG_CLOSE : FG_APPLY] = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
        x -= 10;
    }
    if (!gad) { close_ewin(fs.w); FreeGadgets(fs.glist); fs.glist = NULL; fs.w = NULL; return; }
    AddGList(fs.w, fs.glist, (UWORD)~0, -1, NULL);
    RefreshGList(fs.glist, fs.w, NULL, -1);
    GT_RefreshWindow(fs.w, NULL);
    fs.open = 1;
    fs.font = -1;
    fs.lgad = 0;
    fontsel_sync();
    fs_sample();
}

static void fs_apply(void)
{
    DOC *d = active_doc();
    int f = fs.font, sz = fs.size, a = fs.attr;
    if (!d) return;
    apply_style(d, AP_FONT, f);
    apply_style(d, AP_SIZE, sz);
    apply_style(d, AP_ATTRS, a);
}

static int fs_owns(struct Window *w) { return fs.open && w == fs.w; }

static void fs_msg(struct IntuiMessage *m)
{
    struct Gadget *g;
    int id, dbl;
    switch (m->Class) {
    case IDCMP_CLOSEWINDOW: fontsel_close(); return;
    case IDCMP_REFRESHWINDOW:
        GT_BeginRefresh(fs.w); GT_EndRefresh(fs.w, TRUE);
        fs_sample();
        return;
    case IDCMP_ACTIVEWINDOW: fontsel_sync(); return;
    case IDCMP_GADGETUP:
        g = (struct Gadget *)m->IAddress;
        id = g->GadgetID;
        dbl = id == fs.lgad && (int)m->Code == fs.lsel && DoubleClick(fs.lsecs, fs.lmics, m->Seconds, m->Micros);
        fs.lgad = id; fs.lsel = m->Code; fs.lsecs = m->Seconds; fs.lmics = m->Micros;
        switch (id) {
        case FG_FONTS: if (m->Code < nfonts) fs.font = m->Code; fs_sample(); if (dbl) fs_apply(); break;
        case FG_SIZES: if (m->Code < NFSZ) fs.size = fsz[m->Code]; fs_sample(); if (dbl) fs_apply(); break;
        case FG_BOLD: case FG_ITALIC: case FG_UNDER:
            if (g->Flags & GFLG_SELECTED) fs.attr |= 1 << (id - FG_BOLD); else fs.attr &= ~(1 << (id - FG_BOLD));
            fs_sample();
            break;
        case FG_APPLY: fs_apply(); break;
        case FG_CLOSE: fontsel_close(); break;
        }
        return;
    }
}

static void fs_key(int key, int qual)
{
    (void)qual;
    if (key == K_RETURN || key == K_ENTER) fs_apply();
    else if (key == K_ESC) fontsel_close();
    else if (key == K_UP && fs.font > 0) { fs.font--; fs_show(); }
    else if (key == K_DOWN && fs.font < nfonts - 1) { fs.font++; fs_show(); }
}

static void fs_docgone(DOC *d) { (void)d; }
const TOOL fontsel_tool = { fs_owns, fs_msg, fs_key, fontsel_close, fs_docgone };

/* ================================================================= */
/*  Print preview                                                     */
/* ================================================================= */

enum { PG_PREV = 1, PG_NEXT, PG_ZOOM, PG_PRINT, PG_PDF, PG_CLOSE };
static struct {
    int  open, stale, zoom;          /* zoom 0: whole page, 1: 100% */
    struct Window *w;
    struct Gadget *glist;
    DOC *d;
    int  page;
    long sx, sy, lx, barh;
    char title[64], label[40];
} pv;

static void pv_area(GRECT *a)
{
    a->g_x = pv.w->BorderLeft;
    a->g_y = (short)(pv.w->BorderTop + pv.barh);
    a->g_w = (short)(pv.w->Width - pv.w->BorderLeft - pv.w->BorderRight);
    a->g_h = (short)(pv.w->Height - pv.w->BorderTop - pv.w->BorderBottom - pv.barh);
}

static void pv_geom(const GRECT *a, long *n, long *den, long *ox, long *oy)
{
    long W = pt2px_x(PS(pv.d).w), H = pt2px_y(PS(pv.d).h), aw = a->g_w, ah = a->g_h;
    if (pv.zoom == 0) {
        long p = (aw - 16) * 1000L / W, q = (ah - 16) * 1000L / H;
        *den = 1000;
        *n = p < q ? p : q;
        if (*n > 1000) *n = 1000;
        if (*n < 1) *n = 1;
        *ox = a->g_x + (aw - W * *n / 1000) / 2;
        *oy = a->g_y + (ah - H * *n / 1000) / 2;
    } else {
        *n = *den = 1;
        *ox = a->g_x + 8 - pv.sx;
        *oy = a->g_y + 8 - pv.sy;
    }
}

static void pv_clamp(void)
{
    GRECT a;
    long W, H;
    int np;
    if (!pv.open) return;
    pv_area(&a);
    np = doc_npages(pv.d);
    if (pv.page >= np) pv.page = np - 1;
    if (pv.page < 0) pv.page = 0;
    W = pt2px_x(PS(pv.d).w) + 16; H = pt2px_y(PS(pv.d).h) + 16;
    if (pv.sx > W - a.g_w) pv.sx = W - a.g_w;
    if (pv.sy > H - a.g_h) pv.sy = H - a.g_h;
    if (pv.sx < 0) pv.sx = 0;
    if (pv.sy < 0) pv.sy = 0;
}

static void pv_draw(void)
{
    GRECT a, page;
    long n, den, ox, oy, W, H;
    if (!pv.open) return;
    pv_clamp();
    gfx_begin(pv.w);
    /* the page count beside the buttons */
    sprintf(pv.label, L(S_PAGE_N_OF), pv.page + 1, doc_npages(pv.d));
    fill_rect(pv.lx, pv.w->BorderTop + 2, pv.lx + sys_width(pv.label) + 40, pv.w->BorderTop + pv.barh - 3, P_GREY);
    sys_text(pv.lx, pv.w->BorderTop + (pv.barh - hchar) / 2, pv.label, P_BLACK);
    pv_area(&a);
    if (a.g_w > 0 && a.g_h > 0) {
        clip_set(&a);
        fill_g(&a, P_DESK);
        pv_geom(&a, &n, &den, &ox, &oy);
        W = pt2px_x(PS(pv.d).w) * n / den;
        H = pt2px_y(PS(pv.d).h) * n / den;
        fill_rect(ox + 4, oy + 4, ox + W + 3, oy + H + 3, P_SHADOW);
        fill_rect(ox, oy, ox + W - 1, oy + H - 1, P_WHITE);
        frame(ox, oy, ox + W - 1, oy + H - 1, P_BLACK);
        page.g_x = (short)(ox + 1); page.g_y = (short)(oy + 1); page.g_w = (short)(W - 2); page.g_h = (short)(H - 2);
        if (rc_inter(&a, &page)) {
            clip_set(&page);
            draw_preview_page(pv.d, pv.page, ox, oy, n, den);
        }
        clip_off();
    }
    gfx_end();
}

static void pv_set_title(void)
{
    sprintf(pv.title, "%s: %.30s", L(S_PREVIEW), lat(pv.d->base));
    SetWindowTitles(pv.w, (UBYTE *)pv.title, (UBYTE *)-1);
}

static void preview_close(void)
{
    if (!pv.open) return;
    RemoveGList(pv.w, pv.glist, -1);
    close_ewin(pv.w);
    FreeGadgets(pv.glist);
    pv.glist = NULL;
    pv.w = NULL;
    pv.open = 0;
}

void preview_open(DOC *d)
{
    struct NewGadget ng;
    struct Gadget *gad;
    long W, H, ww, wh, x, bl, bt;
    int i;
    static const char *arrows[2] = { "<", ">" };
    if (!d) return;
    if (d->view != VIEW_DOC && d->cv.dirty) apply_codes(d);
    doc_layout(d);
    pv.d = d;
    pv.page = d->lines[doc_find_line(d, d->cursor)].page;
    if (pv.open) { WindowToFront(pv.w); ActivateWindow(pv.w); pv_set_title(); pv_draw(); return; }
    pv.zoom = 0;
    pv.sx = pv.sy = 0;
    W = pt2px_x(PS(d).w); H = pt2px_y(PS(d).h);
    wh = desk.g_h - 20;
    ww = (wh - hchar - 30) * W / H + 40;
    {   /* room for the buttons */
        long need = 6 + 64 + 12 + sys_width(L(S_PAGE_N_OF)) + 24 + sys_width(L(S_WHOLE_PAGE)) + 26 +
                    sys_width(L(S_PRINT_BTN)) + 26 + sys_width(L(S_EXPORT_PDF)) + 26 + sys_width(L(S_CLOSE)) + 26 + 30;
        if (ww < need) ww = need;
    }
    if (ww > scr_w - 20) ww = scr_w - 20;
    pv.title[0] = 0;
    pv.w = open_ewin((struct TagItem *)(ULONG[]){ WA_Left, (ULONG)(scr_w - ww - 10), WA_Top, (ULONG)(desk.g_y + 10),
        WA_Width, (ULONG)ww, WA_Height, (ULONG)wh, WA_Title, (ULONG)pv.title, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE, WA_SmartRefresh, TRUE, WA_Activate, TRUE,
        WA_MinWidth, 300, WA_MinHeight, 120, WA_MaxWidth, (ULONG)~0, WA_MaxHeight, (ULONG)~0, TAG_END },
        BUTTONIDCMP | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_MOUSEBUTTONS |
        IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MENUPICK);
    if (!pv.w) { alert(1, 3, AL_NO_WINDOWS, AB_OK); return; }
    bl = pv.w->BorderLeft; bt = pv.w->BorderTop;
    pv.barh = hchar + 12;
    gad = CreateContext(&pv.glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &sysattr;
    ng.ng_VisualInfo = vi;
    ng.ng_TopEdge = (WORD)(bt + 3);
    ng.ng_Height = (WORD)(hchar + 6);
    ng.ng_Flags = PLACETEXT_IN;
    x = 6;
    for (i = 0; i < 2; i++) {
        ng.ng_LeftEdge = (WORD)(bl + x); ng.ng_Width = 28; ng.ng_GadgetText = (UBYTE *)arrows[i];
        ng.ng_GadgetID = (UWORD)(PG_PREV + i);
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
        x += 32;
    }
    pv.lx = bl + x + 6;
    x += 12 + sys_width(L(S_PAGE_N_OF)) + 24;
    {
        static const int lab[4] = { S_WHOLE_PAGE, S_PRINT_BTN, S_EXPORT_PDF, S_CLOSE };
        for (i = 0; i < 4; i++) {
            long bw = sys_width(L(lab[i])) + 20;
            if (i == 0 && bw < sys_width("100%") + 20) bw = sys_width("100%") + 20;
            ng.ng_LeftEdge = (WORD)(bl + x); ng.ng_Width = (WORD)bw;
            ng.ng_GadgetText = (UBYTE *)(i == 0 ? "100%" : L(lab[i]));
            ng.ng_GadgetID = (UWORD)(PG_ZOOM + i);
            gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
            x += bw + 6;
        }
    }
    if (!gad) { close_ewin(pv.w); FreeGadgets(pv.glist); pv.glist = NULL; pv.w = NULL; return; }
    AddGList(pv.w, pv.glist, (UWORD)~0, -1, NULL);
    RefreshGList(pv.glist, pv.w, NULL, -1);
    GT_RefreshWindow(pv.w, NULL);
    pv.open = 1;
    pv.stale = 0;
    pv_set_title();
    pv_draw();
}

void preview_changed(DOC *d) { if (pv.open && pv.d == d) pv.stale = 1; }

int preview_idle(int run)
{
    if (!pv.open || !pv.stale) return 0;
    if (run) {
        pv.stale = 0;
        doc_layout(pv.d);
        pv_set_title();
        pv_draw();
    }
    return 1;
}

static void pv_goto(int page)
{
    int np = doc_npages(pv.d);
    if (page < 0) page = 0;
    if (page >= np) page = np - 1;
    if (page == pv.page) return;
    pv.page = page;
    pv.sy = 0;
    pv_draw();
}

static void pv_zoom_at(short mx, short my)
{
    GRECT a;
    long n, den, ox, oy;
    pv_area(&a);
    if (!pv.zoom) {
        pv_geom(&a, &n, &den, &ox, &oy);
        pv.sx = (mx - ox) * den / n - a.g_w / 2;
        pv.sy = (my - oy) * den / n - a.g_h / 2;
    }
    pv.zoom = !pv.zoom;
    pv_draw();
}

static int pv_owns(struct Window *w) { return pv.open && w == pv.w; }

static void pv_msg(struct IntuiMessage *m)
{
    GRECT a;
    switch (m->Class) {
    case IDCMP_CLOSEWINDOW: preview_close(); return;
    case IDCMP_REFRESHWINDOW: GT_BeginRefresh(pv.w); GT_EndRefresh(pv.w, TRUE); pv_draw(); return;
    case IDCMP_NEWSIZE: pv_draw(); return;
    case IDCMP_GADGETUP:
        switch (((struct Gadget *)m->IAddress)->GadgetID) {
        case PG_PREV: pv_goto(pv.page - 1); break;
        case PG_NEXT: pv_goto(pv.page + 1); break;
        case PG_ZOOM: if (!pv.zoom) pv.sx = pv.sy = 0; pv.zoom = !pv.zoom; pv_draw(); break;
        case PG_PDF: cmd_pdf(pv.d); break;
        case PG_PRINT: cmd_print(pv.d); break;
        case PG_CLOSE: preview_close(); break;
        }
        return;
    case IDCMP_MOUSEBUTTONS:
        pv_area(&a);
        if (m->Code != SELECTDOWN || !in_rect(&a, m->MouseX, m->MouseY)) return;
        if (!pv.zoom) { pv_zoom_at(m->MouseX, m->MouseY); return; }
        {
            /* drag to move the page, click to see it whole */
            short x, y, x0 = m->MouseX, y0 = m->MouseY;
            long sx0 = pv.sx, sy0 = pv.sy;
            int q, moved = 0;
            track_begin(pv.w);
            while (track_next(&x, &y, &q)) {
                if (x == x0 && y == y0 && !moved) continue;
                moved = 1;
                pv.sx = sx0 - (x - x0);
                pv.sy = sy0 - (y - y0);
                pv_draw();
            }
            track_end();
            if (!moved) pv_zoom_at(x0, y0);
        }
        return;
    }
}

static void pv_key(int key, int qual)
{
    GRECT a;
    (void)qual;
    pv_area(&a);
    switch (key) {
    case K_LEFT: pv_goto(pv.page - 1); break;
    case K_RIGHT: pv_goto(pv.page + 1); break;
    case K_ESC: preview_close(); break;
    case ' ': pv.zoom = !pv.zoom; pv_draw(); break;
    case K_UP: case K_DOWN:
        if (!pv.zoom) { pv_goto(pv.page + (key == K_UP ? -1 : 1)); break; }
        pv.sy += (key == K_UP ? -1 : 1) * a.g_h / 2;
        pv_draw();
        break;
    }
}

static void pv_docgone(DOC *d) { if (pv.open && pv.d == d) preview_close(); }
const TOOL preview_tool = { pv_owns, pv_msg, pv_key, preview_close, pv_docgone };
