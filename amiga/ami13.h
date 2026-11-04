/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ami13.h - EdWord on Workbench 1.2/1.3 (Kickstart 33/34)
 *
 * The Amiga port is written for Workbench 2.04: tag calls, GadTools,
 * asl.library, public screens, EasyRequest. Built with AMI13 defined,
 * this header (included at the end of amiui.h, after the system headers)
 * sends those calls to ami13.c, which does the same with what 1.3 has:
 * NewWindow/NewScreen, classic gadgets drawn with borders, menus built
 * by hand, a requester window of its own and a file requester.
 */
#ifndef AMI13_H
#define AMI13_H

#include <exec/ports.h>
#include <exec/io.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <utility/tagitem.h>
#include <graphics/displayinfo.h>
#include <devices/timer.h>

/* ---- what ami13.c provides --------------------------------------------- */

struct MsgPort   *c13_CreateMsgPort(void);
void              c13_DeleteMsgPort(struct MsgPort *p);
struct IORequest *c13_CreateIORequest(struct MsgPort *p, ULONG size);
void              c13_DeleteIORequest(struct IORequest *io);
BOOL              c13_AddPart(STRPTR dir, CONST_STRPTR name, ULONG size);
STRPTR            c13_PathPart(CONST_STRPTR path);
STRPTR            c13_FilePart(CONST_STRPTR path);
BOOL              c13_NameFromLock(BPTR lock, STRPTR buf, LONG len);
ULONG             c13_GetTagData(Tag t, ULONG def, const struct TagItem *tags);
ULONG             c13_ReadEClock(struct EClockVal *e);
ULONG             c13_GetVPModeID(struct ViewPort *vp);
ULONG             c13_ModeNotAvailable(ULONG id);
struct Window    *c13_OpenWindowTagList(const struct NewWindow *nw, const struct TagItem *tags);
struct Screen    *c13_OpenScreenTagList(const struct NewScreen *ns, const struct TagItem *tags);
struct Screen    *c13_LockPubScreen(CONST_STRPTR name);
void              c13_UnlockPubScreen(CONST_STRPTR name, struct Screen *s);
LONG              c13_EasyRequestArgs(struct Window *w, const struct EasyStruct *es, ULONG *idcmp, const APTR args);
struct DrawInfo  *c13_GetScreenDrawInfo(struct Screen *s);
void              c13_FreeScreenDrawInfo(struct Screen *s, struct DrawInfo *d);
BOOL              c13_ResetMenuStrip(struct Window *w, struct Menu *m);
void              c13_ChangeWindowBox(struct Window *w, LONG l, LONG t, LONG wd, LONG h);
BOOL              c13_ModifyIDCMP(struct Window *w, ULONG flags);
struct Gadget    *c13_CreateContext(struct Gadget **glist);
struct Gadget    *c13_CreateGadgetA(ULONG kind, struct Gadget *prev, const struct NewGadget *ng, const struct TagItem *tags);
void              c13_FreeGadgets(struct Gadget *g);
void              c13_GT_SetGadgetAttrsA(struct Gadget *g, struct Window *w, struct Requester *r, const struct TagItem *tags);
struct IntuiMessage *c13_GT_GetIMsg(struct MsgPort *p);
void              c13_GT_ReplyIMsg(struct IntuiMessage *m);
void              c13_GT_RefreshWindow(struct Window *w, struct Requester *r);
void              c13_GT_BeginRefresh(struct Window *w);
void              c13_GT_EndRefresh(struct Window *w, LONG complete);
struct Menu      *c13_CreateMenusA(const struct NewMenu *nm, const struct TagItem *tags);
BOOL              c13_LayoutMenusA(struct Menu *m, APTR vi, const struct TagItem *tags);
void              c13_FreeMenus(struct Menu *m);
APTR              c13_GetVisualInfoA(struct Screen *s, const struct TagItem *tags);
void              c13_FreeVisualInfo(APTR vi);
APTR              c13_AllocAslRequest(ULONG type, const struct TagItem *tags);
BOOL              c13_AslRequest(APTR req, const struct TagItem *tags);
void              c13_FreeAslRequest(APTR req);

/* the document windows' scroll bars: classic gadgets, placed once the
 * window is open (1.3 doesn't widen the borders for them); their moves
 * arrive as IDCMP_IDCMPUPDATE with GA_ID and PGA_Top, as from BOOPSI */
enum { SB13_UP, SB13_DOWN, SB13_LEFT, SB13_RIGHT, SB13_VPROP, SB13_HPROP };
struct Gadget    *c13_scroller(int kind, int id);
void              c13_place_scrollers(struct Window *w, struct Gadget **g);  /* the six, in SB13_ order */
void              c13_set_prop(struct Gadget *g, struct Window *w, LONG total, LONG visible, LONG top);
void              c13_free_scroller(struct Gadget *g);
void              c13_scroll_margins(struct Window *w, short *right, short *bottom);
void              c13_init(void);
struct WBStartup;
void              c13_progdir(char *out, int n, struct WBStartup *wb);

extern struct Library c13_asl;           /* stands for asl.library (version 37) */

#ifndef AMI13_IMPL
/* ---- send the 2.0 calls there ------------------------------------------ */
#undef CreateMsgPort
#define CreateMsgPort() c13_CreateMsgPort()
#undef DeleteMsgPort
#define DeleteMsgPort(p) c13_DeleteMsgPort(p)
#undef CreateIORequest
#define CreateIORequest(p, s) c13_CreateIORequest((p), (s))
#undef DeleteIORequest
#define DeleteIORequest(io) c13_DeleteIORequest((struct IORequest *)(io))
#undef AddPart
#define AddPart(d, n, s) c13_AddPart((d), (n), (s))
#undef PathPart
#define PathPart(p) c13_PathPart(p)
#undef FilePart
#define FilePart(p) c13_FilePart(p)
#undef NameFromLock
#define NameFromLock(l, b, n) c13_NameFromLock((l), (b), (n))
#undef GetTagData
#define GetTagData(t, d, l) c13_GetTagData((t), (d), (l))
#undef ReadEClock
#define ReadEClock(e) c13_ReadEClock(e)
#undef GetVPModeID
#define GetVPModeID(vp) c13_GetVPModeID(vp)
#undef ModeNotAvailable
#define ModeNotAvailable(id) c13_ModeNotAvailable(id)
#undef FindDisplayInfo
#define FindDisplayInfo(id) ((DisplayInfoHandle)NULL)
#undef GetDisplayInfoData
#define GetDisplayInfoData(h, b, s, t, id) 0
#undef OpenWindowTagList
#define OpenWindowTagList(nw, t) c13_OpenWindowTagList((nw), (t))
#undef OpenScreenTagList
#define OpenScreenTagList(ns, t) c13_OpenScreenTagList((ns), (t))
#undef LockPubScreen
#define LockPubScreen(n) c13_LockPubScreen(n)
#undef UnlockPubScreen
#define UnlockPubScreen(n, s) c13_UnlockPubScreen((n), (s))
#undef EasyRequestArgs
#define EasyRequestArgs(w, e, i, a) c13_EasyRequestArgs((w), (e), (i), (a))
#undef GetScreenDrawInfo
#define GetScreenDrawInfo(s) c13_GetScreenDrawInfo(s)
#undef FreeScreenDrawInfo
#define FreeScreenDrawInfo(s, d) c13_FreeScreenDrawInfo((s), (d))
#undef ResetMenuStrip
#define ResetMenuStrip(w, m) c13_ResetMenuStrip((w), (m))
#undef ChangeWindowBox
#define ChangeWindowBox(w, l, t, wd, h) c13_ChangeWindowBox((w), (l), (t), (wd), (h))
#undef ModifyIDCMP
#define ModifyIDCMP(w, f) c13_ModifyIDCMP((w), (f))
#undef CreateContext
#define CreateContext(g) c13_CreateContext(g)
#undef CreateGadgetA
#define CreateGadgetA(k, g, n, t) c13_CreateGadgetA((k), (g), (n), (t))
#undef FreeGadgets
#define FreeGadgets(g) c13_FreeGadgets(g)
#undef GT_SetGadgetAttrsA
#define GT_SetGadgetAttrsA(g, w, r, t) c13_GT_SetGadgetAttrsA((g), (w), (r), (t))
#undef GT_GetIMsg
#define GT_GetIMsg(p) c13_GT_GetIMsg(p)
#undef GT_ReplyIMsg
#define GT_ReplyIMsg(m) c13_GT_ReplyIMsg(m)
#undef GT_RefreshWindow
#define GT_RefreshWindow(w, r) c13_GT_RefreshWindow((w), (r))
#undef GT_BeginRefresh
#define GT_BeginRefresh(w) c13_GT_BeginRefresh(w)
#undef GT_EndRefresh
#define GT_EndRefresh(w, c) c13_GT_EndRefresh((w), (c))
#undef CreateMenusA
#define CreateMenusA(n, t) c13_CreateMenusA((n), (t))
#undef LayoutMenusA
#define LayoutMenusA(m, v, t) c13_LayoutMenusA((m), (v), (t))
#undef FreeMenus
#define FreeMenus(m) c13_FreeMenus(m)
#undef GetVisualInfoA
#define GetVisualInfoA(s, t) c13_GetVisualInfoA((s), (t))
#undef FreeVisualInfo
#define FreeVisualInfo(v) c13_FreeVisualInfo(v)
#undef AllocAslRequest
#define AllocAslRequest(k, t) c13_AllocAslRequest((k), (t))
#undef AslRequest
#define AslRequest(r, t) c13_AslRequest((r), (t))
#undef FreeAslRequest
#define FreeAslRequest(r) c13_FreeAslRequest(r)
#endif

#endif
