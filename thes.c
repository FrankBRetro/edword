/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * thes.c - the thesaurus (EDWORD.THS, made by tools/mkthes.py from Open
 *          English WordNet)
 *
 * Only the block index stays in memory (about 20 KB); a look-up reads
 * one block of words, the word's senses and the synonyms' spellings from
 * the file. A word not found as it is is tried without its ending
 * (runs, running, ran -> run; bigger -> big; cities -> city).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

static FILE *tf;
static long  nwords, nblocks, ilen, wlen, dlen, wtab0, woff0, data0;
static char *idx;
static long *boff;               /* per block: its offset in wtab      */
static char **bword;             /* per block: its first word          */

static long be(const unsigned char *p, int n)
{
    long v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}

void thes_close(void)
{
    if (tf) fclose(tf);
    tf = NULL;
    free(idx); free(boff); free(bword);
    idx = NULL; boff = NULL; bword = NULL;
}

int thes_ready(void) { return tf != NULL; }

int thes_open(const char *path)
{
    unsigned char h[28];
    long i, p;
    if (tf) return 1;
    tf = fopen(path, "rb");
    if (!tf) return 0;
    if (fread(h, 1, 28, tf) != 28 || memcmp(h, "EDWTHS3\n", 8)) { thes_close(); return 0; }
    nwords = be(h + 8, 4); nblocks = be(h + 12, 4); ilen = be(h + 16, 4);
    wlen = be(h + 20, 4); dlen = be(h + 24, 4);
    wtab0 = 28 + ilen;
    woff0 = wtab0 + wlen;
    data0 = woff0 + nwords * 3;
    idx = (char *)malloc(ilen + 1);
    boff = (long *)malloc((nblocks + 1) * sizeof(long));
    bword = (char **)malloc((nblocks + 1) * sizeof(char *));
    if (!idx || !boff || !bword || fread(idx, 1, ilen, tf) != (size_t)ilen) { thes_close(); return 0; }
    idx[ilen] = 0;
    for (i = 0, p = 0; i < nblocks && p + 4 < ilen; i++) {
        boff[i] = be((unsigned char *)idx + p, 4);
        bword[i] = idx + p + 4;
        p += 4 + (long)strlen(idx + p + 4) + 1;
    }
    nblocks = i;
    boff[nblocks] = wlen;
    return 1;
}

static int rd(long pos, void *buf, long n)
{
    if (fseek(tf, pos, SEEK_SET)) return 0;
    return fread(buf, 1, n, tf) == (size_t)n;
}

/* the spelling of word number id */
static int word_of(long id, char *out)
{
    unsigned char o[3];
    char buf[WORDMAX + 1];
    long n;
    if (id < 0 || id >= nwords || !rd(woff0 + id * 3, o, 3)) return 0;
    if (fseek(tf, wtab0 + be(o, 3), SEEK_SET)) return 0;
    n = (long)fread(buf, 1, WORDMAX, tf);
    if (n <= 0) return 0;
    buf[n] = 0;
    buf[WORDMAX - 1] = 0;
    strcpy(out, buf);
    return 1;
}

/* the senses of a headword: their data offsets; returns how many */
static int find(const char *w, long *offs)
{
    long lo = 0, hi = nblocks - 1, len, p;
    unsigned char *blk;
    int n = 0;
    while (lo < hi) {
        long mid = (lo + hi + 1) / 2;
        if (strcmp(bword[mid], w) <= 0) lo = mid; else hi = mid - 1;
    }
    if (nblocks <= 0 || strcmp(bword[lo], w) > 0) return 0;
    len = boff[lo + 1] - boff[lo];
    if (len <= 0 || len > 32L * (WORDMAX + 30)) return 0;
    blk = (unsigned char *)malloc(len + 1);
    if (!blk) return 0;
    if (!rd(wtab0 + boff[lo], blk, len)) { free(blk); return 0; }
    blk[len] = 0;
    for (p = 0; p < len;) {
        char *s = (char *)blk + p;
        long sl = (long)strlen(s);
        int k, ns = blk[p + sl + 1];
        if (!strcmp(s, w)) {
            for (k = 0; k < ns && k < THS_SENSES; k++) offs[n++] = be(blk + p + sl + 2 + k * 3, 3);
            break;
        }
        p += sl + 2 + ns * 3;
    }
    free(blk);
    return n;
}

/* simple base forms of an inflected word */
static int base_forms(const char *w, char out[][WORDMAX])
{
    static const struct { const char *end, *rep; } rule[] = {
        { "ies", "y" }, { "ied", "y" }, { "ier", "y" }, { "iest", "y" }, { "ses", "s" }, { "xes", "x" },
        { "zes", "z" }, { "ches", "ch" }, { "shes", "sh" }, { "men", "man" }, { "ing", "" },
        { "ing", "e" }, { "ed", "" }, { "ed", "e" }, { "es", "" }, { "es", "e" }, { "s", "" },
        { "er", "" }, { "er", "e" }, { "est", "" }, { "est", "e" }, { NULL, NULL }
    };
    int n = 0, i, l = (int)strlen(w);
    for (i = 0; rule[i].end && n < 20; i++) {
        int el = (int)strlen(rule[i].end);
        if (l > el + 1 && !strcmp(w + l - el, rule[i].end) && l - el + (int)strlen(rule[i].rep) < WORDMAX) {
            memcpy(out[n], w, l - el);
            strcpy(out[n] + l - el, rule[i].rep);
            n++;
            /* doubled consonants: running -> run, bigger -> big */
            if (rule[i].rep[0] == 0 && l - el >= 3 && w[l - el - 1] == w[l - el - 2] && n < 20) {
                memcpy(out[n], w, l - el - 1);
                out[n][l - el - 1] = 0;
                n++;
            }
        }
    }
    return n;
}

int thes_lookup(const char *word, THSRES *r)
{
    char w[WORDMAX], base[20][WORDMAX];
    long offs[THS_SENSES];
    int n, i, k, nb;
    memset(r, 0, sizeof *r);
    if (!tf || !word[0] || strlen(word) >= WORDMAX) return 0;
    for (i = 0; word[i]; i++) w[i] = (char)st_lower((unsigned char)word[i]);
    w[i] = 0;
    strcpy(r->word, w);
    n = find(w, offs);
    if (!n) {
        nb = base_forms(w, base);
        for (i = 0; i < nb && !n; i++) {
            n = find(base[i], offs);
            if (n) strcpy(r->word, base[i]);
        }
    }
    for (i = 0; i < n; i++) {
        unsigned char hdr[2], ids[2 * 16];
        int ns, s = r->nsense;
        if (!rd(data0 + offs[i], hdr, 2)) break;
        ns = hdr[1] > 16 ? 16 : hdr[1];
        if (!rd(data0 + offs[i] + 2, ids, ns * 2)) break;
        r->pos[s] = (char)hdr[0];
        r->nsyn[s] = 0;
        for (k = 0; k < ns && r->nsyn[s] < THS_SYN; k++) {
            char t[WORDMAX];
            if (!word_of(be(ids + k * 2, 2), t)) continue;
            {
                char tl[WORDMAX];
                int j;
                for (j = 0; t[j]; j++) tl[j] = (char)st_lower((unsigned char)t[j]);
                tl[j] = 0;
                if (!strcmp(tl, r->word)) continue;
            }
            strcpy(r->syn[s][r->nsyn[s]++], t);
        }
        if (r->nsyn[s] == 0) continue;
        /* the meaning is shown as its part of speech and first synonym */
        sprintf(r->gloss[s], "%.30s", r->syn[s][0]);
        r->nsense++;
    }
    return r->nsense;
}
