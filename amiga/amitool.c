/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amitool.c - the font selector window (after
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

static const short fsz[] = { 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 60, 72, 96, 120, 150 };
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
    long lh = 9 * (hchar + 1) + 4, top = hchar + 8, W, H, x, bt, bl, lw = 200, sw = 0;
    int i;
    /* the font list as wide as the longest name (Speedo names are long) */
    for (i = 0; i < nfonts; i++) if (sys_width(fonts[i].name) + 30 > lw) lw = sys_width(fonts[i].name) + 30;
    for (i = 0; i < 3; i++) {
        static const int sty[3] = { S_BOLD, S_ITALIC, S_UNDERLINE };
        if (sys_width(L(sty[i])) > sw) sw = sys_width(L(sty[i]));
    }
    if (lw > scr_w - 64 - 3 * wchar - 70 - sw) lw = scr_w - 64 - 3 * wchar - 70 - sw;
    W = 10 + lw + 10 + 64 + 16 + dlg_cbw() + 8 + sw + 10;
    if (W < 420) W = 420;
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
    ng.ng_LeftEdge = (WORD)(bl + 10); ng.ng_TopEdge = (WORD)(bt + top); ng.ng_Width = (WORD)lw; ng.ng_Height = (WORD)lh;
    ng.ng_GadgetText = (UBYTE *)L(S_FONT); ng.ng_Flags = PLACETEXT_ABOVE | NG_HIGHLABEL; ng.ng_GadgetID = FG_FONTS;
    gad = fs.g[FG_FONTS] = CreateGadget(LISTVIEW_KIND, gad, &ng, GTLV_Labels, (ULONG)&fs.flist, GTLV_ShowSelected, 0, TAG_END);
    ng.ng_LeftEdge = (WORD)(bl + 20 + lw); ng.ng_Width = 64;
    ng.ng_GadgetText = (UBYTE *)L(S_SIZE); ng.ng_GadgetID = FG_SIZES;
    gad = fs.g[FG_SIZES] = CreateGadget(LISTVIEW_KIND, gad, &ng, GTLV_Labels, (ULONG)&fs.slist, GTLV_ShowSelected, 0, TAG_END);
    for (i = 0; i < 3; i++) {
        static const int sty[3] = { S_BOLD, S_ITALIC, S_UNDERLINE };
        ng.ng_LeftEdge = (WORD)(bl + 20 + lw + 64 + 16); ng.ng_TopEdge = (WORD)(bt + top + i * (hchar + 10));
        ng.ng_Width = dlg_cbw(); ng.ng_Height = dlg_cbh();
        ng.ng_GadgetText = (UBYTE *)L(sty[i]); ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_GadgetID = (UWORD)(FG_BOLD + i);
        gad = fs.g[FG_BOLD + i] = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Scaled, TRUE, TAG_END);
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

