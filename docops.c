/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * docops.c - table of contents, find and replace, document statistics
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wp.h"

static const PARA *pf(DOC *d, long k)
{
    return &d->a.paras[d->text[k].para < d->a.npara ? d->text[k].para : 0];
}

static int wordch(unsigned char c)
{
    return st_isalpha(c) || (c >= '0' && c <= '9');
}

/* ---- table of contents ------------------------------------------------------ */

/* The table of contents is a run of paragraphs marked TOCE_TITLE and
 * TOCE_ENTRY. Each entry is "heading text<Tab>page"; layout stretches
 * that tab into a dot leader so the page numbers line up on the right. */

int toc_find(DOC *d, long *a, long *b)
{
    long k = 0, s, e;
    while (k < d->len) {
        e = doc_para_end(d, k);
        if (pf(d, e < d->len ? e : d->len - 1)->tocent) {
            s = k;
            while (k < d->len) {
                e = doc_para_end(d, k);
                if (!pf(d, e < d->len ? e : d->len - 1)->tocent) break;
                k = e + 1;
            }
            *a = s;
            *b = k < d->len ? k : d->len;
            return 1;
        }
        k = e + 1;
    }
    return 0;
}

typedef struct { int level; long pos; char text[72]; } HEAD;

static int collect(DOC *d, HEAD *h, int max, long skip_a, long skip_b)
{
    long k = 0, e, i;
    int n = 0;
    while (k < d->len && n < max) {
        const PARA *p;
        e = doc_para_end(d, k);
        p = pf(d, e < d->len ? e : d->len - 1);
        if (p->toc >= 1 && p->toc <= 3 && !p->tocent && !p->tbl && !(k >= skip_a && k < skip_b)) {
            int m = 0;
            for (i = k; i < e && i < d->len && m < 70; i++) {
                unsigned char c = d->text[i].ch;
                if (c == '\t' || c == CH_CELL) c = ' ';
                if (c == ' ' && (m == 0 || h[n].text[m - 1] == ' ')) continue;
                h[n].text[m++] = (char)c;
            }
            while (m > 0 && h[n].text[m - 1] == ' ') m--;
            h[n].text[m] = 0;
            if (m > 0) { h[n].level = p->toc; h[n].pos = k; n++; }
        }
        k = e + 1;
    }
    return n;
}

static long put_text(CELL *out, long n, const CELL *st, const char *s)
{
    while (*s) { out[n] = *st; out[n].ch = (unsigned char)*s++; n++; }
    return n;
}

/* the cells of a table of contents for headings h, with their pages */
static CELL *build(DOC *d, const char *title, HEAD *h, int nh, const int *pages, long *len)
{
    CELL *out, st, br;
    PARA p;
    long n = 0;
    int i, pt, pe[3];
    char num[12];

    out = (CELL *)malloc((strlen(title) + 2 + (long)nh * 84) * sizeof(CELL));
    if (!out) return NULL;
    memset(&st, 0, sizeof st);
    st.font = (unsigned char)default_font();
    st.size = (unsigned char)(default_size() + 4);
    st.attr = A_BOLD;
    para_default(&p);
    p.tocent = TOCE_TITLE;
    pt = para_find(d, &p);
    st.para = (unsigned char)pt;
    n = put_text(out, n, &st, title);
    br = st; br.ch = '\n';
    out[n++] = br;
    for (i = 0; i < 3; i++) {
        para_default(&p);
        p.tocent = TOCE_ENTRY;
        p.left = (short)(i * 18);
        pe[i] = para_find(d, &p);
    }
    for (i = 0; i < nh; i++) {
        st.size = (unsigned char)default_size();
        st.attr = h[i].level == 1 ? A_BOLD : 0;
        st.para = (unsigned char)pe[h[i].level - 1];
        n = put_text(out, n, &st, h[i].text);
        st.attr = 0;
        out[n] = st; out[n].ch = '\t'; n++;
        sprintf(num, "%d", pages ? pages[i] : 0);
        n = put_text(out, n, &st, num);
        out[n] = st; out[n].ch = '\n'; n++;
    }
    *len = n;
    return out;
}

/* put a table of contents at [a, b) (an empty range to insert one) */
static int toc_put(DOC *d, long a, long b, const char *title)
{
    HEAD *h;
    int *pages, nh, i, pass;
    long len, k;
    CELL *cells;

    h = (HEAD *)malloc(200 * sizeof(HEAD));
    pages = (int *)malloc(200 * sizeof(int));
    if (!h || !pages) { free(h); free(pages); return 0; }
    nh = collect(d, h, 200, a, b);
    for (i = 0; i < nh; i++) pages[i] = 0;
    /* page numbers depend on the table's own length: settle in passes */
    for (pass = 0; pass < 4; pass++) {
        cells = build(d, title, h, nh, pass ? pages : NULL, &len);
        if (!cells) { free(h); free(pages); return 0; }
        if (b > a) doc_delete(d, a, b - a);
        doc_insert(d, a, cells, len);
        free(cells);
        for (i = 0; i < nh; i++) if (h[i].pos >= b) h[i].pos += len - (b - a);
        b = a + len;
        doc_layout(d);
        {
            int changed = 0;
            for (i = 0; i < nh; i++) {
                long li = doc_find_line(d, h[i].pos);
                int pg = d->lines[li].page + 1;
                if (pg != pages[i]) { pages[i] = pg; changed = 1; }
            }
            if (!changed && pass) break;
        }
    }
    /* keep the caret out of the table */
    k = b;
    d->cursor = k <= d->len ? k : d->len;
    d->anchor = -1;
    free(h);
    free(pages);
    return 1;
}

int toc_insert(DOC *d, long pos, const char *title)
{
    long a, b;
    CELL br;
    if (toc_find(d, &a, &b)) return toc_put(d, a, b, title);
    if (doc_in_table(d, pos, NULL, NULL)) return 0;
    /* on a paragraph of its own */
    pos = doc_para_start(d, pos);
    if (pos == d->len && d->len > 0 && !ISBRK(d->text[d->len - 1].ch)) {
        br = d->ins; br.ch = '\n';
        doc_insert(d, d->len, &br, 1);
        pos = d->len;
    }
    return toc_put(d, pos, pos, title);
}

int toc_update(DOC *d)
{
    long a, b, k;
    char title[80];
    int n = 0;
    if (!toc_find(d, &a, &b)) return 0;
    /* keep the title the user may have changed */
    for (k = a; k < b && !ISBRK(d->text[k].ch) && n < 79; k++) title[n++] = (char)d->text[k].ch;
    title[n] = 0;
    return toc_put(d, a, b, title);
}

/* ---- repaginate ----------------------------------------------------------------- */

/* Lay the whole document out again, page by page, and bring the table of
 * contents' page numbers up to date. Returns 1 if the contents changed. */
int doc_repaginate(DOC *d)
{
    long a, b, k, n;
    unsigned char *old;
    int changed = 0, dirty = d->dirty;
    d->lay_full = 1;
    doc_layout(d);
    if (!toc_find(d, &a, &b)) return 0;
    n = b - a;
    old = (unsigned char *)malloc((size_t)n + 1);
    if (old) for (k = 0; k < n; k++) old[k] = d->text[a + k].ch;
    toc_update(d);
    d->lay_full = 1;
    doc_layout(d);
    if (toc_find(d, &a, &b) && old && b - a == n) {
        for (k = 0; k < n && old[k] == d->text[a + k].ch; k++) ;
        changed = k < n;
    } else changed = 1;
    free(old);
    if (!changed) d->dirty = dirty;
    return changed;
}

/* ---- page numbers ----------------------------------------------------------------- */

void hf_pagenum_set(DOC *d, int where, int just, int style, const char *page, const char *of)
{
    char *t;
    where = where ? 1 : 0;
    hf_pagenum_clear(d);
    t = HF(d).text[where];
    switch (style) {
    case 1:  sprintf(t, "%.30s {page}", page); break;
    case 2:  sprintf(t, "%.30s {page} %.20s {pages}", page, of); break;
    case 3:  strcpy(t, "- {page} -"); break;
    default: strcpy(t, "{page}"); break;
    }
    HF(d).just[where] = (unsigned char)(just < 0 || just > 2 ? 1 : just);
    d->dirty = 1;
}

/* take the page number out of the header and footer */
void hf_pagenum_clear(DOC *d)
{
    int i;
    for (i = 0; i < 2; i++)
        if (strstr(HF(d).text[i], "{page}")) { HF(d).text[i][0] = 0; d->dirty = 1; }
}

int hf_pagenum_find(DOC *d, int *where, int *style)
{
    int i;
    for (i = 1; i >= 0; i--) {
        const char *t = HF(d).text[i];
        if (!strstr(t, "{page}")) continue;
        *where = i;
        if (strstr(t, "{pages}")) *style = 2;
        else if (t[0] == '-') *style = 3;
        else if (t[0] == '{') *style = 0;
        else *style = 1;
        return 1;
    }
    return 0;
}

/* ---- page lists --------------------------------------------------------------------- */

int page_list(const char *s, int np, unsigned char *sel)
{
    int n = 0, a, b, k, any = 0;
    memset(sel, 0, (size_t)np);
    while (*s) {
        while (*s == ' ' || *s == ',' || *s == ';') s++;
        if (!*s) break;
        a = 1;
        if (*s >= '0' && *s <= '9') { a = 0; while (*s >= '0' && *s <= '9') a = a * 10 + (*s++ - '0'); }
        else if (*s != '-') { s++; continue; }
        b = a;
        while (*s == ' ') s++;
        if (*s == '-') {
            s++;
            while (*s == ' ') s++;
            if (*s >= '0' && *s <= '9') { b = 0; while (*s >= '0' && *s <= '9') b = b * 10 + (*s++ - '0'); }
            else b = np;
        }
        if (a > b) { k = a; a = b; b = k; }
        if (a < 1) a = 1;
        if (b > np) b = np;
        for (k = a; k <= b; k++) sel[k - 1] = 1;
        any = 1;
    }
    for (k = 0; k < np; k++) n += sel[k];
    (void)any;
    return n;
}

/* ---- find and replace ----------------------------------------------------------- */

static int match_at(DOC *d, long k, const char *w, long n, int flags)
{
    long i;
    if (k < 0 || k + n > d->len) return 0;
    for (i = 0; i < n; i++) {
        unsigned char a = d->text[k + i].ch, b = (unsigned char)w[i];
        if (!(flags & FIND_CASE)) { a = st_lower(a); b = st_lower(b); }
        if (a != b) return 0;
    }
    if (flags & FIND_WORD) {
        if (k > 0 && wordch(d->text[k - 1].ch)) return 0;
        if (k + n < d->len && wordch(d->text[k + n].ch)) return 0;
    }
    return 1;
}

/* the next match at or after from (or before it, with FIND_BACK) */
long find_next(DOC *d, long from, const char *what, int flags, long *end)
{
    long n = (long)strlen(what), k;
    if (n == 0) return -1;
    if (flags & FIND_BACK) {
        for (k = from - 1; k >= 0; k--)
            if (match_at(d, k, what, n, flags)) { *end = k + n; return k; }
    } else {
        for (k = from < 0 ? 0 : from; k + n <= d->len; k++)
            if (match_at(d, k, what, n, flags)) { *end = k + n; return k; }
    }
    return -1;
}

/* replace [a,b) by text in the style of its first character */
void doc_replace(DOC *d, long a, long b, const char *with)
{
    CELL st, *buf;
    long n = (long)strlen(with), i;
    st = a < d->len ? d->text[a] : d->ins;
    if (a < d->len && ISEND(st.ch) && a > 0) st = d->text[a - 1];
    buf = (CELL *)malloc((n + 1) * sizeof(CELL));
    if (!buf) return;
    for (i = 0; i < n; i++) {
        buf[i] = st;
        buf[i].ch = (unsigned char)with[i];
        if (ISEND(buf[i].ch)) buf[i].ch = ' ';
    }
    if (b > a) doc_delete(d, a, b - a);
    if (n) doc_insert(d, a, buf, n);
    free(buf);
}

int find_replace_all(DOC *d, const char *what, const char *with, int flags, int in_sel)
{
    long a = 0, b = d->len, k, e, wl = (long)strlen(with);
    int count = 0;
    if (in_sel && !doc_sel(d, &a, &b)) { a = 0; b = d->len; }
    flags &= ~FIND_BACK;
    k = a;
    while ((k = find_next(d, k, what, flags, &e)) >= 0 && e <= b) {
        doc_replace(d, k, e, with);
        b += wl - (e - k);
        k += wl;
        count++;
    }
    d->anchor = -1;
    if (d->cursor > d->len) d->cursor = d->len;
    return count;
}

/* ---- statistics --------------------------------------------------------------- */

void doc_stats(DOC *d, DSTATS *s)
{
    long k;
    int inword = 0, hastext = 0, prevtbl = 0;
    memset(s, 0, sizeof *s);
    s->pages = doc_npages(d);
    s->lines = d->nlines;
    s->objects = d->a.nobj;
    for (k = 0; k < d->len; k++) {
        unsigned char c = d->text[k].ch;
        if (ISBRK(c)) {
            int t = pf(d, k)->tbl;
            if (hastext) s->paras++;
            if (t && t != prevtbl) s->tables++;
            prevtbl = t;
            hastext = 0;
            inword = 0;
            continue;
        }
        if (c == CH_CELL) { inword = 0; continue; }
        s->chars++;
        if (c != ' ' && c != '\t') { s->chars_ns++; hastext = 1; }
        if (wordch(c) || (c == '\'' && inword)) {
            if (!inword) s->words++;
            inword = 1;
        } else inword = 0;
    }
    if (hastext) s->paras++;
}

/* the word around pos: [a,b) and its text; 0 if there is none */
int word_at(DOC *d, long pos, long *a, long *b, char *word, int max)
{
    long s = pos, e = pos, i;
    if (pos < 0 || pos > d->len) return 0;
    while (s > 0 && (wordch(d->text[s - 1].ch) ||
                     (d->text[s - 1].ch == '\'' && s > 1 && wordch(d->text[s - 2].ch)))) s--;
    while (e < d->len && (wordch(d->text[e].ch) ||
                          (d->text[e].ch == '\'' && e + 1 < d->len && wordch(d->text[e + 1].ch)))) e++;
    if (e <= s || e - s >= max) return 0;
    for (i = s; i < e; i++) word[i - s] = (char)d->text[i].ch;
    word[e - s] = 0;
    *a = s;
    *b = e;
    return 1;
}

/* ---- new documents from a template -------------------------------------------------- */

typedef struct { char *t; long n, cap; } SB;

static void sb_put(SB *b, const char *s, int esc)
{
    for (; *s; s++) {
        if (esc && (*s == '\n' || *s == '\r')) continue;
        if (b->n + 3 >= b->cap) {
            long nc = b->cap ? b->cap * 2 : 1024;
            char *nt = (char *)realloc(b->t, nc);
            if (!nt) return;
            b->t = nt; b->cap = nc;
        }
        if (esc && *s == '[') b->t[b->n++] = '[';
        b->t[b->n++] = *s;
    }
    if (b->t) b->t[b->n] = 0;
}
#define RAW(s_) sb_put(&b, s_, 0)
#define TXT(s_) sb_put(&b, s_, 1)

static void tpl_date(char *out, int us)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm) { strcpy(out, "?"); return; }
    if (us) sprintf(out, "%s %d, %d", date_months[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
    else fmt_date(out, DT_LONG, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, 0, 0);
}

/* A new document from a template into the empty document d. name, addr[0..2]
 * and title are what the user typed, w[NTW] the words of the template, all
 * in the ST character set. Returns 0 if out of memory. */
int doc_template(DOC *d, int kind, const char *name, const char *const *addr,
                 const char *title, const char *const *w)
{
    SB b;
    char date[64];
    long body = 0;
    int i, us = kind == TPL_US;
    PGFMT f;

    if (kind <= TPL_BLANK || kind >= NTPL) return 1;
    memset(&b, 0, sizeof b);
    page_set_paper(&PS(d), us ? P_LETTER : P_A4, 0);
    tpl_date(date, us);
    RAW("[Size 12]");
    if (kind == TPL_UK || kind == TPL_US) {
        /* the sender: on the right in a British letter, on the left in an
         * American block-style one */
        RAW(us ? "[Left]" : "[Right]");
        if (name && *name) { RAW("[B]"); TXT(name); RAW("[/B]\n"); }
        for (i = 0; i < 3; i++) if (addr && addr[i] && *addr[i]) { TXT(addr[i]); RAW("\n"); }
        RAW("\n"); TXT(date); RAW("\n");
        RAW("[Left]\n");
        TXT(w[TW_TO]); RAW("\n");
        TXT(w[TW_TOADDR]); RAW("\n\n");
        TXT(w[us ? TW_DEAR_US : TW_DEAR_UK]); RAW("\n\n");
        RAW("[B]"); TXT(w[TW_SUBJECT]); RAW("[/B]\n\n");
        RAW("[Justify]");
        body = b.n;
        TXT(w[TW_BODY]); RAW("\n\n[Left]");
        TXT(w[us ? TW_YOURS_US : TW_YOURS_UK]); RAW("\n\n\n\n");
        if (name && *name) TXT(name);
    } else {
        RAW("[Centre][Size 32][B]");
        TXT(title && *title ? title : w[TW_ORG]);
        RAW("[/B][Size 12]\n[I]");
        TXT(w[TW_ISSUE]); RAW(" - "); TXT(date);
        RAW("[/I]\n[Left]\n");
        RAW("[Heading 1][Size 18][B]"); TXT(w[TW_MAIN]); RAW("[/B][Size 12]\n[/Heading][Justify]");
        body = b.n;
        TXT(w[TW_TEXT]); RAW("\n");
        RAW("[Page]\n");
        RAW("[Left][Heading 1][Size 18][B]"); TXT(w[TW_NEWS]); RAW("[/B][Size 12]\n[/Heading][Justify]");
        TXT(w[TW_TEXT]); RAW("\n\n");
        RAW("[Left][Heading 1][Size 18][B]"); TXT(w[TW_DATES]); RAW("[/B][Size 12]\n[/Heading]");
        RAW("[Table 130 90 Border][Row][B]"); TXT(w[TW_WHAT]); RAW("[Cell]"); TXT(w[TW_WHEN]);
        RAW("[/B]\n[Row][Cell]\n[Row][Cell]\n[Row][Cell]\n[/Table]\n");
    }
    if (!b.t) return 0;
    free(d->cv.t);
    d->cv.t = b.t;
    d->cv.len = b.n;
    d->cv.cap = b.cap;
    d->cv.cursor = body;
    d->cv.anchor = -1;
    d->cv.dirty = 1;
    if (!codes_apply(d)) return 0;
    d->cv.dirty = 0;
    d->codes_stale = 1;
    if (kind == TPL_NEWS) {
        long k, cur = d->cursor;
        /* two columns from page 2, a contents list before the first story
         * and the page number in the footer */
        page_fmt(d, 1, &f);
        f.cols = 2;
        if (f.colgap < 12) f.colgap = 18;
        page_ovr_set(d, 1, 1, &f);
        for (k = 0; k < d->len; k++)
            if (doc_para_at(d, k)->toc) break;
        if (k < d->len) {
            long n0 = d->len;
            toc_insert(d, k, w[TW_CONTENTS]);
            cur += d->len - n0;
        }
        hf_pagenum_set(d, 1, 1, 1, w[TW_PAGE], "");
        d->cursor = cur <= d->len ? cur : d->len;
        d->anchor = -1;
    }
    d->lay_full = 1;
    doc_layout(d);
    d->dirty = 0;
    return 1;
}
#undef RAW
#undef TXT
