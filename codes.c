/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * codes.c - the "formatting codes" view used by split view and quick edit
 *
 * The document is shown as plain text with codes. Character codes:
 *   [Font Swiss] [Size 12] [B] [/B] [I] [/I] [U] [/U] [Colour Red]
 * paragraph codes, written at the start of a paragraph when they change:
 *   [Left] [Centre] [Right] [Justify] [Spacing 1|1.5|2] [Indent 36 -18]
 *   [Bullet] [Number] [/List] [Heading 1-3] [/Heading]
 *   [Contents] [Contents Title] [/Contents]
 * tables:
 *   [Table 120 120 120 Border] ... [Row] text [Cell] text ... [/Table]
 *   with a chart of its numbers: [Table 120 120 Pie|Bar|Pie3D|Bar3D
 *   Above|Below|Left|Right]
 * and [Tab] [Page] [Column], with "[[" for a literal "[". Codes are
 * switches: each lasts until the next code of its kind. A bracket that
 * isn't a known code is kept as ordinary text.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wp.h"

static const char *jname[4] = { "Left", "Centre", "Right", "Justify" };

void codes_free(CODES *c)
{
    free(c->t);
    free(c->ls);
    c->t = NULL; c->ls = NULL;
    c->len = c->cap = c->nl = c->lcap = 0;
}

static int grow(CODES *c, long need)
{
    char *n; long cap;
    if (need + 1 <= c->cap) return 1;
    cap = c->cap ? c->cap : 1024;
    while (cap < need + 1) cap *= 2;
    n = (char *)realloc(c->t, cap);
    if (!n) return 0;
    c->t = n; c->cap = cap;
    return 1;
}

static int put(CODES *c, const char *s)
{
    long n = (long)strlen(s);
    if (!grow(c, c->len + n)) return 0;
    memcpy(c->t + c->len, s, n);
    c->len += n;
    c->t[c->len] = 0;
    return 1;
}

int codes_insert(CODES *c, long pos, const char *s, long n)
{
    if (pos < 0) pos = 0;
    if (pos > c->len) pos = c->len;
    if (!grow(c, c->len + n)) return 0;
    memmove(c->t + pos + n, c->t + pos, c->len - pos + 1);
    memcpy(c->t + pos, s, n);
    c->len += n;
    c->dirty = 1;
    return 1;
}

void codes_delete(CODES *c, long pos, long n)
{
    if (pos < 0) { n += pos; pos = 0; }
    if (pos + n > c->len) n = c->len - pos;
    if (n <= 0) return;
    memmove(c->t + pos, c->t + pos + n, c->len - pos - n + 1);
    c->len -= n;
    c->dirty = 1;
}

int codes_sel(CODES *c, long *a, long *b)
{
    if (c->anchor < 0 || c->anchor == c->cursor) return 0;
    if (c->anchor < c->cursor) { *a = c->anchor; *b = c->cursor; }
    else { *a = c->cursor; *b = c->anchor; }
    return 1;
}

/* ---- document -> codes --------------------------------------------- */

static void para_codes(CODES *cv, DOC *d, const PARA *p, const PARA *q, int first)
{
    char tmp[120];
    int i;
    if (p->tbl != q->tbl || first) {
        if (q->tbl && !first) put(cv, "[/Table]");
        if (p->tbl && p->tbl <= d->a.ntbl) {
            const TBL *t = &d->a.tbls[p->tbl - 1];
            strcpy(tmp, "[Table");
            for (i = 0; i < t->ncols; i++) sprintf(tmp + strlen(tmp), " %d", t->w[i]);
            if (t->border) strcat(tmp, " Border");
            if (t->chart) {
                static const char *cn[NCHARTS] = { "", "Pie", "Bar", "Pie3D", "Bar3D" };
                static const char *cp[4] = { "Above", "Below", "Left", "Right" };
                sprintf(tmp + strlen(tmp), " %s %s", cn[t->chart % NCHARTS], cp[t->cpos & 3]);
            }
            strcat(tmp, "]");
            put(cv, tmp);
        }
    }
    if (first || p->just != q->just) { sprintf(tmp, "[%s]", jname[p->just & 3]); put(cv, tmp); }
    if ((first && p->spacing) || (!first && p->spacing != q->spacing))
        put(cv, p->spacing == LS_DOUBLE ? "[Spacing 2]" : p->spacing == LS_ONEHALF ? "[Spacing 1.5]" : "[Spacing 1]");
    if ((first && (p->left || p->first)) || (!first && (p->left != q->left || p->first != q->first))) {
        sprintf(tmp, "[Indent %d %d]", p->left, p->first);
        put(cv, tmp);
    }
    if ((first && p->list) || (!first && p->list != q->list))
        put(cv, p->list == LIST_BULLET ? "[Bullet]" : p->list == LIST_NUMBER ? "[Number]" : "[/List]");
    if ((first && p->toc) || (!first && p->toc != q->toc)) {
        if (p->toc) { sprintf(tmp, "[Heading %d]", p->toc); put(cv, tmp); }
        else put(cv, "[/Heading]");
    }
    if ((first && p->tocent) || (!first && p->tocent != q->tocent))
        put(cv, p->tocent == TOCE_TITLE ? "[Contents Title]" : p->tocent == TOCE_ENTRY ? "[Contents]" : "[/Contents]");
    if (p->tbl) put(cv, "[Row]");
}

int codes_build(DOC *d)
{
    CODES *cv = &d->cv;
    CELL cur;
    PARA prev;
    long k, cpos = 0, apos = -1;
    int have = 0, firstpara = 1;
    char tmp[64];

    cv->len = 0;
    if (!grow(cv, 256)) return 0;
    cv->t[0] = 0;
    memset(&cur, 0, sizeof cur);
    para_default(&prev);
    for (k = 0; k <= d->len; k++) {
        CELL *c;
        if (k == d->cursor) cpos = cv->len;
        if (k == d->anchor) apos = cv->len;
        if (k == d->len) break;
        c = &d->text[k];
        if (k == 0 || ISBRK(d->text[k - 1].ch)) {
            const PARA *p = PARA_OF(d, c);
            para_codes(cv, d, p, &prev, firstpara);
            prev = *p;
            firstpara = 0;
        }
        if (!ISEND(c->ch)) {
            if (!have || c->font != cur.font) {
                sprintf(tmp, "[Font %.32s]", c->font < nfonts ? fonts[c->font].name : "?");
                put(cv, tmp);
            }
            if (!have || c->size != cur.size) { sprintf(tmp, "[Size %d]", c->size); put(cv, tmp); }
            if ((c->attr ^ cur.attr) & A_BOLD)   put(cv, c->attr & A_BOLD ? "[B]" : "[/B]");
            if ((c->attr ^ cur.attr) & A_ITALIC) put(cv, c->attr & A_ITALIC ? "[I]" : "[/I]");
            if ((c->attr ^ cur.attr) & A_UNDER)  put(cv, c->attr & A_UNDER ? "[U]" : "[/U]");
            if ((!have || c->color != cur.color) && c->color < NCOLORS) {
                sprintf(tmp, "[Colour %s]", color_name[c->color]);
                put(cv, tmp);
            }
            cur = *c;
            have = 1;
        }
        switch (c->ch) {
        case '[':       put(cv, "[["); break;
        case '\t':      put(cv, "[Tab]"); break;
        case CH_CELL:   put(cv, "[Cell]"); break;
        case CH_PAGE:   put(cv, "[Page]\n"); break;
        case CH_COLBRK: put(cv, "[Column]\n"); break;
        default:
            tmp[0] = (char)c->ch; tmp[1] = 0;
            put(cv, tmp);
        }
    }
    if (prev.tbl) put(cv, "[/Table]");
    cv->cursor = cpos;
    cv->anchor = apos;
    cv->want_col = -1;
    cv->dirty = 0;
    codes_wrap(cv, cv->cols);
    return 1;
}

/* ---- codes -> document --------------------------------------------- */

static int ieq(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return b[n] == 0;
}

static int find_font(const char *s, int n)
{
    int i;
    if (n > 1 && s[0] == '#') return atoi(s + 1) < nfonts ? atoi(s + 1) : -1;
    for (i = 0; i < nfonts; i++)
        if (ieq(s, fonts[i].name, n)) return i;
    for (i = 0; i < nfonts; i++)        /* unique prefix */
        if ((int)strlen(fonts[i].name) > n) {
            char tmp[40];
            memcpy(tmp, fonts[i].name, n);
            tmp[n] = 0;
            if (ieq(s, tmp, n)) return i;
        }
    return -1;
}

enum { R_NONE, R_OK, R_TAB, R_PAGE, R_CELL, R_COLUMN };

/* a [Table ...] code: column widths and "Border" */
static int table_code(DOC *d, PARA *pst, const char *arg, int an)
{
    static const char *cn[NCHARTS] = { "", "Pie", "Bar", "Pie3D", "Bar3D" };
    static const char *cp[4] = { "Above", "Below", "Left", "Right" };
    short w[MAX_COLS];
    int n = 0, border = 0, i = 0, t, k, chart = 0, cpos = CP_BOTTOM;
    char buf[200];
    if (an > 199) an = 199;
    memcpy(buf, arg, an);
    buf[an] = 0;
    while (buf[i]) {
        int wl = 0;
        while (buf[i] == ' ' || buf[i] == ',') i++;
        if (!buf[i]) break;
        while (buf[i + wl] && buf[i + wl] != ' ' && buf[i + wl] != ',') wl++;
        if (buf[i] >= '0' && buf[i] <= '9') {
            int v = atoi(buf + i);
            if (n < MAX_COLS) w[n++] = (short)(v < 12 ? 12 : v > 2000 ? 2000 : v);
        } else if (ieq(buf + i, "Border", wl)) border = 1;
        else {
            for (k = 1; k < NCHARTS; k++) if (ieq(buf + i, cn[k], wl)) chart = k;
            for (k = 0; k < 4; k++) if (ieq(buf + i, cp[k], wl)) cpos = k;
        }
        i += wl;
    }
    if (n == 0) return R_NONE;
    t = doc_tbl_new(d, n, w, border);
    if (!t) return R_NONE;
    d->a.tbls[t - 1].chart = (unsigned char)chart;
    d->a.tbls[t - 1].cpos = (unsigned char)cpos;
    pst->tbl = (unsigned char)t;
    return R_OK;
}

/* Interpret the code s[0..n) ("B", "Size 12"...). */
static int do_code(DOC *d, CELL *st, PARA *pst, const char *s, int n)
{
    int i;
    const char *arg = NULL;
    int an = 0, wn = n;
    for (i = 0; i < n; i++) if (s[i] == ' ') { wn = i; arg = s + i + 1; an = n - i - 1; break; }
    if (!arg) {
        if (ieq(s, "B", n))  { st->attr |= A_BOLD; return R_OK; }
        if (ieq(s, "/B", n)) { st->attr &= ~A_BOLD; return R_OK; }
        if (ieq(s, "I", n))  { st->attr |= A_ITALIC; return R_OK; }
        if (ieq(s, "/I", n)) { st->attr &= ~A_ITALIC; return R_OK; }
        if (ieq(s, "U", n))  { st->attr |= A_UNDER; return R_OK; }
        if (ieq(s, "/U", n)) { st->attr &= ~A_UNDER; return R_OK; }
        if (ieq(s, "Tab", n)) return R_TAB;
        if (ieq(s, "Page", n)) return R_PAGE;
        if (ieq(s, "Column", n)) return R_COLUMN;
        if (ieq(s, "Cell", n)) return R_CELL;
        if (ieq(s, "Row", n)) return R_OK;
        if (ieq(s, "/Table", n)) { pst->tbl = 0; return R_OK; }
        if (ieq(s, "Left", n)) { pst->just = J_LEFT; return R_OK; }
        if (ieq(s, "Centre", n) || ieq(s, "Center", n)) { pst->just = J_CENTRE; return R_OK; }
        if (ieq(s, "Right", n)) { pst->just = J_RIGHT; return R_OK; }
        if (ieq(s, "Justify", n) || ieq(s, "Full", n)) { pst->just = J_FULL; return R_OK; }
        if (ieq(s, "Bullet", n)) { pst->list = LIST_BULLET; return R_OK; }
        if (ieq(s, "Number", n)) { pst->list = LIST_NUMBER; return R_OK; }
        if (ieq(s, "/List", n)) { pst->list = LIST_NONE; return R_OK; }
        if (ieq(s, "/Heading", n)) { pst->toc = 0; return R_OK; }
        if (ieq(s, "Contents", n)) { pst->tocent = TOCE_ENTRY; return R_OK; }
        if (ieq(s, "/Contents", n)) { pst->tocent = TOCE_NONE; return R_OK; }
        return R_NONE;
    }
    while (an > 0 && *arg == ' ') { arg++; an--; }
    if (an <= 0) return R_NONE;
    if (ieq(s, "Font", wn)) {
        int f = find_font(arg, an);
        if (f < 0) return R_NONE;
        st->font = (unsigned char)f;
        return R_OK;
    }
    if (ieq(s, "Size", wn)) {
        int v = atoi(arg);
        if (v < 4 || v > 120) return R_NONE;
        st->size = (unsigned char)v;
        return R_OK;
    }
    if (ieq(s, "Colour", wn) || ieq(s, "Color", wn)) {
        for (i = 0; i < NCOLORS; i++)
            if (ieq(arg, color_name[i], an)) { st->color = (unsigned char)i; return R_OK; }
        if (ieq(arg, "Gray", an)) { st->color = 7; return R_OK; }
        return R_NONE;
    }
    if (ieq(s, "Spacing", wn)) {
        if (ieq(arg, "1", an)) pst->spacing = LS_SINGLE;
        else if (ieq(arg, "1.5", an) || ieq(arg, "1,5", an)) pst->spacing = LS_ONEHALF;
        else if (ieq(arg, "2", an)) pst->spacing = LS_DOUBLE;
        else return R_NONE;
        return R_OK;
    }
    if (ieq(s, "Indent", wn)) {
        int l = atoi(arg), f = 0;
        for (i = 0; i < an && arg[i] != ' '; i++) ;
        if (i < an) f = atoi(arg + i + 1);
        if (l < 0) l = 0;
        if (l > 400) l = 400;
        if (f < -l) f = -l;
        if (f > 400) f = 400;
        pst->left = (short)l; pst->first = (short)f;
        return R_OK;
    }
    if (ieq(s, "Heading", wn)) {
        int v = atoi(arg);
        if (v < 1 || v > 3) return R_NONE;
        pst->toc = (unsigned char)v;
        return R_OK;
    }
    if (ieq(s, "Contents", wn) && ieq(arg, "Title", an)) { pst->tocent = TOCE_TITLE; return R_OK; }
    if (ieq(s, "Table", wn)) return table_code(d, pst, arg, an);
    return R_NONE;
}

int codes_apply(DOC *d)
{
    CODES *cv = &d->cv;
    CELL st, *out = NULL;
    PARA pst, lastp;
    long p = 0, n = 0, cap = 0, newcur = -1;
    int pidx = 0;

    /* the text is rebuilt, and with it the paragraph formats and tables */
    d->a.npara = 1;
    para_default(&d->a.paras[0]);
    d->a.ntbl = 0;
    memset(&st, 0, sizeof st);
    st.font = (unsigned char)default_font();
    st.size = (unsigned char)default_size();
    para_default(&pst);
    para_default(&lastp);

#define EMIT(c_) do { \
        if (n >= cap) { CELL *nn; cap = cap ? cap * 2 : 1024; \
            nn = (CELL *)realloc(out, cap * sizeof(CELL)); \
            if (!nn) { free(out); return 0; } out = nn; } \
        if (memcmp(&pst, &lastp, sizeof(PARA))) { pidx = para_find(d, &pst); lastp = pst; } \
        out[n] = st; out[n].ch = (unsigned char)(c_); out[n].para = (unsigned char)pidx; n++; } while (0)

    while (p <= cv->len) {
        unsigned char ch;
        if (p == cv->cursor && newcur < 0) newcur = n;
        if (p >= cv->len) break;
        ch = (unsigned char)cv->t[p];
        if (ch == '[') {
            long e = p + 1;
            int r = R_NONE;
            if (p + 1 < cv->len && cv->t[p + 1] == '[') {
                if (cv->cursor == p + 1 && newcur < 0) newcur = n;
                EMIT('['); p += 2; continue;
            }
            while (e < cv->len && e - p < 120 && cv->t[e] != ']' && cv->t[e] != '\n' && cv->t[e] != '[') e++;
            if (e < cv->len && cv->t[e] == ']')
                r = do_code(d, &st, &pst, cv->t + p + 1, (int)(e - p - 1));
            if (r != R_NONE) {
                if (cv->cursor > p && cv->cursor <= e && newcur < 0) newcur = n;
                if (r == R_TAB) EMIT('\t');
                if (r == R_CELL) EMIT(CH_CELL);
                if (r == R_PAGE || r == R_COLUMN) {
                    EMIT(r == R_PAGE ? CH_PAGE : CH_COLBRK);
                    if (e + 1 < cv->len && cv->t[e + 1] == '\n') e++;
                }
                p = e + 1;
                continue;
            }
        }
        EMIT(ch);
        p++;
    }
#undef EMIT
    if (newcur < 0) newcur = n;
    free(d->text);
    d->text = out;
    d->len = n;
    d->cap = cap;
    d->cursor = newcur > n ? n : newcur;
    d->anchor = -1;
    d->dirty = 1;
    pidx = para_find(d, &pst);
    d->insp = pst;
    doc_norm_para(d);
    doc_touch_all(d);
    doc_sync_ins(d);
    cv->dirty = 0;
    return 1;
}

/* ---- wrapped lines of the codes text ------------------------------- */

void codes_wrap(CODES *c, long cols)
{
    long s = 0;
    if (cols < 10) cols = 10;
    c->cols = cols;
    c->nl = 0;
    for (;;) {
        long e = s, brk = -1, next;
        if (c->nl >= c->lcap) {
            long nc = c->lcap ? c->lcap * 2 : 256;
            long *nn = (long *)realloc(c->ls, nc * sizeof(long));
            if (!nn) return;
            c->ls = nn; c->lcap = nc;
        }
        c->ls[c->nl++] = s;
        while (e < c->len && c->t[e] != '\n' && e - s < cols) {
            if (c->t[e] == ' ') brk = e + 1;
            e++;
        }
        if (e >= c->len) break;
        if (c->t[e] == '\n') next = e + 1;
        else next = (brk > s) ? brk : e;
        s = next;
        if (s >= c->len) {
            if (c->len > 0 && c->t[c->len - 1] == '\n') {
                if (c->nl < c->lcap) c->ls[c->nl++] = s;
            }
            break;
        }
    }
}

long codes_line_of(CODES *c, long pos)
{
    long lo = 0, hi = c->nl - 1;
    while (lo < hi) {
        long mid = (lo + hi + 1) / 2;
        if (c->ls[mid] <= pos) lo = mid; else hi = mid - 1;
    }
    return lo < 0 ? 0 : lo;
}

/* visible length of a wrapped line (without its newline) */
long codes_line_len(CODES *c, long li)
{
    long s = c->ls[li], e = li + 1 < c->nl ? c->ls[li + 1] : c->len;
    if (e > s && c->t[e - 1] == '\n') e--;
    return e - s;
}

long codes_pos(CODES *c, long li, long col)
{
    long n;
    if (li < 0) return 0;
    if (li >= c->nl) return c->len;
    n = codes_line_len(c, li);
    if (col > n) col = n;
    if (col < 0) col = 0;
    /* on a soft-wrapped line, the end belongs to the next line */
    if (col == n && li + 1 < c->nl && n > 0 && c->t[c->ls[li] + n] != '\n') col = n - 1;
    return c->ls[li] + col;
}
