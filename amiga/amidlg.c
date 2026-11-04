/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * amidlg.c - dialogs built in code on GadTools, popup menus, the file
 *            requester, alerts and the progress gauge (after gemdlg.c)
 *
 * Dialogs are described row by row, as in the GEM version, and laid out
 * when they are finished: labels line up in a column, a row that would
 * be too wide wraps, buttons go bottom right. A group of radio buttons
 * becomes one cycle gadget, the Amiga way. Edit fields hold text in the
 * ST character set, like everything else in EdWord.
 */
#include <exec/memory.h>
#include <intuition/gadgetclass.h>
#include <intuition/sghooks.h>
#include <devices/inputevent.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "amiui.h"

extern struct Library *AslBase;

enum { T_LABEL, T_TEXT, T_RADIO, T_CHECK, T_EDIT, T_BUTTON, T_BIG, T_TITLE };

/* Esc in an edit field ends the dialog, as it does elsewhere */
extern void ami_hookentry(void);
static ULONG esc_edit(struct Hook *h, struct SGWork *sgw, ULONG *msg)
{
    (void)h;
    if (*msg != SGH_KEY) return 0;
    if (sgw->IEvent && sgw->IEvent->ie_Class == IECLASS_RAWKEY && sgw->IEvent->ie_Code == 0x45) {
        sgw->Actions |= SGA_END;
        sgw->Actions &= ~SGA_USE;
        sgw->Code = 27;
    }
    return ~0UL;
}
static struct Hook esc_hook = { { NULL, NULL }, (ULONG (*)())ami_hookentry, (ULONG (*)())esc_edit, NULL };

#define MARGIN 10
#define GAP    10
static short rh(void) { return (short)(hchar + 8); }      /* row height */

/* ================================================================= */
/*  The dialog builder                                                */
/* ================================================================= */

static short item(DLG *g, short type, const char *s)
{
    DITEM *it;
    if (g->nit >= MAXDITEM) return (short)(g->nit - 1);
    it = &g->it[g->nit];
    memset(it, 0, sizeof *it);
    it->type = type;
    it->s = s ? s : "";
    it->row = g->row;
    it->lead = -1;
    return g->nit++;
}

void dlg_begin(DLG *g, const char *title)
{
    memset(g, 0, sizeof *g);
    g->framed = 1;
    g->edit1 = -1;
    g->def = -1;
    g->title = title;
    g->row = 1;
}

void dlg_row(DLG *g) { g->row++; }
void dlg_label(DLG *g, const char *s) { item(g, T_LABEL, s); }
short dlg_text(DLG *g, const char *s) { return item(g, T_TEXT, s); }

short dlg_radio(DLG *g, int group, const char *s, int on)
{
    short i = item(g, T_RADIO, s);
    g->it[i].group = (short)group;
    g->it[i].on = on;
    return i;
}

short dlg_check(DLG *g, const char *s, int on)
{
    short i = item(g, T_CHECK, s);
    g->it[i].on = on;
    return i;
}

/* an editable field; tmpl is "Prompt: ___ after" with '_' for each character */
short dlg_edit(DLG *g, const char *tmpl, char *buf, const char *valid)
{
    short i = item(g, T_EDIT, tmpl);
    DITEM *it = &g->it[i];
    const char *u = strchr(tmpl, '_'), *e;
    int k;
    it->tmpl = tmpl;
    it->buf = buf;
    it->valid = valid;
    if (!u) u = tmpl + strlen(tmpl);
    k = (int)(u - tmpl);
    while (k > 0 && tmpl[k - 1] == ' ') k--;
    if (k > 39) k = 39;
    memcpy(it->label, tmpl, (size_t)k);
    it->label[k] = 0;
    for (e = u; *e == '_'; e++) ;
    it->n = (short)(e - u);
    while (*e == ' ') e++;
    strncpy(it->after, e, 39);
    if (g->edit1 < 0) g->edit1 = i;
    return i;
}

short dlg_button(DLG *g, const char *s, int flags)
{
    short i = item(g, T_BUTTON, s);
    g->it[i].flags = (short)flags;
    if (flags & OF_DEFAULT) g->def = i;
    return i;
}

short dlg_bigbutton(DLG *g, const char *s, int wc, int flags)
{
    short i = item(g, T_BIG, s);
    g->it[i].flags = (short)flags;
    g->it[i].wc = (short)wc;
    return i;
}

int dlg_on(DLG *g, short i)
{
    DITEM *it;
    if (i < 0 || i >= g->nit) return 0;
    it = &g->it[i];
    if (it->type == T_RADIO) return it->lead >= 0 && g->it[it->lead].on == it->n;
    return it->on;
}

void dlg_show(DLG *g, short i, int show) { if (i >= 0 && i < g->nit) g->it[i].hidden = (short)!show; }
void dlg_disable(DLG *g, short i, int dis) { if (i >= 0 && i < g->nit) g->it[i].disabled = (short)dis; }

/* the width of an item in pixels */
static short item_w(DLG *g, DITEM *it)
{
    long w = 0;
    int k;
    switch (it->type) {
    case T_LABEL: case T_TEXT: case T_TITLE: w = sys_width(it->s); break;
    case T_CHECK: w = 26 + 8 + sys_width(it->s); break;
    case T_RADIO:
        if (it->lead != (short)(it - g->it)) return 0;
        for (k = (int)(it - g->it); k < g->nit && g->it[k].lead == it->lead; k++)
            if (sys_width(g->it[k].s) > w) w = sys_width(g->it[k].s);
        w += 34;
        break;
    case T_EDIT:
        w = (it->n < 3 ? 3 : it->n) * wchar + 14;
        if (it->label[0]) w += sys_width(it->label) + 6;
        if (it->after[0]) w += 6 + sys_width(it->after);
        break;
    case T_BUTTON: w = sys_width(it->s) + 20; if (w < 64) w = 64; break;
    case T_BIG: w = it->wc * wchar; break;
    }
    return (short)w;
}

/* lay out the items (pixels) */
void dlg_end(DLG *g)
{
    short labw = 0, vrow = 0, lastrow = -1, x = 0, maxx = 0, nbtn = 0, btnw = 0, i, k;
    short nbrow, brw = 0, maxw = (short)(scr_w - 60 > 600 ? 600 : scr_w - 60);

    /* radio groups: one cycle gadget each, at the first item */
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        if (it->type != T_RADIO || it->lead >= 0) continue;
        it->lead = i;
        it->n = 0;
        it->nopt = 1;
        it->on = it->on ? 0 : -1;
        for (k = i + 1; k < g->nit && g->it[k].type == T_RADIO && g->it[k].group == it->group; k++) {
            g->it[k].lead = i;
            g->it[k].n = it->nopt;
            if (g->it[k].on && it->on < 0) it->on = it->nopt;
            it->nopt++;
        }
        if (it->on < 0) it->on = 0;
    }
    for (i = 0; i < g->nit; i++)
        if (g->it[i].type == T_LABEL && sys_width(g->it[i].s) + 12 > labw) labw = (short)(sys_width(g->it[i].s) + 12);
    if (labw > maxw / 2) labw = (short)(maxw / 2);

    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        short w;
        if (it->type == T_BUTTON) { nbtn++; if (item_w(g, it) > btnw) btnw = item_w(g, it); continue; }
        if (it->type == T_RADIO && it->lead != i) continue;
        w = item_w(g, it);
        it->gw = w;
        if (it->row != lastrow) {
            vrow++;
            lastrow = it->row;
            x = labw;
            if (it->type == T_LABEL) { it->row = vrow; it->col = 0; continue; }
        }
        if (it->type == T_LABEL) { vrow++; it->row = vrow; it->col = 0; x = labw; continue; }
        if (x + w > maxw && x > labw) { vrow++; x = labw; }
        it->row = vrow;
        it->col = x;
        x = (short)(x + w + GAP);
        if (x > maxx) maxx = x;
    }
    /* buttons: all as wide as the widest if they fit */
    nbrow = 0;
    if (nbtn) {
        int uniform = nbtn * (btnw + GAP) <= maxw, rw = 0;
        nbrow = 1;
        for (i = 0; i < g->nit; i++) {
            DITEM *it = &g->it[i];
            short w;
            if (it->type != T_BUTTON) continue;
            w = uniform ? btnw : item_w(g, it);
            if (rw && rw + w > maxw) { nbrow++; rw = 0; }
            it->gw = w;
            it->row = (short)(nbrow - 1);
            rw += w + GAP;
            if (rw > brw) brw = (short)rw;
        }
    }
    g->w = (short)((maxx > brw ? maxx : brw) + 2 * MARGIN);
    if (g->title && sys_width(g->title) + 80 > g->w) g->w = (short)(sys_width(g->title) + 80);
    if (g->w < 200) g->w = 200;
    g->nrows = vrow;
    g->h = (short)(MARGIN + vrow * rh() + (nbtn ? nbrow * rh() + 6 : 0) + MARGIN);
    /* right-align each row of buttons */
    for (k = 0; k < nbrow; k++) {
        short bx = (short)(g->w - MARGIN);
        for (i = g->nit - 1; i >= 0; i--) {
            DITEM *it = &g->it[i];
            if (it->type != T_BUTTON || it->row != k) continue;
            bx = (short)(bx - it->gw);
            it->col = (short)(bx - MARGIN);
            bx -= GAP;
        }
    }
    /* final positions in the window's inner area */
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        it->gx = (short)(MARGIN + it->col);
        if (it->type == T_BUTTON) it->gy = (short)(MARGIN + g->nrows * rh() + 6 + it->row * rh());
        else it->gy = (short)(MARGIN + (it->row - 1) * rh());
    }
}

/* make the gadgets for the window g->win */
int dlg_gadgets(DLG *g)
{
    struct Gadget *gad;
    struct NewGadget ng;
    int i, k;
    long bl = g->win->BorderLeft, bt = g->win->BorderTop;
    g->glist = NULL;
    g->ncyc = 0;
    gad = CreateContext(&g->glist);
    if (!gad) return 0;
    for (i = 0; i < g->nit && gad; i++) {
        DITEM *it = &g->it[i];
        it->gad = NULL;
        if (it->hidden || it->type == T_TITLE || (it->type == T_RADIO && it->lead != i)) continue;
        memset(&ng, 0, sizeof ng);
        ng.ng_TextAttr = &sysattr;
        ng.ng_VisualInfo = vi;
        ng.ng_LeftEdge = (WORD)(bl + it->gx);
        ng.ng_TopEdge = (WORD)(bt + it->gy);
        ng.ng_Width = it->gw;
        ng.ng_Height = (WORD)(hchar + 6);
        ng.ng_GadgetID = (UWORD)(i + 1);
        switch (it->type) {
        case T_LABEL: case T_TEXT:
            ng.ng_Width = (WORD)(it->gw + 4);
            gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)it->s, TAG_END);
            break;
        case T_CHECK:
            ng.ng_Width = 26; ng.ng_Height = 11;
            ng.ng_TopEdge += (WORD)((hchar + 6 - 11) / 2);
            ng.ng_GadgetText = (UBYTE *)it->s;
            ng.ng_Flags = PLACETEXT_RIGHT;
            gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked, (ULONG)it->on, GA_Disabled, (ULONG)it->disabled, TAG_END);
            break;
        case T_RADIO:
            it->gw = item_w(g, it);
            ng.ng_Width = it->gw;
            k = g->ncyc;
            {
                int j;
                for (j = i; j < g->nit && g->it[j].lead == i && g->ncyc < MAXDITEM + 22; j++) g->cyc[g->ncyc++] = g->it[j].s;
                g->cyc[g->ncyc++] = NULL;
            }
            gad = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, (ULONG)&g->cyc[k], GTCY_Active, (ULONG)it->on,
                               GA_Disabled, (ULONG)it->disabled, TAG_END);
            break;
        case T_EDIT: {
            long lw = it->label[0] ? sys_width(it->label) + 6 : 0, aw = it->after[0] ? 6 + sys_width(it->after) : 0;
            ng.ng_LeftEdge += (WORD)lw;
            ng.ng_Width = (WORD)(it->gw - lw - aw);
            ng.ng_GadgetText = (UBYTE *)(it->label[0] ? it->label : NULL);
            ng.ng_Flags = PLACETEXT_LEFT;
            gad = CreateGadget(STRING_KIND, gad, &ng, GTST_String, (ULONG)lat(it->buf),
                               GTST_MaxChars, (ULONG)(it->valid ? strlen(it->valid) : 60),
                               GTST_EditHook, (ULONG)&esc_hook, GA_Disabled, (ULONG)it->disabled, TAG_END);
            it->gad = gad;
            if (gad && it->after[0]) {
                struct NewGadget an = ng;
                an.ng_LeftEdge = (WORD)(ng.ng_LeftEdge + ng.ng_Width + 6);
                an.ng_Width = (WORD)(aw);
                an.ng_GadgetText = NULL;
                an.ng_GadgetID = 0;
                gad = CreateGadget(TEXT_KIND, gad, &an, GTTX_Text, (ULONG)it->after, TAG_END);
            }
            continue;
        }
        case T_BUTTON: case T_BIG:
            ng.ng_GadgetText = (UBYTE *)it->s;
            ng.ng_Flags = PLACETEXT_IN;
            gad = CreateGadget(BUTTON_KIND, gad, &ng, GA_Disabled, (ULONG)it->disabled, TAG_END);
            break;
        }
        it->gad = gad;
    }
    return gad != NULL;
}

void dlg_free_gadgets(DLG *g)
{
    if (g->glist) FreeGadgets(g->glist);
    g->glist = NULL;
}

/* copy the edit fields (to the ST character set) and check boxes back */
void dlg_fetch(DLG *g)
{
    int i;
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        if (!it->gad) continue;
        if (it->type == T_EDIT) {
            struct StringInfo *si = (struct StringInfo *)it->gad->SpecialInfo;
            int max = it->valid ? (int)strlen(it->valid) + 1 : 61;
            stc(it->buf, (const char *)si->Buffer, max);
        } else if (it->type == T_CHECK) it->on = (it->gad->Flags & GFLG_SELECTED) != 0;
    }
}

short dlg_item_of(DLG *g, struct Gadget *gad)
{
    int i = (int)gad->GadgetID - 1;
    return i >= 0 && i < g->nit ? (short)i : -1;
}

/* a message for a dialog window; 1 when it chose an item (-1: cancel) */
int dlg_event(DLG *g, struct IntuiMessage *m, short *item, int *clicks)
{
    short i;
    switch (m->Class) {
    case IDCMP_GADGETUP:
        i = dlg_item_of(g, (struct Gadget *)m->IAddress);
        if (i < 0) return 0;
        switch (g->it[i].type) {
        case T_BUTTON: case T_BIG:
            if (clicks) {
                *clicks = i == g->lastitem && DoubleClick(g->lsecs, g->lmics, m->Seconds, m->Micros) ? 2 : 1;
                g->lastitem = *clicks == 2 ? -1 : i;
                g->lsecs = m->Seconds; g->lmics = m->Micros;
            }
            *item = i;
            return 1;
        case T_CHECK:
            g->it[i].on = (((struct Gadget *)m->IAddress)->Flags & GFLG_SELECTED) != 0;
            return 0;
        case T_RADIO:
            g->it[i].on = m->Code;
            return 0;
        case T_EDIT:
            if (m->Code == 27) { *item = -1; return 1; }          /* Esc in a field */
            if (m->Code == 9 || g->def < 0) return 0;
            *item = g->def;
            if (clicks) *clicks = 1;
            return 1;
        }
        return 0;
    case IDCMP_VANILLAKEY:
        if (m->Code == 13 && g->def >= 0) { *item = g->def; if (clicks) *clicks = 1; return 1; }
        if (m->Code == 27) { *item = -1; return 1; }
        return 0;
    case IDCMP_CLOSEWINDOW:
        *item = -1;
        return 1;
    case IDCMP_REFRESHWINDOW:
        GT_BeginRefresh(m->IDCMPWindow);
        GT_EndRefresh(m->IDCMPWindow, TRUE);
        return 0;
    }
    return 0;
}

#define DLG_IDCMP (BUTTONIDCMP | CHECKBOXIDCMP | CYCLEIDCMP | STRINGIDCMP | TEXTIDCMP | \
                   IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW)

/* open a window for the dialog and put its gadgets in; x < 0: centred */
struct Window *dlg_window(DLG *g, const char *title, long x, long y, struct MsgPort *port, ULONG idcmp)
{
    struct Window *w;
    long ww = g->w + scr->WBorLeft + scr->WBorRight + 4;
    long wh = g->h + scr->WBorTop + scr->Font->ta_YSize + 1 + scr->WBorBottom;
    if (x < 0) x = (scr_w - ww) / 2;
    if (y < 0) y = desk.g_y + (desk.g_h - wh) / 2;
    if (x + ww > scr_w) x = scr_w - ww;
    if (y + wh > scr_h) y = scr_h - wh;
    if (x < 0) x = 0;
    if (y < desk.g_y) y = desk.g_y;
    if (port) {
        w = open_ewin((struct TagItem *)(ULONG[]){ WA_Left, (ULONG)x, WA_Top, (ULONG)y,
            WA_InnerWidth, (ULONG)g->w, WA_InnerHeight, (ULONG)g->h, WA_Title, (ULONG)title,
            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SmartRefresh, TRUE,
            WA_Activate, TRUE, TAG_END }, idcmp | DLG_IDCMP);
    } else {
        w = OpenWindowTags(NULL, SCR_TAG, (ULONG)scr, WA_Left, (ULONG)x, WA_Top, (ULONG)y,
            WA_InnerWidth, (ULONG)g->w, WA_InnerHeight, (ULONG)g->h, WA_Title, (ULONG)title,
            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SmartRefresh, TRUE,
            WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_IDCMP, idcmp | DLG_IDCMP, TAG_END);
    }
    if (!w) return NULL;
    g->win = w;
    if (!dlg_gadgets(g)) { dlg_free_gadgets(g); if (port) close_ewin(w); else CloseWindow(w); g->win = NULL; return NULL; }
    AddGList(w, g->glist, (UWORD)~0, -1, NULL);
    RefreshGList(g->glist, w, NULL, -1);
    GT_RefreshWindow(w, NULL);
    return w;
}

/* new gadgets after texts or visibility changed (tool windows) */
void dlg_rebuild(DLG *g)
{
    struct Window *w = g->win;
    if (!w) return;
    dlg_fetch(g);
    RemoveGList(w, g->glist, -1);
    dlg_free_gadgets(g);
    SetAPen(w->RPort, PEN(P_GREY));
    RectFill(w->RPort, w->BorderLeft, w->BorderTop, w->Width - w->BorderRight - 1, w->Height - w->BorderBottom - 1);
    if (!dlg_gadgets(g)) return;
    AddGList(w, g->glist, (UWORD)~0, -1, NULL);
    RefreshGList(g->glist, w, NULL, -1);
    GT_RefreshWindow(w, NULL);
}

/* run the dialog; returns the item that ended it, -1 for cancel */
short dlg_run(DLG *g)
{
    struct Window *w;
    struct IntuiMessage *m;
    short item = -1;
    int done = 0;
    block_input(1);
    w = dlg_window(g, g->title ? g->title : "EdWord", -1, -1, NULL, 0);
    if (!w) { block_input(0); return -1; }
    if (g->edit1 >= 0 && g->it[g->edit1].gad) ActivateGadget(g->it[g->edit1].gad, w, NULL);
    while (!done) {
        ULONG sig = Wait((1UL << w->UserPort->mp_SigBit) | (1UL << idcmp_port->mp_SigBit));
        if (sig & (1UL << idcmp_port->mp_SigBit)) pump_messages();
        while (!done && (m = GT_GetIMsg(w->UserPort)) != NULL) {
            struct IntuiMessage cm = *m;
            GT_ReplyIMsg(m);
            done = dlg_event(g, &cm, &item, NULL);
        }
    }
    dlg_fetch(g);
    CloseWindow(w);
    dlg_free_gadgets(g);
    g->win = NULL;
    block_input(0);
    return item;
}

/* ================================================================= */
/*  Alerts                                                            */
/* ================================================================= */

int alert(int def, int icon, int text, int buttons, ...)
{
    char t[300], f[400], *p, *q;
    va_list ap;
    struct EasyStruct es;
    struct Window *w = backdrop;
    DOC *d = top_doc();
    int nb = 1, r;
    (void)def; (void)icon;
    va_start(ap, buttons);
    vsprintf(t, L(text), ap);
    va_end(ap);
    for (p = t, q = f; *p && q < f + sizeof f - 2; p++) {      /* | splits lines; % is literal */
        if (*p == '|') *q++ = '\n';
        else if (*p == '%') { *q++ = '%'; *q++ = '%'; }
        else *q++ = *p;
    }
    *q = 0;
    for (p = (char *)L(buttons); *p; p++) if (*p == '|') nb++;
    if (d && doc_win(d)) w = doc_win(d);
    es.es_StructSize = sizeof es;
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"EdWord";
    es.es_TextFormat = (UBYTE *)f;
    es.es_GadgetFormat = (UBYTE *)L(buttons);
    block_input(1);
    r = (int)EasyRequestArgs(w, &es, NULL, NULL);
    block_input(0);
    return r == 0 ? nb : r;           /* EasyRequest: the rightmost is 0 */
}

/* ================================================================= */
/*  Popup menu                                                        */
/* ================================================================= */

int popup(const char **items, int n, int cur, short x, short y)
{
    struct Window *w;
    struct IntuiMessage *m;
    long iw = 0, ih = hchar + 2, ww, wh;
    int i, hl = -1, done = 0, res = -1, seen_down = 0;
    if (n > 40) n = 40;
    for (i = 0; i < n; i++) if (sys_width(items[i]) > iw) iw = sys_width(items[i]);
    ww = iw + 28;
    wh = n * ih + 4;
    if (wh > desk.g_h) { n = (int)((desk.g_h - 4) / ih); wh = n * ih + 4; }
    if (x + ww > scr_w) x = (short)(scr_w - ww);
    if (y + wh > scr_h) y = (short)(scr_h - wh);
    if (x < 0) x = 0;
    if (y < desk.g_y) y = desk.g_y;
    w = OpenWindowTags(NULL, SCR_TAG, (ULONG)scr, WA_Left, (ULONG)x, WA_Top, (ULONG)y, WA_Width, (ULONG)ww,
                       WA_Height, (ULONG)wh, WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                       WA_ReportMouse, TRUE, WA_SmartRefresh, TRUE,
                       WA_IDCMP, IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_INACTIVEWINDOW | IDCMP_VANILLAKEY, TAG_END);
    if (!w) return -1;
    gfx_begin(w);
    fill_rect(0, 0, ww - 1, wh - 1, P_GREY);
    frame(0, 0, ww - 1, wh - 1, P_BLACK);
    line(1, 1, ww - 2, 1, P_WHITE);
    for (i = 0; i < n; i++) {
        long ty = 2 + i * ih;
        if (items[i][0] == '-' && !items[i][1]) { line(4, ty + ih / 2, ww - 5, ty + ih / 2, P_BLACK); continue; }
        if (i == cur) {                               /* a check mark */
            line(5, ty + ih / 2, 8, ty + ih - 3, P_BLACK);
            line(8, ty + ih - 3, 13, ty + 2, P_BLACK);
            line(6, ty + ih / 2, 9, ty + ih - 3, P_BLACK);
        }
        sys_text(18, ty + 1, items[i], P_BLACK);
    }
    gfx_end();
    while (!done) {
        WaitPort(w->UserPort);
        while (!done && (m = (struct IntuiMessage *)GetMsg(w->UserPort)) != NULL) {
            ULONG cl = m->Class;
            UWORD code = m->Code;
            long my = m->MouseY, mx = m->MouseX;
            ReplyMsg((struct Message *)m);
            if (cl == IDCMP_INACTIVEWINDOW || (cl == IDCMP_VANILLAKEY && code == 27)) { done = 1; break; }
            if (cl == IDCMP_MOUSEMOVE || cl == IDCMP_MOUSEBUTTONS) {
                int o = mx >= 0 && mx < ww && my >= 2 && my < 2 + n * ih ? (int)((my - 2) / ih) : -1;
                if (o >= 0 && items[o][0] == '-' && !items[o][1]) o = -1;
                if (o != hl) {
                    gfx_begin(w);
                    if (hl >= 0) xor_rect(2, 2 + hl * ih, ww - 3, 1 + (hl + 1) * ih);
                    if (o >= 0) xor_rect(2, 2 + o * ih, ww - 3, 1 + (o + 1) * ih);
                    gfx_end();
                    hl = o;
                }
                if (cl == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                    seen_down = 1;
                    if (o < 0) done = 1;
                }
                if (cl == IDCMP_MOUSEBUTTONS && code == SELECTUP && (o >= 0 || seen_down)) { res = o; done = 1; }
            }
        }
    }
    CloseWindow(w);
    return res;
}

/* ================================================================= */
/*  File requester                                                    */
/* ================================================================= */

static char seldir[256];

/* without asl.library: a dialog with the whole path */
static int ask_path(const char *title, const char *defname, char *out)
{
    static char buf[200];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short ok;
    strcpy(buf, seldir);
    if (defname) AddPart((STRPTR)buf, (CONST_STRPTR)defname, sizeof buf);
    dlg_begin(&g, title);
    dlg_label(&g, L(S_AMI_FILE));
    dlg_edit(&g, "______________________________________________", buf,
             "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    {
        char lb[200];
        strcpy(lb, lat(buf));                   /* paths are Latin-1 */
        if (!lb[0]) return 0;
        strcpy(out, lb);
        strcpy(seldir, lb);
        *PathPart((STRPTR)seldir) = 0;
    }
    return 1;
}

int ask_file(const char *title, const char *pattern, const char *defname, char *out, int save)
{
    struct FileRequester *fr;
    int ok = 0;
    DOC *d = top_doc();
    struct Window *w = d && doc_win(d) ? doc_win(d) : backdrop;
    if (!AslBase) return ask_path(title, defname, out);
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest, ASL_Hail, (ULONG)title, ASL_Window, (ULONG)w,
             ASL_Dir, (ULONG)seldir, ASL_File, (ULONG)(defname ? defname : ""), ASL_Pattern, (ULONG)pattern,
             ASL_FuncFlags, FILF_PATGAD | (save ? FILF_SAVE : 0), TAG_END);
    if (!fr) return ask_path(title, defname, out);
    block_input(1);
    if (AslRequest(fr, NULL) && fr->rf_File && fr->rf_File[0]) {
        strncpy(seldir, (const char *)fr->rf_Dir, sizeof seldir - 1);
        strcpy(out, seldir);
        if (AddPart((STRPTR)out, fr->rf_File, 256)) ok = 1;
    }
    block_input(0);
    FreeAslRequest(fr);
    return ok;
}

/* ================================================================= */
/*  Small helpers for the dialogs below                               */
/* ================================================================= */

static int pt2mm(int pt) { return (int)((pt * 254L + (pt >= 0 ? 360 : -360)) / 720); }
static int mm2pt(int mm) { return (int)((mm * 720L + (mm >= 0 ? 127 : -127)) / 254); }
static int field_pt(const char *buf, int old) { int mm = atoi(buf); return mm == pt2mm(old) ? old : mm2pt(mm); }

static char tmpls[24][48];
static int  ntmpl;
static const char *tmpl(const char *prompt, int n, const char *after)
{
    char *t = tmpls[ntmpl++ % 24];
    int k = 0, i;
    if (prompt && prompt[0]) k = sprintf(t, "%s ", prompt);
    for (i = 0; i < n && k < 40; i++) t[k++] = '_';
    t[k] = 0;
    if (after && after[0]) { strcat(t, " "); strcat(t, after); }
    return t;
}

static const char digits[] = "9999999999999999";
static const char anyc[]   = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
static const char *valid_n(int n) { return digits + 16 - n; }
static const char *valid_x(int n) { return anyc + 64 - n; }

/* ================================================================= */
/*  Page layout                                                       */
/* ================================================================= */

int dlg_page(DOC *d)
{
    static char mt[4], mb[4], ml[4], mr[4], nc[3], gap[4], wc[4];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    PAGESET *ps = &PS(d);
    PGFMT f;
    short paper[NPAPER], land, one, after, ok, i;
    int page = d->nlines ? d->lines[doc_find_line(d, d->cursor)].page : 0, v;

    page_fmt(d, page, &f);
    sprintf(mt, "%d", pt2mm(f.mt)); sprintf(mb, "%d", pt2mm(f.mb));
    sprintf(ml, "%d", pt2mm(f.ml)); sprintf(mr, "%d", pt2mm(f.mr));
    sprintf(nc, "%d", f.cols); sprintf(gap, "%d", pt2mm(f.colgap));
    sprintf(wc, "%d", ps->wrapcol);

    dlg_begin(&g, L(S_PAGE_TITLE));
    dlg_label(&g, L(S_PAPER));
    for (i = 0; i < NPAPER; i++) paper[i] = dlg_radio(&g, 1, paper_name[i], ps->paper == i);
    dlg_row(&g);
    dlg_label(&g, L(S_ORIENT));
    dlg_radio(&g, 2, L(S_PORTRAIT), !ps->landscape);
    land = dlg_radio(&g, 2, L(S_LANDSCAPE), ps->landscape);
    dlg_row(&g);
    dlg_label(&g, L(S_MARGINS_MM));
    dlg_edit(&g, tmpl(L(S_TOP), 3, NULL), mt, valid_n(3));
    dlg_edit(&g, tmpl(L(S_BOTTOM), 3, NULL), mb, valid_n(3));
    dlg_edit(&g, tmpl(L(S_LEFT_M), 3, NULL), ml, valid_n(3));
    dlg_edit(&g, tmpl(L(S_RIGHT_M), 3, NULL), mr, valid_n(3));
    dlg_row(&g);
    dlg_label(&g, L(S_COLUMNS));
    dlg_edit(&g, tmpl(NULL, 1, NULL), nc, valid_n(1));
    dlg_edit(&g, tmpl(L(S_GAP), 3, L(S_MM)), gap, valid_n(3));
    dlg_row(&g);
    dlg_label(&g, L(S_WRAPCOL));
    dlg_edit(&g, tmpl(NULL, 3, L(S_ZERO_OFF)), wc, valid_n(3));
    dlg_row(&g);
    dlg_label(&g, L(S_APPLY_TO));
    {
        int k, how = d->a.novr ? 1 : 0;          /* 0 whole, 1 this page, 2 onwards */
        for (k = 0; k < d->a.novr; k++) {
            PGOVR *o = &d->a.ovr[k];
            if (o->page == page) { how = o->following ? 2 : 1; break; }
            if (o->page < page && o->following) how = 2;
        }
        dlg_radio(&g, 3, L(S_WHOLE_DOC), how == 0);
        one = dlg_radio(&g, 3, L(S_THIS_PAGE), how == 1);
        after = dlg_radio(&g, 3, L(S_THIS_PAGE_ON), how == 2);
    }
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;

    for (i = 0; i < NPAPER; i++)
        if (dlg_on(&g, paper[i])) break;
    v = dlg_on(&g, land) ? 1 : 0;
    if (i < NPAPER && (i != ps->paper || v != ps->landscape)) page_set_paper(ps, i, v);
    else if (v != ps->landscape) { short t = ps->w; ps->w = ps->h; ps->h = t; ps->landscape = (short)v; }
    f.mt = (short)field_pt(mt, f.mt); f.mb = (short)field_pt(mb, f.mb);
    f.ml = (short)field_pt(ml, f.ml); f.mr = (short)field_pt(mr, f.mr);
    v = atoi(nc);
    f.cols = (short)(v < 1 ? 1 : v > 4 ? 4 : v);
    f.colgap = (short)field_pt(gap, f.colgap);
    v = atoi(wc);
    ps->wrapcol = (short)(v > 250 ? 250 : v);
    if (dlg_on(&g, one) || dlg_on(&g, after)) page_ovr_set(d, page, dlg_on(&g, after), &f);
    else {
        ps->mt = f.mt; ps->mb = f.mb; ps->ml = f.ml; ps->mr = f.mr;
        ps->cols = f.cols; ps->colgap = f.colgap;
        page_ovr_clear(d);
    }
    if (ps->ml + ps->mr > ps->w - 85) ps->ml = ps->mr = (short)((ps->w - 85) / 2);
    if (ps->mt + ps->mb > ps->h - 85) ps->mt = ps->mb = (short)((ps->h - 85) / 2);
    d->dirty = 1;
    return 1;
}

int dlg_wrapcol(DOC *d)
{
    static char wc[4];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short ok;
    int v;
    sprintf(wc, "%d", PS(d).wrapcol);
    dlg_begin(&g, L(S_WRAP_TITLE));
    dlg_label(&g, L(S_WRAPCOL));
    dlg_edit(&g, tmpl(NULL, 3, NULL), wc, valid_n(3));
    dlg_row(&g);
    dlg_text(&g, L(S_WRAP_HINT));
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    v = atoi(wc);
    PS(d).wrapcol = (short)(v > 250 ? 250 : v);
    d->dirty = 1;
    return 1;
}

int dlg_size(int *size)
{
    static char sz[4];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short ok;
    int v;
    sprintf(sz, "%d", *size);
    dlg_begin(&g, L(S_SIZE_TITLE));
    dlg_label(&g, L(S_POINT_SIZE));
    dlg_edit(&g, tmpl(NULL, 3, "(4-120)"), sz, valid_n(3));
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    v = atoi(sz);
    if (v < 4 || v > 120) return 0;
    *size = v;
    return 1;
}

/* ================================================================= */
/*  Progress gauge                                                    */
/* ================================================================= */

static struct Window *gw;
static int g_last;
static long gbx, gby, gbw, gbh;

void gauge_open(const char *title)
{
    long w = 300, h = 40;
    gw = OpenWindowTags(NULL, SCR_TAG, (ULONG)scr, WA_Left, (ULONG)((scr_w - w) / 2),
                        WA_Top, (ULONG)(desk.g_y + (desk.g_h - h) / 2), WA_InnerWidth, (ULONG)w, WA_InnerHeight, (ULONG)h,
                        WA_Title, (ULONG)title, WA_DragBar, TRUE, WA_SmartRefresh, TRUE, WA_RMBTrap, TRUE, TAG_END);
    g_last = -1;
    block_input(1);
    if (!gw) return;
    gbx = gw->BorderLeft + 10; gby = gw->BorderTop + 10; gbw = w - 20; gbh = 14;
    gfx_begin(gw);
    fill_rect(gw->BorderLeft, gw->BorderTop, gw->Width - gw->BorderRight - 1, gw->Height - gw->BorderBottom - 1, P_GREY);
    {
        GRECT r;
        r.g_x = (short)(gbx - 2); r.g_y = (short)(gby - 2); r.g_w = (short)(gbw + 4); r.g_h = (short)(gbh + 4);
        bevel(&r, 1);
    }
    gfx_end();
}

void gauge_set(long done, long total)
{
    int pct;
    char s[8];
    if (!gw) return;
    pct = total > 0 ? (int)(done * 100L / total) : 0;
    if (pct > 100) pct = 100;
    if (pct == g_last) return;
    g_last = pct;
    gfx_begin(gw);
    fill_rect(gbx, gby, gbx + gbw - 1, gby + gbh - 1, P_WHITE);
    if (pct) fill_rect(gbx, gby, gbx + gbw * pct / 100 - 1, gby + gbh - 1, P_BLUE);
    sprintf(s, "%d%%", pct);
    sys_text(gbx + (gbw - sys_width(s)) / 2, gby + (gbh - hchar) / 2, s, P_BLACK);
    gfx_end();
    pump_messages();
}

void gauge_close(void)
{
    if (gw) CloseWindow(gw);
    gw = NULL;
    block_input(0);
}

/* ---- a long job: the gauge comes up only when it takes a while ---- */

static const char *b_title;
static ULONG b_s, b_us;
static int b_shown, b_depth;

static void busy_hook(long v, long total)
{
    if (!b_shown) {
        ULONG s, us;
        long ms;
        CurrentTime(&s, &us);
        ms = (long)(s - b_s) * 1000L + ((long)us - (long)b_us) / 1000L;
        /* not for what is over in a moment, or nearly done already */
        if (ms < 330 || v > 600) return;
        pump_defer = 1;
        gauge_open(b_title);
        b_shown = 1;
    }
    gauge_set(v, total);
}

void busy_begin(const char *title)
{
    busy(1);
    if (b_depth++) return;
    b_title = title;
    CurrentTime(&b_s, &b_us);
    b_shown = 0;
    prog_part(0, 1000);
    prog_hook = busy_hook;
}

void busy_end(void)
{
    if (b_depth && --b_depth == 0) {
        prog_hook = NULL;
        if (b_shown) { gauge_close(); pump_defer = 0; pump_flush(); }
        b_shown = 0;
    }
    busy(0);
}

/* ================================================================= */
/*  Preferences                                                       */
/* ================================================================= */

/* the screen mode requester (asl.library 38, Workbench 2.1) for the
 * screen EdWord opens next time */
static int pick_screenmode(void)
{
    struct ScreenModeRequester *sm;
    int ok = 0;
    DOC *d = top_doc();
    struct Window *w = d && doc_win(d) ? doc_win(d) : backdrop;
    if (!AslBase || AslBase->lib_Version < 38) return 0;
    sm = (struct ScreenModeRequester *)AllocAslRequestTags(ASL_ScreenModeRequest,
             ASLSM_TitleText, (ULONG)L(S_SCR_PICK), ASLSM_Window, (ULONG)w,
             ASLSM_InitialDisplayID, (ULONG)(pref_modeid ? pref_modeid : HIRES_KEY),
             ASLSM_InitialDisplayWidth, (ULONG)(pref_scrw ? pref_scrw : 640),
             ASLSM_InitialDisplayHeight, (ULONG)(pref_scrh ? pref_scrh : 256),
             ASLSM_InitialDisplayDepth, (ULONG)(pref_scrdepth ? pref_scrdepth : 4),
             ASLSM_DoWidth, TRUE, ASLSM_DoHeight, TRUE, ASLSM_DoDepth, TRUE,
             ASLSM_MinWidth, 640, ASLSM_MinHeight, 200, ASLSM_MinDepth, 2, ASLSM_MaxDepth, 4, TAG_END);
    if (!sm) return 0;
    block_input(1);
    if (AslRequest(sm, NULL)) {
        pref_modeid = sm->sm_DisplayID;
        pref_scrw = (int)sm->sm_DisplayWidth;
        pref_scrh = (int)sm->sm_DisplayHeight;
        pref_scrdepth = (int)sm->sm_DisplayDepth;
        ok = 1;
    }
    block_input(0);
    FreeAslRequest(sm);
    return ok;
}

int dlg_prefs(void)
{
    static const int order[NDATEFMT] = { DT_SHORT, DT_US, DT_ISO, DT_LONG, DT_DAYLONG };
    static char dl[NDATEFMT][40], mins[3];
    char st[40];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    static const int scrname[NSCR] = { S_SCR_HIRES, S_SCR_LACE, S_SCR_LIKEWB, S_SCR_OTHER, S_SCR_WB };
    short scrr[NSCR];
    int nscr;
    short lang[NLANG], dict[NLANG], dr[NDATEFMT], pp[NPAPER], land, tbar, live, col, ps[3], dm[4], ason, asm1, ok;
    int i, v;

    for (i = 0; i < NDATEFMT; i++) { fmt_now(st, order[i]); strcpy(dl[i], lat(st)); }
    sprintf(mins, "%d", pref_asmin);
    dlg_begin(&g, L(S_PREFS_TITLE));
    dlg_label(&g, L(S_LANGUAGE));
    for (i = 0; i < NLANG; i++) lang[i] = dlg_radio(&g, 1, lang_name[i], ui_lang == i);
    dlg_row(&g);
    dlg_label(&g, L(S_DATE_FORMAT));
    for (i = 0; i < NDATEFMT; i++) dr[i] = dlg_radio(&g, 2, dl[i], pref_date == order[i]);
    dlg_row(&g);
    dlg_label(&g, L(S_NEW_PAGES));
    for (i = 0; i < NPAPER; i++) pp[i] = dlg_radio(&g, 3, paper_name[i], pref_paper == i);
    dlg_radio(&g, 4, L(S_PORTRAIT), !pref_land);
    land = dlg_radio(&g, 4, L(S_LANDSCAPE), pref_land);
    dlg_row(&g);
    dlg_label(&g, L(S_NEW_WINDOWS));
    tbar = dlg_check(&g, L(S_SHOW_TOOLBAR), pref_toolbar);
    dlg_row(&g);
    dlg_label(&g, L(S_SPELLING));
    live = dlg_check(&g, L(S_LIVE_SPELL), pref_live);
    dlg_row(&g);
    dlg_label(&g, L(S_DICTIONARY));
    for (i = 0; i < NLANG; i++) dict[i] = dlg_radio(&g, 8, lang_name[i], spell_lang == i);
    dlg_row(&g);
    dlg_label(&g, L(S_AMI_SCREEN));
    for (i = 0; i < NSCR; i++)
        scrr[i] = i == SCR_OTHER && (!AslBase || AslBase->lib_Version < 38) ? -1 : dlg_radio(&g, 9, L(scrname[i]), pref_screen == i);
    dlg_text(&g, L(S_AMI_RESTART));
    dlg_row(&g);
    dlg_label(&g, L(S_TEXT_COLOUR));
    col = dlg_check(&g, L(S_SHOW_COLOURS), colour_on);
    dlg_row(&g);
    dlg_label(&g, L(S_PDF_PICTURES));
    ps[0] = dlg_radio(&g, 5, L(S_SCREEN), pdf_scale == 1);
    ps[1] = dlg_radio(&g, 5, "2x", pdf_scale == 2);
    ps[2] = dlg_radio(&g, 5, "3x", pdf_scale == 3);
    dlg_row(&g);
    dlg_label(&g, L(S_EDITING));
    dm[0] = dlg_radio(&g, 6, L(S_D_NORMAL), pref_display == 0);
    dm[1] = dlg_radio(&g, 6, L(S_D_DARK), pref_display == 1);
    dm[2] = dlg_radio(&g, 6, L(S_D_GREEN), pref_display == 2);
    dm[3] = dlg_radio(&g, 6, L(S_D_AMBER), pref_display == 3);
    dlg_row(&g);
    dlg_label(&g, L(S_AUTOSAVE));
    ason = dlg_check(&g, L(S_ON), pref_autosave);
    dlg_edit(&g, tmpl(L(S_EVERY), 2, L(S_MINUTES)), mins, valid_n(2));
    dlg_radio(&g, 7, L(S_BACKUP_COPY), !pref_asmode);
    asm1 = dlg_radio(&g, 7, L(S_SAVE_DOC), pref_asmode);
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_SAVE_BTN), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;

    for (i = 0; i < 4; i++) if (dlg_on(&g, dm[i])) pref_display = i;
    nscr = pref_screen;
    for (i = 0; i < NSCR; i++) if (dlg_on(&g, scrr[i])) nscr = i;
    if (nscr != SCR_OTHER || pick_screenmode()) pref_screen = nscr;
    pref_autosave = dlg_on(&g, ason);
    v = atoi(mins);
    pref_asmin = v < 1 ? 1 : v > 99 ? 99 : v;
    pref_asmode = dlg_on(&g, asm1);
    for (i = 0; i < NDATEFMT; i++) if (dlg_on(&g, dr[i])) pref_date = order[i];
    for (i = 0; i < NPAPER; i++) if (dlg_on(&g, pp[i])) pref_paper = i;
    pref_land = dlg_on(&g, land);
    pref_toolbar = dlg_on(&g, tbar);
    pref_live = dlg_on(&g, live);
    colour_on = dlg_on(&g, col);
    for (i = 0; i < 3; i++) if (dlg_on(&g, ps[i])) pdf_scale = i + 1;
    for (i = 0; i < NLANG; i++) if (dlg_on(&g, lang[i]) && i != ui_lang) set_language(i);
    for (i = 0; i < NLANG; i++)
        if (dlg_on(&g, dict[i]) && i != spell_lang) { spell_unload_main(); spell_lang = i; }
    return 1;
}

/* ================================================================= */
/*  Headers and footers                                               */
/* ================================================================= */

int dlg_headfoot(DOC *d)
{
    static char hb[2][61], sz[3];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short jr[2][3], first, ok;
    int i, k, v;
    const char *jn[3];

    jn[0] = L(S_J_LEFT); jn[1] = L(S_J_CENTRE); jn[2] = L(S_J_RIGHT);
    for (i = 0; i < 2; i++) {
        strncpy(hb[i], HF(d).text[i], 60);
        hb[i][60] = 0;
    }
    sprintf(sz, "%d", HF(d).size);
    dlg_begin(&g, L(S_HF_TITLE));
    for (i = 0; i < 2; i++) {
        dlg_label(&g, L(i ? S_FOOTER : S_HEADER));
        dlg_edit(&g, tmpl(NULL, 40, NULL), hb[i], valid_x(60));
        for (k = 0; k < 3; k++) jr[i][k] = dlg_radio(&g, 1 + i, jn[k], HF(d).just[i] == k);
        dlg_row(&g);
    }
    dlg_label(&g, L(S_SIZE));
    dlg_edit(&g, tmpl(NULL, 2, L(S_POINT)), sz, valid_n(2));
    first = dlg_check(&g, L(S_FIRST_PAGE_TOO), HF(d).first);
    dlg_row(&g);
    dlg_label(&g, L(S_FIELDS));
    dlg_text(&g, "{page} {pages} {date} {time} {file}");
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    for (i = 0; i < 2; i++) {
        strcpy(HF(d).text[i], hb[i]);
        for (k = 0; k < 3; k++) if (dlg_on(&g, jr[i][k])) HF(d).just[i] = (unsigned char)k;
    }
    v = atoi(sz);
    HF(d).size = (unsigned char)(v < 4 ? 4 : v > 72 ? 72 : v);
    HF(d).first = (unsigned char)dlg_on(&g, first);
    d->dirty = 1;
    return 1;
}

/* ================================================================= */
/*  Paragraph indents                                                 */
/* ================================================================= */

int dlg_indents(DOC *d, int *left, int *first)
{
    static char lb[4], fb[5];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short ok;
    int l, f;
    (void)d;
    sprintf(lb, "%d", pt2mm(*left));
    sprintf(fb, "%d", pt2mm(*first));
    dlg_begin(&g, L(S_INDENTS_TITLE));
    dlg_label(&g, L(S_LEFT_INDENT));
    dlg_edit(&g, tmpl(NULL, 3, L(S_MM)), lb, valid_n(3));
    dlg_row(&g);
    dlg_label(&g, L(S_FIRST_LINE));
    dlg_edit(&g, tmpl(NULL, 4, L(S_MM)), fb, valid_x(4));
    dlg_row(&g);
    dlg_text(&g, L(S_HANG_HINT));
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    l = field_pt(lb, *left);
    f = field_pt(fb, *first);
    if (l < 0) l = 0;
    if (l > 400) l = 400;
    if (f < -l) f = -l;
    if (f > 400) f = 400;
    *left = l;
    *first = f;
    return 1;
}

/* ================================================================= */
/*  Tables                                                            */
/* ================================================================= */

int dlg_table(DOC *d, int insert, int *rows, int *cols, short *w, int *border)
{
    static char rb[4], cb[3], wb[MAX_COLS][4];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short ok, brd, i;
    int n;
    (void)d;
    sprintf(rb, "%d", *rows);
    sprintf(cb, "%d", *cols);
    dlg_begin(&g, L(insert ? S_TBL_INS_TITLE : S_TBL_PROP_TITLE));
    dlg_label(&g, L(S_ROWS));
    dlg_edit(&g, tmpl(NULL, 3, NULL), rb, valid_n(3));
    dlg_row(&g);
    dlg_label(&g, L(S_COLUMNS));
    dlg_edit(&g, tmpl(NULL, 2, "(1-12)"), cb, valid_n(2));
    dlg_row(&g);
    if (!insert) {
        dlg_label(&g, L(S_COL_WIDTHS));
        for (i = 0; i < *cols && i < MAX_COLS; i++) {
            sprintf(wb[i], "%d", pt2mm(w[i]));
            dlg_edit(&g, tmpl(NULL, 3, NULL), wb[i], valid_n(3));
        }
        dlg_row(&g);
    }
    dlg_label(&g, "");
    brd = dlg_check(&g, L(S_GRID_LINES), *border);
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(insert ? S_INSERT : S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    n = atoi(rb);
    *rows = n < 1 ? 1 : n > 200 ? 200 : n;
    n = atoi(cb);
    n = n < 1 ? 1 : n > MAX_COLS ? MAX_COLS : n;
    if (!insert)
        for (i = 0; i < *cols && i < MAX_COLS; i++) {
            int v = field_pt(wb[i], w[i]);
            w[i] = (short)(v < 12 ? 12 : v);
        }
    *cols = n;
    *border = dlg_on(&g, brd);
    return 1;
}

/* ================================================================= */
/*  Picture and drawing properties                                    */
/* ================================================================= */

/* returns 0 cancel, 1 OK, 2 delete, 3 edit the drawing */
int dlg_objprops(DOC *d, OBJ *o)
{
    static char xb[5], yb[5], wb[5], hb[5], pb[5];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short ok, del, edt = -1, wr[3], frame_, i;
    int v;
    sprintf(xb, "%d", pt2mm(o->x)); sprintf(yb, "%d", pt2mm(o->y));
    sprintf(wb, "%d", pt2mm(o->w)); sprintf(hb, "%d", pt2mm(o->h));
    sprintf(pb, "%d", o->page + 1);
    dlg_begin(&g, L(o->type == OB_IMG ? S_PIC_TITLE : S_DRAW_TITLE));
    dlg_label(&g, L(S_POSITION_MM));
    dlg_edit(&g, tmpl(L(S_FROM_LEFT), 4, NULL), xb, valid_n(4));
    dlg_edit(&g, tmpl(L(S_FROM_TOP), 4, NULL), yb, valid_n(4));
    dlg_row(&g);
    dlg_label(&g, L(S_SIZE_MM));
    dlg_edit(&g, tmpl(L(S_WIDTH), 4, NULL), wb, valid_n(4));
    dlg_edit(&g, tmpl(L(S_HEIGHT), 4, NULL), hb, valid_n(4));
    dlg_row(&g);
    dlg_label(&g, L(S_PAGE));
    dlg_edit(&g, tmpl(NULL, 4, NULL), pb, valid_n(4));
    dlg_row(&g);
    dlg_label(&g, L(S_TEXT_WRAP));
    wr[0] = dlg_radio(&g, 1, L(S_WRAP_NONE), o->wrap == WR_NONE);
    wr[1] = dlg_radio(&g, 1, L(S_WRAP_BOX), o->wrap == WR_BOX || (o->wrap == WR_SHAPE && o->type != OB_DRAW));
    wr[2] = o->type == OB_DRAW ? dlg_radio(&g, 1, L(S_WRAP_SHAPE), o->wrap == WR_SHAPE) : -1;
    dlg_row(&g);
    dlg_label(&g, "");
    frame_ = dlg_check(&g, L(S_FRAME), o->border);
    del = dlg_button(&g, L(S_DELETE), OF_EXIT);
    if (o->type == OB_DRAW) edt = dlg_button(&g, L(S_EDIT_DRAWING), OF_EXIT);
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    i = dlg_run(&g);
    if (i == del) return 2;
    if (i != ok && i != edt) return 0;
    o->x = (short)field_pt(xb, o->x); o->y = (short)field_pt(yb, o->y);
    v = field_pt(wb, o->w); o->w = (short)(v < 6 ? 6 : v);
    v = field_pt(hb, o->h); o->h = (short)(v < 6 ? 6 : v);
    v = atoi(pb) - 1;
    o->page = (short)(v < 0 ? 0 : v > doc_npages(d) ? doc_npages(d) : v);
    o->wrap = dlg_on(&g, wr[0]) ? WR_NONE : wr[2] >= 0 && dlg_on(&g, wr[2]) ? WR_SHAPE : WR_BOX;
    o->border = (short)dlg_on(&g, frame_);
    return i == edt ? 3 : 1;
}

/* ================================================================= */
/*  Document summary and About                                        */
/* ================================================================= */

void dlg_summary(DOC *d)
{
    static char v[8][16], name[44];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    DSTATS s;
    int i;
    static const int lab[8] = { S_ST_PAGES, S_ST_WORDS, S_ST_CHARS, S_ST_CHARS_NS, S_ST_PARAS, S_ST_LINES,
                                S_ST_TABLES, S_ST_OBJECTS };
    long val[8];
    doc_layout(d);
    doc_stats(d, &s);
    val[0] = s.pages; val[1] = s.words; val[2] = s.chars; val[3] = s.chars_ns;
    val[4] = s.paras; val[5] = s.lines; val[6] = s.tables; val[7] = s.objects;
    strcpy(name, lat(d->base));
    dlg_begin(&g, L(S_SUMMARY_TITLE));
    dlg_text(&g, name);
    dlg_row(&g);
    for (i = 0; i < 8; i++) {
        sprintf(v[i], "%8ld", val[i]);
        dlg_label(&g, L(lab[i]));
        dlg_text(&g, v[i]);
        dlg_row(&g);
    }
    dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    dlg_run(&g);
}

void dlg_about(void)
{
    static char vline[60];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    dlg_begin(&g, "EdWord");
    sprintf(vline, "%s %s", L(S_VERSION), ED_VERSION);
    dlg_text(&g, vline);
    dlg_row(&g);
    dlg_text(&g, ED_ABOUT[0] ? ED_ABOUT : L(S_AMI_ABOUT));
    dlg_row(&g);
    dlg_text(&g, L(S_ABOUT_LINE2));
    dlg_row(&g);
    dlg_text(&g, L(S_ABOUT_LINE3));
    dlg_row(&g);
    dlg_text(&g, L(S_ABOUT_THES));
    dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    dlg_run(&g);
}

/* ================================================================= */
/*  Text import / export options                                      */
/* ================================================================= */

int dlg_textimport(int eol, int det, int *cs, int *join)
{
    static const int eols[] = { S_EOL_NONE, S_EOL_LF, S_EOL_CRLF, S_EOL_CR, S_EOL_MIXED };
    static const char *css[] = { "", "Atari ST", "Amiga/Windows", "UTF-8", "UTF-16" };
    static char info[100];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short r[5], jn, ok;
    int i;

    sprintf(info, L(S_FOUND), L(eols[eol]), css[det]);
    dlg_begin(&g, L(S_IMPORT_TITLE));
    dlg_text(&g, info);
    dlg_row(&g);
    dlg_label(&g, L(S_CHARACTERS));
    r[0] = dlg_radio(&g, 1, L(S_AUTO), 1);
    r[1] = dlg_radio(&g, 1, "Atari ST", 0);
    r[2] = dlg_radio(&g, 1, "Amiga/Windows", 0);
    r[3] = dlg_radio(&g, 1, "UTF-8", 0);
    r[4] = dlg_radio(&g, 1, "UTF-16", 0);
    dlg_row(&g);
    dlg_label(&g, L(S_LINES));
    dlg_radio(&g, 2, L(S_ONE_PARA), 1);
    jn = dlg_radio(&g, 2, L(S_JOIN_LINES), 0);
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_IMPORT), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    *cs = CS_AUTO;
    for (i = 0; i < 5; i++) if (dlg_on(&g, r[i])) *cs = i;
    *join = dlg_on(&g, jn);
    return 1;
}

int dlg_textexport(int *crlf, int *cs)
{
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
    short dos, r[4], ok;
    dlg_begin(&g, L(S_EXPORT_TITLE));
    dlg_label(&g, L(S_LINE_ENDINGS));
    dlg_radio(&g, 1, "Amiga/Unix LF", 1);
    dos = dlg_radio(&g, 1, "DOS/Atari CR LF", 0);
    dlg_row(&g);
    dlg_label(&g, L(S_CHARACTERS));
    r[0] = dlg_radio(&g, 2, "Amiga (ISO-8859-1)", 1);
    r[1] = dlg_radio(&g, 2, "Atari ST", 0);
    r[2] = dlg_radio(&g, 2, "UTF-8", 0);
    r[3] = dlg_radio(&g, 2, "UTF-16", 0);
    dlg_row(&g);
    dlg_text(&g, L(S_EXPORT_HINT));
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_EXPORT), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    *crlf = dlg_on(&g, dos);
    *cs = dlg_on(&g, r[1]) ? CS_ATARI : dlg_on(&g, r[2]) ? CS_UTF8 : dlg_on(&g, r[3]) ? CS_UTF16 : CS_WIN;
    return 1;
}

/* ================================================================= */
/*  Go to page and Print                                              */
/* ================================================================= */

/* Insert > Chart: the kind of chart of the table's numbers, and its place */
int dlg_chart(int *kind, int *pos)
{
    static const int kinds[NCHARTS] = { S_CH_NONE, S_CH_PIE, S_CH_BAR, S_CH_PIE3D, S_CH_BAR3D };
    static const int places[4] = { S_CH_TOP, S_CH_BOTTOM, S_CH_LEFT, S_CH_RIGHT };
    static char title[40];
    DLG g;
    short k[NCHARTS], p[4], ok;
    int i;
    char *dots;
    strncpy(title, L(M_CHART), sizeof title - 1);
    dots = strstr(title, "...");
    if (dots) *dots = 0;
    dlg_begin(&g, title);
    dlg_label(&g, L(S_CH_KIND));
    for (i = 0; i < NCHARTS; i++) k[i] = dlg_radio(&g, 1, L(kinds[i]), *kind == i);
    dlg_row(&g);
    dlg_label(&g, L(S_CH_PLACE));
    for (i = 0; i < 4; i++) p[i] = dlg_radio(&g, 2, L(places[i]), *pos == i);
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, "OK", OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    for (i = 0; i < NCHARTS; i++) if (dlg_on(&g, k[i])) *kind = i;
    for (i = 0; i < 4; i++) if (dlg_on(&g, p[i])) *pos = i;
    return 1;
}

int dlg_goto(DOC *d, int *page)
{
    static DLG g;
    static char pb[6], of[40];
    short ok;
    int v;
    sprintf(pb, "%d", *page + 1);
    sprintf(of, L(S_OF_N), doc_npages(d));
    dlg_begin(&g, L(S_GOTO_TITLE));
    dlg_label(&g, L(S_GOTO_PAGE));
    dlg_edit(&g, tmpl(NULL, 4, of), pb, valid_n(4));
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_GO), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    if (dlg_run(&g) != ok) return 0;
    v = atoi(pb);
    if (v < 1) v = 1;
    if (v > doc_npages(d)) v = doc_npages(d);
    *page = v - 1;
    return 1;
}

int dlg_print(DOC *d, const char *printer, unsigned char *sel, int *copies)
{
    static DLG g;
    static char fb[6], tb[6], lb[41], cb[4], pr[60], cur[40];
    short ok, all, now, rng, lst;
    int np = doc_npages(d), a, b, c, k, page = d->nlines ? d->lines[doc_find_line(d, d->cursor)].page : 0;
    sprintf(fb, "%d", 1);
    sprintf(tb, "%d", np);
    if (!lb[0]) strcpy(lb, "1");
    sprintf(cb, "%d", *copies > 0 ? *copies : 1);
    sprintf(pr, "%.56s", printer);
    sprintf(cur, "%s (%d)", L(S_CUR_PAGE), page + 1);
    dlg_begin(&g, L(S_PRINT_TITLE));
    dlg_label(&g, L(S_PRINTER));
    dlg_text(&g, pr);
    dlg_row(&g);
    dlg_label(&g, L(S_PAGES));
    all = dlg_radio(&g, 1, L(S_ALL_PAGES), 1);
    now = dlg_radio(&g, 1, cur, 0);
    dlg_row(&g);
    dlg_label(&g, "");
    rng = dlg_radio(&g, 1, L(S_PAGES_FROM), 0);
    dlg_edit(&g, tmpl(NULL, 4, NULL), fb, valid_n(4));
    dlg_edit(&g, tmpl(L(S_PAGES_TO), 4, NULL), tb, valid_n(4));
    dlg_row(&g);
    dlg_label(&g, "");
    lst = dlg_radio(&g, 1, L(S_PAGE_LIST), 0);
    dlg_edit(&g, tmpl(NULL, 16, NULL), lb, valid_x(40));
    dlg_text(&g, L(S_PAGE_LIST_HINT));
    dlg_row(&g);
    dlg_label(&g, L(S_COPIES));
    dlg_edit(&g, tmpl(NULL, 2, NULL), cb, valid_n(2));
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_PRINT_BTN), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    (void)rng;
    for (;;) {
        if (dlg_run(&g) != ok) return 0;
        memset(sel, 0, (size_t)np);
        if (dlg_on(&g, all)) memset(sel, 1, (size_t)np);
        else if (dlg_on(&g, now)) sel[page < np ? page : np - 1] = 1;
        else if (dlg_on(&g, lst)) page_list(lb, np, sel);
        else {
            a = atoi(fb); b = atoi(tb);
            if (a < 1) a = 1;
            if (b > np) b = np;
            for (k = a; k <= b; k++) sel[k - 1] = 1;
        }
        for (k = 0; k < np && !sel[k]; k++) ;
        if (k < np) break;
        alert(1, 1, AL_NO_PAGES, AB_OK);
    }
    c = atoi(cb);
    *copies = c < 1 ? 1 : c > 99 ? 99 : c;
    return 1;
}

/* Insert > Page Numbers: a page number in the header or footer */
int dlg_pagenum(DOC *d)
{
    static DLG g;
    static const short sty[NPNSTYLES] = { 0, 1, 2, 3 };
    static char ex[NPNSTYLES][40], pw[34], ow[24];
    short wr[2], jr[3], sr[NPNSTYLES], first, ok, rem, r;
    int where = 1, style = 1, just = 1, i;
    const char *jn[3];
    jn[0] = L(S_J_LEFT); jn[1] = L(S_J_CENTRE); jn[2] = L(S_J_RIGHT);
    if (hf_pagenum_find(d, &where, &style)) just = HF(d).just[where];
    sprintf(ex[0], "1");
    sprintf(ex[1], "%s 1", L(S_PN_PAGE));
    sprintf(ex[2], "%s 1 %s 3", L(S_PN_PAGE), L(S_PN_OF));
    sprintf(ex[3], "- 1 -");
    dlg_begin(&g, L(S_PN_TITLE));
    dlg_label(&g, L(S_PN_WHERE));
    wr[0] = dlg_radio(&g, 1, L(S_PN_TOP), where == 0);
    wr[1] = dlg_radio(&g, 1, L(S_PN_BOTTOM), where == 1);
    dlg_row(&g);
    dlg_label(&g, L(S_PN_ALIGN));
    for (i = 0; i < 3; i++) jr[i] = dlg_radio(&g, 2, jn[i], just == i);
    dlg_row(&g);
    dlg_label(&g, L(S_PN_STYLE));
    for (i = 0; i < NPNSTYLES; i++) sr[i] = dlg_radio(&g, 3, ex[i], style == sty[i]);
    dlg_row(&g);
    dlg_label(&g, "");
    first = dlg_check(&g, L(S_FIRST_PAGE_TOO), HF(d).first);
    rem = dlg_button(&g, L(S_PN_REMOVE), OF_EXIT);
    dlg_button(&g, L(S_CANCEL), OF_EXIT);
    ok = dlg_button(&g, L(S_BTN_OK), OF_EXIT | OF_DEFAULT);
    dlg_end(&g);
    r = dlg_run(&g);
    if (r == rem) { hf_pagenum_clear(d); return 1; }
    if (r != ok) return 0;
    where = dlg_on(&g, wr[0]) ? 0 : 1;
    for (i = 0; i < 3; i++) if (dlg_on(&g, jr[i])) just = i;
    for (i = 0; i < NPNSTYLES; i++) if (dlg_on(&g, sr[i])) style = sty[i];
    stc(pw, L(S_PN_PAGE), sizeof pw);
    stc(ow, L(S_PN_OF), sizeof ow);
    hf_pagenum_set(d, where, just, style, pw, ow);
    HF(d).first = (unsigned char)dlg_on(&g, first);
    d->dirty = 1;
    return 1;
}
