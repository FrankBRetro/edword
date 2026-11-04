/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * fwp.c - 1st Word Plus documents (GST/Atari, versions 1 to 3)
 *
 * A 1st Word Plus file is ST text with CR LF line ends. Lines starting
 * with 0x1F hold settings (0 paper, 1 header, 2 footer, 9 ruler...).
 * ESC (0x1B) and a byte 0x80 + attributes set the style (1 bold, 2 light,
 * 4 italic, 8 underline, 16 superscript, 32 subscript). 0x1C is a space
 * stretched by justification, 0x1D an indent space, 0x1E a variable space,
 * 0x19 a soft hyphen, 0x0C a page break, 0x0B n a conditional page break.
 * A line that ends with a space before its CR LF was wrapped (soft);
 * any other line end is a hard return, the end of a paragraph.
 *
 * The text is set in a fixed pitch: pica, ten characters to the inch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

#define F_ESC     0x1B
#define F_INFO    0x1F
#define F_STRETCH 0x1C
#define F_INDENT  0x1D
#define F_VAR     0x1E
#define F_SHY     0x19
#define F_CPB     0x0B
#define F_FOOT    0x18
/* pica: 7.2 points a character */
static int ch2pt(long n) { return (int)((n * 72 + 5) / 10); }
static int pt2ch(long p) { return (int)((p * 10 + (p >= 0 ? 36 : -36)) / 72); }

/* does this look like a 1st Word Plus file? */
int fwp_detect(const unsigned char *b, long n)
{
    long i, esc = 0;
    if (n > 1 && b[0] == F_INFO && b[1] >= '0' && b[1] <= 'Z') return 1;
    for (i = 0; i + 1 < n && i < 4000; i++)
        if (b[i] == F_ESC && b[i + 1] >= 0x80 && b[i + 1] < 0xC0) esc++;
    return esc > 0;
}

/* ================================================================= */
/*  Import                                                            */
/* ================================================================= */

typedef struct {
    DOC  *d;
    CELL  c, body;
    int   attr;
} FB;

static void fb_put(FB *f, unsigned char ch)
{
    f->c.ch = ch;
    f->c.attr = (unsigned char)(f->attr & (A_BOLD | A_ITALIC | A_UNDER));
    doc_insert(f->d, f->d->len, &f->c, 1);
}

static int fwp_attr(int b)
{
    int a = 0;
    if (b & 1) a |= A_BOLD;
    if (b & 4) a |= A_ITALIC;
    if (b & 8) a |= A_UNDER;
    return a;
}

static int is_sp(int c) { return c == ' ' || c == F_STRETCH || c == F_VAR || c == F_INDENT; }

/* a header or footer line: left 0x1F centre 0x1F right 0x1F first */
static void read_hf(DOC *d, int which, const unsigned char *s, long n)
{
    const unsigned char *part[3];
    long len[3], i;
    int k = 0, pick = -1;
    char *out = HF(d).text[which];
    long o = 0;
    part[0] = s; len[0] = len[1] = len[2] = 0; part[1] = part[2] = s + n;
    for (i = 0; i < n && k < 3; i++) {
        if (s[i] == F_INFO) { k++; if (k < 3) part[k] = s + i + 1; continue; }
        len[k]++;
    }
    if (k >= 3 && i < n) HF(d).first = s[i] == '1';
    if (len[1]) pick = 1; else if (len[0]) pick = 0; else if (len[2]) pick = 2;
    out[0] = 0;
    if (pick < 0) return;
    HF(d).just[which] = (unsigned char)(pick == 0 ? J_LEFT : pick == 1 ? J_CENTRE : J_RIGHT);
    for (i = 0; i < len[pick] && o < 74; i++) {
        unsigned char ch = part[pick][i];
        if (ch == '#') { strcpy(out + o, "{page}"); o += 6; }
        else if (ch == '$' || ch == '&') { strcpy(out + o, "{date}"); o += 6; }
        else if (ch == F_ESC) { i++; }
        else if (ch >= 32) out[o++] = (char)ch;
        else if (is_sp(ch)) out[o++] = ' ';
    }
    out[o] = 0;
}

typedef struct { long a, n; } FLINE;

static int line_soft(const unsigned char *s, long n)
{
    return n > 0 && (s[n - 1] == ' ' || s[n - 1] == F_STRETCH || s[n - 1] == F_VAR || s[n - 1] == F_SHY);
}

int fwp_import(DOC *d, const char *path)
{
    long n, i, k, m, nl = 0;
    unsigned char *b = load_file(path, &n);
    FLINE *ln;
    FB f;
    PARA cur;
    int width = 65, justify = 0, infoot = 0, need_term = 0;

    if (!b) return 0;
    for (i = 0; i < n; i++) if (b[i] == '\n' || b[i] == '\r') nl++;
    ln = (FLINE *)malloc((nl + 2) * sizeof(FLINE));
    if (!ln) { free(b); return 0; }
    memset(&f, 0, sizeof f);
    f.d = d;
    f.body = d->ins;
    f.body.attr = 0;
    f.c = f.body;
    para_default(&cur);

    /* split into lines */
    nl = 0;
    for (i = 0; i < n; ) {
        long a = i, e;
        while (i < n && b[i] != '\r' && b[i] != '\n') i++;
        e = i;
        if (i < n && b[i] == '\r') i++;
        if (i < n && b[i] == '\n') i++;
        ln[nl].a = a; ln[nl].n = e - a;
        nl++;
    }

    for (k = 0; k < nl; k = m) {
        unsigned char *s = b + ln[k].a;
        long len = ln[k].n, j, e, pstart, ind1 = 0, ind2 = -1, tlen = 0, q;
        PARA p;
        int pi;
        PROG(k, nl);

        m = k + 1;
        if (len > 0 && s[0] == F_INFO) {
            int id = len > 1 ? s[1] : 0;
            if (id == 'N') infoot = 1;                     /* footnote text: not kept */
            else if (id == 'E') infoot = 0;
            else if (id == '1' || id == '2') read_hf(d, id - '1', s + 2, len - 2);
            else if (id == '9' || id == '6') {
                long a = 2, z;
                while (a < len && s[a] != '[') a++;
                for (z = a; z < len && s[z] != ']'; z++) ;
                if (z < len) {
                    width = (int)(z - a - 1);
                    if (width < 10) width = 10;
                    if (id == '9' && z + 2 < len) justify = s[z + 2] == '1';
                }
            }
            continue;
        }
        if (infoot) continue;
        if (len > 0 && s[0] == 0x0C) {                    /* page break */
            f.c = f.body;
            fb_put(&f, CH_PAGE);
            need_term = 0;
            continue;
        }
        /* the paragraph: this line and the soft lines after it */
        while (line_soft(b + ln[m - 1].a, ln[m - 1].n) && m < nl && ln[m].n > 0 &&
               b[ln[m].a] != F_INFO && b[ln[m].a] != 0x0C) m++;
        if (need_term) { f.c = f.body; fb_put(&f, '\n'); }
        need_term = 1;
        pstart = d->len;
        for (i = k; i < m; i++) {
            long ind = 0;
            s = b + ln[i].a;
            e = ln[i].n;
            for (j = 0; j < e && (s[j] == F_INDENT || s[j] == ' ' || s[j] == F_VAR); j++) ind++;
            if (i == k) ind1 = ind; else if (i == k + 1) ind2 = ind;
            /* the gap at a soft line end, unless the word was hyphenated */
            if (i > k && d->len > pstart && d->text[d->len - 1].ch != ' ' &&
                !(ln[i - 1].n > 0 && b[ln[i - 1].a + ln[i - 1].n - 1] == F_SHY)) {
                f.c = f.body; fb_put(&f, ' ');
            }
            for (; j < e; j++) {
                unsigned char ch = s[j];
                if (ch == F_ESC && j + 1 < e) { f.attr = fwp_attr(s[++j] & 0x7F); continue; }
                if (ch == F_CPB) { j++; continue; }
                if (ch == F_FOOT) { j++; while (j < e && s[j] != F_FOOT) j++; continue; }
                if (ch == F_SHY) continue;
                if (is_sp(ch)) {
                    if (d->len > pstart && d->text[d->len - 1].ch == ' ') continue;
                    ch = ' ';
                } else if (ch < 32 && ch != '\t') continue;
                fb_put(&f, ch);
                tlen++;
            }
        }
        while (d->len > pstart && d->text[d->len - 1].ch == ' ') { doc_delete(d, d->len - 1, 1); tlen--; }

        /* the paragraph's format from its indents */
        p = cur;
        if (ind2 < 0) ind2 = ind1;
        p.left = (short)ch2pt(ind2);
        p.first = (short)ch2pt(ind1 - ind2);
        p.just = (unsigned char)(justify && m - k > 1 ? J_FULL : J_LEFT);
        if (m - k == 1 && ind1 >= 4 && tlen > 0) {
            long gap = width - tlen;
            if (ind1 + tlen >= width - 1 && ind1 + tlen <= width) { p.just = J_RIGHT; p.left = p.first = 0; }
            else if (gap > 0 && ind1 - gap / 2 <= 1 && gap / 2 - ind1 <= 1) { p.just = J_CENTRE; p.left = p.first = 0; }
        }
        if (p.left > 400) p.left = 400;
        if (p.first > 400) p.first = 400;
        if (p.first < -p.left) p.first = (short)-p.left;
        pi = para_find(d, &p);
        for (q = pstart; q < d->len; q++) d->text[q].para = (unsigned char)pi;
        f.body.para = (unsigned char)pi;
    }
    free(ln);
    free(b);
    doc_norm_para(d);
    d->dirty = 0;
    return 1;
}

/* ================================================================= */
/*  Export                                                            */
/* ================================================================= */

static void out_hf(FILE *f, DOC *d, int which)
{
    int slot, i;
    const char *s = HF(d).text[which];
    fputc(F_INFO, f);
    fputc('1' + which, f);
    slot = HF(d).just[which] == J_LEFT ? 0 : HF(d).just[which] == J_RIGHT ? 2 : 1;
    for (i = 0; i < 3; i++) {
        if (i) fputc(F_INFO, f);
        if (i != slot) continue;
        while (*s) {
            if (!strncmp(s, "{page}", 6)) { fputc('#', f); s += 6; continue; }
            if (!strncmp(s, "{date}", 6)) { fputc('$', f); s += 6; continue; }
            if (*s == '{') { const char *e = strchr(s, '}'); if (e) { s = e + 1; continue; } }
            fputc((unsigned char)*s++, f);
        }
    }
    fputc(F_INFO, f);
    fputc(HF(d).first ? '1' : '0', f);
    fputs("\r\n", f);
}

static void out_ruler(FILE *f, int width, int justify)
{
    int i;
    fputc(F_INFO, f);
    fputc('9', f);
    fputc('[', f);
    for (i = 1; i <= width; i++) fputc(i % 5 == 0 && i < width ? 0x7F : '.', f);
    fputc(']', f);
    fputs(justify ? "0111" : "0011", f);
    fputs("\r\n", f);
}

typedef struct { FILE *f; int attr; } FO;

static void set_style(FO *o, int attr)
{
    int b = 0;
    if (attr == o->attr) return;
    if (attr & A_BOLD) b |= 1;
    if (attr & A_ITALIC) b |= 4;
    if (attr & A_UNDER) b |= 8;
    fputc(F_ESC, o->f);
    fputc(0x80 | b, o->f);
    o->attr = attr;
}

/* one output line: cells [a, b) of the paragraph, ind indent spaces,
 * pad stretched spaces for justification, soft: wrapped */
static void out_line(FO *o, DOC *d, long a, long b, int ind, int pad, int soft, const char *mark)
{
    long k, gaps = 0, done = 0;
    int i;
    for (i = 0; i < ind; i++) fputc(F_INDENT, o->f);
    if (mark) fputs(mark, o->f);
    if (pad > 0) for (k = a; k < b; k++) if (d->text[k].ch == ' ') gaps++;
    for (k = a; k < b; k++) {
        unsigned char ch = d->text[k].ch;
        if (ch == CH_CELL || ch == '\t') ch = ' ';
        set_style(o, d->text[k].attr & (A_BOLD | A_ITALIC | A_UNDER));
        fputc(ch, o->f);
        if (ch == ' ' && pad > 0 && gaps) {
            long want = (long)pad * (done + 1) / gaps - (long)pad * done / gaps;
            while (want-- > 0) fputc(F_STRETCH, o->f);
            done++;
        }
    }
    if (o->attr) set_style(o, 0);
    if (soft) fputc(' ', o->f);
    fputs("\r\n", o->f);
}

/* a paragraph, wrapped to width characters */
static void out_para(FO *o, DOC *d, long a, long b, const PARA *p, int width, int num)
{
    int left = pt2ch(p->left), first = pt2ch(p->left + p->first), ind, avail, firstln = 1;
    char mark[12];
    const char *mk = NULL;
    while (b > a && d->text[b - 1].ch == ' ') b--;
    if (p->list) {
        if (p->list == LIST_BULLET) sprintf(mark, "%c ", 0xF9);
        else sprintf(mark, "%d. ", num);
        mk = mark;
        if (!p->left && !p->first) { first = 0; left = (int)strlen(mark); }
    }
    if (left < 0) left = 0;
    if (first < 0) first = 0;
    if (a == b) { fputs("\r\n", o->f); return; }
    while (a < b) {
        long e = a, brk = -1;
        int len = 0, room;
        ind = firstln ? first : left;
        avail = width - ind - (firstln && mk ? (int)strlen(mk) : 0);
        if (avail < 10) avail = 10;
        while (e < b && len < avail) { if (d->text[e].ch == ' ') brk = e; e++; len++; }
        if (e < b) {
            if (d->text[e].ch == ' ') brk = e;
            if (brk > a) e = brk;
        }
        room = 0;
        if (p->just == J_CENTRE || p->just == J_RIGHT) {
            int w = (int)(e - a);
            room = avail - w;
            if (room < 0) room = 0;
            ind += p->just == J_CENTRE ? room / 2 : room;
            room = 0;
        } else if (p->just == J_FULL && e < b) room = avail - (int)(e - a);
        out_line(o, d, a, e, ind, room, e < b, firstln ? mk : NULL);
        firstln = 0;
        a = e;
        while (a < b && d->text[a].ch == ' ') a++;
    }
}

int fwp_export(DOC *d, const char *path)
{
    FILE *f = fopen(path, "wb");
    PGFMT pf;
    FO o;
    long pos = 0, tw;
    int width, err, lines, top, bot, justify = 0, num = 0;
    if (!f) return 0;
    page_fmt(d, 0, &pf);
    tw = (long)PS(d).w - pf.ml - pf.mr;
    width = pt2ch(tw);
    if (width < 20) width = 20;
    if (width > 250) width = 250;
    lines = PS(d).h / 12; top = pf.mt / 12; bot = pf.mb / 12;
    if (lines > 99) lines = 99;
    if (top > 30) top = 30;
    if (bot > 30) bot = 30;
    /* paper: length, top, header, footer, bottom margins in lines; then
     * the same in half lines (1st Word Plus 3) */
    fprintf(f, "%c0%02d%02d%02d%02d%02d0000%03d%03d%03d%03d%03d000\r\n", F_INFO,
            lines, top, 3, 3, bot, lines * 2, top * 2, 6, 6, bot * 2);
    out_hf(f, d, 0);
    out_hf(f, d, 1);
    out_ruler(f, width, 0);
    fprintf(f, "%cP0001\r\n", F_INFO);
    o.f = f;
    o.attr = 0;
    while (pos < d->len) {
        long e = doc_para_end(d, pos);
        unsigned char brk = e < d->len ? d->text[e].ch : 0;
        const PARA *p = PARA_OF(d, &d->text[e < d->len ? e : e - 1]);
        int full = p->just == J_FULL && !p->tbl;
        PROG(pos, d->len);
        if (full != justify) { justify = full; out_ruler(f, width, justify); }
        num = p->list == LIST_NUMBER ? num + 1 : 0;
        if (p->tbl && p->tbl <= d->a.ntbl) {
            /* a table row: each cell in its column, one line */
            TBL *t = &d->a.tbls[p->tbl - 1];
            long c0 = pos, c;
            int col = 0, at = 0, i;
            for (c = pos; c <= e; c++)
                if (c == e || d->text[c].ch == CH_CELL) {
                    int cw = col < t->ncols ? pt2ch(t->w[col]) : 10, w = 0;
                    long q;
                    for (q = c0; q < c && w < cw - 1; q++, w++) {
                        set_style(&o, d->text[q].attr & (A_BOLD | A_ITALIC | A_UNDER));
                        fputc(d->text[q].ch, f);
                    }
                    if (o.attr) set_style(&o, 0);
                    at += w;
                    if (c < e) for (i = w; i < cw; i++, at++) fputc(' ', f);
                    col++;
                    c0 = c + 1;
                }
            fputs("\r\n", f);
        } else out_para(&o, d, pos, e, p, width, num);
        if (brk == CH_PAGE) fputs("\x0C\r\n", f);
        pos = e + 1;
    }
    err = ferror(f);
    fclose(f);
    return !err;
}
