/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * md.c - Markdown import and export
 *
 * Import reads CommonMark / GitHub Markdown: headings (# and underlined),
 * paragraphs, **bold**, *italic*, <u>underline</u>, `code`, bullet and
 * numbered lists, block quotes, fenced and indented code, pipe tables,
 * rules, links and pictures (their text), HTML entities and a page break
 * written as \pagebreak, \newpage or a page-break <div>.
 *
 * Export writes the same, in UTF-8. What Markdown has no way to say
 * (fonts, sizes, colours, alignment, headers and footers, pictures and
 * drawings) is left out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

/* ================================================================= */
/*  Import                                                            */
/* ================================================================= */

typedef struct {
    DOC  *d;
    CELL  c;                    /* style of the next character             */
    CELL  body;                 /* the body text style                     */
    PARA  p;                    /* format of the paragraph being written   */
    int   pi, pdirty;
    int   started;              /* a paragraph has been begun              */
    unsigned char term;         /* what ends the current paragraph         */
    int   mono, ok;
} MB;

static void mb_put(MB *m, unsigned char ch)
{
    if (m->pdirty) { m->pi = para_find(m->d, &m->p); m->pdirty = 0; }
    m->c.ch = ch;
    m->c.para = (unsigned char)m->pi;
    if (!doc_insert(m->d, m->d->len, &m->c, 1)) m->ok = 0;
}

/* start a paragraph with this format; the one before ends with m->term */
static void mb_begin(MB *m, const PARA *p)
{
    if (m->started) {
        CELL save = m->c;
        m->c = m->body;
        mb_put(m, m->term);
        m->c = save;
    }
    m->started = 1;
    m->term = '\n';
    m->p = *p;
    m->pdirty = 1;
}

static int is_space(int c) { return c == ' ' || c == '\t'; }
static int is_alnum(int c)
{
    return (c >= '0' && c <= '9') || st_isalpha((unsigned char)c);
}
static int is_punct(int c)
{
    return c > 32 && c < 127 && !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

static const struct { const char *n; long u; } ents[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' },
    { "copy", 0xA9 }, { "reg", 0xAE }, { "deg", 0xB0 }, { "eacute", 0xE9 }, { "egrave", 0xE8 },
    { "auml", 0xE4 }, { "ouml", 0xF6 }, { "uuml", 0xFC }, { "Auml", 0xC4 }, { "Ouml", 0xD6 },
    { "Uuml", 0xDC }, { "szlig", 0xDF }, { "pound", 0xA3 }, { "sect", 0xA7 }, { "hellip", 0x2026 },
    { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "laquo", 0xAB }, { "raquo", 0xBB }, { NULL, 0 } };

/* an entity at s (after '&'); returns its length including ';', or 0 */
static int entity(const unsigned char *s, long n, long *u)
{
    long i, v = 0;
    if (n > 1 && s[0] == '#') {
        int hex = s[1] == 'x' || s[1] == 'X';
        for (i = hex ? 2 : 1; i < n && i < 9; i++) {
            int c = s[i], dv;
            if (c == ';') { *u = v; return i > (hex ? 2 : 1) ? (int)i + 1 : 0; }
            if (c >= '0' && c <= '9') dv = c - '0';
            else if (hex && c >= 'a' && c <= 'f') dv = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F') dv = c - 'A' + 10;
            else return 0;
            v = v * (hex ? 16 : 10) + dv;
        }
        return 0;
    }
    for (i = 0; ents[i].n; i++) {
        long l = (long)strlen(ents[i].n);
        if (l < n && !memcmp(s, ents[i].n, l) && s[l] == ';') { *u = ents[i].u; return (int)l + 1; }
    }
    return 0;
}

static long run_len(const unsigned char *s, long i, long n, int ch)
{
    long k = 0;
    while (i + k < n && s[i + k] == ch) k++;
    return k;
}

/* is there a closing run of at least k ch after i? */
static int has_closer(const unsigned char *s, long i, long n, int ch, long k)
{
    for (; i < n; i++) {
        if (s[i] == '\\') { i++; continue; }
        if (s[i] == ch && !is_space(s[i - 1])) {
            long r = run_len(s, i, n, ch);
            if (r >= k && (ch != '_' || i + r >= n || !is_alnum(s[i + r]))) return 1;
            i += r - 1;
        }
    }
    return 0;
}

static void set_attr(MB *m, int attr, int on)
{
    if (on) m->c.attr |= (unsigned char)attr; else m->c.attr &= (unsigned char)~attr;
}

/* the matching ']' of a '[' at i, or -1 */
static long close_bracket(const unsigned char *s, long i, long n)
{
    int depth = 0;
    for (; i < n; i++) {
        if (s[i] == '\\') { i++; continue; }
        if (s[i] == '[') depth++;
        else if (s[i] == ']' && --depth == 0) return i;
    }
    return -1;
}

static void inline_md(MB *m, const unsigned char *s, long n);

/* an HTML tag at s[i] ('<'); returns its length, 0 if not a tag */
static long html_tag(MB *m, const unsigned char *s, long i, long n)
{
    long j = i + 1, e;
    char name[12];
    int k = 0, close = 0;
    if (j < n && s[j] == '/') { close = 1; j++; }
    if (j >= n || !(st_isalpha(s[j]) || s[j] == '!')) return 0;
    for (e = j; e < n && e < i + 300 && s[e] != '>'; e++) ;
    if (e >= n || s[e] != '>') return 0;
    while (j < e && k < 11 && (st_isalpha(s[j]) || (s[j] >= '0' && s[j] <= '9'))) {
        name[k++] = (char)(s[j] >= 'A' && s[j] <= 'Z' ? s[j] + 32 : s[j]);
        j++;
    }
    name[k] = 0;
    if (!strcmp(name, "u") || !strcmp(name, "ins")) set_attr(m, A_UNDER, !close);
    else if (!strcmp(name, "b") || !strcmp(name, "strong")) set_attr(m, A_BOLD, !close);
    else if (!strcmp(name, "i") || !strcmp(name, "em")) set_attr(m, A_ITALIC, !close);
    else if (!strcmp(name, "br")) mb_put(m, ' ');
    return e - i + 1;
}

static void inline_md(MB *m, const unsigned char *s, long n)
{
    long i, k, j;
    for (i = 0; i < n; i++) {
        int ch = s[i];
        if (ch == '\\' && i + 1 < n && is_punct(s[i + 1])) { mb_put(m, s[++i]); continue; }
        if (ch == '`') {
            k = run_len(s, i, n, '`');
            for (j = i + k; j < n; j++)
                if (s[j] == '`') {
                    long r = run_len(s, j, n, '`');
                    if (r == k) break;
                    j += r - 1;
                }
            if (j < n) {
                long a = i + k, b = j;
                unsigned char f = m->c.font;
                if (b - a >= 2 && s[a] == ' ' && s[b - 1] == ' ') { a++; b--; }
                m->c.font = (unsigned char)m->mono;
                for (; a < b; a++) mb_put(m, s[a]);
                m->c.font = f;
                i = j + k - 1;
                continue;
            }
            for (j = 0; j < k; j++) mb_put(m, '`');
            i += k - 1;
            continue;
        }
        if (ch == '*' || ch == '_') {
            int bold = (m->c.attr & A_BOLD) != 0, ital = (m->c.attr & A_ITALIC) != 0;
            int open, close, prev = i > 0 ? s[i - 1] : ' ';
            long want;
            k = run_len(s, i, n, ch);
            open = i + k < n && !is_space(s[i + k]) && (ch == '*' || !is_alnum(prev));
            close = i > 0 && !is_space(prev) && (ch == '*' || i + k >= n || !is_alnum(s[i + k]));
            want = k >= 3 ? 3 : k;
            if (want == 3 && close && bold && ital) { set_attr(m, A_BOLD | A_ITALIC, 0); i += k - 1; continue; }
            if (want == 2 && close && bold) { set_attr(m, A_BOLD, 0); i += k - 1; continue; }
            if (want == 1 && close && ital) { set_attr(m, A_ITALIC, 0); i += k - 1; continue; }
            if (open && has_closer(s, i + k, n, ch, want)) {
                set_attr(m, want == 3 ? A_BOLD | A_ITALIC : want == 2 ? A_BOLD : A_ITALIC, 1);
                i += k - 1;
                continue;
            }
            for (j = 0; j < k; j++) mb_put(m, (unsigned char)ch);
            i += k - 1;
            continue;
        }
        if (ch == '~' && i + 1 < n && s[i + 1] == '~') { i++; continue; }       /* strikethrough: not kept */
        if (ch == '[' || (ch == '!' && i + 1 < n && s[i + 1] == '[')) {
            long a = ch == '!' ? i + 1 : i, b = close_bracket(s, a, n);
            if (b > a) {
                long e = b;
                if (b + 1 < n && s[b + 1] == '(') {                 /* (url "title") */
                    int depth = 0;
                    for (e = b + 1; e < n; e++) {
                        if (s[e] == '(') depth++;
                        else if (s[e] == ')' && --depth == 0) break;
                    }
                    if (e >= n) e = b;
                } else if (b + 1 < n && s[b + 1] == '[') {          /* [ref] */
                    long r = close_bracket(s, b + 1, n);
                    if (r > b) e = r;
                }
                if (e > b || ch == '!') {
                    inline_md(m, s + a + 1, b - a - 1);
                    i = e;
                    continue;
                }
            }
            mb_put(m, (unsigned char)ch);
            continue;
        }
        if (ch == '<') {
            for (j = i + 1; j < n && j < i + 200 && s[j] != '>' && s[j] != ' '; j++) ;
            if (j < n && s[j] == '>' && j > i + 1 &&
                (!strncmp((const char *)s + i + 1, "http", 4) || !strncmp((const char *)s + i + 1, "mailto:", 7) ||
                 memchr(s + i + 1, '@', j - i - 1))) {
                for (k = i + 1; k < j; k++) mb_put(m, s[k]);
                i = j;
                continue;
            }
            k = html_tag(m, s, i, n);
            if (k) { i += k - 1; continue; }
        }
        if (ch == '&') {
            long u;
            int l = entity(s + i + 1, n - i - 1, &u);
            if (l) { mb_put(m, uni_to_st(u)); i += l; continue; }
        }
        if (ch == '\t' || ch >= 32) mb_put(m, (unsigned char)ch);
    }
}

/* ---- blocks --------------------------------------------------------- */

typedef struct { unsigned char *s; long n; } LN;

static long indent_of(const LN *l)
{
    long i, w = 0;
    for (i = 0; i < l->n; i++) {
        if (l->s[i] == ' ') w++;
        else if (l->s[i] == '\t') w = (w + 4) & ~3L;
        else break;
    }
    return w;
}

static long skip_ws(const LN *l)
{
    long i = 0;
    while (i < l->n && is_space(l->s[i])) i++;
    return i;
}

static int blank(const LN *l) { return skip_ws(l) == l->n; }

/* ---, ***, ___ */
static int is_rule(const LN *l)
{
    long i = skip_ws(l), k = 0;
    int ch;
    if (indent_of(l) > 3 || i >= l->n) return 0;
    ch = l->s[i];
    if (ch != '-' && ch != '*' && ch != '_') return 0;
    for (; i < l->n; i++) {
        if (l->s[i] == ch) k++;
        else if (!is_space(l->s[i])) return 0;
    }
    return k >= 3;
}

/* a line of = or - under a paragraph: 1 or 2 */
static int setext(const LN *l)
{
    long i = skip_ws(l);
    int ch;
    if (indent_of(l) > 3 || i >= l->n) return 0;
    ch = l->s[i];
    if (ch != '=' && ch != '-') return 0;
    while (i < l->n && l->s[i] == ch) i++;
    while (i < l->n && is_space(l->s[i])) i++;
    return i == l->n ? (ch == '=' ? 1 : 2) : 0;
}

static int heading(const LN *l, long *a, long *b)
{
    long i = skip_ws(l), k = 0, e;
    if (indent_of(l) > 3) return 0;
    while (i + k < l->n && l->s[i + k] == '#') k++;
    if (k < 1 || k > 6 || (i + k < l->n && !is_space(l->s[i + k]))) return 0;
    i += k;
    while (i < l->n && is_space(l->s[i])) i++;
    e = l->n;
    while (e > i && is_space(l->s[e - 1])) e--;
    {   /* closing #s */
        long t = e;
        while (t > i && l->s[t - 1] == '#') t--;
        if (t == i || is_space(l->s[t - 1])) { e = t; while (e > i && is_space(l->s[e - 1])) e--; }
    }
    *a = i; *b = e;
    return (int)k;
}

static int fence(const LN *l, int *ch, long *len)
{
    long i = skip_ws(l), k;
    if (indent_of(l) > 3 || i >= l->n || (l->s[i] != '`' && l->s[i] != '~')) return 0;
    k = run_len(l->s, i, l->n, l->s[i]);
    if (k < 3) return 0;
    *ch = l->s[i];
    *len = k;
    return 1;
}

/* list item: 1 bullet, 2 number; *text where the item's text starts */
static int list_item(const LN *l, long *text)
{
    long i = skip_ws(l);
    if (i >= l->n) return 0;
    if ((l->s[i] == '-' || l->s[i] == '*' || l->s[i] == '+') && i + 1 < l->n && is_space(l->s[i + 1])) {
        *text = i + 2;
        while (*text < l->n && is_space(l->s[*text])) (*text)++;
        return 1;
    }
    if (l->s[i] >= '0' && l->s[i] <= '9') {
        long k = i;
        while (k < l->n && k < i + 9 && l->s[k] >= '0' && l->s[k] <= '9') k++;
        if (k < l->n && (l->s[k] == '.' || l->s[k] == ')') && (k + 1 >= l->n || is_space(l->s[k + 1]))) {
            *text = k + 1;
            while (*text < l->n && is_space(l->s[*text])) (*text)++;
            return 2;
        }
    }
    return 0;
}

static int has_pipe(const LN *l)
{
    long i;
    for (i = 0; i < l->n; i++) {
        if (l->s[i] == '\\') { i++; continue; }
        if (l->s[i] == '|') return 1;
    }
    return 0;
}

/* | --- | :---: | */
static int table_delim(const LN *l)
{
    long i;
    int dashes = 0;
    if (!has_pipe(l) && l->s[skip_ws(l)] != '-' && l->s[skip_ws(l)] != ':') return 0;
    for (i = 0; i < l->n; i++) {
        int c = l->s[i];
        if (c == '-') dashes++;
        else if (c != '|' && c != ':' && !is_space(c)) return 0;
    }
    return dashes > 0;
}

/* the cells of a table row: starts and ends */
static int split_cells(const LN *l, long *ca, long *cb, int max)
{
    long i = skip_ws(l), e = l->n, a;
    int n = 0, code = 0;
    while (e > i && is_space(l->s[e - 1])) e--;
    if (i < e && l->s[i] == '|') i++;
    if (e > i && l->s[e - 1] == '|' && (e < 2 || l->s[e - 2] != '\\')) e--;
    a = i;
    for (; i <= e; i++) {
        if (i < e && l->s[i] == '\\') { i++; continue; }
        if (i < e && l->s[i] == '`') code = !code;
        if (i == e || (l->s[i] == '|' && !code)) {
            long x = a, y = i;
            while (x < y && is_space(l->s[x])) x++;
            while (y > x && is_space(l->s[y - 1])) y--;
            if (n < max) { ca[n] = x; cb[n] = y; n++; }
            a = i + 1;
        }
    }
    return n;
}

static int page_break_line(const LN *l)
{
    char t[80];
    long i = skip_ws(l), e = l->n, k = 0;
    while (e > i && is_space(l->s[e - 1])) e--;
    if (e - i >= (long)sizeof t) return 0;
    for (; i < e; i++) t[k++] = (char)(l->s[i] >= 'A' && l->s[i] <= 'Z' ? l->s[i] + 32 : l->s[i]);
    t[k] = 0;
    return !strcmp(t, "\\pagebreak") || !strcmp(t, "\\newpage") || !strcmp(t, "<!-- pagebreak -->") ||
           (t[0] == '<' && strstr(t, "page-break-") != NULL);
}

/* the paragraph being gathered */
typedef struct { unsigned char *t; long n, cap; PARA p; int head, any; } ACC;

static int acc_add(ACC *a, const unsigned char *s, long n)
{
    if (a->n + n + 2 > a->cap) {
        long c = a->cap ? a->cap * 2 : 256;
        unsigned char *t;
        while (c < a->n + n + 2) c *= 2;
        t = (unsigned char *)realloc(a->t, c);
        if (!t) return 0;
        a->t = t; a->cap = c;
    }
    if (a->n && a->t[a->n - 1] != ' ') a->t[a->n++] = ' ';
    memcpy(a->t + a->n, s, n);
    a->n += n;
    a->any = 1;
    return 1;
}

static const unsigned char head_size[7] = { 0, 24, 18, 14, 12, 12, 12 };

static void flush(MB *m, ACC *a)
{
    long e;
    if (!a->any) return;
    e = a->n;
    while (e > 0 && is_space(a->t[e - 1])) e--;
    mb_begin(m, &a->p);
    m->c = m->body;
    if (a->head) {
        m->c.size = head_size[a->head];
        m->c.attr = A_BOLD;
    }
    inline_md(m, a->t, e);
    m->c = m->body;
    a->n = 0;
    a->any = 0;
    a->head = 0;
}

int md_import(DOC *d, const char *path, int cs)
{
    long n, nl = 0, i, k, li;
    unsigned char *raw = load_file(path, &n), *b;
    LN *lines;
    MB m;
    ACC a;
    PARA plain;
    PGFMT f;
    long tw;

    if (!raw) return 0;
    b = (unsigned char *)malloc(n + 1);
    if (!b) { free(raw); return 0; }
    n = text_to_st(raw, n, cs, b);
    free(raw);
    for (i = 0; i < n; i++) if (b[i] == '\n') nl++;
    lines = (LN *)malloc((nl + 2) * sizeof(LN));
    if (!lines) { free(b); return 0; }
    nl = 0;
    for (i = 0, k = 0; i <= n; i++)
        if (i == n || b[i] == '\n') {
            if (i == n && i == k) break;
            lines[nl].s = b + k; lines[nl].n = i - k; nl++;
            k = i + 1;
        }

    memset(&m, 0, sizeof m);
    memset(&a, 0, sizeof a);
    m.d = d;
    m.ok = 1;
    m.body = d->ins;
    m.body.attr = 0;
    m.body.color = 0;
    m.c = m.body;
    m.mono = font_family_find(2);
    para_default(&plain);
    page_fmt(d, 0, &f);
    tw = (long)PS(d).w - f.ml - f.mr;

    for (li = 0; li < nl && m.ok; li++) {
        LN *l = &lines[li];
        long ta, tb;
        int lv, fc;
        long fl;
        PROG(li, nl);

        if (blank(l)) { flush(&m, &a); continue; }
        if (a.any && !a.head && !a.p.list && !a.p.left && (lv = setext(l)) != 0) {
            a.head = lv;
            a.p.toc = (unsigned char)lv;
            flush(&m, &a);
            continue;
        }
        if (page_break_line(l)) {
            flush(&m, &a);
            if (!m.started || m.term != '\n') mb_begin(&m, &plain);
            m.term = CH_PAGE;
            continue;
        }
        if (is_rule(l)) {
            static const unsigned char rule[] = "* * *";
            PARA p = plain;
            flush(&m, &a);
            p.just = J_CENTRE;
            mb_begin(&m, &p);
            for (k = 0; rule[k]; k++) mb_put(&m, rule[k]);
            continue;
        }
        if ((lv = heading(l, &ta, &tb)) != 0) {
            flush(&m, &a);
            a.p = plain;
            a.p.toc = (unsigned char)(lv > 3 ? 3 : lv);
            a.head = lv;
            acc_add(&a, l->s + ta, tb - ta);
            if (!a.any) { a.any = 1; }
            flush(&m, &a);
            continue;
        }
        if (fence(l, &fc, &fl)) {
            flush(&m, &a);
            for (li++; li < nl; li++) {
                LN *c = &lines[li];
                int c2;
                long l2;
                if (fence(c, &c2, &l2) && c2 == fc && l2 >= fl) {
                    long t = skip_ws(c) + l2;
                    while (t < c->n && is_space(c->s[t])) t++;
                    if (t == c->n) break;
                }
                mb_begin(&m, &plain);
                m.c = m.body;
                m.c.font = (unsigned char)m.mono;
                for (k = 0; k < c->n; k++) if (c->s[k] >= 32 || c->s[k] == '\t') mb_put(&m, c->s[k]);
                m.c = m.body;
            }
            continue;
        }
        if (!a.any && indent_of(l) >= 4 && !(m.started && m.p.list)) {     /* indented code */
            long t = 0, w = 0;
            while (t < l->n && w < 4) { w = l->s[t] == '\t' ? (w + 4) & ~3L : w + 1; t++; }
            mb_begin(&m, &plain);
            m.c = m.body;
            m.c.font = (unsigned char)m.mono;
            for (; t < l->n; t++) if (l->s[t] >= 32 || l->s[t] == '\t') mb_put(&m, l->s[t]);
            m.c = m.body;
            continue;
        }
        if (li + 1 < nl && has_pipe(l) && table_delim(&lines[li + 1]) && !a.any) {
            long ca[MAX_COLS + 1], cb[MAX_COLS + 1];
            short w[MAX_COLS];
            int nc = split_cells(l, ca, cb, MAX_COLS), t, c, row = 0;
            PARA p = plain;
            if (nc < 1) nc = 1;
            for (c = 0; c < nc; c++) w[c] = (short)(tw / nc);
            t = doc_tbl_new(d, nc, w, 1);
            if (t) {
                p.tbl = (unsigned char)t;
                for (; li < nl; li++) {
                    LN *r = &lines[li];
                    int got;
                    if (row == 1) { row++; continue; }             /* the delimiter row */
                    if (blank(r) || (row > 1 && !has_pipe(r))) { li--; break; }
                    got = split_cells(r, ca, cb, MAX_COLS);
                    mb_begin(&m, &p);
                    for (c = 0; c < nc; c++) {
                        m.c = m.body;
                        if (row == 0) m.c.attr = A_BOLD;
                        if (c < got) inline_md(&m, r->s + ca[c], cb[c] - ca[c]);
                        m.c = m.body;
                        if (c < nc - 1) mb_put(&m, CH_CELL);
                    }
                    row++;
                }
                continue;
            }
        }
        /* block quote */
        {
            long t = skip_ws(l);
            int q = 0;
            if (indent_of(l) <= 3 && t < l->n && l->s[t] == '>') {
                LN in = *l;
                while (t < l->n && l->s[t] == '>') {
                    q++;
                    t++;
                    if (t < l->n && l->s[t] == ' ') t++;
                    while (t < l->n && is_space(l->s[t])) t++;
                }
                in.s = l->s + t; in.n = l->n - t;
                if (a.any && a.p.left != q * 36) flush(&m, &a);
                if (blank(&in)) { flush(&m, &a); continue; }
                if ((lv = heading(&in, &ta, &tb)) != 0) { flush(&m, &a); in.s += ta; in.n = tb - ta; }
                if (!a.any) { a.p = plain; a.p.left = (short)(q * 36); a.head = 0; }
                acc_add(&a, in.s, in.n);
                continue;
            }
        }
        /* list item */
        if ((lv = list_item(l, &ta)) != 0 && (!a.any || !is_rule(l))) {
            int level = (int)(indent_of(l) / 2);
            flush(&m, &a);
            if (level > 4) level = 4;
            a.p = plain;
            a.p.list = (unsigned char)(lv == 1 ? LIST_BULLET : LIST_NUMBER);
            if (level) { a.p.left = (short)(24 * (level + 1)); a.p.first = -18; }
            if (ta >= l->n) { a.any = 1; continue; }
            acc_add(&a, l->s + ta, l->n - ta);
        } else {
            long t = skip_ws(l);
            int hard;
            if (!a.any) {
                a.p = plain;
                /* a paragraph inside a list item, after a blank line */
                if (m.started && (m.p.list || (m.p.left && !m.p.tbl)) && indent_of(l) >= 2)
                    a.p.left = (short)(m.p.list ? (m.p.left ? m.p.left : 24) : m.p.left);
            }
            hard = (l->n >= 2 && l->s[l->n - 1] == ' ' && l->s[l->n - 2] == ' ') ||
                   (l->n >= 1 && l->s[l->n - 1] == '\\');
            acc_add(&a, l->s + t, l->n - t - (hard && l->s[l->n - 1] == '\\' ? 1 : 0));
            if (hard) {
                PARA keep = a.p;
                flush(&m, &a);
                a.p = keep;
                a.p.list = 0;
                if (keep.list && !keep.left) a.p.left = 24;
            }
            continue;
        }
        /* a heading after a bullet */
        (void)tb;
    }
    flush(&m, &a);
    /* a table at the very end still needs its row ended */
    if (m.started && m.p.tbl) { m.c = m.body; mb_put(&m, '\n'); }
    else if (m.started && m.term == CH_PAGE) { m.c = m.body; mb_put(&m, CH_PAGE); }
    free(a.t);
    free(lines);
    free(b);
    doc_norm_para(d);
    d->dirty = 0;
    return m.ok;
}

/* ================================================================= */
/*  Export                                                            */
/* ================================================================= */

enum { K_NONE, K_PARA, K_BULLET, K_NUMBER, K_QUOTE, K_CODE, K_HEAD, K_TABLE, K_RULE, K_EMPTY };

static void put_md(FILE *f, unsigned char ch, int esc)
{
    if (esc && strchr("\\`*_[]<", ch) && ch) fputc('\\', f);
    if (ch == CH_CELL) ch = ' ';
    put_utf8(f, ch);
}

/* style of a cell for Markdown: 1 bold, 2 italic, 4 underline, 8 code */
static int md_style(const CELL *c, int nobold, int mono_para)
{
    int s = 0;
    if ((c->attr & A_BOLD) && !nobold) s |= 1;
    if (c->attr & A_ITALIC) s |= 2;
    if (c->attr & A_UNDER) s |= 4;
    if (!mono_para && font_is_mono(c->font)) s |= 8;
    return s;
}

static void open_style(FILE *f, int s)
{
    if (s & 1) fputs("**", f);
    if (s & 2) fputs("*", f);
    if (s & 4) fputs("<u>", f);
    if (s & 8) fputs("`", f);
}

static void close_style(FILE *f, int s)
{
    if (s & 8) fputs("`", f);
    if (s & 4) fputs("</u>", f);
    if (s & 2) fputs("*", f);
    if (s & 1) fputs("**", f);
}

/* the text of [a, b) with emphasis; table: escape | too */
static void md_runs(FILE *f, DOC *d, long a, long b, int nobold, int table)
{
    long k, r, x, y;
    int first = 1;
    while (a < b && (d->text[a].ch == ' ' || d->text[a].ch == '\t')) a++;
    while (b > a && (d->text[b - 1].ch == ' ' || d->text[b - 1].ch == '\t')) b--;
    for (k = a; k < b; k = r) {
        int s = md_style(&d->text[k], nobold, 0);
        for (r = k; r < b && md_style(&d->text[r], nobold, 0) == s; r++) ;
        x = k; y = r;
        if (s) {
            while (x < y && d->text[x].ch == ' ') put_md(f, ' ', 0), x++;
            while (y > x && d->text[y - 1].ch == ' ') y--;
            if (x == y) { for (; x < r; x++) put_md(f, ' ', 0); first = 0; continue; }
            open_style(f, s);
        }
        for (; x < y; x++) {
            unsigned char ch = d->text[x].ch;
            if (s & 8) { put_utf8(f, ch == '\t' ? ' ' : ch); continue; }
            if (first && x == a) {
                /* things that would start a block */
                long z = x;
                if (ch == '#' || ch == '>' || ((ch == '-' || ch == '+') && x + 1 < b && d->text[x + 1].ch == ' '))
                    fputc('\\', f);
                while (z < b && d->text[z].ch >= '0' && d->text[z].ch <= '9') z++;
                if (z > x && z < b && (d->text[z].ch == '.' || d->text[z].ch == ')')) {
                    for (; x < z; x++) put_utf8(f, d->text[x].ch);
                    fputc('\\', f);
                    ch = d->text[x].ch;
                }
            }
            if (table && ch == '|') fputc('\\', f);
            put_md(f, ch, 1);
        }
        if (s) close_style(f, s);
        for (; y < r; y++) put_md(f, ' ', 0);
        first = 0;
    }
}

/* the whole paragraph [a, b) is in a monospaced font */
static int all_mono(DOC *d, long a, long b)
{
    long k;
    if (a == b) return 0;
    for (k = a; k < b; k++) if (!font_is_mono(d->text[k].font)) return 0;
    return 1;
}

static int is_rule_para(DOC *d, long a, long b)
{
    static const char r[] = "* * *";
    long k;
    if (b - a != 5) return 0;
    for (k = 0; k < 5; k++) if (d->text[a + k].ch != (unsigned char)r[k]) return 0;
    return 1;
}

static int kind_of(DOC *d, long a, long b, const PARA *p)
{
    if (p->tbl) return K_TABLE;
    if (p->toc && !p->tocent) return K_HEAD;
    if (p->list == LIST_BULLET) return K_BULLET;
    if (p->list == LIST_NUMBER) return K_NUMBER;
    if (a == b) return K_EMPTY;
    if (all_mono(d, a, b)) return K_CODE;
    if (is_rule_para(d, a, b)) return K_RULE;
    if (p->left > 0 && !p->tocent) return K_QUOTE;
    return K_PARA;
}

static int md_write(DOC *d, const char *path)
{
    FILE *f = fopen(path, "wb");
    long pos = 0, k;
    int prev = K_NONE, num = 0, err, curtbl = 0, row = 0;
    if (!f) return 0;
    while (pos < d->len) {
        long e = doc_para_end(d, pos);
        unsigned char brk = e < d->len ? d->text[e].ch : 0;
        const PARA *p = PARA_OF(d, &d->text[e < d->len ? e : e - 1]);
        int kind = kind_of(d, pos, e, p);
        PROG(pos, d->len);

        /* separators */
        if (kind == K_EMPTY) {
            if (prev == K_CODE) {
                /* keep it in the code block if more code follows */
                long n2 = e + 1, e2;
                while (n2 < d->len && d->text[n2].ch == '\n') n2++;
                e2 = doc_para_end(d, n2);
                if (n2 < d->len && e2 > n2 && all_mono(d, n2, e2)) fputs("\n", f);
                else { fputs("\n```", f); prev = K_PARA; }
            } else if (prev == K_TABLE) prev = K_PARA;
            if (brk == CH_PAGE) { fputs("\n\n<div style=\"page-break-after: always\"></div>", f); prev = K_PARA; }
            pos = e + 1;
            continue;
        }
        if (prev == K_CODE && kind != K_CODE) { fputs("\n```", f); }
        if (prev != K_NONE) {
            if ((kind == K_BULLET || kind == K_NUMBER) && prev == kind) fputs("\n", f);
            else if (kind == K_CODE && prev == K_CODE) fputs("\n", f);
            else if (kind == K_TABLE && prev == K_TABLE && p->tbl == curtbl) fputs("\n", f);
            else fputs("\n\n", f);
        }
        if (kind != K_NUMBER) num = 0;
        if (kind != K_TABLE) curtbl = 0;

        switch (kind) {
        case K_HEAD:
            for (k = 0; k < p->toc; k++) fputc('#', f);
            fputc(' ', f);
            md_runs(f, d, pos, e, 1, 0);
            break;
        case K_BULLET:
        case K_NUMBER: {
            int level = p->left > 24 ? (p->left - 24) / 24 : 0;
            for (k = 0; k < level * 2; k++) fputc(' ', f);
            if (kind == K_BULLET) fputs("- ", f);
            else fprintf(f, "%d. ", ++num);
            md_runs(f, d, pos, e, 0, 0);
            break;
        }
        case K_QUOTE: {
            int q = p->left / 36;
            if (q < 1) q = 1;
            for (k = 0; k < q; k++) fputs("> ", f);
            md_runs(f, d, pos, e, 0, 0);
            break;
        }
        case K_CODE:
            if (prev != K_CODE) fputs("```\n", f);
            for (k = pos; k < e; k++) put_utf8(f, d->text[k].ch == CH_CELL ? '\t' : d->text[k].ch);
            break;
        case K_RULE:
            fputs("---", f);
            break;
        case K_TABLE: {
            long c0 = pos, c;
            int nc = 0, i;
            if (p->tbl != curtbl) { curtbl = p->tbl; row = 0; }
            fputs("|", f);
            for (c = pos; c <= e; c++)
                if (c == e || d->text[c].ch == CH_CELL) {
                    fputc(' ', f);
                    md_runs(f, d, c0, c, row == 0, 1);
                    fputs(" |", f);
                    nc++;
                    c0 = c + 1;
                }
            if (row == 0) {
                fputs("\n|", f);
                for (i = 0; i < nc; i++) fputs(" --- |", f);
            }
            row++;
            break;
        }
        default:
            md_runs(f, d, pos, e, 0, 0);
            break;
        }
        prev = kind;
        if (brk == CH_PAGE) {
            if (prev == K_CODE) fputs("\n```", f);
            fputs("\n\n<div style=\"page-break-after: always\"></div>", f);
            prev = K_PARA;
        }
        pos = e + 1;
    }
    if (prev == K_CODE) fputs("\n```", f);
    fputs("\n", f);
    err = ferror(f);
    fclose(f);
    return !err;
}

/* Markdown is written as UTF-8 with LF line ends, then turned into the
 * characters and line ends asked for */
int md_export(DOC *d, const char *path, int crlf, int cs)
{
    if (!md_write(d, path)) return 0;
    if (cs == CS_UTF8 && !crlf) return 1;
    return file_transcode(path, cs == CS_AUTO ? CS_UTF8 : cs, crlf);
}
