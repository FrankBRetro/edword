/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * protext.c - Protext documents (Arnor; CPC, PCW, ST, Amiga, PC)
 *
 * A Protext file is text in lines. CR LF is a hard return (the end of a
 * paragraph); CR 0x8A is a soft return made by word wrap. Bytes 0xE0 to
 * 0xFA are printer controls @ and a-z that switch a style on and off:
 * 0xE2 b bold, 0xE9 i italics, 0xF5 u underline, 0xEE n normal, 0xE0 @
 * reset. 0x90 is a soft space added by justification, 0x91 a non-break
 * space, 0x8E a soft hyphen; 0x80-0x8C are markers. A line starting with
 * '>' is a stored command (">HE text" header, ">FO text" footer, with %
 * for the page number; ">PA" new page; ">CE text" a centred line; ">LS n"
 * line spacing; ">CO" or "> " a comment) or a ruler: '-' with '!' for a
 * tab, 'L' the left and 'R' the right margin, '>' being column 1.
 * CPC files may start with a 128-byte AMSDOS header and end with 0x1A.
 *
 * Protext sets text in a fixed pitch: pica, ten characters to the inch.
 * Characters above 0x91 that aren't controls are read as ST characters;
 * on export, accented letters whose codes Protext uses for controls are
 * written without their accents.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

#define P_SOFTLF  0x8A
#define P_SHY     0x8E
#define P_SSPACE  0x90
#define P_NBSP    0x91
#define P_RESET   0xE0
#define P_BOLD    0xE2
#define P_ITALIC  0xE9
#define P_NORMAL  0xEE
#define P_UNDER   0xF5

static int ch2pt(long n) { return (int)((n * 72 + 5) / 10); }
static int pt2ch(long p) { return (int)((p * 10 + (p >= 0 ? 36 : -36)) / 72); }

static int is_ctl(unsigned char c) { return (c >= 0x80 && c <= 0x91) || (c >= 0xE0 && c <= 0xFA); }

/* a CPC AMSDOS header: the checksum of bytes 0-66 is in 67-68 */
static int amsdos(const unsigned char *b, long n)
{
    unsigned sum = 0;
    int i;
    if (n < 128) return 0;
    for (i = 0; i < 67; i++) sum += b[i];
    return sum && (sum & 0xFFFF) == (unsigned)(b[67] | (b[68] << 8));
}

/* a ruler line: '>' then '-', '!', '.', 'L', 'R', with an R */
static int is_ruler(const unsigned char *s, long n)
{
    long i;
    int r = 0, dash = 0;
    if (n < 3 || s[0] != '>') return 0;
    for (i = 1; i < n; i++) {
        if (s[i] == 'R') r++;
        else if (s[i] == '-') dash++;
        else if (s[i] != '!' && s[i] != '.' && s[i] != 'L' && s[i] != ' ' && s[i] != '=') return 0;
    }
    return r == 1 && dash > 0;
}

int pt_detect(const unsigned char *b, long n)
{
    long i, soft = 0, e = 0;
    if (amsdos(b, n)) return 1;
    for (i = 0; i < n && b[i] != '\r' && b[i] != '\n'; i++) ;
    if (is_ruler(b, i)) return 1;
    for (i = 0; i + 1 < n && i < 8000; i++)
        if (b[i] == '\r' && b[i + 1] == P_SOFTLF) soft++;
        else if (b[i] == P_BOLD || b[i] == P_ITALIC || b[i] == P_UNDER) e++;
    return soft > 0 || (e >= 2 && !(e & 1) && n > 0 && b[0] == '>');
}

/* ================================================================= */
/*  Import                                                            */
/* ================================================================= */

typedef struct { long a, n; int soft; } PLINE;

typedef struct {
    DOC  *d;
    CELL  c;
    int   attr;
} PB;

static void pb_put(PB *f, unsigned char ch)
{
    f->c.ch = ch;
    f->c.attr = (unsigned char)(ch == '\n' || ch == CH_PAGE ? 0 : f->attr);
    doc_insert(f->d, f->d->len, &f->c, 1);
}

static void read_hf(DOC *d, int which, const unsigned char *s, long n)
{
    char *out = HF(d).text[which];
    long i = 0, lead, o = 0;
    while (i < n && s[i] == ' ') i++;
    lead = i;
    if (n - i == 2 && (s[i] == 'O' || s[i] == 'o') && (s[i + 1] == 'N' || s[i + 1] == 'n')) return;
    if (n - i == 3 && (s[i] == 'O' || s[i] == 'o') && (s[i + 1] == 'F' || s[i + 1] == 'f')) return;
    for (; i < n && o < 74; i++) {
        unsigned char ch = s[i];
        if (ch == '%') { strcpy(out + o, "{page}"); o += 6; }
        else if (ch == P_NBSP) out[o++] = ' ';
        else if (ch >= 32 && !is_ctl(ch)) out[o++] = (char)ch;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
    HF(d).just[which] = (unsigned char)(lead >= 16 ? J_CENTRE : J_LEFT);
}

/* the command's two letters, upper case, and where its text starts */
static int cmd_is(const unsigned char *s, long n, const char *name, long *arg)
{
    long i;
    if (n < 3) return 0;
    if ((s[1] & 0xDF) != name[0] || (s[2] & 0xDF) != name[1]) return 0;
    if (n > 3 && s[3] != ' ' && s[3] != '\t') return 0;
    i = 3;
    if (i < n && s[i] == ' ') i++;
    *arg = i;
    return 1;
}

int pt_import(DOC *d, const char *path)
{
    long n, i, k, m, nl = 0, start = 0, arg;
    unsigned char *b = load_file(path, &n);
    PLINE *ln;
    PB f;
    PARA cur;
    int need_term = 0, left = 0, spacing = LS_SINGLE;

    if (!b) return 0;
    if (amsdos(b, n)) start = 128;
    for (i = start; i < n; i++) if (b[i] == 0x1A) { n = i; break; }
    for (i = start; i < n; i++) if (b[i] == '\n' || b[i] == '\r' || b[i] == P_SOFTLF) nl++;
    ln = (PLINE *)malloc((nl + 2) * sizeof(PLINE));
    if (!ln) { free(b); return 0; }
    memset(&f, 0, sizeof f);
    f.d = d;
    f.c = d->ins;
    f.c.attr = 0;
    para_default(&cur);

    /* split into lines; CR 0x8A (or a lone 0x8A) is soft */
    nl = 0;
    for (i = start; i < n; ) {
        long a = i, e;
        int soft = 0;
        while (i < n && b[i] != '\r' && b[i] != '\n' && b[i] != P_SOFTLF) i++;
        e = i;
        if (i < n && b[i] == '\r') {
            i++;
            if (i < n && b[i] == P_SOFTLF) { soft = 1; i++; }
            else if (i < n && b[i] == '\n') i++;
        } else if (i < n && b[i] == P_SOFTLF) { soft = 1; i++; }
        else if (i < n) i++;
        ln[nl].a = a; ln[nl].n = e - a; ln[nl].soft = soft;
        nl++;
    }

    for (k = 0; k < nl; k = m) {
        unsigned char *s = b + ln[k].a;
        long len = ln[k].n, j, pstart, q, lead = -1;
        int centre = 0, justify = 0, pi;
        PARA p;
        PROG(k, nl);

        m = k + 1;
        if (len > 0 && s[0] == '>') {
            if (is_ruler(s, len)) {
                for (j = 1; j < len && s[j] != 'L'; j++) ;
                left = j < len ? (int)j : 0;
                continue;
            }
            if (cmd_is(s, len, "HE", &arg) || cmd_is(s, len, "OH", &arg)) { read_hf(d, 0, s + arg, len - arg); continue; }
            if (cmd_is(s, len, "FO", &arg) || cmd_is(s, len, "OF", &arg)) { read_hf(d, 1, s + arg, len - arg); continue; }
            if (cmd_is(s, len, "PA", &arg) || cmd_is(s, len, "EP", &arg) || cmd_is(s, len, "OP", &arg)) {
                if (d->len > 0 && d->text[d->len - 1].ch == '\n') doc_delete(d, d->len - 1, 1);
                pb_put(&f, CH_PAGE);
                need_term = 0;
                continue;
            }
            if (cmd_is(s, len, "LS", &arg)) {
                int v = arg < len ? atoi((const char *)s + arg) : 1;
                spacing = v >= 2 ? LS_DOUBLE : LS_SINGLE;
                continue;
            }
            if (cmd_is(s, len, "CE", &arg)) { centre = 1; s += arg; len -= arg; }
            else continue;                          /* other commands, comments */
        }
        if (len > 0 && s[0] == 0x0C) {
            pb_put(&f, CH_PAGE);
            need_term = 0;
            continue;
        }
        /* the paragraph: this line and the soft lines after it */
        if (!centre)
            while (ln[m - 1].soft && m < nl && !(ln[m].n > 0 && b[ln[m].a] == '>')) m++;
        if (need_term) pb_put(&f, '\n');
        need_term = 1;
        pstart = d->len;
        for (i = k; i < m; i++) {
            const unsigned char *t = i == k ? s : b + ln[i].a;
            long e = i == k ? len : ln[i].n;
            j = 0;
            if (i == k) {
                while (j < e && (t[j] == ' ' || t[j] == P_SSPACE)) j++;
                lead = j;
            }
            /* the gap at a soft line end, unless the word was hyphenated */
            if (i > k && d->len > pstart && d->text[d->len - 1].ch != ' ' &&
                !(ln[i - 1].n > 0 && b[ln[i - 1].a + ln[i - 1].n - 1] == P_SHY) &&
                !(e > 0 && t[0] == ' '))
                pb_put(&f, ' ');
            for (; j < e; j++) {
                unsigned char ch = t[j];
                if (ch >= 0xE0 && ch <= 0xFA) {
                    if (ch == P_BOLD) f.attr ^= A_BOLD;
                    else if (ch == P_ITALIC) f.attr ^= A_ITALIC;
                    else if (ch == P_UNDER) f.attr ^= A_UNDER;
                    else if (ch == P_NORMAL || ch == P_RESET) f.attr = 0;
                    continue;
                }
                if (ch == P_SSPACE) { justify = 1; continue; }
                if (ch == P_NBSP) ch = ' ';
                else if (ch >= 0x80 && ch <= 0x8F) continue;
                else if (ch < 32 && ch != '\t') continue;
                if (ch == ' ' && d->len > pstart && d->text[d->len - 1].ch == ' ' && i > k && j == 0) continue;
                pb_put(&f, ch);
            }
        }
        while (d->len > pstart && d->text[d->len - 1].ch == ' ') doc_delete(d, d->len - 1, 1);

        p = cur;
        p.spacing = (unsigned char)spacing;
        p.left = (short)ch2pt(left);
        p.first = (short)(centre ? 0 : ch2pt(lead > 0 ? lead : 0));
        p.just = (unsigned char)(centre ? J_CENTRE : justify ? J_FULL : J_LEFT);
        if (centre) p.left = 0;
        if (p.left > 400) p.left = 400;
        if (p.first > 400) p.first = 400;
        pi = para_find(d, &p);
        for (q = pstart; q < d->len; q++) d->text[q].para = (unsigned char)pi;
        f.c.para = (unsigned char)pi;
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

/* an ST character for Protext: codes it uses for controls lose accents */
static unsigned char pt_char(unsigned char ch)
{
    static const char base[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    long u;
    if (!is_ctl(ch)) return ch;
    u = st_to_uni(ch);
    if (u >= 0xC0 && u <= 0xFF) return (unsigned char)base[u - 0xC0];
    if (u == 0xDF) return 's';
    return '?';
}

typedef struct { FILE *f; int attr; } PO;

static void set_style(PO *o, int attr)
{
    int x = attr ^ o->attr;
    if (x & A_BOLD) fputc(P_BOLD, o->f);
    if (x & A_ITALIC) fputc(P_ITALIC, o->f);
    if (x & A_UNDER) fputc(P_UNDER, o->f);
    o->attr = attr;
}

static void out_hf(FILE *f, DOC *d, int which, int width)
{
    char line[200];
    const char *s = HF(d).text[which];
    int o = 0, pad = 0, i;
    if (!s[0]) return;
    while (*s && o < 150) {
        if (!strncmp(s, "{page}", 6)) { line[o++] = '%'; s += 6; continue; }
        if (*s == '{') { const char *e = strchr(s, '}'); if (e) { s = e + 1; continue; } }
        line[o++] = (char)pt_char((unsigned char)*s++);
    }
    line[o] = 0;
    /* the text starts in column 5, after ">HE " */
    if (HF(d).just[which] == J_CENTRE) pad = (width - o) / 2 - 4;
    else if (HF(d).just[which] == J_RIGHT) pad = width - o - 4;
    fputs(which ? ">FO " : ">HE ", f);
    for (i = 0; i < pad; i++) fputc(' ', f);
    fputs(line, f);
    fputs("\r\n", f);
}

/* a ruler: '>' in column 1, L at column left + 1, R at column width */
static void out_ruler(FILE *f, int width, int left)
{
    int c;
    for (c = 1; c <= width; c++) {
        char ch = c == 1 ? '>' : c == width ? 'R' : (c == left + 1 && left > 0) ? 'L' : (c % 5 == 1 ? '!' : '-');
        fputc(ch, f);
    }
    fputs("\r\n", f);
}

/* one line: cells [a, b); pad soft spaces for justification; soft:
 * wrapped (CR 0x8A), else a hard return */
static void out_line(PO *o, DOC *d, long a, long b, int pre, int pad, int soft, const char *lead)
{
    long k, gaps = 0, done = 0;
    int i;
    for (i = 0; i < pre; i++) fputc(' ', o->f);
    if (lead) fputs(lead, o->f);
    if (pad > 0) for (k = a; k < b; k++) if (d->text[k].ch == ' ') gaps++;
    for (k = a; k < b; k++) {
        unsigned char ch = d->text[k].ch;
        if (ch == CH_CELL) ch = '\t';
        set_style(o, d->text[k].attr & (A_BOLD | A_ITALIC | A_UNDER));
        if (ch == '\t' || ch >= 32) fputc(ch == '\t' ? ch : pt_char(ch), o->f);
        if (ch == ' ' && pad > 0 && gaps) {
            long want = (long)pad * (done + 1) / gaps - (long)pad * done / gaps;
            while (want-- > 0) fputc(P_SSPACE, o->f);
            done++;
        }
    }
    if (soft) fputs(" \r\x8A", o->f);
    else { if (o->attr) set_style(o, 0); fputs("\r\n", o->f); }
}

static void out_para(PO *o, DOC *d, long a, long b, const PARA *p, int width, int left, int num)
{
    int first = pt2ch(p->first), avail, firstln = 1;
    char mark[12];
    const char *mk = NULL;
    while (b > a && d->text[b - 1].ch == ' ') b--;
    if (p->list) {
        if (p->list == LIST_BULLET) strcpy(mark, "- ");
        else sprintf(mark, "%d. ", num);
        mk = mark;
    }
    if (first < 0) first = 0;
    if (p->just == J_CENTRE || p->just == J_RIGHT) first = 0;
    if (a == b) { fputs("\r\n", o->f); return; }
    while (a < b) {
        long e = a, brk = -1;
        int len = 0, pre = firstln ? first : 0, w, room = 0;
        avail = width - left - pre - (firstln && mk ? (int)strlen(mk) : 0);
        if (p->just == J_CENTRE) avail -= 3;
        if (avail < 10) avail = 10;
        while (e < b && len < avail) { if (d->text[e].ch == ' ') brk = e; e++; len++; }
        if (e < b) {
            if (d->text[e].ch == ' ') brk = e;
            if (brk > a) e = brk;
        }
        w = (int)(e - a);
        if (p->just == J_CENTRE) {
            fputs(">CE ", o->f);
            out_line(o, d, a, e, 0, 0, 0, firstln ? mk : NULL);
        } else if (p->just == J_RIGHT) {
            pre = avail - w;
            if (pre < 0) pre = 0;
            out_line(o, d, a, e, pre, 0, 0, firstln ? mk : NULL);
        } else {
            if (p->just == J_FULL && e < b) room = avail - w;
            out_line(o, d, a, e, pre, room, e < b, firstln ? mk : NULL);
        }
        firstln = 0;
        a = e;
        while (a < b && d->text[a].ch == ' ') a++;
    }
}

int pt_export(DOC *d, const char *path)
{
    FILE *f = fopen(path, "wb");
    PGFMT pf;
    PO o;
    long pos = 0;
    int width, err, left = -1, spacing = LS_SINGLE, num = 0;
    if (!f) return 0;
    page_fmt(d, 0, &pf);
    width = pt2ch((long)PS(d).w - pf.ml - pf.mr);
    if (width < 20) width = 20;
    if (width > 250) width = 250;
    out_hf(f, d, 0, width);
    out_hf(f, d, 1, width);
    o.f = f;
    o.attr = 0;
    while (pos < d->len) {
        long e = doc_para_end(d, pos);
        unsigned char brk = e < d->len ? d->text[e].ch : 0;
        const PARA *p = PARA_OF(d, &d->text[e < d->len ? e : e - 1]);
        int l = p->tbl ? 0 : pt2ch(p->left);
        PROG(pos, d->len);
        if (l < 0) l = 0;
        if (l > width - 20) l = width - 20;
        if (l != left) { left = l; out_ruler(f, width, left); }
        if (p->spacing != spacing) {
            spacing = p->spacing == LS_DOUBLE ? LS_DOUBLE : LS_SINGLE;
            fprintf(f, ">LS %d\r\n", spacing == LS_DOUBLE ? 2 : 1);
        }
        num = p->list == LIST_NUMBER ? num + 1 : 0;
        if (p->tbl) out_line(&o, d, pos, e, 0, 0, 0, NULL);   /* cells split by tabs */
        else out_para(&o, d, pos, e, p, width, left, num);
        if (brk == CH_PAGE) fputs(">PA\r\n", f);
        pos = e + 1;
    }
    err = ferror(f);
    fclose(f);
    return !err;
}

/* ================================================================= */
/*  Any format (used by every version's Open, Import and Export)      */
/* ================================================================= */

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext), i;
    if (n < m) return 0;
    for (i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != ext[i]) return 0;
    }
    return 1;
}

/* what a file is: RTF, Markdown, 1st Word Plus, Protext; else plain text
 * (text) or RTF, which also reads plain text */
int file_detect(const char *path, int text)
{
    long n;
    unsigned char *b;
    int t = text ? FT_TEXT : FT_RTF;
    if (ends_with(path, ".MD") || ends_with(path, ".MARKDOWN") || ends_with(path, ".MKD")) return FT_MD;
    b = load_file(path, &n);
    if (!b) return t;
    if (n >= 5 && !memcmp(b, "{\\rtf", 5)) t = FT_RTF;
    else if (fwp_detect(b, n)) t = FT_FWP;
    else if (pt_detect(b, n)) t = FT_PT;
    else if (is_utf16(b, n)) t = FT_TEXT;
    else if (ends_with(path, ".TXT") || ends_with(path, ".ASC") || ends_with(path, ".TEXT") || text) t = FT_TEXT;
    free(b);
    return t;
}

int doc_read(DOC *d, const char *path, int type, int cs, int join)
{
    if (type == FT_AUTO) type = file_detect(path, 1);
    switch (type) {
    case FT_TEXT: return text_import(d, path, cs, join);
    case FT_MD:   return md_import(d, path, cs);
    case FT_FWP:  return fwp_import(d, path);
    case FT_PT:   return pt_import(d, path);
    case FT_RTF:  return rtf_import(d, path);
    default:      return 0;
    }
}

int doc_write(DOC *d, const char *path, int type, int crlf, int cs)
{
    switch (type) {
    case FT_TEXT: return text_export(d, path, crlf, cs);
    case FT_MD:   return md_export(d, path, crlf, cs);
    case FT_FWP:  return fwp_export(d, path);
    case FT_PT:   return pt_export(d, path);
    case FT_RTF:  return rtf_export(d, path);
    default:      return 0;
    }
}
