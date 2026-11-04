/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ami13.c - what EdWord's Amiga port uses from Workbench 2.04, done with
 *           what Workbench 1.2/1.3 has (see ami13.h): tag calls, public
 *           screens, GadTools gadgets and menus, EasyRequest, the file
 *           requester, and the scroll bars of the document windows
 */
#define AMI13_IMPL
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <graphics/gfxbase.h>
#include <workbench/startup.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiui.h"

extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;
extern struct DosLibrary *DOSBase;

struct Library c13_asl;
static const char c13_tag[] = "c13";               /* UserData of our gadgets */
#define IS13(g) ((g) && (g)->UserData == (APTR)c13_tag)

static UBYTE pshine(void)  { return penmap[P_WHITE]; }
static UBYTE pshadow(void) { return penmap[P_BLACK]; }
static UBYTE ptext(void)   { return penmap[P_BLACK]; }
static UBYTE pback(void)   { return penmap[P_GREY]; }
static UBYTE pfill(void)   { return penmap[P_BLUE]; }

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, 0, 0 };
static struct TextAttr *ui_ta(void) { return sysattr.ta_Name ? &sysattr : &topaz8; }

/* where EdWord was started from (there is no PROGDIR: before 2.0) */
void c13_progdir(char *out, int n, struct WBStartup *wb)
{
    out[0] = 0;
    if (wb && wb->sm_NumArgs > 0 && wb->sm_ArgList[0].wa_Lock) {
        if (c13_NameFromLock(wb->sm_ArgList[0].wa_Lock, (STRPTR)out, n - 2)) {
            int l = (int)strlen(out);
            if (l && out[l - 1] != ':' && out[l - 1] != '/') { out[l] = '/'; out[l + 1] = 0; }
        } else out[0] = 0;
        return;
    } else {
        struct Process *pr = (struct Process *)FindTask(NULL);
        struct CommandLineInterface *cli = (struct CommandLineInterface *)BADDR(pr->pr_CLI);
        UBYTE *b = cli ? (UBYTE *)BADDR(cli->cli_CommandName) : NULL;
        char name[256];
        int len = b ? b[0] : 0;
        if (len >= (int)sizeof name) len = sizeof name - 1;
        if (b) memcpy(name, b + 1, len);
        name[len] = 0;
        if (strchr(name, ':') || strchr(name, '/')) {
            *c13_PathPart((STRPTR)name) = 0;
            len = (int)strlen(name);
            if (len && name[len - 1] != ':' && name[len - 1] != '/') strcat(name, "/");
            strncpy(out, name, n - 1);
            out[n - 1] = 0;
        }
    }
}

void c13_init(void)
{
    c13_asl.lib_Version = 37;
}

/* ================================================================= */
/*  exec, dos, utility, timer, graphics                               */
/* ================================================================= */

static void new_list(struct List *l)
{
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_Tail = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}

struct MsgPort *c13_CreateMsgPort(void)
{
    struct MsgPort *p;
    BYTE sig = AllocSignal(-1);
    if (sig < 0) return NULL;
    p = (struct MsgPort *)AllocMem(sizeof *p, MEMF_PUBLIC | MEMF_CLEAR);
    if (!p) { FreeSignal(sig); return NULL; }
    p->mp_Node.ln_Type = NT_MSGPORT;
    p->mp_Flags = PA_SIGNAL;
    p->mp_SigBit = (UBYTE)sig;
    p->mp_SigTask = FindTask(NULL);
    new_list(&p->mp_MsgList);
    return p;
}

void c13_DeleteMsgPort(struct MsgPort *p)
{
    if (!p) return;
    FreeSignal(p->mp_SigBit);
    FreeMem(p, sizeof *p);
}

struct IORequest *c13_CreateIORequest(struct MsgPort *p, ULONG size)
{
    struct IORequest *io;
    if (!p) return NULL;
    io = (struct IORequest *)AllocMem(size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!io) return NULL;
    io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    io->io_Message.mn_ReplyPort = p;
    io->io_Message.mn_Length = (UWORD)size;
    return io;
}

void c13_DeleteIORequest(struct IORequest *io)
{
    if (io) FreeMem(io, io->io_Message.mn_Length);
}

STRPTR c13_FilePart(CONST_STRPTR path)
{
    const char *p = (const char *)path, *s = strrchr(p, '/'), *c = strrchr(p, ':');
    if (s && (!c || s > c)) return (STRPTR)(s + 1);
    if (c) return (STRPTR)(c + 1);
    return (STRPTR)p;
}

STRPTR c13_PathPart(CONST_STRPTR path)
{
    const char *p = (const char *)path, *s = strrchr(p, '/'), *c = strrchr(p, ':');
    if (s && (!c || s > c)) return (STRPTR)s;
    if (c) return (STRPTR)(c + 1);
    return (STRPTR)p;
}

BOOL c13_AddPart(STRPTR dir, CONST_STRPTR name, ULONG size)
{
    char *d = (char *)dir;
    const char *n = (const char *)name;
    size_t l;
    if (strchr(n, ':')) {                           /* an absolute name replaces the path */
        if (strlen(n) + 1 > size) return FALSE;
        strcpy(d, n);
        return TRUE;
    }
    l = strlen(d);
    if (l + strlen(n) + 2 > size) return FALSE;
    if (l && d[l - 1] != ':' && d[l - 1] != '/') d[l++] = '/';
    strcpy(d + l, n);
    return TRUE;
}

BOOL c13_NameFromLock(BPTR lock, STRPTR buf, LONG len)
{
    struct FileInfoBlock *fib;
    char tmp[300], part[110];
    BPTR l, p;
    int root;
    if (!lock || len < 2) return FALSE;
    fib = (struct FileInfoBlock *)AllocMem(sizeof *fib, MEMF_PUBLIC | MEMF_CLEAR);
    if (!fib) return FALSE;
    tmp[0] = 0;
    l = DupLock(lock);
    while (l) {
        if (!Examine(l, fib)) { UnLock(l); FreeMem(fib, sizeof *fib); return FALSE; }
        p = ParentDir(l);
        root = !p;
        strncpy(part, (const char *)fib->fib_FileName, 107);
        part[107] = 0;
        strcat(part, root ? ":" : (tmp[0] ? "/" : ""));
        if (strlen(part) + strlen(tmp) >= sizeof tmp) { UnLock(l); if (p) UnLock(p); FreeMem(fib, sizeof *fib); return FALSE; }
        memmove(tmp + strlen(part), tmp, strlen(tmp) + 1);
        memcpy(tmp, part, strlen(part));
        UnLock(l);
        l = p;
    }
    FreeMem(fib, sizeof *fib);
    if ((LONG)strlen(tmp) >= len) return FALSE;
    strcpy((char *)buf, tmp);
    return TRUE;
}

/* the next real tag (TAG_MORE, TAG_IGNORE and TAG_SKIP followed) */
static const struct TagItem *next_tag(const struct TagItem **tp)
{
    const struct TagItem *t = *tp;
    while (t) {
        switch (t->ti_Tag) {
        case TAG_DONE: *tp = NULL; return NULL;
        case TAG_MORE: t = (const struct TagItem *)t->ti_Data; continue;
        case TAG_IGNORE: t++; continue;
        case TAG_SKIP: t += t->ti_Data + 1; continue;
        default: *tp = t + 1; return t;
        }
    }
    *tp = NULL;
    return NULL;
}

ULONG c13_GetTagData(Tag tag, ULONG def, const struct TagItem *tags)
{
    const struct TagItem *t;
    while ((t = next_tag(&tags)) != NULL) if (t->ti_Tag == tag) return t->ti_Data;
    return def;
}

ULONG c13_ReadEClock(struct EClockVal *e)
{
    ULONG s, m;
    CurrentTime(&s, &m);
    e->ev_hi = 0;
    e->ev_lo = s * 50 + m / 20000;
    return 50;
}

ULONG c13_GetVPModeID(struct ViewPort *vp)
{
    return vp ? (ULONG)(vp->Modes & (HIRES | LACE)) : INVALID_ID;
}

ULONG c13_ModeNotAvailable(ULONG id)
{
    return (id & ~(ULONG)(HIRES | LACE)) != 0;
}

/* ================================================================= */
/*  Screens and windows from tags                                     */
/* ================================================================= */

static struct Screen *wb_screen(void)
{
    struct Screen *s;
    ULONG lk;
    OpenWorkBench();
    lk = LockIBase(0);
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen)
        if ((s->Flags & SCREENTYPE) == WBENCHSCREEN) break;
    UnlockIBase(lk);
    return s;
}

struct Screen *c13_LockPubScreen(CONST_STRPTR name)
{
    (void)name;
    return wb_screen();
}

void c13_UnlockPubScreen(CONST_STRPTR name, struct Screen *s) { (void)name; (void)s; }

static UWORD rows_of_display(void)
{
    UWORD r = GfxBase->NormalDisplayRows;
    return r >= 200 ? r : 200;
}

struct Screen *c13_OpenScreenTagList(const struct NewScreen *ons, const struct TagItem *tags)
{
    struct NewScreen ns;
    const struct TagItem *t;
    ULONG id = HIRES_KEY;
    LONG w = STDSCREENWIDTH, h = STDSCREENHEIGHT;
    if (ons) ns = *ons;
    else memset(&ns, 0, sizeof ns);
    ns.Depth = ns.Depth ? ns.Depth : 2;
    ns.Type = CUSTOMSCREEN;
    while ((t = next_tag(&tags)) != NULL) {
        switch (t->ti_Tag) {
        case SA_DisplayID: id = t->ti_Data; break;
        case SA_Width: w = (LONG)t->ti_Data; break;
        case SA_Height: h = (LONG)t->ti_Data; break;
        case SA_Depth: ns.Depth = (WORD)t->ti_Data; break;
        case SA_Title: ns.DefaultTitle = (UBYTE *)t->ti_Data; break;
        case SA_Font: ns.Font = (struct TextAttr *)t->ti_Data; break;
        case SA_Type: ns.Type = (UWORD)t->ti_Data; break;
        }
    }
    ns.ViewModes = (UWORD)(id & (HIRES | LACE));
    if (w <= 0 || w > 1024) w = (ns.ViewModes & HIRES) ? 640 : 320;
    if (h <= 0 || h > 1024) h = (LONG)rows_of_display() * ((ns.ViewModes & LACE) ? 2 : 1);
    ns.LeftEdge = 0; ns.TopEdge = 0;
    ns.Width = (WORD)w; ns.Height = (WORD)h;
    ns.DetailPen = 1;                                /* black on white bars, as in 2.0 */
    ns.BlockPen = 2;
    ns.Type = (ns.Type & ~SCREENTYPE) | CUSTOMSCREEN;
    return OpenScreen(&ns);
}

/* the IDCMP flags 1.3 knows; BOOPSI updates come from gadget messages */
static ULONG idcmp13(ULONG f)
{
    if (f & IDCMP_IDCMPUPDATE) f |= IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS;
    return f & ~(ULONG)(IDCMP_IDCMPUPDATE | IDCMP_CHANGEWINDOW | IDCMP_MENUHELP | 0x40000000UL);
}

BOOL c13_ModifyIDCMP(struct Window *w, ULONG flags)
{
    return ModifyIDCMP(w, idcmp13(flags));
}

static void scroll_fix(struct Window *w);

struct Window *c13_OpenWindowTagList(const struct NewWindow *onw, const struct TagItem *tags)
{
    struct NewWindow nw;
    const struct TagItem *t;
    struct Screen *s = NULL;
    LONG iw = -1, ih = -1, sx, sy;
    const char *stitle = NULL;
    struct Window *w;
    ULONG fl = 0, fset = 0;
#define FLAG(bit) do { fset |= (bit); if (t->ti_Data) fl |= (bit); else fl &= ~(ULONG)(bit); } while (0)
    if (onw) nw = *onw;
    else {
        memset(&nw, 0, sizeof nw);
        nw.DetailPen = nw.BlockPen = (UBYTE)~0;
        nw.Width = 200; nw.Height = 100;
        nw.MinWidth = nw.MinHeight = 0;
        nw.MaxWidth = nw.MaxHeight = (UWORD)~0;
        nw.Type = WBENCHSCREEN;
    }
    fl = nw.Flags;
    while ((t = next_tag(&tags)) != NULL) {
        switch (t->ti_Tag) {
        case WA_Left: nw.LeftEdge = (WORD)t->ti_Data; break;
        case WA_Top: nw.TopEdge = (WORD)t->ti_Data; break;
        case WA_Width: nw.Width = (WORD)t->ti_Data; break;
        case WA_Height: nw.Height = (WORD)t->ti_Data; break;
        case WA_InnerWidth: iw = (LONG)t->ti_Data; break;
        case WA_InnerHeight: ih = (LONG)t->ti_Data; break;
        case WA_IDCMP: nw.IDCMPFlags = idcmp13(t->ti_Data); break;
        case WA_Flags: fl = t->ti_Data; break;
        case WA_Gadgets: nw.FirstGadget = (struct Gadget *)t->ti_Data; break;
        case WA_Title: nw.Title = (UBYTE *)t->ti_Data; break;
        case WA_ScreenTitle: stitle = (const char *)t->ti_Data; break;
        case WA_CustomScreen: s = (struct Screen *)t->ti_Data; nw.Type = CUSTOMSCREEN; break;
        case WA_PubScreen:
            s = (struct Screen *)t->ti_Data;
            nw.Type = s && (s->Flags & SCREENTYPE) == WBENCHSCREEN ? WBENCHSCREEN : CUSTOMSCREEN;
            break;
        case WA_MinWidth: nw.MinWidth = (WORD)t->ti_Data; break;
        case WA_MinHeight: nw.MinHeight = (WORD)t->ti_Data; break;
        case WA_MaxWidth: nw.MaxWidth = (UWORD)t->ti_Data; break;
        case WA_MaxHeight: nw.MaxHeight = (UWORD)t->ti_Data; break;
        case WA_SizeGadget: FLAG(WFLG_SIZEGADGET); break;
        case WA_DragBar: FLAG(WFLG_DRAGBAR); break;
        case WA_DepthGadget: FLAG(WFLG_DEPTHGADGET); break;
        case WA_CloseGadget: FLAG(WFLG_CLOSEGADGET); break;
        case WA_Backdrop: FLAG(WFLG_BACKDROP); break;
        case WA_ReportMouse: FLAG(WFLG_REPORTMOUSE); break;
        case WA_NoCareRefresh: FLAG(WFLG_NOCAREREFRESH); break;
        case WA_Borderless: FLAG(WFLG_BORDERLESS); break;
        case WA_Activate: FLAG(WFLG_ACTIVATE); break;
        case WA_RMBTrap: FLAG(WFLG_RMBTRAP); break;
        case WA_SimpleRefresh: if (t->ti_Data) fl = (fl & ~(ULONG)WFLG_REFRESHBITS) | WFLG_SIMPLE_REFRESH; break;
        case WA_SmartRefresh: if (t->ti_Data) fl = (fl & ~(ULONG)WFLG_REFRESHBITS) | WFLG_SMART_REFRESH; break;
        case WA_SizeBRight: FLAG(WFLG_SIZEBRIGHT); break;
        case WA_SizeBBottom: FLAG(WFLG_SIZEBBOTTOM); break;
        case WA_GimmeZeroZero: FLAG(WFLG_GIMMEZEROZERO); break;
        }
    }
#undef FLAG
    (void)fset;
    if (!s) s = nw.Type == CUSTOMSCREEN ? nw.Screen : wb_screen();
    if (!s) return NULL;
    nw.Screen = nw.Type == CUSTOMSCREEN ? s : NULL;
    nw.Flags = fl & 0x0007FFFFUL;
    if (iw >= 0 || ih >= 0) {
        int bl = (fl & WFLG_BORDERLESS) ? 0 : s->WBorLeft, br = (fl & WFLG_BORDERLESS) ? 0 : s->WBorRight;
        int bt = (fl & WFLG_BORDERLESS) ? 0 : s->WBorTop + (nw.Title || (fl & (WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET)) ? s->Font->ta_YSize + 1 : 0);
        int bb = (fl & WFLG_BORDERLESS) ? 0 : s->WBorBottom;
        if (iw >= 0) nw.Width = (WORD)(iw + bl + br);
        if (ih >= 0) nw.Height = (WORD)(ih + bt + bb);
    }
    /* 1.3 won't open a window that doesn't fit the screen */
    sx = s->Width; sy = s->Height;
    if (nw.Width > sx) nw.Width = (WORD)sx;
    if (nw.Height > sy) nw.Height = (WORD)sy;
    if (nw.LeftEdge < 0) nw.LeftEdge = 0;
    if (nw.TopEdge < 0) nw.TopEdge = 0;
    if (nw.LeftEdge + nw.Width > sx) nw.LeftEdge = (WORD)(sx - nw.Width);
    if (nw.TopEdge + nw.Height > sy) nw.TopEdge = (WORD)(sy - nw.Height);
    if (nw.MinWidth > nw.Width) nw.MinWidth = nw.Width;
    if (nw.MinHeight > nw.Height) nw.MinHeight = nw.Height;
    w = OpenWindow(&nw);
    if (w && stitle) SetWindowTitles(w, (UBYTE *)~0, (UBYTE *)stitle);
    if (w) scroll_fix(w);
    return w;
}

void c13_ChangeWindowBox(struct Window *w, LONG l, LONG t, LONG wd, LONG h)
{
    struct Screen *s = w->WScreen;
    if (wd > s->Width) wd = s->Width;
    if (h > s->Height) h = s->Height;
    if (l + wd > s->Width) l = s->Width - wd;
    if (t + h > s->Height) t = s->Height - h;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (wd <= w->Width && h <= w->Height) {
        SizeWindow(w, wd - w->Width, h - w->Height);
        MoveWindow(w, l - w->LeftEdge, t - w->TopEdge);
    } else {
        MoveWindow(w, l - w->LeftEdge, t - w->TopEdge);
        SizeWindow(w, wd - w->Width, h - w->Height);
    }
}

BOOL c13_ResetMenuStrip(struct Window *w, struct Menu *m)
{
    return SetMenuStrip(w, m);
}

static struct DrawInfo dri13;
static UWORD dpens[NUMDRIPENS + 1];

struct DrawInfo *c13_GetScreenDrawInfo(struct Screen *s)
{
    dpens[DETAILPEN] = 1; dpens[BLOCKPEN] = 2; dpens[TEXTPEN] = ptext(); dpens[SHINEPEN] = pshine();
    dpens[SHADOWPEN] = pshadow(); dpens[FILLPEN] = pfill(); dpens[FILLTEXTPEN] = ptext();
    dpens[BACKGROUNDPEN] = pback(); dpens[HIGHLIGHTTEXTPEN] = pshine();
    dpens[NUMDRIPENS] = (UWORD)~0;
    dri13.dri_Version = 1;
    dri13.dri_NumPens = NUMDRIPENS;
    dri13.dri_Pens = dpens;
    dri13.dri_Font = s->RastPort.Font;
    dri13.dri_Depth = s->RastPort.BitMap->Depth;
    dri13.dri_Resolution.X = 44; dri13.dri_Resolution.Y = 44;
    return &dri13;
}

void c13_FreeScreenDrawInfo(struct Screen *s, struct DrawInfo *d) { (void)s; (void)d; }

APTR c13_GetVisualInfoA(struct Screen *s, const struct TagItem *tags) { (void)tags; return (APTR)s; }
void c13_FreeVisualInfo(APTR vi) { (void)vi; }

/* ================================================================= */
/*  Gadgets in the GadTools manner                                    */
/* ================================================================= */

/* a two-colour frame: light top and left, dark bottom and right (or
 * the other way round, sunk in) */
typedef struct {
    struct Border b[4];
    WORD p[4][6];
} BEVEL;

static struct Border *bevel13(BEVEL *v, WORD x, WORD y, WORD w, WORD h, int sunk, struct Border *next)
{
    UBYTE lt = sunk ? pshadow() : pshine(), dk = sunk ? pshine() : pshadow();
    int i;
    WORD *p;
    memset(v, 0, sizeof *v);
    p = v->p[0]; p[0] = 0; p[1] = (WORD)(h - 1); p[2] = 0; p[3] = 0; p[4] = (WORD)(w - 2); p[5] = 0;
    p = v->p[1]; p[0] = 1; p[1] = (WORD)(h - 2); p[2] = 1; p[3] = 1; p[4] = 1; p[5] = 1;
    p = v->p[2]; p[0] = 1; p[1] = (WORD)(h - 1); p[2] = (WORD)(w - 1); p[3] = (WORD)(h - 1); p[4] = (WORD)(w - 1); p[5] = 0;
    p = v->p[3]; p[0] = (WORD)(w - 2); p[1] = 1; p[2] = (WORD)(w - 2); p[3] = (WORD)(h - 2); p[4] = (WORD)(w - 2); p[5] = (WORD)(h - 2);
    for (i = 0; i < 4; i++) {
        v->b[i].LeftEdge = x; v->b[i].TopEdge = y;
        v->b[i].FrontPen = i < 2 ? lt : dk;
        v->b[i].DrawMode = JAM1;
        v->b[i].Count = 3;
        v->b[i].XY = v->p[i];
        v->b[i].NextBorder = i < 3 ? &v->b[i + 1] : next;
    }
    return &v->b[0];
}

enum { K_CONTEXT = 100, K_SCROLLER = 101 };

typedef struct G13 {
    struct Gadget g;
    UWORD kind, part;               /* part of a list: 0 list, 1 up, 2 down, 3 knob */
    struct G13 *owner, *nextfree;
    BEVEL bv;
    struct Border glyph[3];
    WORD gp[3][12];
    struct IntuiText label, text;
    struct TextAttr *ta;
    /* string */
    struct StringInfo si;
    UBYTE *buf, *undo;
    WORD maxch;
    /* cycle */
    STRPTR *labels;
    WORD active, nlab;
    /* list view */
    struct List *list;
    WORD top, sel, vis, rowh, count, showsel;
    struct TextFont *font;
    struct G13 *up, *down, *knob;
    struct PropInfo pi;
    struct Image kimg;
    /* scroll bar of a document window */
    LONG total, visible, ptop;
    WORD sbkind;
} G13;

static G13 *g13_new(UWORD kind)
{
    G13 *g = (G13 *)AllocMem(sizeof *g, MEMF_PUBLIC | MEMF_CLEAR);
    if (!g) return NULL;
    g->kind = kind;
    g->g.UserData = (APTR)c13_tag;
    return g;
}

static void link_after(struct Gadget *prev, G13 *g)
{
    if (prev) prev->NextGadget = &g->g;
}

static WORD font_h(struct TextAttr *ta) { return (WORD)(ta && ta->ta_YSize ? ta->ta_YSize : 8); }

static WORD text_len(struct TextAttr *ta, const char *s)
{
    struct IntuiText it;
    memset(&it, 0, sizeof it);
    it.ITextFont = ta;
    it.IText = (UBYTE *)s;
    return (WORD)IntuiTextLength(&it);
}

static void set_label(G13 *g, const struct NewGadget *ng, struct IntuiText *next)
{
    const char *s = (const char *)ng->ng_GadgetText;
    WORD w = g->g.Width, h = g->g.Height, tl, fh = font_h(ng->ng_TextAttr);
    ULONG pl = ng->ng_Flags & (PLACETEXT_LEFT | PLACETEXT_RIGHT | PLACETEXT_ABOVE | PLACETEXT_BELOW | PLACETEXT_IN);
    if (!s) { g->g.GadgetText = next; return; }
    g->label.IText = (UBYTE *)s;
    g->label.ITextFont = ng->ng_TextAttr;
    g->label.FrontPen = (ng->ng_Flags & NG_HIGHLABEL) ? pshine() : ptext();
    g->label.DrawMode = JAM1;
    tl = text_len(ng->ng_TextAttr, s);
    if (!pl) pl = PLACETEXT_LEFT;
    switch (pl) {
    case PLACETEXT_RIGHT: g->label.LeftEdge = (WORD)(w + 8); g->label.TopEdge = (WORD)((h - fh) / 2); break;
    case PLACETEXT_ABOVE: g->label.LeftEdge = (WORD)((w - tl) / 2); g->label.TopEdge = (WORD)(-fh - 3); break;
    case PLACETEXT_BELOW: g->label.LeftEdge = (WORD)((w - tl) / 2); g->label.TopEdge = (WORD)(h + 3); break;
    case PLACETEXT_IN: g->label.LeftEdge = (WORD)((w - tl) / 2); g->label.TopEdge = (WORD)((h - fh) / 2); break;
    default: g->label.LeftEdge = (WORD)(-tl - 6); g->label.TopEdge = (WORD)((h - fh) / 2); break;
    }
    g->label.NextText = next;
    g->g.GadgetText = &g->label;
}

static void box(G13 *g, const struct NewGadget *ng)
{
    g->g.LeftEdge = ng->ng_LeftEdge;
    g->g.TopEdge = ng->ng_TopEdge;
    g->g.Width = ng->ng_Width;
    g->g.Height = ng->ng_Height;
    g->g.GadgetID = ng->ng_GadgetID;
    g->ta = ng->ng_TextAttr;
}

struct Gadget *c13_CreateContext(struct Gadget **glist)
{
    G13 *g = g13_new(K_CONTEXT);
    *glist = NULL;
    if (!g) return NULL;
    g->g.Flags = GFLG_GADGHNONE;
    g->g.GadgetType = GTYP_BOOLGADGET;
    *glist = &g->g;
    return &g->g;
}

/* the cycle gadget's little circular arrow, and its divider */
static void cycle_glyph(G13 *g)
{
    WORD h = g->g.Height, *p;
    p = g->gp[0];                                    /* the arrow */
    p[0] = 12; p[1] = 3; p[2] = 7; p[3] = 3; p[4] = 6; p[5] = 4; p[6] = 6; p[7] = (WORD)(h - 4);
    p[8] = 7; p[9] = (WORD)(h - 3); p[10] = 12; p[11] = (WORD)(h - 3);
    g->glyph[0].Count = 6;
    p = g->gp[1];                                    /* its head */
    p[0] = 10; p[1] = 1; p[2] = 12; p[3] = 3; p[4] = 10; p[5] = 5;
    g->glyph[1].Count = 3;
    p = g->gp[2];                                    /* the divider */
    p[0] = 18; p[1] = 2; p[2] = 18; p[3] = (WORD)(h - 3);
    g->glyph[2].Count = 2;
    {
        int i;
        for (i = 0; i < 3; i++) {
            g->glyph[i].FrontPen = i == 2 ? pshadow() : ptext();
            g->glyph[i].DrawMode = JAM1;
            g->glyph[i].XY = g->gp[i];
            g->glyph[i].NextBorder = i < 2 ? &g->glyph[i + 1] : NULL;
        }
    }
    g->bv.b[3].NextBorder = &g->glyph[0];
}

static void cycle_text(G13 *g)
{
    WORD tl, fh = font_h(g->ta), x0 = 20;
    g->text.IText = (UBYTE *)(g->nlab ? g->labels[g->active] : (STRPTR)"");
    g->text.ITextFont = g->ta;
    g->text.FrontPen = ptext();
    g->text.DrawMode = JAM1;
    tl = text_len(g->ta, (const char *)g->text.IText);
    g->text.LeftEdge = (WORD)(x0 + (g->g.Width - x0 - tl) / 2);
    g->text.TopEdge = (WORD)((g->g.Height - fh) / 2);
}

/* the check box: one border set draws the tick, the other rubs it out */
typedef struct { BEVEL a, b; struct Border t[2]; WORD tp[2][8]; } CHECK13;

static void list_draw(G13 *g, struct Window *w);
static void list_prop(G13 *g, struct Window *w);

struct Gadget *c13_CreateGadgetA(ULONG kind, struct Gadget *prev, const struct NewGadget *ng, const struct TagItem *tags)
{
    G13 *g;
    const struct TagItem *t;
    WORD fh = font_h(ng->ng_TextAttr);
    if (!prev) return NULL;
    g = g13_new((UWORD)kind);
    if (!g) return NULL;
    box(g, ng);
    g->g.GadgetType = GTYP_BOOLGADGET;
    switch (kind) {
    case TEXT_KIND:
        g->g.Flags = GFLG_GADGHNONE;
        g->text.IText = (UBYTE *)c13_GetTagData(GTTX_Text, (ULONG)"", tags);
        g->text.ITextFont = ng->ng_TextAttr;
        g->text.FrontPen = ptext();
        g->text.DrawMode = JAM1;
        g->text.TopEdge = (WORD)((g->g.Height - fh) / 2);
        set_label(g, ng, &g->text);
        break;
    case BUTTON_KIND:
        g->g.Flags = GFLG_GADGHCOMP;
        g->g.Activation = GACT_RELVERIFY;
        g->g.GadgetRender = bevel13(&g->bv, 0, 0, g->g.Width, g->g.Height, 0, NULL);
        {
            struct NewGadget n2 = *ng;
            n2.ng_Flags = (n2.ng_Flags & ~(ULONG)0x3F) | PLACETEXT_IN;
            set_label(g, &n2, NULL);
        }
        break;
    case CHECKBOX_KIND: {
        CHECK13 *c = (CHECK13 *)AllocMem(sizeof *c, MEMF_PUBLIC | MEMF_CLEAR);
        WORD w = g->g.Width, h = g->g.Height, *p;
        int i;
        if (!c) { FreeMem(g, sizeof *g); return NULL; }
        g->buf = (UBYTE *)c;
        p = c->tp[0];
        p[0] = 6; p[1] = (WORD)(h / 2); p[2] = 9; p[3] = (WORD)(h - 3); p[4] = (WORD)(w - 7); p[5] = 2; p[6] = (WORD)(w - 8); p[7] = 2;
        memcpy(c->tp[1], c->tp[0], sizeof c->tp[0]);
        for (i = 0; i < 2; i++) {
            c->t[i].FrontPen = i ? ptext() : pback();
            c->t[i].DrawMode = JAM1;
            c->t[i].Count = 4;
            c->t[i].XY = c->tp[i];
        }
        g->g.GadgetRender = bevel13(&c->a, 0, 0, w, h, 0, &c->t[0]);
        g->g.SelectRender = bevel13(&c->b, 0, 0, w, h, 0, &c->t[1]);
        g->g.Flags = GFLG_GADGHIMAGE | (c13_GetTagData(GTCB_Checked, 0, tags) ? GFLG_SELECTED : 0);
        g->g.Activation = GACT_RELVERIFY | GACT_TOGGLESELECT;
        set_label(g, ng, NULL);
        break;
    }
    case CYCLE_KIND:
        g->labels = (STRPTR *)c13_GetTagData(GTCY_Labels, 0, tags);
        for (g->nlab = 0; g->labels && g->labels[g->nlab]; g->nlab++) ;
        g->active = (WORD)c13_GetTagData(GTCY_Active, 0, tags);
        if (g->active >= g->nlab) g->active = 0;
        g->g.Flags = GFLG_GADGHCOMP;
        g->g.Activation = GACT_RELVERIFY;
        g->g.GadgetRender = bevel13(&g->bv, 0, 0, g->g.Width, g->g.Height, 0, NULL);
        cycle_glyph(g);
        cycle_text(g);
        set_label(g, ng, &g->text);
        if (!ng->ng_GadgetText) g->g.GadgetText = &g->text;
        break;
    case STRING_KIND: {
        const char *s = (const char *)c13_GetTagData(GTST_String, (ULONG)"", tags);
        WORD w = g->g.Width, h = g->g.Height;
        g->maxch = (WORD)c13_GetTagData(GTST_MaxChars, 64, tags);
        g->buf = (UBYTE *)AllocMem((ULONG)g->maxch + 1, MEMF_PUBLIC | MEMF_CLEAR);
        g->undo = (UBYTE *)AllocMem((ULONG)g->maxch + 1, MEMF_PUBLIC | MEMF_CLEAR);
        if (!g->buf || !g->undo) {
            if (g->buf) FreeMem(g->buf, (ULONG)g->maxch + 1);
            if (g->undo) FreeMem(g->undo, (ULONG)g->maxch + 1);
            FreeMem(g, sizeof *g);
            return NULL;
        }
        if (s) strncpy((char *)g->buf, s, g->maxch);
        g->si.Buffer = g->buf;
        g->si.UndoBuffer = g->undo;
        g->si.MaxChars = (WORD)(g->maxch + 1);
        g->g.GadgetType = GTYP_STRGADGET;
        g->g.SpecialInfo = (APTR)&g->si;
        g->g.Flags = GFLG_GADGHCOMP;
        g->g.Activation = GACT_RELVERIFY;
        /* the text inside the frame */
        g->g.LeftEdge += 6; g->g.Width -= 12;
        g->g.TopEdge += (WORD)((h - fh) / 2); g->g.Height = fh;
        g->g.GadgetRender = bevel13(&g->bv, -6, (WORD)(-(h - fh) / 2), w, h, 1, NULL);
        {
            struct NewGadget n2 = *ng;
            n2.ng_LeftEdge += 6; n2.ng_Width -= 12;
            n2.ng_TopEdge += (WORD)((h - fh) / 2); n2.ng_Height = fh;
            set_label(g, &n2, NULL);
            if (ng->ng_GadgetText) g->label.LeftEdge -= 6;
        }
        break;
    }
    case LISTVIEW_KIND: {
        G13 *up, *dn, *kn;
        WORD w = ng->ng_Width - 16, h = ng->ng_Height, ah = 10;
        g->list = (struct List *)c13_GetTagData(GTLV_Labels, 0, tags);
        if (g->list == (struct List *)~0) g->list = NULL;
        g->showsel = c13_GetTagData(GTLV_ShowSelected, (ULONG)~0, tags) != (ULONG)~0;
        g->sel = (WORD)c13_GetTagData(GTLV_Selected, (ULONG)~0, tags);
        g->top = (WORD)c13_GetTagData(GTLV_Top, 0, tags);
        g->rowh = fh;
        g->vis = (WORD)((h - 4) / fh);
        if (g->vis < 1) g->vis = 1;
        g->font = OpenFont(ng->ng_TextAttr);
        g->g.Width = w;
        g->g.Flags = GFLG_GADGHNONE;
        g->g.Activation = GACT_IMMEDIATE | GACT_RELVERIFY;
        g->g.GadgetRender = bevel13(&g->bv, 0, 0, w, h, 1, NULL);
        set_label(g, ng, NULL);
        up = g13_new(K_SCROLLER); dn = g13_new(K_SCROLLER); kn = g13_new(K_SCROLLER);
        if (!up || !dn || !kn) {
            if (up) FreeMem(up, sizeof *up);
            if (dn) FreeMem(dn, sizeof *dn);
            if (kn) FreeMem(kn, sizeof *kn);
            if (g->font) CloseFont(g->font);
            FreeMem(g, sizeof *g);
            return NULL;
        }
        g->up = up; g->down = dn; g->knob = kn;
        up->owner = dn->owner = kn->owner = g;
        up->kind = dn->kind = kn->kind = LISTVIEW_KIND;
        up->part = 1; dn->part = 2; kn->part = 3;
        up->g.GadgetID = dn->g.GadgetID = kn->g.GadgetID = ng->ng_GadgetID;
        /* arrows at the bottom right, the knob above them */
        up->g.LeftEdge = dn->g.LeftEdge = kn->g.LeftEdge = (WORD)(ng->ng_LeftEdge + w);
        up->g.Width = dn->g.Width = kn->g.Width = 16;
        dn->g.TopEdge = (WORD)(ng->ng_TopEdge + h - ah); dn->g.Height = ah;
        up->g.TopEdge = (WORD)(dn->g.TopEdge - ah); up->g.Height = ah;
        kn->g.TopEdge = ng->ng_TopEdge; kn->g.Height = (WORD)(h - 2 * ah);
        {
            G13 *a[2];
            int i;
            a[0] = up; a[1] = dn;
            for (i = 0; i < 2; i++) {
                WORD *p = a[i]->gp[0];
                a[i]->g.GadgetType = GTYP_BOOLGADGET;
                a[i]->g.Flags = GFLG_GADGHCOMP;
                a[i]->g.Activation = GACT_IMMEDIATE | GACT_RELVERIFY;
                if (i == 0) { p[0] = 4; p[1] = 6; p[2] = 8; p[3] = 2; p[4] = 12; p[5] = 6; }
                else { p[0] = 4; p[1] = 3; p[2] = 8; p[3] = 7; p[4] = 12; p[5] = 3; }
                a[i]->glyph[0].FrontPen = ptext();
                a[i]->glyph[0].DrawMode = JAM1;
                a[i]->glyph[0].Count = 3;
                a[i]->glyph[0].XY = p;
                a[i]->g.GadgetRender = bevel13(&a[i]->bv, 0, 0, 16, ah, 0, &a[i]->glyph[0]);
            }
        }
        kn->g.GadgetType = GTYP_PROPGADGET;
        kn->g.Flags = GFLG_GADGHNONE | GFLG_GADGIMAGE;
        kn->g.Activation = GACT_IMMEDIATE | GACT_RELVERIFY | GACT_FOLLOWMOUSE;
        kn->g.GadgetRender = (APTR)&kn->kimg;
        kn->g.SpecialInfo = (APTR)&kn->pi;
        kn->pi.Flags = AUTOKNOB | FREEVERT;
        kn->pi.VertBody = MAXBODY;
        g->g.NextGadget = &up->g;
        up->g.NextGadget = &dn->g;
        dn->g.NextGadget = &kn->g;
        link_after(prev, g);
        list_prop(g, NULL);
        return &kn->g;
    }
    default:
        g->g.Flags = GFLG_GADGHNONE;
        break;
    }
    if (c13_GetTagData(GA_Disabled, 0, tags)) g->g.Flags |= GFLG_DISABLED;
    link_after(prev, g);
    return &g->g;
}

/* GadTools hands back the last gadget it made; for a list view that is
 * its knob, but the caller keeps it as "the list view": find the list */
static G13 *list_of(struct Gadget *gad)
{
    G13 *g = (G13 *)gad;
    if (!IS13(gad)) return NULL;
    if (g->kind == LISTVIEW_KIND && g->owner) return g->owner;
    return g;
}

void c13_FreeGadgets(struct Gadget *gl)
{
    while (gl) {
        struct Gadget *n = gl->NextGadget;
        if (IS13(gl)) {
            G13 *g = (G13 *)gl;
            if (g->kind == STRING_KIND) { FreeMem(g->buf, (ULONG)g->maxch + 1); FreeMem(g->undo, (ULONG)g->maxch + 1); }
            if (g->kind == CHECKBOX_KIND && g->buf) FreeMem(g->buf, sizeof(CHECK13));
            if (g->kind == LISTVIEW_KIND && !g->owner && g->font) CloseFont(g->font);
            FreeMem(g, sizeof *g);
        }
        gl = n;
    }
}

/* ---- list views ----------------------------------------------------- */

static struct Node *nth(struct List *l, int n)
{
    struct Node *nd;
    if (!l) return NULL;
    for (nd = l->lh_Head; nd->ln_Succ; nd = nd->ln_Succ) if (n-- == 0) return nd;
    return NULL;
}

static int count_list(struct List *l)
{
    struct Node *nd;
    int n = 0;
    if (!l) return 0;
    for (nd = l->lh_Head; nd->ln_Succ; nd = nd->ln_Succ) n++;
    return n;
}

static void list_clamp(G13 *g)
{
    g->count = (WORD)count_list(g->list);
    if (g->top > g->count - g->vis) g->top = (WORD)(g->count - g->vis);
    if (g->top < 0) g->top = 0;
}

static void list_prop(G13 *g, struct Window *w)
{
    UWORD body, pot;
    list_clamp(g);
    if (g->count <= g->vis) { body = MAXBODY; pot = 0; }
    else {
        body = (UWORD)((ULONG)MAXBODY * g->vis / g->count);
        pot = (UWORD)((ULONG)MAXPOT * g->top / (g->count - g->vis));
    }
    if (w) NewModifyProp(&g->knob->g, w, NULL, AUTOKNOB | FREEVERT, 0, pot, MAXBODY, body, 1);
    else { g->knob->pi.VertBody = body; g->knob->pi.VertPot = pot; }
}

static void list_draw(G13 *g, struct Window *w)
{
    struct RastPort *rp = w->RPort;
    WORD x0 = (WORD)(g->g.LeftEdge + 3), y0 = (WORD)(g->g.TopEdge + 2), wd = (WORD)(g->g.Width - 6);
    int i;
    struct TextFont *of = rp->Font;
    list_clamp(g);
    if (g->font) SetFont(rp, g->font);
    SetDrMd(rp, JAM2);
    for (i = 0; i < g->vis; i++) {
        struct Node *nd = nth(g->list, g->top + i);
        WORD y = (WORD)(y0 + i * g->rowh);
        int hl = nd && g->showsel && g->top + i == g->sel;
        SetAPen(rp, hl ? pfill() : pback());
        RectFill(rp, x0, y, x0 + wd - 1, y + g->rowh - 1);
        if (nd && nd->ln_Name) {
            const char *s = nd->ln_Name;
            int n = (int)strlen(s);
            while (n > 0 && TextLength(rp, (STRPTR)s, n) > wd - 4) n--;
            SetAPen(rp, hl ? pshine() : ptext());
            SetBPen(rp, hl ? pfill() : pback());
            Move(rp, x0 + 2, y + rp->TxBaseline);
            Text(rp, (STRPTR)s, n);
        }
    }
    SetDrMd(rp, JAM1);
    if (of) SetFont(rp, of);
}

static void list_show(G13 *g, struct Window *w)
{
    if (!w) return;
    list_prop(g, w);
    list_draw(g, w);
}

static void list_view_draw_all(struct Window *w)
{
    struct Gadget *gd;
    for (gd = w->FirstGadget; gd; gd = gd->NextGadget)
        if (IS13(gd) && ((G13 *)gd)->kind == LISTVIEW_KIND && !((G13 *)gd)->owner) list_draw((G13 *)gd, w);
}

void c13_GT_SetGadgetAttrsA(struct Gadget *gad, struct Window *w, struct Requester *r, const struct TagItem *tags)
{
    G13 *g = list_of(gad);
    const struct TagItem *t;
    (void)r;
    if (!g) return;
    while ((t = next_tag(&tags)) != NULL) {
        switch (t->ti_Tag) {
        case GA_Disabled:
            if (w) RemoveGadget(w, &g->g);
            if (t->ti_Data) g->g.Flags |= GFLG_DISABLED; else g->g.Flags &= ~GFLG_DISABLED;
            if (w) { AddGadget(w, &g->g, (UWORD)~0); RefreshGList(&g->g, w, NULL, 1); }
            break;
        case GTCB_Checked:
            if (g->kind != CHECKBOX_KIND) break;
            if (w) RemoveGadget(w, &g->g);
            if (t->ti_Data) g->g.Flags |= GFLG_SELECTED; else g->g.Flags &= ~GFLG_SELECTED;
            if (w) { AddGadget(w, &g->g, (UWORD)~0); RefreshGList(&g->g, w, NULL, 1); }
            break;
        case GTST_String:
            if (g->kind != STRING_KIND) break;
            if (w) RemoveGadget(w, &g->g);
            strncpy((char *)g->buf, t->ti_Data ? (const char *)t->ti_Data : "", g->maxch);
            g->buf[g->maxch] = 0;
            g->si.BufferPos = g->si.DispPos = 0;
            if (w) {
                AddGadget(w, &g->g, (UWORD)~0);
                SetAPen(w->RPort, pback());
                RectFill(w->RPort, g->g.LeftEdge, g->g.TopEdge, g->g.LeftEdge + g->g.Width - 1, g->g.TopEdge + g->g.Height - 1);
                RefreshGList(&g->g, w, NULL, 1);
            }
            break;
        case GTCY_Active:
            if (g->kind != CYCLE_KIND) break;
            g->active = (WORD)t->ti_Data;
            if (g->active >= g->nlab) g->active = 0;
            cycle_text(g);
            if (w) {
                SetAPen(w->RPort, pback());
                RectFill(w->RPort, g->g.LeftEdge + 20, g->g.TopEdge + 1, g->g.LeftEdge + g->g.Width - 3, g->g.TopEdge + g->g.Height - 2);
                RefreshGList(&g->g, w, NULL, 1);
            }
            break;
        case GTLV_Labels: g->list = t->ti_Data == ~0UL ? NULL : (struct List *)t->ti_Data; break;
        case GTLV_Top: g->top = (WORD)t->ti_Data; break;
        case GTLV_MakeVisible:
            if ((WORD)t->ti_Data < g->top) g->top = (WORD)t->ti_Data;
            else if ((WORD)t->ti_Data >= g->top + g->vis) g->top = (WORD)(t->ti_Data - g->vis + 1);
            break;
        case GTLV_Selected: g->sel = (WORD)t->ti_Data; break;
        }
    }
    if (g->kind == LISTVIEW_KIND) list_show(g, w);
}

void c13_GT_RefreshWindow(struct Window *w, struct Requester *r)
{
    (void)r;
    list_view_draw_all(w);
}

void c13_GT_BeginRefresh(struct Window *w) { BeginRefresh(w); }

void c13_GT_EndRefresh(struct Window *w, LONG complete)
{
    list_view_draw_all(w);
    EndRefresh(w, complete);
}

/* ---- messages ----------------------------------------------------------- */

static G13 *held;                      /* an arrow held down, or a knob being dragged */
static struct Window *held_win;
static ULONG upd_tags[5];

/* the document scroll bar's state as BOOPSI would report it */
static void as_update(struct IntuiMessage *m, G13 *g)
{
    LONG top = g->ptop;
    if (g->sbkind == SB13_VPROP || g->sbkind == SB13_HPROP) {
        UWORD pot = g->sbkind == SB13_VPROP ? g->pi.VertPot : g->pi.HorizPot;
        LONG range = g->total - g->visible;
        top = range > 0 ? (LONG)(((ULONG)pot * (ULONG)range + MAXPOT / 2) / MAXPOT) : 0;
        g->ptop = top;
    }
    upd_tags[0] = GA_ID; upd_tags[1] = g->g.GadgetID;
    upd_tags[2] = PGA_Top; upd_tags[3] = (ULONG)top;
    upd_tags[4] = TAG_DONE;
    m->Class = IDCMP_IDCMPUPDATE;
    m->IAddress = (APTR)upd_tags;
}

/* 1: give the message to the caller (perhaps changed), 0: used up here */
static int filter(struct IntuiMessage *m)
{
    G13 *g;
    switch (m->Class) {
    case IDCMP_INTUITICKS:
        if (held && held_win == m->IDCMPWindow && (held->g.Flags & GFLG_SELECTED)) {
            if (held->kind == K_SCROLLER && held->sbkind <= SB13_RIGHT) { as_update(m, held); return 1; }
            if (held->kind == LISTVIEW_KIND && held->part != 3) {
                G13 *l = held->owner;
                l->top = (WORD)(l->top + (held->part == 1 ? -1 : 1));
                list_show(l, m->IDCMPWindow);
                return 0;
            }
        }
        return 1;
    case IDCMP_MOUSEMOVE:
        if (held && held_win == m->IDCMPWindow) {
            if (held->kind == K_SCROLLER && held->sbkind >= SB13_VPROP) { as_update(m, held); return 1; }
            if (held->kind == LISTVIEW_KIND && held->part == 3) {
                G13 *l = held->owner;
                if (l->count > l->vis) {
                    WORD nt = (WORD)(((ULONG)held->pi.VertPot * (l->count - l->vis) + MAXPOT / 2) / MAXPOT);
                    if (nt != l->top) { l->top = nt; list_draw(l, m->IDCMPWindow); }
                }
                return 0;
            }
        }
        return 1;
    case IDCMP_GADGETDOWN:
    case IDCMP_GADGETUP:
        if (!IS13((struct Gadget *)m->IAddress)) return 1;
        g = (G13 *)m->IAddress;
        if (m->Class == IDCMP_GADGETDOWN) { held = g; held_win = m->IDCMPWindow; }
        else { if (held == g) held = NULL; }
        switch (g->kind) {
        case K_SCROLLER:
            if (m->Class == IDCMP_GADGETUP && g->sbkind <= SB13_RIGHT) return 0;
            as_update(m, g);
            return 1;
        case CYCLE_KIND:
            if (m->Class != IDCMP_GADGETUP || !g->nlab) return 1;
            if (m->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                g->active = (WORD)(g->active ? g->active - 1 : g->nlab - 1);
            else g->active = (WORD)((g->active + 1) % g->nlab);
            cycle_text(g);
            SetAPen(m->IDCMPWindow->RPort, pback());
            RectFill(m->IDCMPWindow->RPort, g->g.LeftEdge + 20, g->g.TopEdge + 1,
                     g->g.LeftEdge + g->g.Width - 3, g->g.TopEdge + g->g.Height - 2);
            RefreshGList(&g->g, m->IDCMPWindow, NULL, 1);
            m->Code = (UWORD)g->active;
            return 1;
        case LISTVIEW_KIND:
            if (g->part == 0) {
                WORD row;
                if (m->Class != IDCMP_GADGETDOWN) { held = NULL; return 0; }
                held = NULL;
                row = (WORD)((m->MouseY - g->g.TopEdge - 2) / g->rowh);
                if (row < 0 || row >= g->vis || g->top + row >= g->count) return 0;
                g->sel = (WORD)(g->top + row);
                list_draw(g, m->IDCMPWindow);
                m->Class = IDCMP_GADGETUP;
                m->Code = (UWORD)g->sel;
                return 1;
            } else {
                G13 *l = g->owner;
                if (g->part == 3) {
                    if (l->count > l->vis) l->top = (WORD)(((ULONG)g->pi.VertPot * (l->count - l->vis) + MAXPOT / 2) / MAXPOT);
                    list_draw(l, m->IDCMPWindow);
                } else if (m->Class == IDCMP_GADGETDOWN) {
                    l->top = (WORD)(l->top + (g->part == 1 ? -1 : 1));
                    list_show(l, m->IDCMPWindow);
                }
                return 0;
            }
        }
        return 1;
    }
    return 1;
}

struct IntuiMessage *c13_GT_GetIMsg(struct MsgPort *p)
{
    struct IntuiMessage *m;
    while ((m = (struct IntuiMessage *)GetMsg(p)) != NULL) {
        if (filter(m)) return m;
        ReplyMsg((struct Message *)m);
    }
    return NULL;
}

void c13_GT_ReplyIMsg(struct IntuiMessage *m)
{
    ReplyMsg((struct Message *)m);
}

/* ================================================================= */
/*  The document windows' scroll bars                                 */
/* ================================================================= */

#define SBW 16
#define SBH 10

struct Gadget *c13_scroller(int kind, int id)
{
    G13 *g = g13_new(K_SCROLLER);
    WORD *p;
    if (!g) return NULL;
    g->sbkind = (WORD)kind;
    g->g.GadgetID = (UWORD)id;
    g->total = g->visible = 1;
    if (kind >= SB13_VPROP) {
        g->g.GadgetType = GTYP_PROPGADGET;
        g->g.Flags = GFLG_GADGHNONE | GFLG_GADGIMAGE;
        g->g.Activation = GACT_IMMEDIATE | GACT_RELVERIFY | GACT_FOLLOWMOUSE;
        g->g.GadgetRender = (APTR)&g->kimg;
        g->g.SpecialInfo = (APTR)&g->pi;
        g->pi.Flags = AUTOKNOB | (kind == SB13_VPROP ? FREEVERT : FREEHORIZ);
        g->pi.VertBody = g->pi.HorizBody = MAXBODY;
        return &g->g;
    }
    g->g.GadgetType = GTYP_BOOLGADGET;
    g->g.Flags = GFLG_GADGHCOMP;
    g->g.Activation = GACT_IMMEDIATE | GACT_RELVERIFY;
    p = g->gp[0];
    switch (kind) {
    case SB13_UP:    p[0] = 4; p[1] = 6; p[2] = 8; p[3] = 2; p[4] = 12; p[5] = 6; break;
    case SB13_DOWN:  p[0] = 4; p[1] = 3; p[2] = 8; p[3] = 7; p[4] = 12; p[5] = 3; break;
    case SB13_LEFT:  p[0] = 10; p[1] = 2; p[2] = 5; p[3] = 5; p[4] = 10; p[5] = 8; break;
    default:         p[0] = 6; p[1] = 2; p[2] = 11; p[3] = 5; p[4] = 6; p[5] = 8; break;
    }
    g->glyph[0].FrontPen = ptext();
    g->glyph[0].DrawMode = JAM1;
    g->glyph[0].Count = 3;
    g->glyph[0].XY = p;
    g->g.GadgetRender = bevel13(&g->bv, 0, 0, SBW, SBH, 0, &g->glyph[0]);
    g->g.Width = SBW;
    g->g.Height = SBH;
    return &g->g;
}

void c13_free_scroller(struct Gadget *g)
{
    if (IS13(g)) {
        if (held == (G13 *)g) held = NULL;
        FreeMem(g, sizeof(G13));
    }
}

/* where the bars go: in the borders where they are wide enough (as on
 * 2.0), otherwise just inside them */
void c13_scroll_margins(struct Window *w, short *right, short *bottom)
{
    *right = (short)(w->BorderRight >= SBW ? 0 : SBW);
    *bottom = (short)(w->BorderBottom >= SBH ? 0 : SBH);
}

void c13_place_scrollers(struct Window *w, struct Gadget **gs)
{
    short mr, mb;
    WORD colr, rowb, vbot, hright;
    int inr = w->BorderRight >= SBW, inb = w->BorderBottom >= SBH, i;
    c13_scroll_margins(w, &mr, &mb);
    colr = (WORD)(w->BorderRight + mr);             /* the column's left edge, from the right */
    rowb = (WORD)(w->BorderBottom + mb);             /* the row's top edge, from the bottom */
    vbot = (WORD)(inr ? (w->BorderBottom > SBH ? w->BorderBottom : SBH) : rowb);
    hright = (WORD)(inb ? (w->BorderRight > 18 ? w->BorderRight : 18) : colr);
    for (i = 0; i < 6; i++) {
        struct Gadget *g = gs[i];
        if (!g) continue;
        g->Flags &= ~(UWORD)(GFLG_RELRIGHT | GFLG_RELBOTTOM | GFLG_RELWIDTH | GFLG_RELHEIGHT);
        g->Activation &= ~(UWORD)(GACT_RIGHTBORDER | GACT_BOTTOMBORDER);
        switch (i) {
        case SB13_UP: case SB13_DOWN: case SB13_VPROP:
            g->Flags |= GFLG_RELRIGHT;
            g->LeftEdge = (WORD)(1 - colr + (inr ? (colr - SBW) / 2 : 0));
            if (inr) g->Activation |= GACT_RIGHTBORDER;
            g->Width = SBW;
            break;
        default:
            g->Flags |= GFLG_RELBOTTOM;
            g->TopEdge = (WORD)(1 - rowb + (inb ? (rowb - SBH) / 2 : 0));
            if (inb) g->Activation |= GACT_BOTTOMBORDER;
            g->Height = SBH;
            break;
        }
        switch (i) {
        case SB13_DOWN: g->Flags |= GFLG_RELBOTTOM; g->TopEdge = (WORD)(1 - vbot - SBH); break;
        case SB13_UP: g->Flags |= GFLG_RELBOTTOM; g->TopEdge = (WORD)(1 - vbot - 2 * SBH); break;
        case SB13_VPROP:
            g->Flags |= GFLG_RELHEIGHT;
            g->TopEdge = (WORD)(w->BorderTop + 1);
            g->Height = (WORD)-(w->BorderTop + 1 + vbot + 2 * SBH + 1);
            g->LeftEdge += 2; g->Width = SBW - 4;
            break;
        case SB13_RIGHT: g->Flags |= GFLG_RELRIGHT; g->LeftEdge = (WORD)(1 - hright - SBW); break;
        case SB13_LEFT: g->Flags |= GFLG_RELRIGHT; g->LeftEdge = (WORD)(1 - hright - 2 * SBW); break;
        case SB13_HPROP:
            g->Flags |= GFLG_RELWIDTH;
            g->LeftEdge = (WORD)(w->BorderLeft + 1);
            g->Width = (WORD)-(w->BorderLeft + 1 + hright + 2 * SBW + 1);
            g->TopEdge += 2; g->Height = SBH - 4;
            break;
        }
        g->NextGadget = NULL;
    }
    for (i = 0; i < 6; i++) if (gs[i]) AddGadget(w, gs[i], (UWORD)~0);
    RefreshGadgets(w->FirstGadget, w, NULL);
}

void c13_set_prop(struct Gadget *gad, struct Window *w, LONG total, LONG visible, LONG top)
{
    G13 *g = (G13 *)gad;
    UWORD body, pot;
    if (!IS13(gad)) return;
    if (visible > total) total = visible;
    if (top > total - visible) top = total - visible;
    if (top < 0) top = 0;
    g->total = total; g->visible = visible; g->ptop = top;
    if (total <= visible || total <= 0) { body = MAXBODY; pot = 0; }
    else {
        body = (UWORD)((ULONG)MAXBODY * (ULONG)visible / (ULONG)total);
        pot = (UWORD)((ULONG)MAXPOT * (ULONG)top / (ULONG)(total - visible));
    }
    if (g->sbkind == SB13_VPROP) NewModifyProp(gad, w, NULL, AUTOKNOB | FREEVERT, 0, pot, MAXBODY, body, 1);
    else NewModifyProp(gad, w, NULL, AUTOKNOB | FREEHORIZ, pot, 0, body, MAXBODY, 1);
}

static void scroll_fix(struct Window *w) { (void)w; }

/* ================================================================= */
/*  Menus                                                             */
/* ================================================================= */

typedef struct MI13 {
    struct MenuItem mi;
    APTR userdata;                  /* where GTMENUITEM_USERDATA looks */
    struct IntuiText it;
    struct Image bar;
} MI13;

typedef struct {
    struct Menu *first;
    struct Menu *menus;
    MI13 *items;
    LONG nmenu, nitem;
} STRIP13;

#define NSTRIP 4
static STRIP13 strips[NSTRIP];

struct Menu *c13_CreateMenusA(const struct NewMenu *nm, const struct TagItem *tags)
{
    LONG nmenu = 0, nitem = 0, i, mi = -1, ii = 0;
    STRIP13 *s = NULL;
    struct MenuItem *last = NULL;
    (void)tags;
    for (i = 0; nm[i].nm_Type != NM_END; i++) {
        if (nm[i].nm_Type == NM_TITLE) nmenu++;
        else if (nm[i].nm_Type == NM_ITEM) nitem++;
    }
    for (i = 0; i < NSTRIP; i++) if (!strips[i].first) { s = &strips[i]; break; }
    if (!s || !nmenu) return NULL;
    s->menus = (struct Menu *)AllocMem(sizeof(struct Menu) * nmenu, MEMF_PUBLIC | MEMF_CLEAR);
    s->items = (MI13 *)AllocMem(sizeof(MI13) * (nitem ? nitem : 1), MEMF_PUBLIC | MEMF_CLEAR);
    if (!s->menus || !s->items) {
        if (s->menus) FreeMem(s->menus, sizeof(struct Menu) * nmenu);
        if (s->items) FreeMem(s->items, sizeof(MI13) * (nitem ? nitem : 1));
        s->menus = NULL; s->items = NULL;
        return NULL;
    }
    s->nmenu = nmenu; s->nitem = nitem;
    for (i = 0; nm[i].nm_Type != NM_END; i++) {
        const struct NewMenu *n = &nm[i];
        if (n->nm_Type == NM_TITLE) {
            struct Menu *m = &s->menus[++mi];
            if (mi > 0) s->menus[mi - 1].NextMenu = m;
            m->MenuName = (BYTE *)n->nm_Label;
            m->Flags = (n->nm_Flags & NM_MENUDISABLED) ? 0 : MENUENABLED;
            last = NULL;
        } else if (n->nm_Type == NM_ITEM && mi >= 0) {
            MI13 *it = &s->items[ii++];
            if (last) last->NextItem = &it->mi;
            else s->menus[mi].FirstItem = &it->mi;
            last = &it->mi;
            it->userdata = n->nm_UserData;
            if (n->nm_Label == NM_BARLABEL) {
                it->mi.Flags = HIGHNONE | ITEMENABLED;      /* not ghosted; it does nothing */
                it->bar.Height = 2;
                it->bar.Depth = 1;
                it->bar.PlanePick = 0;
                it->bar.PlaneOnOff = pshadow();
                it->mi.ItemFill = (APTR)&it->bar;
            } else {
                it->mi.Flags = ITEMTEXT | HIGHCOMP | ((n->nm_Flags & NM_ITEMDISABLED) ? 0 : ITEMENABLED);
                it->mi.Flags |= n->nm_Flags & (CHECKIT | MENUTOGGLE | CHECKED);
                it->mi.MutualExclude = n->nm_MutualExclude;
                if (n->nm_CommKey && n->nm_CommKey[0]) { it->mi.Flags |= COMMSEQ; it->mi.Command = (BYTE)n->nm_CommKey[0]; }
                it->it.FrontPen = ptext();
                it->it.BackPen = 2;
                it->it.DrawMode = JAM1;
                it->it.IText = (UBYTE *)n->nm_Label;
                it->mi.ItemFill = (APTR)&it->it;
            }
        }
    }
    s->first = &s->menus[0];
    return s->first;
}

BOOL c13_LayoutMenusA(struct Menu *first, APTR vi, const struct TagItem *tags)
{
    struct Screen *sc = (struct Screen *)vi;
    struct RastPort rp;
    struct Menu *m;
    WORD x = 5, fh, cw;
    (void)tags;
    if (!sc) return FALSE;
    InitRastPort(&rp);
    SetFont(&rp, sc->RastPort.Font);
    fh = (WORD)sc->RastPort.Font->tf_YSize;
    cw = (WORD)TextLength(&rp, (STRPTR)"M", 1);
    for (m = first; m; m = m->NextMenu) {
        struct MenuItem *it;
        WORD y = 0, maxw = 0, ck = 0, cm = 0;
        m->LeftEdge = x;
        m->TopEdge = 0;
        m->Width = (WORD)(TextLength(&rp, (STRPTR)m->MenuName, strlen((const char *)m->MenuName)) + cw + 6);
        m->Height = (WORD)sc->BarHeight;
        x = (WORD)(x + m->Width + 4);
        for (it = m->FirstItem; it; it = it->NextItem) {
            if (it->Flags & CHECKIT) ck = CHECKWIDTH;
            if (it->Flags & COMMSEQ) cm = (WORD)(COMMWIDTH + cw + 8);
        }
        for (it = m->FirstItem; it; it = it->NextItem)
            if (it->Flags & ITEMTEXT) {
                struct IntuiText *t = (struct IntuiText *)it->ItemFill;
                WORD w = (WORD)(TextLength(&rp, (STRPTR)t->IText, strlen((const char *)t->IText)) + ck + cm + 8);
                if (w > maxw) maxw = w;
            }
        for (it = m->FirstItem; it; it = it->NextItem) {
            it->LeftEdge = 0;
            it->TopEdge = y;
            it->Width = maxw;
            if (it->Flags & ITEMTEXT) {
                struct IntuiText *t = (struct IntuiText *)it->ItemFill;
                t->LeftEdge = (WORD)(ck + 2);
                t->TopEdge = 1;
                it->Height = (WORD)(fh + 2);
            } else {
                struct Image *b = (struct Image *)it->ItemFill;
                b->LeftEdge = 2; b->TopEdge = (WORD)(fh / 2 - 2);
                b->Width = (WORD)(maxw - 4);
                it->Height = (WORD)(fh - 1);        /* as tall as GadTools makes them */
            }
            y = (WORD)(y + it->Height);
        }
        /* a menu at the right edge opens to its left */
        if (m->LeftEdge + maxw > sc->Width - 2)
            for (it = m->FirstItem; it; it = it->NextItem) it->LeftEdge = (WORD)(sc->Width - 2 - m->LeftEdge - maxw);
    }
    return TRUE;
}

void c13_FreeMenus(struct Menu *m)
{
    int i;
    if (!m) return;
    for (i = 0; i < NSTRIP; i++)
        if (strips[i].first == m) {
            FreeMem(strips[i].menus, sizeof(struct Menu) * strips[i].nmenu);
            FreeMem(strips[i].items, sizeof(MI13) * (strips[i].nitem ? strips[i].nitem : 1));
            memset(&strips[i], 0, sizeof strips[i]);
        }
}

/* ================================================================= */
/*  Requesters                                                        */
/* ================================================================= */

/* a small window with gadgets made above, run until a button ends it */
typedef struct {
    struct Window *w;
    struct Gadget *glist, *last;
    struct NewGadget ng;
    struct Screen *s;
    WORD fh, bl, bt;
} RQ;

static int rq_open(RQ *r, struct Window *parent, const char *title, WORD iw, WORD ih, ULONG idcmp)
{
    struct Screen *s = parent ? parent->WScreen : scr ? scr : wb_screen();
    LONG x, y, ww, wh;
    if (!s) return 0;
    r->s = s;
    ww = iw + s->WBorLeft + s->WBorRight;
    wh = ih + s->WBorTop + s->Font->ta_YSize + 1 + s->WBorBottom;
    x = (s->Width - ww) / 2;
    y = (s->Height - wh) / 2;
    if (parent) {
        x = parent->LeftEdge + (parent->Width - ww) / 2;
        y = parent->TopEdge + (parent->Height - wh) / 2;
    }
    r->w = c13_OpenWindowTagList(NULL, (struct TagItem *)(ULONG[]){
        (s->Flags & SCREENTYPE) == WBENCHSCREEN ? WA_PubScreen : WA_CustomScreen, (ULONG)s,
        WA_Left, (ULONG)x, WA_Top, (ULONG)y, WA_Width, (ULONG)ww, WA_Height, (ULONG)wh,
        WA_Title, (ULONG)title, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_Activate, TRUE,
        WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
        WA_IDCMP, idcmp | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW |
                  IDCMP_MOUSEMOVE | IDCMP_INTUITICKS, TAG_DONE });
    if (!r->w) return 0;
    r->bl = r->w->BorderLeft;
    r->bt = r->w->BorderTop;
    SetAPen(r->w->RPort, pback());
    RectFill(r->w->RPort, r->bl, r->bt, r->w->Width - r->w->BorderRight - 1, r->w->Height - r->w->BorderBottom - 1);
    return 1;
}

static void rq_close(RQ *r)
{
    struct IntuiMessage *m;
    if (!r->w) return;
    if (r->glist) RemoveGList(r->w, r->glist, -1);
    while ((m = (struct IntuiMessage *)GetMsg(r->w->UserPort)) != NULL) ReplyMsg((struct Message *)m);
    CloseWindow(r->w);
    r->w = NULL;
    if (r->glist) c13_FreeGadgets(r->glist);
    r->glist = NULL;
}

static void rq_show(RQ *r)
{
    AddGList(r->w, r->glist, (UWORD)~0, -1, NULL);
    RefreshGList(r->glist, r->w, NULL, -1);
    list_view_draw_all(r->w);
}

static struct Gadget *rq_gad(RQ *r, ULONG kind, WORD x, WORD y, WORD w, WORD h, const char *label, ULONG flags, UWORD id,
                             const struct TagItem *tags)
{
    r->ng.ng_LeftEdge = (WORD)(r->bl + x);
    r->ng.ng_TopEdge = (WORD)(r->bt + y);
    r->ng.ng_Width = w;
    r->ng.ng_Height = h;
    r->ng.ng_GadgetText = (UBYTE *)label;
    r->ng.ng_Flags = flags;
    r->ng.ng_GadgetID = id;
    r->ng.ng_TextAttr = ui_ta();
    r->last = c13_CreateGadgetA(kind, r->last, &r->ng, tags);
    return r->last;
}

LONG c13_EasyRequestArgs(struct Window *pw, const struct EasyStruct *es, ULONG *idcmp, const APTR args)
{
    char text[400], btns[160], *lines[12], *bt[6];
    int nl = 0, nb = 0, i, res = -1;
    WORD fh, maxw = 0, bw = 0, iw, ih, x;
    RQ r;
    const char *p;
    char *q;
    (void)idcmp; (void)args;
    memset(&r, 0, sizeof r);
    /* the text: %% is a single %; | or newline splits lines */
    for (p = (const char *)es->es_TextFormat, q = text; *p && q < text + sizeof text - 1; p++) {
        if (p[0] == '%' && p[1] == '%') p++;
        *q++ = *p;
    }
    *q = 0;
    lines[nl++] = text;
    for (q = text; *q && nl < 12; q++) if (*q == '\n') { *q = 0; lines[nl++] = q + 1; }
    strncpy(btns, (const char *)es->es_GadgetFormat, sizeof btns - 1);
    btns[sizeof btns - 1] = 0;
    bt[nb++] = btns;
    for (q = btns; *q && nb < 6; q++) if (*q == '|') { *q = 0; bt[nb++] = q + 1; }
    fh = (WORD)ui_ta()->ta_YSize;
    for (i = 0; i < nl; i++) { WORD l = text_len(ui_ta(), lines[i]); if (l > maxw) maxw = l; }
    for (i = 0; i < nb; i++) { WORD l = (WORD)(text_len(ui_ta(), bt[i]) + 24); if (l < 64) l = 64; bw = (WORD)(bw + l + 8); }
    iw = (WORD)((maxw + 40 > bw + 16 ? maxw + 40 : bw + 16));
    ih = (WORD)(16 + nl * (fh + 1) + 12 + fh + 6 + 8);
    if (!rq_open(&r, pw, es->es_Title ? (const char *)es->es_Title : "EdWord", iw, ih, 0)) {
        /* nothing to show it in: the first button */
        return nb > 1 ? 1 : 0;
    }
    c13_CreateContext(&r.glist);
    r.last = r.glist;
    {
        WORD sp = (WORD)(nb > 1 ? (iw - 16 - (bw - 8 * nb)) / (nb - 1) : 0);
        x = 8;
        for (i = 0; i < nb; i++) {
            WORD l = (WORD)(text_len(ui_ta(), bt[i]) + 24);
            if (l < 64) l = 64;
            if (nb == 1) x = (WORD)((iw - l) / 2);
            rq_gad(&r, BUTTON_KIND, x, (WORD)(ih - fh - 6 - 8), l, (WORD)(fh + 6), bt[i], PLACETEXT_IN,
                   (UWORD)(i + 1 < nb ? i + 1 : 100), NULL);
            x = (WORD)(x + l + sp);
        }
    }
    rq_show(&r);
    {
        struct RastPort *rp = r.w->RPort;
        SetFont(rp, r.w->WScreen->RastPort.Font);
        SetAPen(rp, ptext());
        SetDrMd(rp, JAM1);
        for (i = 0; i < nl; i++) {
            Move(rp, r.bl + 20, r.bt + 12 + i * (fh + 1) + rp->TxBaseline);
            Text(rp, (STRPTR)lines[i], strlen(lines[i]));
        }
    }
    while (res < 0) {
        struct IntuiMessage *m;
        WaitPort(r.w->UserPort);
        while (res < 0 && (m = c13_GT_GetIMsg(r.w->UserPort)) != NULL) {
            if (m->Class == IDCMP_GADGETUP) {
                UWORD id = ((struct Gadget *)m->IAddress)->GadgetID;
                res = id == 100 ? 0 : id;
            } else if (m->Class == IDCMP_VANILLAKEY) {
                if (m->Code == 13) res = nb > 1 ? 1 : 0;
                else if (m->Code == 27) res = 0;
            }
            ReplyMsg((struct Message *)m);
        }
    }
    rq_close(&r);
    return res;
}

/* ---- the file requester --------------------------------------------- */

typedef struct ENT { struct Node n; UBYTE dir; char name[34]; } ENT;

typedef struct {
    struct FileRequester fr;        /* what the caller reads */
    char file[110], drawer[256], pattern[80], title[80];
    int save, patgad;
    struct Window *win;
    ENT *ents;
    LONG nent, cap;
    struct List list;
    int volumes;
} FREQ;

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c; }

/* AmigaDOS patterns: #x (any number of x), ? (any one character),
 * (a|b) (either); case doesn't matter */
static const char *unit_end(const char *p, const char *pe)
{
    if (*p == '(') {
        int d = 0;
        do { if (*p == '(') d++; else if (*p == ')') d--; p++; } while (p < pe && d);
        return p;
    }
    if (*p == '#' && p + 1 < pe) return unit_end(p + 1, pe);
    return p + 1;
}

static int pmatch(const char *p, const char *pe, const char *s, const char *se);

static int unit_match(const char *u, const char *ue, const char *s, const char *se)
{
    if (*u == '?') return se == s + 1;
    if (*u == '(') {
        const char *a = u + 1, *e = ue - 1, *k;
        int d = 0;
        for (k = a; k <= e; k++) {
            if (k == e || (*k == '|' && d == 0)) {
                if (pmatch(a, k, s, se)) return 1;
                a = k + 1;
            } else if (*k == '(') d++;
            else if (*k == ')') d--;
        }
        return 0;
    }
    return se == s + 1 && lower((unsigned char)*u) == lower((unsigned char)*s);
}

static int pmatch(const char *p, const char *pe, const char *s, const char *se)
{
    const char *ue, *t;
    if (p == pe) return s == se;
    if (*p == '#') {
        const char *u = p + 1;
        ue = unit_end(u, pe);
        if (pmatch(ue, pe, s, se)) return 1;
        for (t = s + 1; t <= se; t++)
            if (unit_match(u, ue, s, t) && pmatch(p, pe, t, se)) return 1;
        return 0;
    }
    ue = unit_end(p, pe);
    for (t = s; t <= se; t++)
        if (unit_match(p, ue, s, t) && pmatch(ue, pe, t, se)) return 1;
    return 0;
}

static int pattern_ok(const char *pat, const char *name)
{
    if (!pat[0] || !strcmp(pat, "#?") || !strcmp(pat, "*")) return 1;
    return pmatch(pat, pat + strlen(pat), name, name + strlen(name));
}

static int ent_cmp(const ENT *a, const ENT *b)
{
    const char *x = a->name, *y = b->name;
    if (a->dir != b->dir) return a->dir ? -1 : 1;
    while (*x && lower((unsigned char)*x) == lower((unsigned char)*y)) { x++; y++; }
    return lower((unsigned char)*x) - lower((unsigned char)*y);
}

static int add_ent(FREQ *f, const char *name, int dir)
{
    ENT *e;
    if (f->nent >= f->cap) {
        LONG nc = f->cap ? f->cap * 2 : 64;
        ENT *n = (ENT *)malloc(sizeof(ENT) * nc);
        if (!n) return 0;
        if (f->ents) { memcpy(n, f->ents, sizeof(ENT) * f->nent); free(f->ents); }
        f->ents = n;
        f->cap = nc;
    }
    e = &f->ents[f->nent++];
    memset(e, 0, sizeof *e);
    strncpy(e->name, name, 31);
    e->dir = (UBYTE)dir;
    if (dir == 1) strcat(e->name, "/");
    return 1;
}

static void build_list(FREQ *f)
{
    LONG i, j;
    /* insertion sort: directories are rarely long */
    for (i = 1; i < f->nent; i++) {
        ENT t = f->ents[i];
        for (j = i; j > 0 && ent_cmp(&f->ents[j - 1], &t) > 0; j--) f->ents[j] = f->ents[j - 1];
        f->ents[j] = t;
    }
    new_list(&f->list);
    for (i = 0; i < f->nent; i++) {
        f->ents[i].n.ln_Name = f->ents[i].name;
        AddTail(&f->list, &f->ents[i].n);
    }
}

static void read_dir(FREQ *f)
{
    BPTR l;
    struct FileInfoBlock *fib;
    f->nent = 0;
    f->volumes = 0;
    l = Lock((STRPTR)(f->drawer[0] ? f->drawer : ""), ACCESS_READ);
    fib = (struct FileInfoBlock *)AllocMem(sizeof *fib, MEMF_PUBLIC | MEMF_CLEAR);
    if (l && fib && Examine(l, fib)) {
        while (ExNext(l, fib)) {
            int dir = fib->fib_DirEntryType > 0;
            const char *n = (const char *)fib->fib_FileName;
            int len = (int)strlen(n);
            if (!dir && len > 5 && !strcmp(n + len - 5, ".info")) continue;
            if (!dir && !pattern_ok(f->pattern, n)) continue;
            if (!add_ent(f, n, dir)) break;
        }
    }
    if (fib) FreeMem(fib, sizeof *fib);
    if (l) UnLock(l);
    build_list(f);
}

/* the volumes and assigns (DOS's device list, as 1.3 keeps it) */
static void read_volumes(FREQ *f)
{
    struct RootNode *rn = (struct RootNode *)DOSBase->dl_Root;
    struct DosInfo *di = (struct DosInfo *)BADDR(rn->rn_Info);
    struct DeviceList *dl;
    f->nent = 0;
    f->volumes = 1;
    Forbid();
    for (dl = (struct DeviceList *)BADDR(di->di_DevInfo); dl; dl = (struct DeviceList *)BADDR(dl->dl_Next)) {
        if (dl->dl_Type == DLT_VOLUME || dl->dl_Type == DLT_DIRECTORY) {
            UBYTE *b = (UBYTE *)BADDR(dl->dl_Name);
            char n[34];
            int len = b ? b[0] : 0;
            if (len > 30) len = 30;
            if (!len) continue;
            memcpy(n, b + 1, len);
            n[len] = ':';
            n[len + 1] = 0;
            if (!add_ent(f, n, 2)) break;
        }
    }
    Permit();
    build_list(f);
}

enum { FR_LIST = 1, FR_DRAWER, FR_FILE, FR_PATTERN, FR_OK, FR_VOLUMES, FR_PARENT, FR_CANCEL };

APTR c13_AllocAslRequest(ULONG type, const struct TagItem *tags)
{
    FREQ *f;
    const struct TagItem *t;
    if (type != ASL_FileRequest) return NULL;
    f = (FREQ *)calloc(1, sizeof *f);
    if (!f) return NULL;
    strcpy(f->pattern, "#?");
    while ((t = next_tag(&tags)) != NULL) {
        switch (t->ti_Tag) {
        case ASL_Hail: strncpy(f->title, (const char *)t->ti_Data, sizeof f->title - 1); break;
        case ASL_Window: f->win = (struct Window *)t->ti_Data; break;
        case ASL_Dir: if (t->ti_Data) strncpy(f->drawer, (const char *)t->ti_Data, sizeof f->drawer - 1); break;
        case ASL_File: if (t->ti_Data) strncpy(f->file, (const char *)t->ti_Data, sizeof f->file - 1); break;
        case ASL_Pattern: if (t->ti_Data) strncpy(f->pattern, (const char *)t->ti_Data, sizeof f->pattern - 1); break;
        case ASL_FuncFlags: f->save = (t->ti_Data & FILF_SAVE) != 0; f->patgad = (t->ti_Data & FILF_PATGAD) != 0; break;
        }
    }
    f->fr.fr_File = (STRPTR)f->file;
    f->fr.fr_Drawer = (STRPTR)f->drawer;
    return (APTR)f;
}

void c13_FreeAslRequest(APTR req)
{
    FREQ *f = (FREQ *)req;
    if (!f) return;
    if (f->ents) free(f->ents);
    free(f);
}

static void fr_refresh(FREQ *f, RQ *r, struct Gadget *lv, struct Gadget *dg)
{
    c13_GT_SetGadgetAttrsA(lv, r->w, NULL, (struct TagItem *)(ULONG[]){ GTLV_Labels, ~0UL, TAG_DONE });
    if (f->volumes) read_volumes(f); else read_dir(f);
    c13_GT_SetGadgetAttrsA(lv, r->w, NULL, (struct TagItem *)(ULONG[]){
        GTLV_Labels, (ULONG)&f->list, GTLV_Top, 0, GTLV_Selected, ~0UL, TAG_DONE });
    c13_GT_SetGadgetAttrsA(dg, r->w, NULL, (struct TagItem *)(ULONG[]){ GTST_String, (ULONG)f->drawer, TAG_DONE });
}

static void get_str(struct Gadget *g, char *out, int n)
{
    struct StringInfo *si = (struct StringInfo *)g->SpecialInfo;
    strncpy(out, (const char *)si->Buffer, n - 1);
    out[n - 1] = 0;
}

static void parent_of(char *d)
{
    char *p = (char *)c13_PathPart((STRPTR)d);
    int n = (int)strlen(d);
    if (n && d[n - 1] == ':') return;              /* a volume has no parent here */
    if (!strchr(d, ':') && !strchr(d, '/')) { d[0] = 0; return; }
    *p = 0;
}

BOOL c13_AslRequest(APTR req, const struct TagItem *tags)
{
    FREQ *f = (FREQ *)req;
    RQ r;
    WORD fh = (WORD)ui_ta()->ta_YSize, gh, iw = 320, ih, y, rows, lw;
    struct Gadget *lv, *dg, *fg, *pg = NULL;
    struct Screen *s;
    int res = -1, i;
    ULONG lsec = 0, lmic = 0;
    LONG lsel = -1;
    static const int bl[4] = { S_BTN_OK, S_FR_VOLUMES, S_FR_PARENT, S_CANCEL };
    (void)tags;
    if (!f) return FALSE;
    memset(&r, 0, sizeof r);
    gh = (WORD)(fh + 6);
    s = f->win ? f->win->WScreen : scr;
    if (!s) return FALSE;
    rows = (WORD)((s->Height - s->BarHeight - 40 - (f->patgad ? 4 : 3) * (gh + 4) - 30) / fh);
    if (rows > 14) rows = 14;
    if (rows < 4) rows = 4;
    ih = (WORD)(8 + rows * fh + 4 + 6 + (f->patgad ? 3 : 2) * (gh + 4) + gh + 10);
    if (!rq_open(&r, f->win, f->title[0] ? f->title : "EdWord", iw, ih, 0)) return FALSE;
    c13_CreateContext(&r.glist);
    r.last = r.glist;
    lw = (WORD)(text_len(ui_ta(), L(S_FR_PATTERN)) + 12);
    if (text_len(ui_ta(), L(S_FR_DRAWER)) + 12 > lw) lw = (WORD)(text_len(ui_ta(), L(S_FR_DRAWER)) + 12);
    if (text_len(ui_ta(), L(S_AMI_FILE)) + 12 > lw) lw = (WORD)(text_len(ui_ta(), L(S_AMI_FILE)) + 12);
    y = 8;
    lv = rq_gad(&r, LISTVIEW_KIND, 8, y, (WORD)(iw - 16), (WORD)(rows * fh + 4), NULL, 0, FR_LIST,
                (struct TagItem *)(ULONG[]){ GTLV_ShowSelected, 0, TAG_DONE });
    y = (WORD)(y + rows * fh + 4 + 6);
    if (f->patgad) {
        pg = rq_gad(&r, STRING_KIND, (WORD)(8 + lw), y, (WORD)(iw - 16 - lw), gh, L(S_FR_PATTERN), PLACETEXT_LEFT, FR_PATTERN,
                    (struct TagItem *)(ULONG[]){ GTST_String, (ULONG)f->pattern, GTST_MaxChars, 79, TAG_DONE });
        y = (WORD)(y + gh + 4);
    }
    dg = rq_gad(&r, STRING_KIND, (WORD)(8 + lw), y, (WORD)(iw - 16 - lw), gh, L(S_FR_DRAWER), PLACETEXT_LEFT, FR_DRAWER,
                (struct TagItem *)(ULONG[]){ GTST_String, (ULONG)f->drawer, GTST_MaxChars, 255, TAG_DONE });
    y = (WORD)(y + gh + 4);
    fg = rq_gad(&r, STRING_KIND, (WORD)(8 + lw), y, (WORD)(iw - 16 - lw), gh, L(S_AMI_FILE), PLACETEXT_LEFT, FR_FILE,
                (struct TagItem *)(ULONG[]){ GTST_String, (ULONG)f->file, GTST_MaxChars, 107, TAG_DONE });
    y = (WORD)(y + gh + 10);
    for (i = 0; i < 4; i++)
        rq_gad(&r, BUTTON_KIND, (WORD)(8 + i * ((iw - 16) / 4)), y, (WORD)((iw - 16) / 4 - 4), gh, L(bl[i]), PLACETEXT_IN,
               (UWORD)(FR_OK + i), NULL);
    if (!r.last || !lv || !dg || !fg) { rq_close(&r); return FALSE; }
    rq_show(&r);
    fr_refresh(f, &r, lv, dg);
    ActivateGadget(fg, r.w, NULL);
    while (res < 0) {
        struct IntuiMessage *m;
        WaitPort(r.w->UserPort);
        while (res < 0 && (m = c13_GT_GetIMsg(r.w->UserPort)) != NULL) {
            ULONG cl = m->Class, sec = m->Seconds, mic = m->Micros;
            UWORD code = m->Code;
            struct Gadget *g = (struct Gadget *)m->IAddress;
            ReplyMsg((struct Message *)m);
            if (cl == IDCMP_VANILLAKEY && code == 27) { res = 0; break; }
            if (cl == IDCMP_REFRESHWINDOW) { BeginRefresh(r.w); list_view_draw_all(r.w); EndRefresh(r.w, TRUE); continue; }
            if (cl != IDCMP_GADGETUP) continue;
            switch (g->GadgetID) {
            case FR_LIST:
                if (code >= f->nent) break;
                {
                    ENT *e = &f->ents[code];
                    if (e->dir == 2) {                      /* a volume */
                        strcpy(f->drawer, e->name);
                        f->volumes = 0;
                        fr_refresh(f, &r, lv, dg);
                    } else if (e->dir == 1) {               /* a drawer */
                        char n[34];
                        strcpy(n, e->name);
                        n[strlen(n) - 1] = 0;
                        get_str(dg, f->drawer, sizeof f->drawer);
                        if (c13_AddPart((STRPTR)f->drawer, (STRPTR)n, sizeof f->drawer)) fr_refresh(f, &r, lv, dg);
                    } else {
                        c13_GT_SetGadgetAttrsA(fg, r.w, NULL, (struct TagItem *)(ULONG[]){ GTST_String, (ULONG)e->name, TAG_DONE });
                        if (lsel == code && DoubleClick(lsec, lmic, sec, mic)) res = 1;
                    }
                }
                lsel = code; lsec = sec; lmic = mic;
                break;
            case FR_DRAWER:
                get_str(dg, f->drawer, sizeof f->drawer);
                f->volumes = 0;
                fr_refresh(f, &r, lv, dg);
                break;
            case FR_PATTERN:
                if (pg) get_str(pg, f->pattern, sizeof f->pattern);
                f->volumes = 0;
                fr_refresh(f, &r, lv, dg);
                break;
            case FR_FILE:
            case FR_OK: res = 1; break;
            case FR_VOLUMES: f->volumes = 1; fr_refresh(f, &r, lv, dg); break;
            case FR_PARENT:
                get_str(dg, f->drawer, sizeof f->drawer);
                parent_of(f->drawer);
                f->volumes = 0;
                fr_refresh(f, &r, lv, dg);
                break;
            case FR_CANCEL: res = 0; break;
            }
        }
    }
    if (res == 1) {
        char fl[110];
        get_str(dg, f->drawer, sizeof f->drawer);
        get_str(fg, fl, sizeof fl);
        /* a name with a path in it: the path goes to the drawer */
        if (strchr(fl, '/') || strchr(fl, ':')) {
            char full[370];
            strcpy(full, f->drawer);
            c13_AddPart((STRPTR)full, (STRPTR)fl, sizeof full);
            strncpy(f->file, (const char *)c13_FilePart((STRPTR)full), sizeof f->file - 1);
            *c13_PathPart((STRPTR)full) = 0;
            strncpy(f->drawer, full, sizeof f->drawer - 1);
        } else strcpy(f->file, fl);
        if (!f->file[0]) res = 0;
    }
    rq_close(&r);
    return res == 1;
}
