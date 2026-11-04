/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * text.c - plain text import/export (any line endings, ST / Windows /
 *          UTF-8 character sets) and date formatting
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wp.h"

/* ---- progress ---------------------------------------------------- */

void (*prog_hook)(long done, long total);
long prog_next;
static int prog_lo, prog_hi = 1000, prog_last = -1, prog_split_at;

void prog_part(int lo, int hi)
{
    prog_lo = lo; prog_hi = hi;
    prog_next = 0;
    prog_split_at = 0;
}

void prog_split(int per_mille)
{
    prog_split_at = prog_lo + (int)((long)(prog_hi - prog_lo) * per_mille / 1000);
}

void prog(long done, long total)
{
    long d = done, t = total;
    int v, lo = prog_lo, hi = prog_split_at ? prog_split_at : prog_hi;
    if (!prog_hook || total <= 0) return;
    if (d > t) d = t;
    if (d < 0) d = 0;
    while (t > 1000000L) { t >>= 4; d >>= 4; }
    if (t < 1) t = 1;
    v = lo + (int)((long)(hi - lo) * d / t);
    /* the next call worth making: half a per mille of the bar later */
    prog_next = done + (hi > lo ? total / ((hi - lo) * 2L) : total) + 1;
    if (v == prog_last) return;
    prog_last = v;
    prog_hook(v, 1000);
}

/* a step has finished: the rest of the bar is for what comes next */
void prog_step_done(void)
{
    if (prog_split_at) { prog_lo = prog_split_at; prog_split_at = 0; }
    prog_next = 0;
}

/* the whole file in memory (one byte more, a 0 at the end); reading takes
 * the first quarter of the bar's part, what is done with it the rest */
unsigned char *load_file(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long got = 0;
    if (!f) return NULL;
    fseek(f, 0L, SEEK_END);
    *n = ftell(f);
    fseek(f, 0L, SEEK_SET);
    if (*n < 0) { fclose(f); return NULL; }
    buf = (unsigned char *)malloc(*n + 1);
    if (buf) {
        prog_split(250);
        while (got < *n) {
            long k = *n - got > 16384L ? 16384L : *n - got;
            if (fread(buf + got, 1, k, f) != (size_t)k) { free(buf); buf = NULL; break; }
            got += k;
            PROG(got, *n);
        }
        prog_step_done();
    }
    fclose(f);
    if (buf) buf[*n] = 0;
    return buf;
}

/* length of a valid UTF-8 sequence at b, or 0 */
static int utf8_len(const unsigned char *b, long n, long *u)
{
    int len, i;
    long v;
    if (b[0] < 0x80) { *u = b[0]; return 1; }
    if ((b[0] & 0xE0) == 0xC0) { len = 2; v = b[0] & 0x1F; }
    else if ((b[0] & 0xF0) == 0xE0) { len = 3; v = b[0] & 0x0F; }
    else if ((b[0] & 0xF8) == 0xF0) { len = 4; v = b[0] & 0x07; }
    else return 0;
    if (n < len) return 0;
    for (i = 1; i < len; i++) {
        if ((b[i] & 0xC0) != 0x80) return 0;
        v = (v << 6) | (b[i] & 0x3F);
    }
    if (v < 0x80) return 0;
    *u = v;
    return len;
}


/* ---- UTF-16 ----------------------------------------------------------------- */

/* 1 little endian, 2 big endian (a byte order mark, or zero bytes in every
 * other place, as plain text in UTF-16 has), 0 not UTF-16 */
static int utf16_kind(const unsigned char *b, long n)
{
    long i, z0 = 0, z1 = 0, m;
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) return 1;
    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) return 2;
    if (n < 4 || (n & 1)) return 0;
    m = n > 4000 ? 4000 : n;
    for (i = 0; i + 1 < m; i += 2) { if (!b[i]) z0++; if (!b[i + 1]) z1++; }
    if (z1 * 10 >= m / 2 * 7 && z0 * 10 < m / 2) return 1;
    if (z0 * 10 >= m / 2 * 7 && z1 * 10 < m / 2) return 2;
    return 0;
}

int is_utf16(const unsigned char *b, long n) { return utf16_kind(b, n) != 0; }

static long put8(unsigned char *o, long u)
{
    if (u < 0x80) { o[0] = (unsigned char)u; return 1; }
    if (u < 0x800) { o[0] = (unsigned char)(0xC0 | (u >> 6)); o[1] = (unsigned char)(0x80 | (u & 0x3F)); return 2; }
    if (u < 0x10000L) {
        o[0] = (unsigned char)(0xE0 | (u >> 12)); o[1] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
        o[2] = (unsigned char)(0x80 | (u & 0x3F)); return 3;
    }
    o[0] = (unsigned char)(0xF0 | (u >> 18)); o[1] = (unsigned char)(0x80 | ((u >> 12) & 0x3F));
    o[2] = (unsigned char)(0x80 | ((u >> 6) & 0x3F)); o[3] = (unsigned char)(0x80 | (u & 0x3F));
    return 4;
}

unsigned char *utf16_to_utf8(const unsigned char *b, long n, long *out, int force)
{
    int k = utf16_kind(b, n);
    long i = 0, o = 0;
    unsigned char *r;
    if (!k && !force) return NULL;
    if (!k) k = 1;
    if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) i = 2;
    r = (unsigned char *)malloc((size_t)(n / 2 * 3 + 4));
    if (!r) return NULL;
    for (; i + 1 < n; i += 2) {
        long u = k == 1 ? b[i] | (b[i + 1] << 8) : (b[i] << 8) | b[i + 1];
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {          /* a surrogate pair */
            long v = k == 1 ? b[i + 2] | (b[i + 3] << 8) : (b[i + 2] << 8) | b[i + 3];
            if (v >= 0xDC00 && v < 0xE000) { u = 0x10000L + ((u - 0xD800) << 10) + (v - 0xDC00); i += 2; }
        }
        o += put8(r + o, u);
    }
    r[o] = 0;
    *out = o;
    return r;
}

/* one ST character in the character set cs */
void put_cs(FILE *f, unsigned char ch, int cs)
{
    long u;
    unsigned char t[4];
    int n, i;
    switch (cs) {
    case CS_WIN: fputc(st_to_win(ch), f); return;
    case CS_UTF8:
        if (ch < 0x80) { fputc(ch, f); return; }
        n = (int)put8(t, st_to_uni(ch));
        for (i = 0; i < n; i++) fputc(t[i], f);
        return;
    case CS_UTF16:
        u = ch < 0x80 ? ch : st_to_uni(ch);
        fputc((int)(u & 0xFF), f);
        fputc((int)((u >> 8) & 0xFF), f);
        return;
    default: fputc(ch, f);
    }
}

/* a file written as UTF-8 with LF line ends, again in cs (UTF-16: little
 * endian with a byte order mark), with CR LF if crlf */
int file_transcode(const char *path, int cs, int crlf)
{
    long n, i, u;
    unsigned char *b = load_file(path, &n);
    FILE *f;
    int err;
    if (!b) return 0;
    f = fopen(path, "wb");
    if (!f) { free(b); return 0; }
    if (cs == CS_UTF16) { fputc(0xFF, f); fputc(0xFE, f); }
    for (i = 0; i < n; ) {
        int l = utf8_len(b + i, n - i, &u);
        if (l < 1) { u = b[i]; l = 1; }
        i += l;
        if (u == '\n' && crlf) {
            if (cs == CS_UTF16) { fputc('\r', f); fputc(0, f); } else fputc('\r', f);
        }
        if (cs == CS_UTF8) { unsigned char t[4]; int k, m = (int)put8(t, u); for (k = 0; k < m; k++) fputc(t[k], f); }
        else if (cs == CS_UTF16) {
            if (u >= 0x10000L) {
                long v = u - 0x10000L, hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
                fputc((int)(hi & 0xFF), f); fputc((int)(hi >> 8), f);
                fputc((int)(lo & 0xFF), f); fputc((int)(lo >> 8), f);
            } else { fputc((int)(u & 0xFF), f); fputc((int)(u >> 8), f); }
        } else {
            unsigned char c = u < 0x80 ? (unsigned char)u : uni_to_st(u);
            fputc(cs == CS_WIN ? st_to_win(c) : c, f);
        }
    }
    free(b);
    err = ferror(f);
    return fclose(f) == 0 && !err;
}

void text_detect(const unsigned char *b, long n, int *eol, int *cs)
{
    long i, crlf = 0, lf = 0, cr = 0, hi = 0, bad = 0, c1 = 0, c2 = 0, u;
    int kinds;
    if (utf16_kind(b, n)) {                 /* its line ends, from the UTF-8 */
        long m;
        unsigned char *c = utf16_to_utf8(b, n, &m, 0);
        if (c) { text_detect(c, m, eol, cs); free(c); }
        *cs = CS_UTF16;
        return;
    }
    for (i = 0; i < n; i++) {
        if (b[i] == '\r') { if (i + 1 < n && b[i + 1] == '\n') { crlf++; i++; } else cr++; }
        else if (b[i] == '\n') lf++;
        else if (b[i] >= 0x80) {
            int l = utf8_len(b + i, n - i, &u);
            hi++;
            if (b[i] < 0xA0) c1++; else c2++;
            if (l > 1) i += l - 1; else bad++;
        }
    }
    kinds = (crlf > 0) + (lf > 0) + (cr > 0);
    *eol = kinds > 1 ? EOL_MIXED : crlf ? EOL_CRLF : lf ? EOL_LF : cr ? EOL_CR : EOL_NONE;
    if (!hi) *cs = CS_ATARI;
    else if (!bad) *cs = CS_UTF8;
    else *cs = c1 > c2 ? CS_ATARI : CS_WIN;   /* ST accents sit at 0x80-0x9F */
}

static void put(DOC *d, CELL *c, unsigned char ch)
{
    c->ch = ch;
    doc_insert(d, d->len, c, 1);
}

/* join: single line breaks become spaces, blank lines end paragraphs */
int text_import_buf(DOC *d, const unsigned char *b, long n, int cs, int join)
{
    int eol, det;
    long i = 0, nl = 0, k;
    CELL c = d->ins;

    unsigned char *u16 = NULL;
    text_detect(b, n, &eol, &det);
    if (cs == CS_AUTO) cs = det;
    if (cs == CS_UTF16 && (u16 = utf16_to_utf8(b, n, &n, 1)) != NULL) { b = u16; cs = CS_UTF8; }
    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { i = 3; cs = CS_UTF8; }
    while (i < n) {
        unsigned ch = b[i++];
        long u;
        PROG(i, n);
        if (ch == '\r') { if (i < n && b[i] == '\n') i++; ch = '\n'; }
        if (ch == '\n') { nl++; continue; }
        if (nl) {
            if (!join) for (k = 0; k < nl; k++) put(d, &c, '\n');
            else if (nl == 1) { if (d->len && d->text[d->len - 1].ch != ' ') put(d, &c, ' '); }
            else for (k = 1; k < nl; k++) put(d, &c, '\n');
            nl = 0;
        }
        if (ch == 0x0C) { put(d, &c, CH_PAGE); continue; }
        if (ch < 32 && ch != '\t') continue;
        if (ch >= 0x80) {
            if (cs == CS_UTF8) {
                int l = utf8_len(b + i - 1, n - i + 1, &u);
                if (l > 1) { i += l - 1; ch = uni_to_st(u); }
                else ch = win_to_st((unsigned char)ch);
            } else if (cs == CS_WIN) ch = win_to_st((unsigned char)ch);
        }
        put(d, &c, (unsigned char)ch);
    }
    /* the final line terminator does not start a new paragraph */
    if (!join) for (k = 1; k < nl; k++) put(d, &c, '\n');
    free(u16);
    doc_norm_para(d);
    d->dirty = 0;
    return 1;
}

/* convert a buffer to the ST character set (cs: CS_AUTO detects); CR LF
 * and CR become LF. out needs room for n + 1 bytes. Returns the length. */
long text_to_st(const unsigned char *b, long n, int cs, unsigned char *out)
{
    long i = 0, k = 0, u;
    int eol, det;
    unsigned char *u16 = NULL;
    text_detect(b, n, &eol, &det);
    if (cs == CS_AUTO) cs = det;
    if (cs == CS_UTF16 && (u16 = utf16_to_utf8(b, n, &n, 1)) != NULL) { b = u16; cs = CS_UTF8; }
    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { i = 3; cs = CS_UTF8; }
    while (i < n) {
        unsigned ch = b[i++];
        if (ch == '\r') { if (i < n && b[i] == '\n') i++; ch = '\n'; }
        else if (ch >= 0x80) {
            if (cs == CS_UTF8) {
                int l = utf8_len(b + i - 1, n - i + 1, &u);
                if (l > 1) { i += l - 1; ch = uni_to_st(u); }
                else ch = win_to_st((unsigned char)ch);
            } else if (cs == CS_WIN) ch = win_to_st((unsigned char)ch);
        }
        out[k++] = (unsigned char)ch;
    }
    out[k] = 0;
    free(u16);
    return k;
}

/* one ST character to a file as UTF-8 */
void put_utf8(FILE *f, unsigned char ch)
{
    long u = st_to_uni(ch);
    if (u < 0x80) fputc((int)u, f);
    else if (u < 0x800) { fputc(0xC0 | (int)(u >> 6), f); fputc(0x80 | (int)(u & 0x3F), f); }
    else {
        fputc(0xE0 | (int)(u >> 12), f);
        fputc(0x80 | (int)((u >> 6) & 0x3F), f);
        fputc(0x80 | (int)(u & 0x3F), f);
    }
}

int text_import(DOC *d, const char *path, int cs, int join)
{
    long n;
    int ok;
    unsigned char *b = load_file(path, &n);
    if (!b) return 0;
    ok = text_import_buf(d, b, n, cs, join);
    free(b);
    return ok;
}

static void put_char(FILE *f, unsigned char ch, int cs) { put_cs(f, ch, cs); }

int text_export(DOC *d, const char *path, int crlf, int cs)
{
    FILE *f = fopen(path, "wb");
    const char *eol = crlf ? "\r\n" : "\n";
    long li, k;
    int err, wc = PS(d).wrapcol > 0;

    if (!f) return 0;
    if (cs == CS_UTF16) { fputc(0xFF, f); fputc(0xFE, f); }     /* the byte order mark */
    prog_split(300);
    doc_layout(d);
    prog_step_done();
    for (li = 0; li < d->nlines; li++) {
        LINE *ln = &d->lines[li];
        long e = ln->start + ln->len;
        int brk = 0;
        PROG(li, d->nlines);
        if (e > ln->start && ISEND(d->text[e - 1].ch)) { brk = d->text[e - 1].ch; e--; }
        if (wc && !brk) while (e > ln->start && d->text[e - 1].ch == ' ') e--;
        if (ln->mark) {
            char m[12];
            if (ln->mark < 0) { put_char(f, 0xF9, cs); put_char(f, ' ', cs); }
            else {
                doc_marker(d, li, m);
                for (k = 0; m[k]; k++) put_char(f, (unsigned char)m[k], cs);
                put_char(f, ' ', cs);
            }
        }
        for (k = ln->start; k < e; k++) put_char(f, d->text[k].ch == CH_CELL ? '\t' : d->text[k].ch, cs);
        if (brk == CH_PAGE) put_char(f, '\f', cs);
        else if (brk == CH_CELL) put_char(f, '\t', cs);
        else if (brk == '\n' || brk == CH_COLBRK || (wc && li < d->nlines - 1 && !(ln->flags & LF_CELL)))
            for (k = 0; eol[k]; k++) put_char(f, (unsigned char)eol[k], cs);
    }
    err = ferror(f);
    fclose(f);
    return !err;
}

/* ---- dates --------------------------------------------------------- */

static const char *months_en[12] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};
static const char *days_en[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
/* the names dates use; the front end points these at the UI language */
const char **date_months = months_en, **date_days = days_en;

static int weekday(int y, int m, int d)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void fmt_date(char *out, int style, int y, int m, int day, int hh, int mm)
{
    if (m < 1 || m > 12) m = 1;
    switch (style) {
    case DT_LONG:    sprintf(out, "%d %s %d", day, date_months[m - 1], y); break;
    case DT_DAYLONG: sprintf(out, "%s %d %s %d", date_days[weekday(y, m, day)], day, date_months[m - 1], y); break;
    case DT_ISO:     sprintf(out, "%04d-%02d-%02d", y, m, day); break;
    case DT_TIME:    sprintf(out, "%02d:%02d", hh, mm); break;
    case DT_US:      sprintf(out, "%02d/%02d/%04d", m, day, y); break;
    default:         sprintf(out, "%02d/%02d/%04d", day, m, y); break;
    }
}

void fmt_now(char *out, int style)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm) { strcpy(out, "?"); return; }
    fmt_date(out, style, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
}

/* ---- headers and footers ------------------------------------------- */

int hf_visible(DOC *d, int which, int page)
{
    return HF(d).text[which][0] && (page > 0 || HF(d).first);
}

/* fill in {page} {pages} {date} {time} {file} */
void hf_expand(DOC *d, int which, int page, int npages, int datefmt, char *out, int max)
{
    const char *s = HF(d).text[which];
    int n = 0;
    char tmp[48];

    while (*s && n < max - 1) {
        if (*s == '{') {
            const char *e = strchr(s, '}');
            int len = e ? (int)(e - s - 1) : 0;
            tmp[0] = 0;
            if (e && len == 4 && !strncmp(s + 1, "page", 4)) sprintf(tmp, "%d", page + 1);
            else if (e && len == 5 && !strncmp(s + 1, "pages", 5)) sprintf(tmp, "%d", npages);
            else if (e && len == 4 && !strncmp(s + 1, "date", 4)) fmt_now(tmp, datefmt);
            else if (e && len == 4 && !strncmp(s + 1, "time", 4)) fmt_now(tmp, DT_TIME);
            else if (e && len == 4 && !strncmp(s + 1, "file", 4)) sprintf(tmp, "%.40s", d->base);
            else e = NULL;
            if (e) {
                int k;
                for (k = 0; tmp[k] && n < max - 1; k++) out[n++] = tmp[k];
                s = e + 1;
                continue;
            }
        }
        out[n++] = *s++;
    }
    out[n] = 0;
}
