/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * windlg.c - dialogs built from a list of items and laid out by
 *            themselves, on Windows controls; alerts, the popup menu,
 *            the file dialogs, the progress gauge, and all of EdWord's
 *            dialogs (after amiga/amidlg.c)
 *
 * A radio group is a drop-down list, as on the Amiga a cycle gadget.
 */
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "winui.h"

enum { T_LABEL, T_TEXT, T_RADIO, T_CHECK, T_EDIT, T_BUTTON, T_BIG, T_TITLE };

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

/* ---- the controls --------------------------------------------------- */

#define ID0 100                       /* control ids: ID0 + item */

static HWND ctl(DLG *g, const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(!strcmp(cls, "EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, g->win, (HMENU)(INT_PTR)id, hinst, NULL);
    if (c) SendMessageA(c, WM_SETFONT, (WPARAM)uifont, FALSE);
    return c;
}

/* make the controls for the window g->win */
int dlg_gadgets(DLG *g)
{
    int i, j, hh = hchar + 6;
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        int x = it->gx, y = it->gy, w = it->gw;
        it->gad = it->gad2 = it->gad3 = NULL;
        if (it->hidden || it->type == T_TITLE || (it->type == T_RADIO && it->lead != i)) continue;
        switch (it->type) {
        case T_LABEL: case T_TEXT:
            it->gad = ctl(g, "STATIC", it->s, SS_LEFT | SS_NOPREFIX, x, y + 3, w + 4, hchar + 2, ID0 + i);
            break;
        case T_CHECK:
            it->gad = ctl(g, "BUTTON", it->s, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, w, hh, ID0 + i);
            SendMessageA(it->gad, BM_SETCHECK, it->on ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        case T_RADIO:
            it->gw = item_w(g, it);
            it->gad = ctl(g, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, x, y, it->gw, hh + 200, ID0 + i);
            for (j = i; j < g->nit && g->it[j].lead == i; j++) SendMessageA(it->gad, CB_ADDSTRING, 0, (LPARAM)g->it[j].s);
            SendMessageA(it->gad, CB_SETCURSEL, (WPARAM)it->on, 0);
            break;
        case T_EDIT: {
            long lw = it->label[0] ? sys_width(it->label) + 6 : 0, aw = it->after[0] ? 6 + sys_width(it->after) : 0;
            char tmp[260];
            if (lw) it->gad2 = ctl(g, "STATIC", it->label, SS_LEFT | SS_NOPREFIX, x, y + 3, (int)lw, hchar + 2, 0);
            lat_n(tmp, it->buf, (long)strlen(it->buf));
            it->gad = ctl(g, "EDIT", tmp, ES_AUTOHSCROLL | WS_TABSTOP |
                          (it->valid && it->valid[0] == '9' ? ES_NUMBER : 0), x + (int)lw, y, (int)(w - lw - aw), hh, ID0 + i);
            SendMessageA(it->gad, EM_LIMITTEXT, it->valid ? strlen(it->valid) : 60, 0);
            if (aw) it->gad3 = ctl(g, "STATIC", it->after, SS_LEFT | SS_NOPREFIX, x + (int)(w - aw + 6), y + 3, (int)aw, hchar + 2, 0);
            break;
        }
        case T_BUTTON: case T_BIG:
            it->gad = ctl(g, "BUTTON", it->s, ((it->flags & OF_DEFAULT) ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON) | WS_TABSTOP,
                          x, y, w, hh, ID0 + i);
            break;
        }
        if (it->gad && it->disabled) EnableWindow(it->gad, FALSE);
    }
    return 1;
}

void dlg_free_gadgets(DLG *g)
{
    int i;
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        if (it->gad) DestroyWindow(it->gad);
        if (it->gad2) DestroyWindow(it->gad2);
        if (it->gad3) DestroyWindow(it->gad3);
        it->gad = it->gad2 = it->gad3 = NULL;
    }
}

/* copy the edit fields (to the ST character set) and check boxes back */
void dlg_fetch(DLG *g)
{
    int i;
    for (i = 0; i < g->nit; i++) {
        DITEM *it = &g->it[i];
        if (!it->gad) continue;
        if (it->type == T_EDIT) {
            char tmp[260];
            int max = it->valid ? (int)strlen(it->valid) + 1 : 61;
            GetWindowTextA(it->gad, tmp, sizeof tmp);
            stc(it->buf, tmp, max);
        } else if (it->type == T_CHECK) it->on = SendMessageA(it->gad, BM_GETCHECK, 0, 0) == BST_CHECKED;
        else if (it->type == T_RADIO) {
            LRESULT s = SendMessageA(it->gad, CB_GETCURSEL, 0, 0);
            if (s >= 0) it->on = (int)s;
        }
    }
}

void dlg_settext(DLG *g, short i, const char *s)
{
    if (i < 0 || i >= g->nit) return;
    g->it[i].s = s;
    if (g->it[i].gad) SetWindowTextA(g->it[i].gad, s);
}

/* a message for a dialog window; 1 when it chose an item (-1: cancel) */
int dlg_event(DLG *g, EVT *m, short *item, int *clicks)
{
    short i;
    switch (m->Class) {
    case IDCMP_GADGETUP:
        i = (short)(INT_PTR)m->IAddress;
        if (i < 0 || i >= g->nit) return 0;
        switch (g->it[i].type) {
        case T_BUTTON: case T_BIG:
            if (clicks) {
                *clicks = i == g->lastitem && m->Seconds - g->lsecs <= GetDoubleClickTime() ? 2 : 1;
                g->lastitem = *clicks == 2 ? -1 : i;
                g->lsecs = m->Seconds;
            }
            *item = i;
            return 1;
        case T_CHECK:
            g->it[i].on = m->Code;
            return 0;
        case T_RADIO:
            g->it[i].on = m->Code;
            return 0;
        case T_EDIT:
            if (m->Code == 27) { *item = -1; return 1; }
            if (m->Code != 13 || g->def < 0) return 0;
            *item = g->def;
            if (clicks) *clicks = 1;
            return 1;
        }
        return 0;
    case IDCMP_CLOSEWINDOW:
        *item = -1;
        return 1;
    }
    return 0;
}

static short item_of_id(DLG *g, int id)
{
    int i = id - ID0;
    return i >= 0 && i < g->nit ? (short)i : -1;
}

/* the window procedure of dialogs and tool windows */
static LRESULT CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    DLG *g = (DLG *)GetWindowLongPtrA(h, GWLP_USERDATA);
    EVT e;
    short item = -1;
    if (!g) return DefWindowProcA(h, msg, wp, lp);
    memset(&e, 0, sizeof e);
    e.IDCMPWindow = h;
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        short i;
        if (id == IDOK || id == IDCANCEL) {
            /* Return and Esc, through IsDialogMessage */
            if (id == IDOK) {
                HWND f = GetFocus();
                short fi = -1;
                int k;
                for (k = 0; k < g->nit; k++) if (g->it[k].gad == f) fi = (short)k;
                if (fi >= 0 && (g->it[fi].type == T_BUTTON || g->it[fi].type == T_BIG)) i = fi;
                else if (fi >= 0 && g->it[fi].type == T_EDIT) { i = fi; e.Code = 13; }
                else i = g->def;
                if (i < 0) return 0;
            } else {
                e.Class = IDCMP_CLOSEWINDOW;
                goto deliver;
            }
        } else {
            i = item_of_id(g, id);
            if (i < 0) break;
            switch (g->it[i].type) {
            case T_BUTTON: case T_BIG: if (code != BN_CLICKED) return 0; break;
            case T_CHECK:
                if (code != BN_CLICKED) return 0;
                e.Code = (UWORD)(SendMessageA(g->it[i].gad, BM_GETCHECK, 0, 0) == BST_CHECKED);
                break;
            case T_RADIO:
                if (code != CBN_SELCHANGE) return 0;
                e.Code = (UWORD)SendMessageA(g->it[i].gad, CB_GETCURSEL, 0, 0);
                break;
            case T_EDIT:
                if (code != EN_CHANGE) return 0;
                e.Code = 0;
                break;
            default: return 0;
            }
        }
        e.Class = IDCMP_GADGETUP;
        e.IAddress = (void *)(INT_PTR)i;
        e.Seconds = (ULONG)GetMessageTime();
        goto deliver;
    }
    case WM_CLOSE:
        e.Class = IDCMP_CLOSEWINDOW;
        goto deliver;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && g->tool) { e.Class = IDCMP_ACTIVEWINDOW; goto deliver; }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_PAINT:
        if (g->tool) { e.Class = IDCMP_REFRESHWINDOW; tool_route(&e); }
        break;
    }
    return DefWindowProcA(h, msg, wp, lp);
deliver:
    if (g->modal) {
        if (dlg_event(g, &e, &item, NULL)) { g->done = 1; g->result = item; }
        return 0;
    }
    if (g->tool) tool_route(&e);
    return 0;
}

void dlg_register(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = dlg_proc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconA(hinst, MAKEINTRESOURCEA(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "EdWordDlg";
    RegisterClassA(&wc);
}

/* a window for the dialog, with its controls; x < 0: centred */
static WIN open_dlg_window(DLG *g, const char *title, long x, long y, int tool)
{
    RECT r, fr;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    long ww, wh;
    r.left = 0; r.top = 0; r.right = g->w; r.bottom = g->h;
    AdjustWindowRectEx(&r, style, FALSE, tool ? WS_EX_TOOLWINDOW : WS_EX_DLGMODALFRAME);
    ww = r.right - r.left; wh = r.bottom - r.top;
    GetWindowRect(frame, &fr);
    if (x < 0) x = fr.left + (fr.right - fr.left - ww) / 2;
    if (y < 0) y = fr.top + (fr.bottom - fr.top - wh) / 2;
    if (x + ww > scr_w) x = scr_w - ww;
    if (y + wh > scr_h) y = scr_h - wh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    g->tool = tool;
    g->win = CreateWindowExA(tool ? WS_EX_TOOLWINDOW : WS_EX_DLGMODALFRAME, "EdWordDlg", title, style,
                             (int)x, (int)y, (int)ww, (int)wh, frame, NULL, hinst, NULL);
    if (!g->win) return NULL;
    SetWindowLongPtrA(g->win, GWLP_USERDATA, (LONG_PTR)g);
    dlg_gadgets(g);
    ShowWindow(g->win, SW_SHOW);
    return g->win;
}

/* a tool window: it stays open while the documents are worked on */
WIN dlg_window(DLG *g, const char *title, long x, long y)
{
    return open_dlg_window(g, title, x, y, 1);
}

void dlg_close(DLG *g)
{
    if (!g->win) return;
    dlg_fetch(g);
    SetWindowLongPtrA(g->win, GWLP_USERDATA, 0);
    DestroyWindow(g->win);
    g->win = NULL;
    SetActiveWindow(frame);
}

/* new controls after texts or visibility changed (tool windows) */
void dlg_rebuild(DLG *g)
{
    if (!g->win) return;
    dlg_fetch(g);
    dlg_free_gadgets(g);
    dlg_gadgets(g);
    InvalidateRect(g->win, NULL, TRUE);
}

/* run the dialog; returns the item that ended it, -1 for cancel */
short dlg_run(DLG *g)
{
    MSG msg;
    WIN w;
    HWND before = GetActiveWindow();
    block_input(1);
    g->modal = 1;
    g->done = 0;
    g->result = -1;
    w = open_dlg_window(g, g->title ? g->title : "EdWord", -1, -1, 0);
    if (!w) { block_input(0); return -1; }
    if (g->edit1 >= 0 && g->it[g->edit1].gad) {
        SetFocus(g->it[g->edit1].gad);
        SendMessageA(g->it[g->edit1].gad, EM_SETSEL, 0, -1);
    } else if (g->def >= 0 && g->it[g->def].gad) SetFocus(g->it[g->def].gad);
    while (!g->done && GetMessageA(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
        if (IsDialogMessageA(w, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    dlg_fetch(g);
    g->modal = 0;
    block_input(0);
    SetWindowLongPtrA(w, GWLP_USERDATA, 0);
    DestroyWindow(w);
    dlg_free_gadgets(g);
    g->win = NULL;
    if (before && IsWindow(before)) SetActiveWindow(before);
    return (short)g->result;
}

/* ================================================================= */
/*  Alerts                                                            */
/* ================================================================= */

int alert(int def, int icon, int text, int buttons, ...)
{
    static char t[400], bl[200];
    static DLG g;
    static const char *lines[12];
    char *p, *bs[6];
    va_list ap;
    int nb = 0, nl = 0, i, r;
    short ids[6];
    (void)def;
    va_start(ap, buttons);
    vsprintf(t, L(text), ap);
    va_end(ap);
    lines[nl++] = t;
    for (p = t; *p && nl < 12; p++) if (*p == '|') { *p = 0; lines[nl++] = p + 1; }
    strncpy(bl, L(buttons), sizeof bl - 1);
    bs[nb++] = bl;
    for (p = bl; *p && nb < 6; p++) if (*p == '|') { *p = 0; bs[nb++] = p + 1; }
    MessageBeep(icon == 3 ? MB_ICONHAND : icon == 1 ? MB_ICONEXCLAMATION : MB_ICONQUESTION);
    dlg_begin(&g, "EdWord");
    for (i = 0; i < nl; i++) { dlg_text(&g, lines[i]); dlg_row(&g); }
    for (i = 0; i < nb; i++) ids[i] = dlg_button(&g, bs[i], OF_EXIT | (i == (def > 0 ? def - 1 : 0) ? OF_DEFAULT : 0));
    dlg_end(&g);
    r = dlg_run(&g);
    for (i = 0; i < nb; i++) if (ids[i] == r) return i + 1;
    return nb;                       /* closed: the last button (Cancel) */
}

/* ================================================================= */
/*  Popup menu                                                        */
/* ================================================================= */

int popup(const char **items, int n, int cur, short x, short y)
{
    HMENU m = CreatePopupMenu();
    int i, r;
    if (!m) return -1;
    if (n > 60) n = 60;
    for (i = 0; i < n; i++) {
        if (items[i][0] == '-' && !items[i][1]) AppendMenuA(m, MF_SEPARATOR, 0, NULL);
        else AppendMenuA(m, MF_STRING | (i == cur ? MF_CHECKED : 0), (UINT_PTR)(i + 1), items[i]);
    }
    SetForegroundWindow(frame);
    r = (int)TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTBUTTON,
                            x, y, 0, frame, NULL);
    DestroyMenu(m);
    return r - 1;
}

/* ================================================================= */
/*  File requester                                                    */
/* ================================================================= */

static char seldir[MAX_PATH];

/* an AmigaDOS pattern as the code shares it with the Amiga version
 * ("#?.rtf", "#?.(iff|png)") to a Windows one ("*.rtf", "*.iff;*.png") */
static void win_pattern(const char *a, char *o)
{
    const char *p;
    if (!a || !strcmp(a, "#?") || !a[0]) { strcpy(o, "*.*"); return; }
    if (strncmp(a, "#?.", 3)) { strcpy(o, a); return; }
    p = a + 3;
    if (*p != '(') { sprintf(o, "*.%s", p); return; }
    p++;
    o[0] = 0;
    while (*p && *p != ')') {
        const char *e = p;
        while (*e && *e != '|' && *e != ')') e++;
        if (o[0]) strcat(o, ";");
        strcat(o, "*.");
        strncat(o, p, (size_t)(e - p));
        p = *e == '|' ? e + 1 : e;
    }
}

/* the dialog offers the pattern's files and all files */
int ask_file(const char *title, const char *apattern, const char *defname, char *out, int save)
{
    OPENFILENAMEA of;
    char filter[400], *p, file[MAX_PATH], pattern[120];
    int ok;
    const char *all = L(S_W_ALLFILES);
    win_pattern(apattern, pattern);
    p = filter;
    if (pattern[0] && strcmp(pattern, "*.*") && strcmp(pattern, "*")) {
        p += sprintf(p, "%s", pattern) + 1;
        p += sprintf(p, "%s", pattern) + 1;
    }
    p += sprintf(p, "%s (*.*)", all) + 1;
    p += sprintf(p, "*.*") + 1;
    *p = 0;
    lstrcpynA(file, defname ? defname : "", sizeof file);
    memset(&of, 0, sizeof of);
    of.lStructSize = OPENFILENAME_SIZE_VERSION_400A;          /* what Windows 95 knows */
    of.hwndOwner = GetActiveWindow() ? GetActiveWindow() : frame;
    of.lpstrFilter = filter;
    of.nFilterIndex = 1;
    of.lpstrFile = file;
    of.nMaxFile = sizeof file;
    of.lpstrInitialDir = seldir[0] ? seldir : NULL;
    of.lpstrTitle = title;
    of.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR |
               (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (pattern[0] == '*' && pattern[1] == '.' && !strchr(pattern, ';')) of.lpstrDefExt = pattern + 2;
    block_input(1);
    ok = save ? GetSaveFileNameA(&of) : GetOpenFileNameA(&of);
    block_input(0);
    if (!ok) return 0;
    strcpy(out, file);
    strcpy(seldir, file);
    p = strrchr(seldir, '\\');
    if (p) *p = 0;
    return 1;
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

static HWND gw;
static int g_last;
static long gbx, gby, gbw, gbh;

static LRESULT CALLBACK gauge_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        GRECT r;
        char s[8];
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        gfx_begin(h);
        r.g_x = (short)(gbx - 2); r.g_y = (short)(gby - 2); r.g_w = (short)(gbw + 4); r.g_h = (short)(gbh + 4);
        bevel(&r, 1);
        fill_rect(gbx, gby, gbx + gbw - 1, gby + gbh - 1, P_WHITE);
        if (g_last > 0) fill_rect(gbx, gby, gbx + gbw * g_last / 100 - 1, gby + gbh - 1, P_BLUE);
        sprintf(s, "%d%%", g_last < 0 ? 0 : g_last);
        sys_text(gbx + (gbw - sys_width(s)) / 2, gby + (gbh - hchar) / 2, s, P_BLACK);
        gfx_end();
        return 0;
    }
    if (msg == WM_CLOSE) return 0;
    return DefWindowProcA(h, msg, wp, lp);
}

void gauge_open(const char *title)
{
    static int reg;
    RECT r, fr;
    long w = 300, h = 40;
    if (!reg) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = gauge_proc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(NULL, IDC_WAIT);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "EdWordGauge";
        RegisterClassA(&wc);
        reg = 1;
    }
    g_last = -1;
    block_input(1);
    r.left = 0; r.top = 0; r.right = w; r.bottom = h;
    AdjustWindowRect(&r, WS_POPUP | WS_CAPTION, FALSE);
    GetWindowRect(frame, &fr);
    gw = CreateWindowA("EdWordGauge", title, WS_POPUP | WS_CAPTION, fr.left + (fr.right - fr.left - (r.right - r.left)) / 2,
                       fr.top + (fr.bottom - fr.top - (r.bottom - r.top)) / 2, r.right - r.left, r.bottom - r.top,
                       frame, NULL, hinst, NULL);
    if (!gw) return;
    gbx = 10; gby = 13; gbw = w - 20; gbh = 14;
    ShowWindow(gw, SW_SHOWNOACTIVATE);
    UpdateWindow(gw);
}

void gauge_set(long done, long total)
{
    int pct;
    if (!gw) return;
    pct = total > 0 ? (int)(done * 100L / total) : 0;
    if (pct > 100) pct = 100;
    if (pct == g_last) return;
    g_last = pct;
    InvalidateRect(gw, NULL, FALSE);
    UpdateWindow(gw);
    pump_messages();
}

void gauge_close(void)
{
    if (gw) DestroyWindow(gw);
    gw = NULL;
    block_input(0);
}

/* ---- a long job: the gauge comes up only when it takes a while ---- */

static const char *b_title;
static DWORD b_t0;
static int b_shown, b_depth;

static void busy_hook(long v, long total)
{
    if (!b_shown) {
        long ms = (long)(GetTickCount() - b_t0);
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
    b_t0 = GetTickCount();
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

int dlg_prefs(void)
{
    static const int order[NDATEFMT] = { DT_SHORT, DT_US, DT_ISO, DT_LONG, DT_DAYLONG };
    static char dl[NDATEFMT][40], mins[3];
    char st[40];
    static DLG g;           /* static: GCC 15 for the 68000 mis-addresses very large stack frames */
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
    dlg_text(&g, ED_ABOUT[0] ? ED_ABOUT : L(S_W_ABOUT));
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
    static const char *css[] = { "", "Atari ST", "Windows", "UTF-8", "UTF-16" };
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
