/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* hosttest.c - exercise the portable core on a PC with fake font metrics
 * usage: hosttest sample.rtf outdir dictfile thesaurus */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

static METRICS cache[16];
static int ckey[16], nc, fails, checks;

#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

const METRICS *get_metrics(int font, int size, int attr)
{
    int k = (font << 16) | (size << 8) | (attr & 3), i, c;
    for (i = 0; i < nc; i++) if (ckey[i] == k) return &cache[i];
    if (nc == 16) nc = 0;
    ckey[nc] = k;
    for (c = 0; c < 256; c++) {
        int w = c < 32 ? 0 : (c == 'i' || c == 'l' || c == '.' || c == ' ') ? size / 3 + 1
              : (c == 'm' || c == 'W' || c == 'M') ? size : size * 6 / 10;
        if (font == 0) w = size * 7 / 10;
        if (attr & A_BOLD) w++;
        cache[nc].w[c] = (short)w;
    }
    cache[nc].asc = (short)size;
    cache[nc].desc = (short)(size / 4 + 1);
    return &cache[nc++];
}

/* each glyph is a solid box, enough to check placement */
static GLYPHBM gb;
static unsigned char gbits[64 * 64];
static const GLYPHBM *box_glyph(void *ctx, int font, int size, int attr, int ch, int scale)
{
    const METRICS *m = get_metrics(font, size, attr);
    long w = (m->w[ch] - 1) * scale, h = m->asc * 7 / 10 * scale, rb, r;
    (void)ctx;
    if (w < 1) w = 1;
    if (w > 64) w = 64;
    if (h > 64) h = 64;
    rb = (w + 7) / 8;
    memset(gbits, 0, sizeof gbits);
    for (r = 0; r < h; r++) memset(gbits + r * rb, 0xFF, rb);
    gb.w = (short)w; gb.h = (short)h; gb.ox = 0; gb.oy = (short)-h; gb.bits = gbits;
    return &gb;
}

static int fake_render(void *ctx, DOC *d, int page, int color, long y0, long h, long w,
                       unsigned char *bits, long rb)
{
    BAND b;
    b.bits = bits; b.rb = rb; b.w = w; b.y0 = y0; b.h = h;
    return page_render(d, page, color, &b, *(int *)ctx, box_glyph, NULL, DT_SHORT);
}

/* printing: the fake renderer at the scale print_page chose */
static long rows_out, width_out, ink_out;
static int prt_render(void *ctx, DOC *dd, int page, int color, long y0, long h, long w, unsigned char *bits, long rb)
{
    return fake_render(&((PRTOUT *)ctx)->scale, dd, page, color, y0, h, w, bits, rb);
}
static int prt_out(void *ctx, long y0, long h, long w, const unsigned char *bits, long rb)
{
    long k;
    (void)ctx; (void)y0;
    rows_out += h;
    width_out = w;
    for (k = 0; k < h * rb; k++) if (bits[k]) ink_out++;
    return 1;
}

static const PARA *pp(DOC *d, long k) { return PARA_OF(d, &d->text[k]); }

static int same_cells(DOC *a, DOC *b)
{
    long i;
    if (a->len != b->len) { printf("  len %ld vs %ld\n", a->len, b->len); return 0; }
    for (i = 0; i < a->len; i++) {
        CELL x = a->text[i], y = b->text[i];
        const PARA *px = pp(a, i), *py = pp(b, i);
        PARA qx = *px, qy = *py;
        if (ISEND(x.ch)) { x.font = y.font; x.size = y.size; x.attr = y.attr; x.color = y.color; }
        x.para = y.para;
        if (qx.tbl && qy.tbl) {      /* compare the tables themselves */
            const TBL *tx = &a->a.tbls[qx.tbl - 1], *ty = &b->a.tbls[qy.tbl - 1];
            if (tx->ncols != ty->ncols || memcmp(tx->w, ty->w, tx->ncols * sizeof(short)) || tx->border != ty->border) {
                printf("  cell %ld: tables differ\n", i);
                return 0;
            }
            qx.tbl = qy.tbl = 1;
        }
        if (memcmp(&x, &y, sizeof(CELL)) || memcmp(&qx, &qy, sizeof(PARA))) {
            printf("  cell %ld differs: ch %02x/%02x f%d/%d s%d/%d a%d/%d c%d/%d j%d/%d sp%d/%d l%d/%d t%d/%d\n", i,
                   x.ch, y.ch, x.font, y.font, x.size, y.size, x.attr, y.attr, x.color, y.color,
                   qx.just, qy.just, qx.spacing, qy.spacing, qx.list, qy.list, qx.tbl, qy.tbl);
            return 0;
        }
    }
    return 1;
}

static void import_str(DOC *d, const char *s, int cs, int join)
{
    doc_init(d);
    text_import_buf(d, (const unsigned char *)s, (long)strlen(s), cs, join);
}

static void show(DOC *d, const char *label)
{
    long i;
    printf("  %s: \"", label);
    for (i = 0; i < d->len && i < 300; i++) {
        unsigned char c = d->text[i].ch;
        putchar(c == '\n' ? '|' : c == CH_PAGE ? '^' : c == CH_CELL ? '#' : c == CH_COLBRK ? '~' : c < 128 ? c : '%');
    }
    printf("\"\n");
}

static int text_is(DOC *d, long at, const char *s)
{
    long i, n = (long)strlen(s);
    if (at + n > d->len) return 0;
    for (i = 0; i < n; i++) if (d->text[at + i].ch != (unsigned char)s[i]) return 0;
    return 1;
}

/* copy a codes text into a new document and apply it */
static void codes_into(DOC *src, DOC *dst)
{
    doc_init(dst);
    dst->cv = src->cv;
    dst->cv.t = (char *)malloc(src->cv.len + 1);
    memcpy(dst->cv.t, src->cv.t, src->cv.len + 1);
    dst->cv.ls = NULL; dst->cv.lcap = 0; dst->cv.nl = 0; dst->cv.cap = src->cv.len + 1;
    codes_apply(dst);
}

static long line_of_text(DOC *d, const char *s)
{
    long li;
    for (li = 0; li < d->nlines; li++) if (text_is(d, d->lines[li].start, s)) return li;
    return -1;
}

/* write a small IMG: 16x4, mono, with a literal run, a solid run, a
 * pattern run and a repeated line */
static void write_img(const char *path, int ximg)
{
    FILE *f = fopen(path, "wb");
    unsigned char h[64];
    int n = 0, hlen = ximg ? 8 + 3 + 4 * 3 : 8, i;
    #define W16(v) do { h[n++] = (unsigned char)((v) >> 8); h[n++] = (unsigned char)(v); } while (0)
    W16(1); W16(hlen); W16(ximg ? 2 : 1); W16(1); W16(85); W16(85); W16(16); W16(4);
    if (ximg) {
        h[n++] = 'X'; h[n++] = 'I'; h[n++] = 'M'; h[n++] = 'G'; W16(0);
        W16(1000); W16(1000); W16(1000);     /* 0 white */
        W16(0); W16(0); W16(0);              /* 1 black */
        W16(1000); W16(0); W16(0);           /* 2 red: dark-ish */
        W16(500); W16(500); W16(500);        /* 3 grey */
    }
    fwrite(h, 1, n, f);
    if (!ximg) {
        unsigned char data[] = {
            0x80, 2, 0xF0, 0x0F,             /* line 0: literal */
            0x82,                            /* line 1: 2 bytes of 0xFF */
            0x00, 2, 0xAA,                   /* line 2: pattern 0xAA twice */
            0x00, 0x00, 0xFF, 1, 0x02        /* line 3: repeated once: 2 bytes of 0x00 */
        };
        fwrite(data, 1, sizeof data, f);
    } else {
        /* 4 lines, 2 planes each: plane0 = 0xFF00, plane1 = 0x0FF0 */
        for (i = 0; i < 4; i++) {
            unsigned char p0[] = { 0x80, 2, 0xFF, 0x00 }, p1[] = { 0x80, 2, 0x0F, 0xF0 };
            fwrite(p0, 1, 4, f); fwrite(p1, 1, 4, f);
        }
    }
    fclose(f);
    #undef W16
}

static int px(const IMG *im, int x, int y) { return (im->bits[y * im->rb + (x >> 3)] >> (7 - (x & 7))) & 1; }

int main(int argc, char **argv)
{
    static DOC d, d2;
    char path[300], word[WORDMAX], sug[8][WORDMAX], buf[64];
    long li, s, e, a, b;
    int scale, eol, cs, n, i;
    PDFOUT po;

    if (argc < 5) return 2;
    nfonts = 3;
    strcpy(fonts[0].name, "System"); fonts[0].id = 1;
    strcpy(fonts[1].name, "Swiss 721");  fonts[1].id = 2;
    strcpy(fonts[2].name, "Dutch 801");  fonts[2].id = 14;

    /* --- RTF import, layout --- */
    doc_init(&d);
    CHECK(rtf_import(&d, argv[1]), "RTF import");
    doc_layout(&d);
    printf("  cells=%ld lines=%ld pages=%d wrap=%ldpx paper=%d %dx%d margins %d/%d/%d/%d\n",
           d.len, d.nlines, doc_npages(&d), text_w_px(&d, 0), PS(&d).paper, PS(&d).w, PS(&d).h,
           PS(&d).ml, PS(&d).mr, PS(&d).mt, PS(&d).mb);
    CHECK(pp(&d, 0)->just == J_CENTRE, "first paragraph centred");
    CHECK(d.text[0].color == 3, "title is blue");
    {   /* every soft-wrapped, fully justified line ends exactly at the right edge */
        int nfull = 0, okfull = 1;
        for (li = 0; li < d.nlines; li++) {
            LINE *ln = &d.lines[li];
            long end = ln->start + ln->len, lastns;
            if (!ln->len || pp(&d, ln->start)->just != J_FULL || ISEND(d.text[end - 1].ch) || (ln->flags & LF_LAST)) continue;
            lastns = end;
            while (lastns > ln->start && d.text[lastns - 1].ch == ' ') lastns--;
            if (ln->nsp) { nfull++; if (doc_x_of(&d, li, lastns) != ln->x + ln->w) okfull = 0; }
        }
        printf("  %d stretched lines\n", nfull);
        CHECK(nfull > 0 && okfull, "full justification reaches the right margin");
    }
    {   /* a centred line is centred */
        LINE *l0 = &d.lines[0];
        long w = doc_x_of(&d, 0, l0->start + l0->len - 1) - l0->x - l0->xoff;
        CHECK(labs(2 * l0->xoff + w - l0->w) <= 1, "centred line is centred");
    }
    {   int found = 0;
        for (li = 1; li < d.nlines; li++)
            if (d.lines[li].page != d.lines[li - 1].page && d.lines[li - 1].len &&
                d.text[d.lines[li - 1].start + d.lines[li - 1].len - 1].ch == CH_PAGE) found = 1;
        CHECK(found, "hard page break starts a new page");
    }
    CHECK(d.npages >= 2 && d.pgline[1] > 0 && d.lines[d.pgline[1]].page == 1, "page index");

    /* --- codes view round trip --- */
    d.cv.cols = 70;
    d.cursor = 30;
    codes_build(&d);
    printf("  codes: %.150s...\n", d.cv.t);
    codes_into(&d, &d2);
    CHECK(same_cells(&d, &d2), "codes -> document round trip is exact");
    CHECK(d2.cursor == 30, "cursor survives the codes round trip");
    codes_insert(&d2.cv, 0, "[Size 20][B]Hello[/B] [[x] [Bogus] ", 34);
    codes_apply(&d2);
    show(&d2, "after code edit");
    CHECK(d2.text[0].ch == 'H' && d2.text[0].size == 20 && (d2.text[0].attr & A_BOLD), "typed codes take effect");
    CHECK(d2.text[6].ch == '[' && d2.text[7].ch == 'x', "[[ gives a literal bracket");
    CHECK(d2.text[10].ch == '[' && d2.text[11].ch == 'B', "unknown code stays as text");
    codes_wrap(&d.cv, 40);
    {   int okw = 1;
        for (li = 0; li < d.cv.nl; li++) if (codes_line_len(&d.cv, li) > 40) okw = 0;
        CHECK(okw && d.cv.nl > 10, "codes text wraps to the pane width");
    }

    /* --- RTF round trip with page set-up, colours, justification --- */
    PS(&d).wrapcol = 0;
    page_set_paper(&PS(&d), P_LETTER, 1);
    PS(&d).ml = 40; PS(&d).mt = 30;
    sprintf(path, "%s/round.rtf", argv[2]);
    CHECK(rtf_export(&d, path), "RTF export");
    doc_init(&d2);
    rtf_import(&d2, path);
    CHECK(same_cells(&d, &d2), "RTF round trip keeps every cell");
    CHECK(PS(&d2).paper == P_LETTER && PS(&d2).landscape && PS(&d2).ml == 40 && PS(&d2).mt == 30,
          "RTF round trip keeps page set-up");
    strcpy(HF(&d).text[0], "{file} - page {page} of {pages}");
    strcpy(HF(&d).text[1], "Printed {date} [\x84] {x}");
    HF(&d).just[0] = J_RIGHT; HF(&d).size = 9; HF(&d).first = 0;
    {
        char hb[100];
        strcpy(d.base, "LETTER.RTF");
        hf_expand(&d, 0, 1, 3, DT_ISO, hb, sizeof hb);
        CHECK(!strcmp(hb, "LETTER.RTF - page 2 of 3"), hb);
        hf_expand(&d, 1, 0, 3, DT_ISO, hb, sizeof hb);
        CHECK(!strncmp(hb, "Printed 20", 10) && hb[12] == '-' && strstr(hb, "{x}"), hb);
        CHECK(!hf_visible(&d, 0, 0) && hf_visible(&d, 0, 1), "header can skip the first page");
    }
    PS(&d).wrapcol = 40;
    rtf_export(&d, path);
    doc_init(&d2);
    rtf_import(&d2, path);
    CHECK(PS(&d2).wrapcol == 40, "RTF keeps the wrap column");
    CHECK(!strcmp(HF(&d2).text[0], HF(&d).text[0]) && !strcmp(HF(&d2).text[1], HF(&d).text[1]) &&
          HF(&d2).just[0] == J_RIGHT && HF(&d2).size == 9 && !HF(&d2).first, "RTF keeps header and footer");
    CHECK(same_cells(&d, &d2), "header groups don't leak into the text");

    /* --- wrap at column --- */
    page_set_paper(&PS(&d), P_A4, 0);
    PS(&d).ml = PS(&d).mr = 56;
    PS(&d).wrapcol = 30;
    doc_layout(&d);
    {   int okc = 1;
        for (li = 0; li < d.nlines; li++) {
            long l = d.lines[li].len, k = d.lines[li].start + l;
            while (l > 0 && (d.text[k - 1].ch == ' ' || ISEND(d.text[k - 1].ch))) { l--; k--; }
            if (l > 30) okc = 0;
        }
        CHECK(okc, "no line is longer than the wrap column");
    }
    sprintf(path, "%s/wrapped.txt", argv[2]);
    CHECK(text_export(&d, path, 1, CS_WIN), "text export with wrap column (DOS)");
    PS(&d).wrapcol = 0;

    /* --- text import: line endings and character sets --- */
    text_detect((const unsigned char *)"a\r\nb\r\n", 6, &eol, &cs);
    CHECK(eol == EOL_CRLF && cs == CS_ATARI, "detect DOS line endings");
    text_detect((const unsigned char *)"a\nb\n", 4, &eol, &cs);
    CHECK(eol == EOL_LF, "detect Unix line endings");
    text_detect((const unsigned char *)"a\rb\r", 4, &eol, &cs);
    CHECK(eol == EOL_CR, "detect old Mac line endings");
    text_detect((const unsigned char *)"caf\xc3\xa9\n", 6, &eol, &cs);
    CHECK(cs == CS_UTF8, "detect UTF-8");
    text_detect((const unsigned char *)"caf\xe9 na\xefve\n", 12, &eol, &cs);
    CHECK(cs == CS_WIN, "detect Windows-1252");
    import_str(&d2, "one\r\ntwo\r\n\r\nthree\r\n", CS_AUTO, 0);
    CHECK(d2.len == 14 && d2.text[3].ch == '\n' && d2.text[7].ch == '\n' && d2.text[8].ch == '\n', "DOS text keeps lines");
    import_str(&d2, "one\ntwo\n\nthree\nfour\n", CS_AUTO, 1);
    CHECK(text_is(&d2, 0, "one two") && d2.text[7].ch == '\n', "joined lines become one paragraph");
    import_str(&d2, "caf\xc3\xa9 \xe2\x80\x9cq\xe2\x80\x9d\f\n", CS_AUTO, 0);
    CHECK(d2.text[3].ch == 0x82 && d2.text[5].ch == '"' && d2.text[8].ch == CH_PAGE, "UTF-8 to ST charset, form feed to page break");
    sprintf(path, "%s/utf8.txt", argv[2]);
    text_export(&d2, path, 0, CS_UTF8);
    {   long nn; unsigned char *bb = load_file(path, &nn);
        CHECK(nn == 10 && bb[3] == 0xC3 && bb[4] == 0xA9, "UTF-8 export"); free(bb); }
    /* UTF-16 both ways, little and big endian, a character the ST lacks */
    {
        static const unsigned char le[] = { 0xFF,0xFE, 'c',0, 'a',0, 'f',0, 0xE9,0, '\r',0, '\n',0, 0x31,0x01, 0x22,0x20, 0x3C,0xD8,0x89,0xDF };
        static const unsigned char be[] = { 0xFE,0xFF, 0,'c', 0,'a', 0,'f', 0,0xE9 };
        static const unsigned char nobom[] = { 'h',0, 'i',0, '\n',0, 'y',0, 'o',0 };
        long nn; unsigned char *bb;
        text_detect(le, sizeof le, &eol, &cs);
        CHECK(cs == CS_UTF16 && eol == EOL_CRLF, "detect UTF-16 with a byte order mark");
        text_detect(nobom, sizeof nobom, &eol, &cs);
        CHECK(cs == CS_UTF16, "detect UTF-16 without a byte order mark");
        doc_free(&d2); doc_init(&d2);
        text_import_buf(&d2, le, sizeof le, CS_AUTO, 0);
        CHECK(d2.len >= 7 && d2.text[3].ch == 0x82 && d2.text[4].ch == '\n' && d2.text[5].ch == 'i' && d2.text[6].ch == 0xF9,
              "UTF-16 LE import (accents, a dotless i, a bullet, an emoji)");
        doc_free(&d2); doc_init(&d2);
        text_import_buf(&d2, be, sizeof be, CS_AUTO, 0);
        CHECK(d2.len == 4 && d2.text[3].ch == 0x82, "UTF-16 BE import");
        sprintf(path, "%s/utf16.txt", argv[2]);
        text_export(&d2, path, 1, CS_UTF16);
        bb = load_file(path, &nn);
        CHECK(nn >= 10 && bb[0] == 0xFF && bb[1] == 0xFE && bb[2] == 'c' && bb[3] == 0 && bb[8] == 0xE9 && bb[9] == 0, "UTF-16 export");
        free(bb);
        CHECK(uni_to_st(0x0151) == 'o' && uni_to_st(0x03B1) == 0xE0 && st_to_uni(0xE0) == 0x03B1 && uni_to_st(0x05D0) == 0xC2,
              "Unicode letters the ST lacks, Greek and Hebrew");
        sprintf(path, "%s/u16.md", argv[2]);
        {   FILE *f = fopen(path, "wb");
            static const unsigned char md16[] = { 0xFF,0xFE, '#',0, ' ',0, 'C',0, 'a',0, 'f',0, 0xE9,0, '\n',0, 'x',0, '\n',0 };
            fwrite(md16, 1, sizeof md16, f); fclose(f); }
        doc_free(&d2); doc_init(&d2);
        CHECK(md_import(&d2, path, CS_AUTO) && d2.len >= 4 && d2.text[3].ch == 0x82, "UTF-16 Markdown import");
        CHECK(md_export(&d2, path, 1, CS_UTF16), "UTF-16 Markdown export");
        bb = load_file(path, &nn);
        CHECK(nn > 4 && bb[0] == 0xFF && bb[1] == 0xFE && bb[2] == '#' && bb[3] == 0, "UTF-16 Markdown has a byte order mark");
        free(bb);
        doc_free(&d2); doc_init(&d2);
        CHECK(md_import(&d2, path, CS_AUTO) && d2.text[3].ch == 0x82, "UTF-16 Markdown reads back");
        CHECK(md_export(&d2, path, 0, CS_WIN), "Windows-1252 Markdown export");
        bb = load_file(path, &nn);
        CHECK(nn > 6 && bb[5] == 0xE9, "Windows-1252 Markdown has the accent as one byte");
        free(bb);
    }

    /* --- dates --- */
    fmt_date(buf, DT_SHORT, 2026, 10, 26, 8, 45);  CHECK(!strcmp(buf, "26/10/2026"), buf);
    fmt_date(buf, DT_DAYLONG, 2026, 10, 26, 8, 45); CHECK(!strcmp(buf, "Monday 26 October 2026"), buf);
    fmt_date(buf, DT_ISO, 2026, 1, 5, 8, 45);       CHECK(!strcmp(buf, "2026-01-05"), buf);
    fmt_date(buf, DT_TIME, 2026, 1, 5, 8, 5);       CHECK(!strcmp(buf, "08:05"), buf);

    /* --- paragraph formats: spacing, indents --- */
    import_str(&d, "First paragraph is long enough to wrap onto a second line of text in the column.\n"
                   "Second paragraph is also long enough to wrap onto a second line of text here.\nThird.", CS_AUTO, 0);
    doc_layout(&d);
    {
        long l0 = d.lines[0].lh, x0 = d.lines[0].x;
        doc_para_set(&d, 0, 1, PF_SPACING, LS_DOUBLE);
        doc_layout(&d);
        CHECK(d.lines[0].lh == 2 * l0 && d.lines[1].py - d.lines[0].py == 2 * l0, "double line spacing");
        doc_para_set(&d, 0, 1, PF_SPACING, LS_ONEHALF);
        doc_layout(&d);
        CHECK(d.lines[0].lh == l0 + l0 / 2, "one and a half line spacing");
        li = line_of_text(&d, "Second");
        doc_para_set(&d, d.lines[li].start, d.lines[li].start, PF_LEFT, 36);
        doc_para_set(&d, d.lines[li].start, d.lines[li].start, PF_FIRST, -36);
        doc_layout(&d);
        li = line_of_text(&d, "Second");
        CHECK(d.lines[li].x == x0 && d.lines[li + 1].x == x0 + pt2px_x(36), "hanging indent");
        doc_para_set(&d, d.lines[li].start, d.lines[li].start, PF_FIRST, 18);
        doc_layout(&d);
        li = line_of_text(&d, "Second");
        CHECK(d.lines[li].x == x0 + pt2px_x(54) && d.lines[li + 1].x == x0 + pt2px_x(36), "left and first line indent");
    }

    /* --- lists --- */
    import_str(&d, "Apples\nPears\nPlums\nNot a list\nOne\nTwo", CS_AUTO, 0);
    doc_para_set(&d, 0, 15, PF_LIST, LIST_NUMBER);
    doc_para_set(&d, 30, 37, PF_LIST, LIST_NUMBER);
    doc_layout(&d);
    {
        char m[12];
        CHECK(d.lines[0].mark == 1 && d.lines[1].mark == 2 && d.lines[2].mark == 3 && d.lines[3].mark == 0,
              "numbered list counts 1, 2, 3");
        CHECK(d.lines[4].mark == 1 && d.lines[5].mark == 2, "numbering restarts after another paragraph");
        doc_marker(&d, 2, m);
        CHECK(!strcmp(m, "3."), "marker text");
        CHECK(d.lines[0].x > d.lines[3].x && d.lines[0].markx < d.lines[0].x, "list text is indented past its marker");
    }
    doc_para_set(&d, 0, 15, PF_LIST, LIST_BULLET);
    doc_layout(&d);
    CHECK(d.lines[0].mark == -1 && d.lines[2].mark == -1, "bullets");
    sprintf(path, "%s/lists.rtf", argv[2]);
    rtf_export(&d, path);
    doc_init(&d2); rtf_import(&d2, path); doc_layout(&d2);
    CHECK(same_cells(&d, &d2) && d2.lines[0].mark == -1 && d2.lines[4].mark == 1, "RTF keeps lists");
    {   /* a Word 97 style list */
        const char *w97 = "{\\rtf1{\\*\\listtable{\\list\\listtemplateid1{\\listlevel\\levelnfc23{\\leveltext\\'01\\u-3913 ?;}}"
            "{\\listlevel\\levelnfc0}\\listid77}{\\list{\\listlevel\\levelnfc0}\\listid88}}"
            "{\\*\\listoverridetable{\\listoverride\\listid77\\listoverridecount0\\ls1}{\\listoverride\\listid88\\ls2}}"
            "{\\listtext\\'b7\\tab}\\pard\\ls1 Bullet\\par{\\listtext 1.\\tab}\\pard\\ls2 Number\\par}";
        FILE *f;
        sprintf(path, "%s/w97.rtf", argv[2]);
        f = fopen(path, "wb"); fputs(w97, f); fclose(f);
        doc_init(&d2); rtf_import(&d2, path);
        show(&d2, "Word 97 lists");
        CHECK(text_is(&d2, 0, "Bullet\nNumber") && pp(&d2, 0)->list == LIST_BULLET && pp(&d2, 7)->list == LIST_NUMBER,
              "Word 97 list table");
    }

    /* --- tables --- */
    import_str(&d, "Before\nAfter", CS_AUTO, 0);
    CHECK(tbl_insert(&d, 7, 2, 3, 1), "insert a 2 x 3 table");
    show(&d, "table");
    doc_layout(&d);
    CHECK(text_is(&d, 0, "Before\n\x1f\x1f\n\x1f\x1f\nAfter") && pp(&d, 7)->tbl == 1 && pp(&d, 13)->tbl == 0, "table cells in the text");
    CHECK(d.nrows == 2 && d.rows[1].y > d.rows[0].y, "two table rows laid out");
    {
        int cell; long rs;
        CHECK(doc_in_table(&d, 8, &rs, &cell) == 1 && rs == 7 && cell == 1, "position in table");
        CHECK(doc_next_cell(&d, 7, 1) == 8 && doc_next_cell(&d, 9, 1) == 10 && doc_next_cell(&d, 12, 1) == -1, "next cell");
        d.cursor = 8; d.anchor = -1;
        doc_type_str(&d, "B1");
        d.cursor = 7;
        doc_type_str(&d, "A1\nx");           /* a break typed in a cell becomes a space */
        show(&d, "typed in cells");
        CHECK(text_is(&d, 7, "A1 x\x1f" "B1\x1f\n"), "typing in cells");
        doc_layout(&d);
        li = doc_find_line(&d, 12);
        CHECK(d.lines[li].flags & LF_CELL && d.lines[li].cell == 1 && d.lines[li].x > d.lines[doc_find_line(&d, 7)].x,
              "second cell laid out to the right of the first");
        CHECK(d.lines[li].py == d.lines[doc_find_line(&d, 7)].py, "cells of a row share their top");
        /* deleting across cells keeps the structure */
        d.anchor = 9; d.cursor = 14;
        doc_delete_sel(&d);
        show(&d, "after deleting across cells");
        CHECK(text_is(&d, 7, "A1\x1f\x1f\n\x1f\x1f\nAfter"), "deleting across cells keeps separators");
        CHECK(tbl_rows(&d, 8) == 2, "rows");
        tbl_row_insert(&d, 8, 1);
        CHECK(tbl_rows(&d, 8) == 3 && text_is(&d, 7, "A1\x1f\x1f\n\x1f\x1f\n"), "insert a row");
        tbl_col_insert(&d, 7, 1);
        show(&d, "after inserting a column");
        CHECK(d.a.tbls[0].ncols == 4 && text_is(&d, 7, "A1\x1f\x1f\x1f\n\x1f\x1f\x1f\n"), "insert a column");
        tbl_col_delete(&d, 7);
        CHECK(d.a.tbls[0].ncols == 3 && text_is(&d, 7, "\x1f\x1f\n"), "delete a column");
        tbl_row_delete(&d, 7);
        CHECK(tbl_rows(&d, 7) == 2, "delete a row");
        {
            short w[3] = { 100, 50, 150 };
            tbl_resize(&d, 7, 4, 2, w, 0);
            show(&d, "after resize");
            CHECK(tbl_rows(&d, 7) == 4 && d.a.tbls[0].ncols == 2 && d.a.tbls[0].w[0] == 100 && !d.a.tbls[0].border,
                  "table properties: rows, columns, widths, border");
        }
        d.cursor = 7; doc_type_str(&d, "Cell text");
        sprintf(path, "%s/table.rtf", argv[2]);
        rtf_export(&d, path);
        doc_init(&d2); rtf_import(&d2, path);
        show(&d2, "table after RTF");
        CHECK(same_cells(&d, &d2), "RTF keeps the table");
        d.cv.cols = 60; codes_build(&d);
        printf("  codes: %.200s\n", d.cv.t);
        codes_into(&d, &d2);
        CHECK(same_cells(&d, &d2), "codes keep the table");
        CHECK(tbl_delete(&d, 8) && text_is(&d, 0, "Before\nAfter"), "delete the table");
    }

    /* --- columns, column breaks, page formats --- */
    import_str(&d, "", CS_AUTO, 0);
    for (i = 0; i < 60; i++) doc_type_str(&d, "Words in columns flow down one column and then the next. \n");
    PS(&d).cols = 2;
    doc_layout(&d);
    {
        long c1 = -1, c2 = -1;
        for (li = 0; li < d.nlines; li++) {
            if (d.lines[li].page != 0) break;
            if (d.lines[li].col == 0 && c1 < 0) c1 = li;
            if (d.lines[li].col == 1 && c2 < 0) c2 = li;
        }
        CHECK(c1 >= 0 && c2 > c1 && d.lines[c2].x > d.lines[c1].x + d.lines[c1].w && d.lines[c2].py == d.lines[c1].py,
              "two columns side by side");
    }
    PS(&d).cols = 1;
    {
        CELL cb = d.ins;
        cb.ch = CH_COLBRK;
        PS(&d).cols = 3;
        d.cursor = 0;
        doc_type_str(&d, "Top");
        doc_type(&d, &cb, 1);
        doc_layout(&d);
        CHECK(d.lines[1].col == 1 && d.lines[1].page == 0, "column break");
        PS(&d).cols = 1;
        doc_layout(&d);
        CHECK(d.lines[1].page == 1 && d.lines[1].col == 0, "a column break in one column starts a page");
    }
    {
        PGFMT f;
        page_fmt(&d, 1, &f);
        f.ml = 144; f.cols = 1;
        page_ovr_set(&d, 1, 0, &f);
        doc_layout(&d);
        li = d.pgline[1];
        CHECK(d.lines[li].x == pt2px_x(144) && d.lines[d.pgline[0]].x == pt2px_x(56) &&
              d.lines[d.pgline[2]].x == pt2px_x(56), "margins for one page");
        f.ml = 100;
        page_ovr_set(&d, 2, 1, &f);
        doc_layout(&d);
        CHECK(d.lines[d.pgline[2]].x == pt2px_x(100) && d.lines[d.pgline[d.npages - 1]].x == pt2px_x(100),
              "margins for a page and those after it");
        sprintf(path, "%s/pages.rtf", argv[2]);
        rtf_export(&d, path);
        doc_init(&d2); rtf_import(&d2, path); doc_layout(&d2);
        CHECK(d2.a.novr == 2 && d2.lines[d2.pgline[1]].x == pt2px_x(144) && d2.npages == d.npages, "RTF keeps page formats");
    }

    /* --- text around pictures and drawings --- */
    import_str(&d, "", CS_AUTO, 0);
    for (i = 0; i < 20; i++) doc_type_str(&d, "Text flows around the box in the middle of this page of text. ");
    doc_layout(&d);
    {
        OBJ o;
        long top, bot, bx0, bx1, ok = 1, beside = 0, lx;
        memset(&o, 0, sizeof o);
        o.type = OB_DRAW; o.page = 0; o.x = 56; o.y = 100; o.w = 150; o.h = 100; o.wrap = WR_BOX; o.img = -1;
        o.nsh = 1;
        o.sh[0].type = SH_ELLIPSE; o.sh[0].fill = FL_GREY; o.sh[0].lw = 2; o.sh[0].x2 = 150; o.sh[0].y2 = 100;
        doc_obj_add(&d, &o);
        doc_layout(&d);
        top = pt2px_y(100); bot = pt2px_y(200); bx0 = pt2px_x(56); bx1 = pt2px_x(206);
        for (li = 0; li < d.nlines; li++) {
            LINE *ln = &d.lines[li];
            if (ln->py + ln->lh <= top || ln->py >= bot) continue;
            beside++;
            if (ln->x < bx1 && ln->x + ln->w > bx0) ok = 0;
        }
        CHECK(beside > 2 && ok, "text flows beside a picture");
        lx = d.lines[0].x;
        d.a.objs[0].wrap = WR_SHAPE;
        doc_layout(&d);
        {   /* around an ellipse, lines near its top and bottom come closer than at its middle */
            long xmin = 9999, xmax = 0;
            for (li = 0; li < d.nlines; li++) {
                LINE *ln = &d.lines[li];
                if (ln->py + ln->lh <= top || ln->py >= bot) continue;
                if (ln->x < xmin) xmin = ln->x;
                if (ln->x > xmax) xmax = ln->x;
            }
            CHECK(xmax > xmin + 4, "text follows the shape of an ellipse");
        }
        d.a.objs[0].wrap = WR_NONE;
        doc_layout(&d);
        CHECK(d.lines[3].x == lx && d.lines[3].w == text_w_px(&d, 0), "no wrapping leaves the text alone");
        d.a.objs[0].wrap = WR_BOX;
        sprintf(path, "%s/test.img", argv[2]);
        write_img(path, 0);
        {
            IMG im;
            CHECK(img_load(path, &im) && im.w == 16 && im.h == 4, "IMG header");
            CHECK(im.bits[0] == 0xF0 && im.bits[1] == 0x0F && im.bits[2] == 0xFF && im.bits[3] == 0xFF &&
                  im.bits[4] == 0xAA && im.bits[5] == 0xAA && im.bits[6] == 0 && im.bits[7] == 0, "IMG runs decoded");
            o.type = OB_IMG; o.img = (short)doc_img_add(&d, &im); o.nsh = 0; o.x = 300; o.y = 400; o.w = 64; o.h = 16;
            doc_obj_add(&d, &o);
        }
        write_img(path, 1);
        {
            IMG im;
            CHECK(img_load(path, &im), "XIMG with two planes");
            /* index 1 (black) at x 0-3, 3 (grey) at 4-7, 2 (red) at 8-11, 0 (white) at 12-15 */
            CHECK(px(&im, 0, 0) && px(&im, 1, 1) && !px(&im, 13, 0) && !px(&im, 14, 2), "palette colours become black and white");
            img_free(&im);
        }
        sprintf(path, "%s/objects.rtf", argv[2]);
        rtf_export(&d, path);
        doc_init(&d2); rtf_import(&d2, path);
        CHECK(d2.a.nobj == 2 && d2.a.objs[0].nsh == 1 && d2.a.objs[0].sh[0].type == SH_ELLIPSE &&
              d2.a.objs[0].sh[0].x2 == 150 && d2.a.objs[0].sh[0].y2 == 100 &&
              d2.a.objs[1].type == OB_IMG && d2.nimg == 1 && d2.imgs[0].w == 16 && d2.imgs[0].bits[0] == 0xF0,
              "RTF keeps the drawing and the picture");
        CHECK(same_cells(&d, &d2), "object groups don't leak into the text");
        /* the drawing renders: the ellipse centre is grey, outside it white */
        {
            static unsigned char band[4000 * 2];
            BAND bd;
            long w = page_w_px(&d), rb = (w + 7) / 8, cy = pt2px_y(150), cx = pt2px_x(131);
            int ink = 0, out = 0;
            bd.bits = band; bd.rb = rb; bd.w = w; bd.y0 = cy; bd.h = 2;
            memset(band, 0, sizeof band);
            rs_obj(&bd, &d, &d.a.objs[0], -1, 1, 1);
            for (i = -3; i < 3; i++) ink |= (band[(cx + i) >> 3] >> (7 - ((cx + i) & 7))) & 1;
            bd.y0 = pt2px_y(103);                       /* the box's top left corner: outside */
            memset(band, 0, sizeof band);
            rs_obj(&bd, &d, &d.a.objs[0], -1, 1, 1);
            out = (band[(pt2px_x(60)) >> 3] >> (7 - (pt2px_x(60) & 7))) & 1;
            CHECK(ink && !out, "a drawing renders into the page");
            rs_end();
        }
    }

    /* --- undo and redo --- */
    import_str(&d, "Hello world.\nSecond line.", CS_AUTO, 0);
    undo_reset(&d);
    d.cursor = 5; d.anchor = -1;
    doc_type_str(&d, ","); undo_commit(&d, 1);
    { CELL c = d.ins; c.ch = 'X'; doc_type(&d, &c, 1); undo_commit(&d, 1); c.ch = 'Y'; doc_type(&d, &c, 1); undo_commit(&d, 1); }
    CHECK(text_is(&d, 0, "Hello,XY world.") && d.u.n == 1, "typing merges into one step");
    doc_apply(&d, 0, 5, AP_TOGGLE, A_BOLD); undo_commit(&d, 0);
    doc_para_set(&d, 0, 0, PF_JUST, J_CENTRE); undo_commit(&d, 0);
    d.anchor = 0; d.cursor = 6; doc_delete_sel(&d); undo_commit(&d, 0);
    tbl_insert(&d, d.len, 1, 2, 1); undo_commit(&d, 0);
    show(&d, "after edits");
    CHECK(d.u.n == 5, "five steps");
    while (undo_do(&d)) ;
    CHECK(text_is(&d, 0, "Hello world.\nSecond line.") && d.len == 25 && !(d.text[0].attr & A_BOLD) &&
          pp(&d, 0)->just == J_LEFT && d.a.ntbl == 0, "undo everything");
    while (redo_do(&d)) ;
    show(&d, "after redo");
    CHECK(text_is(&d, 0, "XY world.") && pp(&d, 0)->just == J_CENTRE && d.a.ntbl == 1, "redo everything");
    undo_do(&d); undo_do(&d);
    doc_type_str(&d, "Z"); undo_commit(&d, 1);
    CHECK(!redo_can(&d), "a new edit clears redo");
    codes_build(&d); codes_apply(&d); undo_commit(&d, 0);
    CHECK(undo_can(&d), "codes edits can be undone");

    /* --- find and replace --- */
    import_str(&d, "The cat sat on the mat. CAT and category. The end.", CS_AUTO, 0);
    s = find_next(&d, 0, "cat", 0, &e);
    CHECK(s == 4 && e == 7, "find");
    s = find_next(&d, e, "cat", 0, &e);
    CHECK(s == 24, "find ignores case");
    CHECK(find_next(&d, 5, "cat", FIND_CASE, &e) == 32, "match case");
    CHECK(find_next(&d, 25, "cat", FIND_WORD, &e) < 0, "whole words");
    CHECK(find_next(&d, 40, "the", FIND_BACK, &e) == 15, "find backwards");
    n = find_replace_all(&d, "cat", "dog", FIND_WORD, 0);
    show(&d, "replaced");
    CHECK(n == 2 && text_is(&d, 0, "The dog sat on the mat. dog and category."), "replace all");

    /* --- summary --- */
    {
        DSTATS st;
        doc_layout(&d);
        doc_stats(&d, &st);
        CHECK(st.words == 11 && st.paras == 1 && st.pages == 1 && st.chars == 50, "word, paragraph and character counts");
        CHECK(word_at(&d, 6, &a, &b, word, WORDMAX) && !strcmp(word, "dog") && a == 4 && b == 7, "word at a position");
    }

    /* --- table of contents --- */
    import_str(&d, "Introduction\nSome text.\nChapter One\nMore text.\nA Section\nText.\n", CS_AUTO, 0);
    doc_para_set(&d, 0, 0, PF_TOC, 1);
    doc_para_set(&d, 24, 24, PF_TOC, 1);
    doc_para_set(&d, 47, 47, PF_TOC, 2);
    {
        CELL pb = d.ins;
        pb.ch = CH_PAGE;
        d.cursor = 46;
        doc_sync_ins(&d);
        doc_type(&d, &pb, 1);           /* "A Section" on page 2 */
    }
    show(&d, "headings");
    CHECK(toc_insert(&d, 0, "Contents"), "insert a table of contents");
    doc_layout(&d);
    show(&d, "with contents");
    CHECK(text_is(&d, 0, "Contents\nIntroduction\t1\nChapter One\t1\nA Section\t2\n"), "contents with page numbers");
    CHECK(pp(&d, 9)->tocent == TOCE_ENTRY && pp(&d, 0)->tocent == TOCE_TITLE && pp(&d, 40)->left == 18, "contents formats");
    li = line_of_text(&d, "Chapter One\t");
    CHECK(li >= 0 && d.lines[li].tabk == 11 && doc_x_of(&d, li, d.lines[li].start + 13) == d.lines[li].x + d.lines[li].w,
          "dot leader puts the page number at the right");
    {
        CELL pb = d.ins;
        pb.ch = CH_PAGE;
        toc_find(&d, &a, &b);
        d.cursor = find_next(&d, b, "Chapter One", FIND_CASE, &e);
        doc_sync_ins(&d);
        doc_type(&d, &pb, 1);
        toc_update(&d);
        show(&d, "updated contents");
        CHECK(text_is(&d, 0, "Contents\nIntroduction\t1\nChapter One\t2\nA Section\t3\n"), "update the contents");
    }
    sprintf(path, "%s/toc.rtf", argv[2]);
    rtf_export(&d, path);
    doc_init(&d2); rtf_import(&d2, path);
    CHECK(same_cells(&d, &d2), "RTF keeps headings and the contents");

    /* --- new codes --- */
    import_str(&d2, "", CS_AUTO, 0);
    d2.cv.t = (char *)malloc(400);
    strcpy(d2.cv.t, "[Spacing 2][Indent 36 -18][Heading 1]Title\n[Spacing 1][Indent 0 0][/Heading][Bullet]Item\n"
                    "[/List][Table 100 200 Border][Row]a[Cell]b\n[Row]c[Cell]d\n[/Table][Column]\nEnd");
    d2.cv.len = (long)strlen(d2.cv.t); d2.cv.cap = 400; d2.cv.ls = NULL; d2.cv.nl = 0; d2.cv.lcap = 0;
    codes_apply(&d2);
    show(&d2, "from codes");
    CHECK(text_is(&d2, 0, "Title\nItem\na\x1f" "b\nc\x1f" "d\n\x0b") && pp(&d2, 0)->spacing == LS_DOUBLE &&
          pp(&d2, 0)->left == 36 && pp(&d2, 0)->first == -18 && pp(&d2, 0)->toc == 1 && pp(&d2, 6)->list == LIST_BULLET &&
          pp(&d2, 11)->tbl == 1 && d2.a.tbls[0].w[1] == 200 && d2.a.tbls[0].border && pp(&d2, 19)->tbl == 0,
          "new codes are read");

    /* --- spell checker --- */
    CHECK(spell_load_main(argv[3]), "load main dictionary");
    CHECK(spell_check("colour") && spell_check("organise") && spell_check("organize"), "British spellings known");
    CHECK(spell_check("London") && spell_check("LONDON") && spell_check("Hello") && spell_check("don't"), "case, contractions");
    CHECK(!spell_check("teh") && !spell_check("recieve") && !spell_check("london"), "misspellings found");
    n = spell_suggest("recieve", sug, 6);
    CHECK(n > 0 && !strcmp(sug[0], "receive"), "suggestion for recieve");
    sprintf(path, "%s/USER.UDC", argv[2]);
    remove(path);
    CHECK(spell_user_load(path, 1) == 0, "create user dictionary");
    CHECK(!spell_check("EdWord") && spell_user_add("EdWord") && spell_check("EdWord"), "add word to user dictionary");
    import_str(&d2, "This sentance has a speling mistake.", CS_AUTO, 0);
    s = spell_next(&d2, 0, &e, word);
    CHECK(s == 5 && e == 13 && !strcmp(word, "sentance"), "spell_next finds first error");
    spell_free_all();

    /* --- pictures: PNG, IFF, true-colour IMG; colours kept, saved, re-read --- */
    {
        static const char *files[] = { "rgb.png", "sub.png", "inter.png", "rgb.iff", "rgb24.img", NULL };
        char cmd[400];
        IMG im, im2;
        int f, okp;
        sprintf(cmd, "python3 test/mkpics.py %s", argv[2]);
        CHECK(system(cmd) == 0, "test pictures made");
#define PX(im, x, y, c) ((im).rgb[(((y) * (im).w) + (x)) * 3 + (c)])
        for (f = 0; files[f]; f++) {
            sprintf(path, "%s/%s", argv[2], files[f]);
            okp = img_load(path, &im) && im.w == 40 && im.h == 30 && im.rgb &&
                  PX(im, 13, 11, 0) == 78 && PX(im, 13, 11, 1) == 88 && PX(im, 13, 11, 2) == 72 &&
                  PX(im, 39, 29, 0) == (39 * 6) % 256;
            CHECK(okp, files[f]);
            img_free(&im);
        }
        sprintf(path, "%s/rgba.png", argv[2]);
        CHECK(img_load(path, &im) && PX(im, 2, 3, 0) == 255 && PX(im, 2, 3, 2) == 255 && PX(im, 30, 3, 0) == 180,
              "PNG with alpha on white");
        img_free(&im);
        sprintf(path, "%s/grey16.png", argv[2]);
        CHECK(img_load(path, &im) && im.rgb && PX(im, 0, 10, 0) == 80 && PX(im, 5, 10, 2) == 80, "16-bit grey PNG");
        img_free(&im);
        sprintf(path, "%s/pal4.png", argv[2]);
        CHECK(img_load(path, &im) && im.rgb && PX(im, 3, 0, 0) == 48 && PX(im, 3, 0, 2) == 207, "4-bit palette PNG");
        img_free(&im);
        sprintf(path, "%s/rgb16.img", argv[2]);
        CHECK(img_load(path, &im) && im.rgb && PX(im, 13, 11, 0) / 8 == 78 / 8 && PX(im, 13, 11, 1) / 4 == 88 / 4,
              "16-plane IMG");
        img_free(&im);
        sprintf(path, "%s/rgb.png", argv[2]);
        img_load(path, &im);
        {   /* the 1-bit version has some ink, but not all */
            long k, ink = 0;
            for (k = 0; k < 40L * 30; k++) ink += (im.bits[(k / 40) * im.rb + (k % 40) / 8] >> (7 - k % 8)) & 1;
            printf("  dithered: %ld of 1200 pixels inked\n", ink);
            CHECK(ink > 100 && ink < 1100, "colour picture dithered");
        }
        sprintf(path, "%s/SCRAP.IMG", argv[2]);
        CHECK(img_save(&im, path) && img_load(path, &im2) && im2.rgb && im2.w == 40 &&
              !memcmp(im.rgb, im2.rgb, 40 * 30 * 3), "IMG written in colour and read back");
        img_free(&im2);
        {   /* a colour picture in a document survives RTF */
            OBJ o;
            doc_init(&d);
            import_str(&d, "Picture", CS_AUTO, 0);
            memset(&o, 0, sizeof o);
            o.type = OB_IMG; o.w = 100; o.h = 75; o.wrap = WR_BOX;
            o.img = (short)doc_img_add(&d, &im);
            doc_obj_add(&d, &o);
            sprintf(path, "%s/pic.rtf", argv[2]);
            CHECK(rtf_export(&d, path), "RTF with a colour picture");
            doc_init(&d2);
            CHECK(rtf_import(&d2, path) && d2.nimg == 1 && d2.imgs[0].rgb &&
                  !memcmp(d2.imgs[0].rgb, d.imgs[0].rgb, 40 * 30 * 3) && !memcmp(d2.imgs[0].bits, d.imgs[0].bits, 5 * 30),
                  "colours read back from RTF");
            img_rgb_max = 0;
            doc_init(&d2);
            CHECK(rtf_import(&d2, path) && d2.nimg == 1 && !d2.imgs[0].rgb, "colours skipped when there's no room");
            img_rgb_max = 0x7fffffffL;
        }
        {   /* the colours scaled */
            unsigned char *sc = img_scale_rgb(&d.imgs[0], 20, 15);
            CHECK(sc && sc[0] < 10 && abs(sc[(7 * 20 + 6) * 3] - 78) < 12, "colours scaled down");
            free(sc);
        }
#undef PX
    }

    /* --- the other languages' dictionaries (ST characters) --- */
    {
        static const char *dics[] = { "de", "es", "it", "fr" };
        char dir[300], *sl;
        int l;
        strcpy(dir, argv[3]);
        sl = strrchr(dir, '/');
        if (sl) sl[1] = 0; else dir[0] = 0;
        for (l = 1; l <= 4; l++) {
            spell_unload_main();
            spell_lang = l;
            sprintf(path, "%s%s", dir, spell_dic_name(l));
            CHECK(spell_load_main(path), dics[l - 1]);
            if (l == 1) {
                CHECK(spell_check("H\x84user") && spell_check("gehst") && spell_check("Haust\x81r") &&
                      spell_check("Arbeitszimmer") && spell_check("Donaudampfschiff"), "German words and compounds");
                CHECK(!spell_check("Hauss") && !spell_check("gehts\x81") && !spell_check("Hxyzhaus"), "German misspellings");
                n = spell_suggest("H\x84suer", sug, 6);
                CHECK(n > 0 && !strcmp(sug[0], "H\x84user"), "German suggestion");
                n = spell_suggest("schn", sug, 6);
                for (i = 0; i < n; i++) if (!strcmp(sug[i], "sch\x94n")) break;
                CHECK(i < n, "suggestion with an umlaut");
            } else if (l == 2) {
                CHECK(spell_check("ni\xa4os") && spell_check("coraz\xa2n") && !spell_check("corazone"), "Spanish");
            } else if (l == 3) {
                CHECK(spell_check("citt\x85") && spell_check("dell'anno") && !spell_check("bambinni"), "Italian, elision");
            } else {
                CHECK(spell_check("l'homme") && spell_check("c\xb4ur") && spell_check("\x82tait") && !spell_check("homm"), "French, elision");
            }
        }
        spell_unload_main();
        spell_lang = 0;
    }

    /* --- thesaurus --- */
    CHECK(thes_open(argv[4]), "open the thesaurus");
    {
        THSRES r;
        int found = 0, k;
        n = thes_lookup("happy", &r);
        for (i = 0; i < r.nsense; i++) for (k = 0; k < r.nsyn[i]; k++) if (!strcmp(r.syn[i][k], "glad")) found = 1;
        printf("  happy: %d senses, first: (%c) ", r.nsense, r.pos[0]);
        for (k = 0; k < r.nsyn[0]; k++) printf("%s, ", r.syn[0][k]);
        printf("\n");
        CHECK(n > 0 && found, "happy -> glad");
        n = thes_lookup("Happier", &r);
        CHECK(n > 0 && !strcmp(r.word, "happy"), "happier is looked up as happy");
        n = thes_lookup("colour", &r);
        CHECK(n > 0, "British spelling is a headword");
        CHECK(thes_lookup("xyzzyq", &r) == 0, "unknown word");
    }
    thes_close();

    /* --- Markdown --- */
    {
        static const char md[] =
            "# Title *one*\n\nSome **bold** and *it*, _also_ and <u>under</u> with `code` and \\*stars\\*.\n"
            "A second line joins.\n\n- first\n- second item\n  wraps\n\n1. one\n2. two\n\n"
            "| A | B |\n|---|:-:|\n| x | **y** |\n| 3 | 4 |\n\n> quoted text\n\n```\ncode line\n\n  more\n```\n\n"
            "Heading Two\n---\n\n\\pagebreak\n\nAfter [a link](http://x.y) &amp; &eacute;t&#233; ![pic](a.img)\n\n***\n\nend\n";
        FILE *f;
        long k, pbrk = -1;
        int nb = 0, nn = 0, nt = 0, hb = 0;
        sprintf(path, "%s/t.md", argv[2]);
        f = fopen(path, "wb"); fputs(md, f); fclose(f);
        doc_init(&d);
        CHECK(md_import(&d, path, CS_AUTO), "Markdown import");
        show(&d, "markdown");
        CHECK(pp(&d, 0)->toc == 1 && d.text[0].size == 24 && (d.text[0].attr & A_BOLD), "# heading");
        for (k = 0; k < d.len; k++) {
            const PARA *p = pp(&d, k);
            if (ISBRK(d.text[k].ch) || d.text[k].ch == CH_CELL) {
                if (d.text[k].ch == CH_PAGE) pbrk = k;
                if (d.text[k].ch == '\n' && p->list == LIST_BULLET) nb++;
                if (d.text[k].ch == '\n' && p->list == LIST_NUMBER) nn++;
                if (p->tbl) nt++;
            }
            if (p->toc == 2) hb = 1;
        }
        CHECK(nb == 2 && nn == 2, "bullet and numbered lists");
        CHECK(nt == 6, "pipe table: 3 rows of 2 cells");
        CHECK(pbrk > 0, "page break");
        CHECK(hb, "underlined heading");
        a = find_next(&d, 0, "bold", 0, &b);
        CHECK(a > 0 && (d.text[a].attr & A_BOLD) && !(d.text[a - 1].attr & A_BOLD), "**bold**");
        a = find_next(&d, 0, "also", 0, &b);
        CHECK(a > 0 && (d.text[a].attr & A_ITALIC), "_italic_");
        a = find_next(&d, 0, "under", 0, &b);
        CHECK(a > 0 && (d.text[a].attr & A_UNDER), "<u>underline</u>");
        a = find_next(&d, 0, "code", 0, &b);
        CHECK(a > 0 && font_is_mono(d.text[a].font), "`code`");
        a = find_next(&d, 0, "*stars*", 0, &b);
        CHECK(a > 0 && !(d.text[a].attr & (A_BOLD | A_ITALIC)), "escaped stars");
        a = find_next(&d, 0, "second item wraps", 0, &b);
        CHECK(a > 0, "list item continuation");
        a = find_next(&d, 0, "a link & \x82t\x82 pic", 0, &b);
        CHECK(a > 0, "link text, entities, picture alt text");
        a = find_next(&d, 0, "quoted", 0, &b);
        CHECK(a > 0 && pp(&d, a)->left == 36, "block quote");
        a = find_next(&d, 0, "  more", 0, &b);
        CHECK(a > 0 && font_is_mono(d.text[a].font), "fenced code keeps spaces");
        sprintf(path, "%s/out.md", argv[2]);
        CHECK(md_export(&d, path, 0, CS_UTF8), "Markdown export");
        doc_init(&d2);
        CHECK(md_import(&d2, path, CS_AUTO), "Markdown re-import");
        {
            int same = d.len == d2.len;
            for (k = 0; same && k < d.len; k++)
                if (d.text[k].ch != d2.text[k].ch || (d.text[k].attr & 7) != (d2.text[k].attr & 7) ||
                    font_is_mono(d.text[k].font) != font_is_mono(d2.text[k].font) ||
                    pp(&d, k)->toc != pp(&d2, k)->toc || pp(&d, k)->list != pp(&d2, k)->list ||
                    (pp(&d, k)->tbl != 0) != (pp(&d2, k)->tbl != 0)) {
                    printf("  differs at %ld: '%c' %d/%d '%c' %d/%d\n", k, d.text[k].ch, d.text[k].attr, pp(&d, k)->toc,
                           d2.text[k].ch, d2.text[k].attr, pp(&d2, k)->toc);
                    same = 0;
                }
            if (!same) printf("  lengths %ld %ld\n", d.len, d2.len);
            CHECK(same, "Markdown round trip keeps text, styles, headings, lists, tables");
        }
        doc_layout(&d2);

        /* --- 1st Word Plus --- */
        sprintf(path, "%s/T.DOC", argv[2]);
        doc_init(&d);
        rtf_import(&d, argv[1]);
        strcpy(HF(&d).text[1], "Page {page}");
        CHECK(fwp_export(&d, path), "1st Word Plus export");
        {
            long n2;
            unsigned char *raw = load_file(path, &n2);
            CHECK(raw && fwp_detect(raw, n2), "1st Word Plus file detected");
            free(raw);
        }
        doc_init(&d2);
        CHECK(fwp_import(&d2, path), "1st Word Plus import");
        show(&d2, "1st word");
        {   /* the words and their styles come back */
            long x = 0, y = 0, bad = 0, words = 0;
            while (x < d.len && y < d2.len) {
                while (x < d.len && (d.text[x].ch == ' ' || d.text[x].ch < 32)) x++;
                while (y < d2.len && (d2.text[y].ch == ' ' || d2.text[y].ch < 32)) y++;
                if (x >= d.len || y >= d2.len) break;
                if (d.text[x].ch != d2.text[y].ch || (d.text[x].attr & 7) != (d2.text[y].attr & 7)) {
                    if (pp(&d, x)->list || pp(&d, x)->tocent) { x++; continue; }   /* markers, contents */
                    if (!bad) printf("  first difference at %ld/%ld: '%c' vs '%c'\n", x, y, d.text[x].ch, d2.text[y].ch);
                    bad++;
                    x++; y++;
                    continue;
                }
                words++;
                x++; y++;
            }
            printf("  %ld characters matched, %ld differ\n", words, bad);
            CHECK(words > 500 && bad < words / 50, "1st Word Plus round trip keeps the text and styles");
        }
        CHECK(!strcmp(HF(&d2).text[1], "Page {page}"), "1st Word Plus footer with page number");
        {
            int pb = 0, cen = 0;
            long k2;
            for (k2 = 0; k2 < d2.len; k2++) {
                if (d2.text[k2].ch == CH_PAGE) pb++;
                if (pp(&d2, k2)->just == J_CENTRE) cen = 1;
            }
            CHECK(pb > 0, "1st Word Plus page break");
            CHECK(cen, "1st Word Plus centred line");
        }
        doc_layout(&d2);

        /* --- Protext --- */
        sprintf(path, "%s/T.PRO", argv[2]);
        CHECK(pt_export(&d, path), "Protext export");
        {
            long n2;
            unsigned char *raw = load_file(path, &n2);
            CHECK(raw && pt_detect(raw, n2) && !fwp_detect(raw, n2), "Protext file detected");
            free(raw);
        }
        doc_init(&d2);
        CHECK(pt_import(&d2, path), "Protext import");
        show(&d2, "protext");
        {
            long x = 0, y = 0, bad = 0, words = 0;
            int pb = 0, cen = 0, full = 0;
            while (x < d.len && y < d2.len) {
                while (x < d.len && (d.text[x].ch == ' ' || d.text[x].ch < 32)) x++;
                while (y < d2.len && (d2.text[y].ch == ' ' || d2.text[y].ch < 32)) y++;
                if (x >= d.len || y >= d2.len) break;
                if (d.text[x].ch != d2.text[y].ch || (d.text[x].attr & 7) != (d2.text[y].attr & 7)) {
                    if (pp(&d, x)->list || pp(&d, x)->tocent) { x++; continue; }
                    if (!bad) printf("  first difference at %ld/%ld: '%c' vs '%c'\n", x, y, d.text[x].ch, d2.text[y].ch);
                    bad++;
                    x++; y++;
                    continue;
                }
                words++;
                x++; y++;
            }
            printf("  %ld characters matched, %ld differ\n", words, bad);
            CHECK(words > 500 && bad < words / 50, "Protext round trip keeps the text and styles");
            for (k = 0; k < d2.len; k++) {
                if (d2.text[k].ch == CH_PAGE) pb++;
                if (pp(&d2, k)->just == J_CENTRE) cen = 1;
                if (pp(&d2, k)->just == J_FULL) full = 1;
            }
            CHECK(pb > 0, "Protext page break");
            CHECK(cen, "Protext centred line");
            (void)full;
        }
        CHECK(!strcmp(HF(&d2).text[1], "Page {page}"), "Protext footer with page number");
        CHECK(file_detect(path, 1) == FT_PT, "Protext found by its contents");
        sprintf(path, "%s/T.DOC", argv[2]);
        CHECK(file_detect(path, 1) == FT_FWP, "1st Word Plus found by its contents");
        sprintf(path, "%s/round.rtf", argv[2]);
        CHECK(file_detect(path, 1) == FT_RTF, "RTF found by its contents");
        sprintf(path, "%s/T.PRO", argv[2]);
        doc_layout(&d2);

        /* a file written the way Protext writes one */
        {
            static const unsigned char pt[] =
                ">---!----!----!----!----!----!----!----!----!----!----!----!---R\r\n"
                ">HE                       My header %\r\n"
                ">CO a comment\r\n"
                "First \xE2" "bold\xE2 and \xF5under\xF5 words in a paragraph\x90 that \r\x8A"
                "wraps onto a \xE9second\xE9 line.\r\n"
                ">CE Centred title\r\n"
                ">PA\r\n"
                ">----L----!----!----!----!----!----!----!----!----!----!-------R\r\n"
                "Indented\x91text, soft\x8Ehyphen.\r\n\x1A";
            FILE *fp;
            sprintf(path, "%s/HAND.PRO", argv[2]);
            fp = fopen(path, "wb");
            fwrite(pt, 1, sizeof pt - 1, fp);
            fclose(fp);
            doc_init(&d2);
            CHECK(pt_import(&d2, path), "Protext hand-made file");
            show(&d2, "hand");
            a = find_next(&d2, 0, "bold", 0, &b);
            CHECK(a > 0 && (d2.text[a].attr & A_BOLD) && !(d2.text[a + 4].attr & A_BOLD), "Protext bold toggles");
            a = find_next(&d2, 0, "under", 0, &b);
            CHECK(a > 0 && (d2.text[a].attr & A_UNDER), "Protext underline");
            a = find_next(&d2, 0, "that wraps onto a second line.", 0, &b);
            CHECK(a > 0 && (d2.text[a + 18].attr & A_ITALIC) && pp(&d2, a)->just == J_FULL, "soft return joins the paragraph, justified");
            a = find_next(&d2, 0, "Centred title", 0, &b);
            CHECK(a > 0 && pp(&d2, a)->just == J_CENTRE, ">CE centred line");
            a = find_next(&d2, 0, "Indented text, softhyphen.", 0, &b);
            CHECK(a > 0 && pp(&d2, a)->left == 36, "ruler L gives the left indent");
            CHECK(!strcmp(HF(&d2).text[0], "My header {page}") && HF(&d2).just[0] == J_CENTRE, ">HE header with %");
            CHECK(find_next(&d2, 0, "comment", 0, &b) < 0, ">CO comment dropped");
            for (k = 0, a = 0; k < d2.len; k++) if (d2.text[k].ch == CH_PAGE) a++;
            CHECK(a == 1, ">PA page break");
        }
    }

    /* --- printing at a printer's resolution --- */
    {
        PRTOUT pr;
        doc_init(&d);
        rtf_import(&d, argv[1]);
        doc_layout(&d);
        memset(&pr, 0, sizeof pr);
        pr.pixw = pr.pixh = 85;                       /* 300 dpi */
        pr.band = 100;
        pr.w = 2480;
        pr.render = prt_render;
        pr.out = prt_out;
        pr.ctx = &pr;
        CHECK(print_page(&d, 0, &pr), "print a page at 300 dpi");
        printf("  print: scale %d, %ld rows of %ld dots, %ld inked bytes\n", pr.scale, rows_out, width_out, ink_out);
        CHECK(rows_out == (long)PS(&d).h * 25400L / 72L / 85, "the page's height in printer dots");
        CHECK(width_out == (long)PS(&d).w * 25400L / 72L / 85 && ink_out > 1000, "the page's width, with ink");
    }

    /* --- bitmap PDF --- */
    doc_init(&d);
    rtf_import(&d, argv[1]);
    for (scale = 1; scale <= 2; scale++) {
        po.scale = scale; po.max_band = 100; po.colour = 1;
        po.render = fake_render; po.ctx = &scale; po.progress = NULL; po.log = NULL;
        sprintf(path, "%s/out%d.pdf", argv[2], scale);
        CHECK(pdf_export(&d, path, &po), path);
    }
    po.colour = 0; scale = 1;
    sprintf(path, "%s/mono.pdf", argv[2]);
    CHECK(pdf_export(&d, path, &po), path);
    /* a page with everything: table, list, contents, drawing */
    rtf_import(&d2, argv[1]);
    sprintf(path, "%s/objects.rtf", argv[2]);
    doc_init(&d); rtf_import(&d, path);
    tbl_insert(&d, 0, 3, 3, 1);
    doc_para_set(&d, 20, 30, PF_LIST, LIST_NUMBER);
    po.colour = 1; scale = 2;
    sprintf(path, "%s/everything.pdf", argv[2]);
    CHECK(pdf_export(&d, path, &po), path);

    /* --- charts of a table's numbers --- */
    {
        static CHOP op[CH_MAXOPS];
        static CHDATA cd;
        long v;
        int pos, kind, n2, t, polys;
        CHECK(chart_number("1,234.5", &v) && v == 123450 && chart_number("3,5", &v) && v == 350 &&
              chart_number("12%", &v) && v == 1200 && chart_number("(4)", &v) && v == -400 &&
              chart_number("1.234,5", &v) && v == 123450 && chart_number("0.07", &v) && v == 7 &&
              !chart_number("abc", &v) && !chart_number("", &v),
              "numbers in cells");
        doc_init(&d);
        import_str(&d, "Sales\n", CS_AUTO, 0);
        tbl_insert(&d, d.len, 4, 3, 1);
        /* fill the cells: header row, then rows of a name and two numbers */
        {
            static const char *cells[12] = { "Fruit", "2025", "2026", "Apples", "120", "150", "Pears", "80", "60",
                                             "Plums", "40", "95" };
            long k = 0;
            int c = 0;
            for (k = 0; k < d.len && c < 12; k++)
                if (pp(&d, k)->tbl && (d.text[k].ch == CH_CELL || ISBRK(d.text[k].ch))) {
                    CELL cc = d.text[k];
                    size_t L = strlen(cells[c]), q;
                    for (q = 0; q < L; q++) { cc.ch = (unsigned char)cells[c][q]; doc_insert(&d, k + (long)q, &cc, 1); }
                    k += (long)L;
                    c++;
                }
        }
        import_str(&d2, "", CS_AUTO, 0);
        t = pp(&d, 7)->tbl - 1;
        d.a.tbls[t].w[0] = 90; d.a.tbls[t].w[1] = 60; d.a.tbls[t].w[2] = 60;     /* room beside it */
        CHECK(t >= 0 && chart_data(&d, t, &cd) && cd.ncat == 3 && cd.nser == 2 && !strcmp(cd.cat[2], "Plums") &&
              !strcmp(cd.ser[1], "2026") && cd.v[1][0] == 8000, "a table's numbers read");
        for (kind = CH_PIE; kind < NCHARTS; kind++)
            for (pos = CP_TOP; pos <= CP_RIGHT; pos++) {
                TROW *r0;
                CHBOX *cb;
                long k;
                char what[200];
                d.a.tbls[t].chart = (unsigned char)kind;
                d.a.tbls[t].cpos = (unsigned char)pos;
                doc_layout(&d);
                cb = &d.cbox[t];
                if (pos == CP_LEFT && kind == CH_BAR) {     /* no room beside it: below */
                    d.a.tbls[t].w[0] = 300; doc_layout(&d);
                    CHECK(d.cbox[t].pos == CP_BOTTOM && d.cbox[t].h > 100, "a chart with no room beside the table goes below");
                    d.a.tbls[t].w[0] = 90; doc_layout(&d);
                }
                for (k = 0; k < d.nrows && d.rows[k].tbl != t; k++) ;
                if (k + 2 >= d.nrows) { printf("  rows %ld, table %d not found\n", d.nrows, t); CHECK(0, "chart table laid out"); continue; }
                r0 = &d.rows[k];
                n2 = chart_ops(&d, t, cb->w, cb->h, op, CH_MAXOPS);
                { long q; for (q = 0, polys = 0; q < n2; q++) polys += op[q].op == CO_POLY; }
                sprintf(what, "chart %d at %d: page %d box %d,%d %dx%d, table row at %d,%d; %d drawing steps",
                        kind, pos, cb->page, cb->x, cb->y, cb->w, cb->h, r0->x, r0->y, n2);
                CHECK(cb->page == 0 && cb->w > 50 && cb->h > 50 && polys >= 3 &&
                      (pos == CP_TOP ? cb->y + cb->h <= r0->y :
                       pos == CP_BOTTOM ? cb->y >= d.rows[k + 2].y + d.rows[k + 2].h :
                       pos == CP_LEFT ? cb->x + cb->w <= r0->x && cb->y == r0->y :
                       cb->x >= r0->x + 200 && cb->y == r0->y && cb->x + cb->w <= 560), what);
            }
        /* kept in RTF and in the codes */
        d.a.tbls[t].chart = CH_BAR3D; d.a.tbls[t].cpos = CP_RIGHT;
        sprintf(path, "%s/chart.rtf", argv[2]);
        doc_init(&d2);
        CHECK(rtf_export(&d, path) && rtf_import(&d2, path) && pp(&d2, 7)->tbl &&
              d2.a.tbls[pp(&d2, 7)->tbl - 1].chart == CH_BAR3D && d2.a.tbls[pp(&d2, 7)->tbl - 1].cpos == CP_RIGHT,
              "chart kept in RTF");
        d.cv.cols = 70;
        codes_build(&d);
        CHECK(d.cv.t && strstr(d.cv.t, "Bar3D Right]") != NULL, "chart in the codes");
        doc_init(&d2);
        codes_into(&d, &d2);
        CHECK(pp(&d2, 7)->tbl && d2.a.tbls[pp(&d2, 7)->tbl - 1].chart == CH_BAR3D, "chart read from the codes");
        /* in the PDF, in colour */
        d.a.tbls[t].chart = CH_PIE3D; d.a.tbls[t].cpos = CP_BOTTOM;
        po.colour = 1; scale = 2;
        sprintf(path, "%s/chart.pdf", argv[2]);
        CHECK(pdf_export(&d, path, &po), "chart PDF");
        {
            char cmd[400];
            sprintf(cmd, "pdftoppm -r 72 -png %s %s/chartpg >/dev/null 2>&1", path, argv[2]);
            if (system(cmd)) printf("  (pdftoppm failed)\n");
        }
    }

    /* a colour picture in the PDF: an image of its own under the page */
    {
        OBJ o;
        IMG im;
        char cmd[400];
        FILE *pf;
        int ok2 = 0;
        doc_init(&d);
        import_str(&d, "Colour picture", CS_AUTO, 0);
        sprintf(path, "%s/rgb.png", argv[2]);
        img_load(path, &im);
        memset(&o, 0, sizeof o);
        o.type = OB_IMG; o.x = 200; o.y = 300; o.w = 160; o.h = 120;
        o.img = (short)doc_img_add(&d, &im);
        doc_obj_add(&d, &o);
        for (i = 0; i < 2; i++) {
            po.colour = i; scale = 1;
            sprintf(path, "%s/pic%d.pdf", argv[2], i);
            CHECK(pdf_export(&d, path, &po), path);
            /* the picture's pixel (13,11) is at 200 + 160*13.5/40, 300 + 120*11.5/30 points */
            sprintf(cmd, "pdftoppm -r 72 -x 254 -y 344 -W 1 -H 1 %s > %s/px%d.ppm 2>/dev/null", path, argv[2], i);
            if (system(cmd) == 0) {
                unsigned char px3[64];
                sprintf(path, "%s/px%d.ppm", argv[2], i);
                pf = fopen(path, "rb");
                if (pf) {
                    size_t n3 = fread(px3, 1, sizeof px3, pf);
                    fclose(pf);
                    if (n3 >= 3) {
                        unsigned char *c3 = px3 + n3 - 3;
                        printf("  PDF %s: pixel %d %d %d\n", i ? "colour" : "mono", c3[0], c3[1], c3[2]);
                        ok2 += abs(c3[0] - 78) < 20 && abs(c3[1] - 88) < 20 && abs(c3[2] - 72) < 20;
                    }
                }
            }
        }
        CHECK(ok2 == 1, "the colour picture shows in the colour PDF only");
    }
    {   /* templates */
        static const char *w[NTW] = { "To", "Addr", "Dear Sir or Madam,", "Dear Sir or Madam:", "Subject",
            "Body text.", "Yours sincerely,", "Sincerely,", "Issue 1", "Main story", "Story text.",
            "News in brief", "Dates", "What", "When", "In this issue", "Org", "Page" };
        const char *addr[3] = { "1 High St", "", "Town [X]" };
        DOC t; PGFMT f; long k, e; int ok = 1, two = 0, hp;
        int kinds[3] = { TPL_UK, TPL_US, TPL_NEWS };
        for (k = 0; k < 3; k++) {
            doc_init(&t);
            ok &= doc_template(&t, kinds[k], "Ann Smith", addr, "The Gazette", w);
            if (kinds[k] == TPL_UK) {
                ok &= PS(&t).paper == P_A4 && pp(&t, 0)->just == J_RIGHT && t.text[0].ch == 'A' &&
                      find_next(&t, 0, "Town [X]", 0, &e) >= 0 && find_next(&t, 0, "Yours sincerely,", 0, &e) > 0 &&
                      t.cursor == find_next(&t, 0, "Body text.", 0, &e) && !t.dirty;
            } else if (kinds[k] == TPL_US) {
                ok &= PS(&t).paper == P_LETTER && pp(&t, 0)->just == J_LEFT && find_next(&t, 0, "Sincerely,", 0, &e) > 0 &&
                      find_next(&t, 0, "Madam:", 0, &e) > 0;
            } else {
                ok &= find_next(&t, 0, "The Gazette", 0, &e) == 0 && t.cursor == find_next(&t, 0, "Story text.", 0, &e) &&
                      find_next(&t, 0, "In this issue", 0, &e) > 0 && t.npages == 2;
                page_fmt(&t, 1, &f); two = f.cols == 2;
                page_fmt(&t, 0, &f); two &= f.cols == 1;
                ok &= hf_pagenum_find(&t, &hp, &hp);
            }
            doc_free(&t);
        }
        CHECK(ok && two, "letter and newsletter templates");
    }
    rs_end();

    printf("%s: %d checks, %d failures\n", fails ? "FAILED" : "PASSED", checks, fails);
    return fails != 0;
}
