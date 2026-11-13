/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * spell.c - spell checker with a compressed main dictionary and up to
 *           four plain-text user dictionaries
 *
 * EDWORD.DIC layout (big-endian):
 *   "EDWDIC1\n" nwords:4 nblocks:4 datalen:4 offset[nblocks]:4 data
 * Words are sorted (byte order, ST charset) and stored in blocks of 32:
 * the first word in full, the rest as <shared prefix length> <suffix>,
 * every word ending with a 0 byte.
 *
 * A user dictionary is a text file with one word per line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

#define BLOCK 32

static unsigned char *mdata;
static long *moff, mblocks, mlen;
static unsigned char alpha[96];          /* the dictionary's lower-case letters */
static int nalpha;
int spell_lang;                          /* the main dictionary's language    */

typedef struct { char **w; long n, cap; } WLIST;
static WLIST udict[MAX_UDICT], ignored;
int  spell_nuser, spell_active = -1;
char spell_upath[MAX_UDICT][256];

/* ---- word lists ----------------------------------------------------- */

static long wl_find(WLIST *l, const char *w, int *found)
{
    long lo = 0, hi = l->n;
    while (lo < hi) {
        long mid = (lo + hi) / 2;
        int c = strcmp(l->w[mid], w);
        if (c == 0) { *found = 1; return mid; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    *found = 0;
    return lo;
}

static int wl_has(WLIST *l, const char *w)
{
    int f;
    if (!l->n) return 0;
    wl_find(l, w, &f);
    return f;
}

static int wl_add(WLIST *l, const char *w)
{
    int f;
    long i = wl_find(l, w, &f);
    char *s;
    if (f) return 1;
    if (l->n >= l->cap) {
        long nc = l->cap ? l->cap * 2 : 64;
        char **nw = (char **)realloc(l->w, nc * sizeof(char *));
        if (!nw) return 0;
        l->w = nw; l->cap = nc;
    }
    s = (char *)malloc(strlen(w) + 1);
    if (!s) return 0;
    strcpy(s, w);
    memmove(l->w + i + 1, l->w + i, (l->n - i) * sizeof(char *));
    l->w[i] = s;
    l->n++;
    return 1;
}

static void wl_free(WLIST *l)
{
    long i;
    for (i = 0; i < l->n; i++) free(l->w[i]);
    free(l->w);
    l->w = NULL; l->n = l->cap = 0;
}

/* ---- main dictionary ------------------------------------------------ */

static long be32(const unsigned char *p)
{
    return ((long)p[0] << 24) | ((long)p[1] << 16) | ((long)p[2] << 8) | p[3];
}

int spell_load_main(const char *path)
{
    FILE *f;
    unsigned char h[20];
    long i, nb, dl;
    unsigned char *ob;
    int ok = 0;

    if (mdata) return 1;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(h, 1, 20, f) != 20 || memcmp(h, "EDWDIC1\n", 8)) { fclose(f); return 0; }
    nb = be32(h + 12);
    dl = be32(h + 16);
    moff = (long *)malloc(nb * sizeof(long) + 4);
    ob = (unsigned char *)malloc(nb * 4 + 4);
    mdata = (unsigned char *)malloc(dl + 1);
    if (moff && ob && mdata && fread(ob, 4, nb, f) == (size_t)nb) {
        long got = 0;                   /* in parts, to show how far it has got */
        while (got < dl) {
            long k = dl - got > 16384L ? 16384L : dl - got;
            if (fread(mdata + got, 1, k, f) != (size_t)k) break;
            got += k;
            PROG(got, dl);
        }
        ok = got == dl;
    }
    if (!ok) {
        fclose(f); free(ob); free(moff); free(mdata);
        moff = NULL; mdata = NULL;
        return 0;
    }
    fclose(f);
    for (i = 0; i < nb; i++) moff[i] = be32(ob + i * 4);
    free(ob);
    mblocks = nb;
    mlen = dl;
    /* the letters its words use, for the suggestions */
    {
        static unsigned char seen[256];
        int c;
        memset(seen, 0, sizeof seen);
        for (i = 0; i < dl; i++) seen[mdata[i]] = 1;
        nalpha = 0;
        for (c = 'a'; c <= 'z'; c++) alpha[nalpha++] = (unsigned char)c;
        for (c = 128; c < 256 && nalpha < (int)sizeof alpha - 1; c++)
            if (seen[c] && st_isalpha((unsigned char)c) && st_lower((unsigned char)c) == c) alpha[nalpha++] = (unsigned char)c;
    }
    return 1;
}

void spell_unload_main(void)
{
    free(moff); free(mdata);
    moff = NULL; mdata = NULL;
    mblocks = mlen = 0;
}

/* the main dictionary's file name for a language (EDWORD.DIC: English) */
const char *spell_dic_name(int lang)
{
    static const char *n[] = { "EDWORD.DIC", "EDWORDDE.DIC", "EDWORDES.DIC", "EDWORDIT.DIC", "EDWORDFR.DIC", "EDWORDCS.DIC" };
    return lang >= 1 && lang <= 5 ? n[lang] : n[0];
}

int spell_main_loaded(void) { return mdata != NULL; }

static int main_has(const char *w)
{
    long lo = 0, hi = mblocks - 1, p, end;
    char cur[WORDMAX + 4];
    int c, i;

    if (!mdata) return 0;
    while (lo < hi) {                       /* last block whose first word <= w */
        long mid = (lo + hi + 1) / 2;
        if (strcmp((char *)mdata + moff[mid], w) <= 0) lo = mid; else hi = mid - 1;
    }
    p = moff[lo];
    end = lo + 1 < mblocks ? moff[lo + 1] : mlen;
    strncpy(cur, (char *)mdata + p, WORDMAX);
    cur[WORDMAX] = 0;
    c = strcmp(cur, w);
    if (c == 0) return 1;
    if (c > 0) return 0;
    p += strlen((char *)mdata + p) + 1;
    for (i = 1; i < BLOCK && p < end; i++) {
        int pre = mdata[p++];
        long sl = (long)strlen((char *)mdata + p);
        if (pre + sl > WORDMAX) { p += sl + 1; continue; }
        memcpy(cur + pre, mdata + p, sl + 1);
        p += sl + 1;
        c = strcmp(cur, w);
        if (c == 0) return 1;
        if (c > 0) return 0;
    }
    return 0;
}

/* ---- user dictionaries --------------------------------------------- */

int spell_user_load(const char *path, int create)
{
    FILE *f;
    char line[80];
    int slot, i;

    for (i = 0; i < spell_nuser; i++)
        if (!strcmp(spell_upath[i], path)) { spell_active = i; return i; }
    if (spell_nuser >= MAX_UDICT) return -1;
    f = fopen(path, "r");
    if (!f) {
        if (!create || !(f = fopen(path, "w"))) return -1;
        fclose(f);
        f = NULL;
    }
    slot = spell_nuser++;
    strncpy(spell_upath[slot], path, 255);
    spell_upath[slot][255] = 0;
    while (f && fgets(line, sizeof line, f)) {
        int n = (int)strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (n > 0 && n < WORDMAX && line[0] != '#') wl_add(&udict[slot], line);
    }
    if (f) fclose(f);
    spell_active = slot;
    return slot;
}

void spell_user_unload(int slot)
{
    int i;
    if (slot < 0 || slot >= spell_nuser) return;
    wl_free(&udict[slot]);
    for (i = slot; i < spell_nuser - 1; i++) {
        udict[i] = udict[i + 1];
        strcpy(spell_upath[i], spell_upath[i + 1]);
    }
    spell_nuser--;
    memset(&udict[spell_nuser], 0, sizeof(WLIST));
    if (spell_active >= spell_nuser) spell_active = spell_nuser - 1;
}

/* add to the active user dictionary, and append it to the file */
int spell_user_add(const char *word)
{
    FILE *f;
    if (spell_active < 0) return 0;
    if (!wl_add(&udict[spell_active], word)) return 0;
    f = fopen(spell_upath[spell_active], "a");
    if (!f) return 0;
    fprintf(f, "%s\n", word);
    fclose(f);
    return 1;
}

void spell_ignore(const char *word) { wl_add(&ignored, word); }

void spell_free_all(void)
{
    while (spell_nuser > 0) spell_user_unload(spell_nuser - 1);
    wl_free(&ignored);
    free(mdata); free(moff);
    mdata = NULL; moff = NULL;
}

/* ---- checking -------------------------------------------------------- */

static int known(const char *w)
{
    int i;
    if (main_has(w) || wl_has(&ignored, w)) return 1;
    for (i = 0; i < spell_nuser; i++) if (wl_has(&udict[i], w)) return 1;
    return 0;
}

static void lowcopy(char *d, const char *s)
{
    while (*s) *d++ = (char)st_lower((unsigned char)*s++);
    *d = 0;
}

static int compound_part(const char *w, int first)
{
    char t[WORDMAX];
    strcpy(t, w);
    if (known(t)) return 1;
    if (!first) {                           /* the second part as a word of its own */
        t[0] = (char)st_upper((unsigned char)t[0]);
        if (known(t)) return 1;
    }
    lowcopy(t, w);
    return known(t);
}

static int compound(const char *w)
{
    static const char *joint[] = { "", "s", "es", "n", "en", NULL };
    char a[WORDMAX];
    int n = (int)strlen(w), i, j;
    for (i = 3; i <= n - 3; i++) {
        const char *b = w + i;
        if (!compound_part(b, 0) && !(n - i >= 7 && compound(b))) continue;
        for (j = 0; joint[j]; j++) {
            int k = (int)strlen(joint[j]);
            if (i - k < 3 || strncmp(w + i - k, joint[j], (size_t)k)) continue;
            memcpy(a, w, (size_t)(i - k));
            a[i - k] = 0;
            if (compound_part(a, 1)) return 1;
        }
    }
    return 0;
}

int spell_check(const char *w)
{
    char lw[WORDMAX], cw[WORDMAX];
    int n = (int)strlen(w), i, cap, allcap = 1;

    if (!mdata && !spell_nuser) return 1;
    if (n < 2 || n >= WORDMAX) return 1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)w[i];
        if (c >= '0' && c <= '9') return 1;
        if (st_isalpha(c) && st_lower(c) == c) allcap = 0;
    }
    if (known(w)) return 1;
    lowcopy(lw, w);
    cap = st_upper((unsigned char)w[0]) == (unsigned char)w[0];
    if ((cap || allcap) && known(lw)) return 1;
    if (allcap) {
        strcpy(cw, lw);
        cw[0] = (char)st_upper((unsigned char)cw[0]);
        if (known(cw)) return 1;
    }
    /* elided articles and prepositions: l'homme, dell'anno, qu'il */
    {
        const char *ap = strchr(w, '\'');
        if (ap && ap > w && ap - w <= 5 && ap[1] && ap[2] && spell_check(ap + 1)) return 1;
    }
    /* German compounds: two known words, maybe joined by s, es, n or en */
    if (spell_lang == 1 && n >= 7 && compound(w)) return 1;
    /* possessives and plural possessives */
    if (n > 3 && w[n - 2] == '\'' && (w[n - 1] == 's' || w[n - 1] == 'S')) {
        strcpy(cw, w); cw[n - 2] = 0;
        return spell_check(cw);
    }
    if (n > 3 && w[n - 1] == '\'') {
        strcpy(cw, w); cw[n - 1] = 0;
        return spell_check(cw);
    }
    return 0;
}

/* ---- suggestions ----------------------------------------------------- */

/* Candidates one edit away are collected with a rank for the kind of
 * slip, and the best returned: a missing apostrophe, swapped letters,
 * a doubled or undoubled letter, a wrong vowel, a dropped letter, an
 * extra letter, another wrong letter, a missing space. */
#define MAXCAND 48
static char cand_w[MAXCAND][WORDMAX];
static int  cand_r[MAXCAND], ncand, scap, sallcap;

static int is_vowel(char ch)
{
    static const char base[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyby";
    long u = st_to_uni((unsigned char)ch);
    char c = (char)(u >= 0xE0 && u <= 0xFF ? base[u - 0xE0] : u < 128 ? (char)u : 0);
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}

static void cand(const char *cw, int rank)
{
    char t[WORDMAX];
    int i;
    if (ncand >= MAXCAND || (int)strlen(cw) >= WORDMAX) return;
    if (known(cw)) strcpy(t, cw);
    else {
        strcpy(t, cw);
        t[0] = (char)st_upper((unsigned char)t[0]);     /* proper nouns */
        if (!known(t)) return;
    }
    if (sallcap) for (i = 0; t[i]; i++) t[i] = (char)st_upper((unsigned char)t[i]);
    else if (scap) t[0] = (char)st_upper((unsigned char)t[0]);
    for (i = 0; i < ncand; i++)
        if (!strcmp(cand_w[i], t)) { if (rank < cand_r[i]) cand_r[i] = rank; return; }
    strcpy(cand_w[ncand], t);
    cand_r[ncand++] = rank;
}

int spell_suggest(const char *word, char out[][WORDMAX], int max)
{
    char w[WORDMAX], c[WORDMAX + 2];
    int n = 0, len, i, r, k;
    char ch;

    len = (int)strlen(word);
    if (len < 1 || len >= WORDMAX - 1) return 0;
    lowcopy(w, word);
    scap = st_isalpha((unsigned char)word[0]) &&
           st_upper((unsigned char)word[0]) == (unsigned char)word[0];
    sallcap = len > 1;
    for (i = 0; i < len; i++)
        if (st_isalpha((unsigned char)word[i]) && st_lower((unsigned char)word[i]) == (unsigned char)word[i])
            sallcap = 0;
    ncand = 0;

    for (i = 0; i + 1 < len; i++) {                 /* transpositions */
        strcpy(c, w);
        ch = c[i]; c[i] = c[i + 1]; c[i + 1] = ch;
        cand(c, 1);
    }
    for (i = 0; i < len && len > 1; i++) {          /* deletions */
        memcpy(c, w, i);
        strcpy(c + i, w + i + 1);
        cand(c, (i > 0 && w[i] == w[i - 1]) ? 2 : 5);
    }
    if (!nalpha) for (nalpha = 0; nalpha < 26; nalpha++) alpha[nalpha] = (unsigned char)('a' + nalpha);
    for (i = 0; i < len; i++)                       /* substitutions */
        for (k = 0; k < nalpha; k++) {
            ch = (char)alpha[k];
            if (w[i] == ch) continue;
            strcpy(c, w);
            c[i] = ch;
            cand(c, is_vowel(ch) && is_vowel(w[i]) ? 3 : 6);
        }
    for (i = 0; i <= len; i++)                      /* insertions */
        for (k = 0; k <= nalpha; k++) {
            char ic = (char)(k == nalpha ? '\'' : alpha[k]);
            if (ic == '\'' && (i == 0 || i == len)) continue;
            memcpy(c, w, i);
            c[i] = ic;
            strcpy(c + i + 1, w + i);
            cand(c, ic == '\'' ? 0 : (i > 0 && w[i - 1] == ic) ? 2 : 5);
        }
    for (i = 2; i <= len - 2 && ncand < MAXCAND; i++) {   /* missing space */
        char a[WORDMAX], b[WORDMAX];
        memcpy(a, w, i); a[i] = 0;
        strcpy(b, w + i);
        if (known(a) && known(b)) {
            sprintf(c, "%.15s %.15s", a, b);
            if (scap) c[0] = (char)st_upper((unsigned char)c[0]);
            strcpy(cand_w[ncand], c);
            cand_r[ncand++] = 7;
        }
    }
    for (r = 0; r <= 7 && n < max; r++)
        for (i = 0; i < ncand && n < max; i++)
            if (cand_r[i] == r) strcpy(out[n++], cand_w[i]);
    return n;
}

/* ---- scanning a document --------------------------------------------- */

static int wordch(DOC *d, long k)
{
    unsigned char c = d->text[k].ch;
    if (st_isalpha(c) || (c >= '0' && c <= '9')) return 1;
    /* an apostrophe inside a word */
    return c == '\'' && k > 0 && k + 1 < d->len &&
           st_isalpha(d->text[k - 1].ch) && st_isalpha(d->text[k + 1].ch);
}

/* find the next misspelt word at or after from; returns its start */
long spell_next(DOC *d, long from, long *end, char *word)
{
    long k = from;
    if (k > 0 && k < d->len && wordch(d, k - 1))          /* skip the rest of a word */
        while (k < d->len && wordch(d, k)) k++;
    while (k < d->len) {
        long s, e, n;
        while (k < d->len && !(st_isalpha(d->text[k].ch) || (d->text[k].ch >= '0' && d->text[k].ch <= '9'))) k++;
        if (k >= d->len) break;
        s = k;
        while (k < d->len && wordch(d, k)) k++;
        e = k;
        /* a trailing possessive apostrophe (students') */
        if (e < d->len && d->text[e].ch == '\'' && e > s && (d->text[e - 1].ch == 's')) e++;
        n = e - s;
        if (n >= 2 && n < WORDMAX) {
            long i;
            for (i = 0; i < n; i++) word[i] = (char)d->text[s + i].ch;
            word[n] = 0;
            if (!spell_check(word)) { *end = e; return s; }
        }
        k = e;
    }
    return -1;
}
