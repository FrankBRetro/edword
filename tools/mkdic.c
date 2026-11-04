/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * mkdic.c - build EDWORD.DIC from a word list (one word per line,
 *           UTF-8 or plain ASCII). Runs on a PC or on the ST.
 *
 *   mkdic words.txt EDWORD.DIC
 *
 * Possessive forms (word's) are dropped: the checker accepts them when
 * the stem is known. Words that can't be written in the ST character
 * set are skipped.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../wp.h"

#define BLOCK 32

static int cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void put32(FILE *f, long v)
{
    fputc((int)((v >> 24) & 255), f); fputc((int)((v >> 16) & 255), f);
    fputc((int)((v >> 8) & 255), f);  fputc((int)(v & 255), f);
}

/* UTF-8 line -> ST charset; 0 if it can't be represented */
static int convert(const char *in, char *out)
{
    const unsigned char *s = (const unsigned char *)in;
    int n = 0;
    while (*s && *s != '\n' && *s != '\r') {
        long u;
        if (*s < 0x80) u = *s++;
        else if ((*s & 0xE0) == 0xC0 && s[1]) { u = ((long)(*s & 0x1F) << 6) | (s[1] & 0x3F); s += 2; }
        else if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
            u = ((long)(*s & 0x0F) << 12) | ((long)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); s += 3;
        } else return 0;
        if (u == '?') return 0;
        out[n] = (char)uni_to_st(u);
        if (out[n] == '?' || out[n] == ' ') return 0;
        if (++n >= WORDMAX) return 0;
    }
    out[n] = 0;
    return n;
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    char line[256], w[WORDMAX + 1], **words = NULL, *prev = "";
    long n = 0, cap = 0, i, nu = 0, nb, dl = 0, *offs;
    unsigned char *data;

    if (argc < 3) { fprintf(stderr, "usage: mkdic words.txt EDWORD.DIC\n"); return 2; }
    in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }
    while (fgets(line, sizeof line, in)) {
        int l = convert(line, w);
        if (l < 1) continue;
        if (l > 2 && w[l - 2] == '\'' && w[l - 1] == 's') continue;
        if (n >= cap) {
            cap = cap ? cap * 2 : 4096;
            words = (char **)realloc(words, cap * sizeof(char *));
            if (!words) { fprintf(stderr, "out of memory\n"); return 1; }
        }
        words[n] = (char *)malloc(l + 1);
        strcpy(words[n++], w);
    }
    fclose(in);
    qsort(words, n, sizeof(char *), cmp);
    for (i = 0; i < n; i++)                         /* unique */
        if (!nu || strcmp(words[i], words[nu - 1])) words[nu++] = words[i];

    nb = (nu + BLOCK - 1) / BLOCK;
    offs = (long *)malloc((nb + 1) * sizeof(long));
    data = (unsigned char *)malloc(nu * (WORDMAX + 2));
    for (i = 0; i < nu; i++) {
        const char *s = words[i];
        if (i % BLOCK == 0) {
            offs[i / BLOCK] = dl;
            strcpy((char *)data + dl, s);
            dl += (long)strlen(s) + 1;
        } else {
            int p = 0;
            while (s[p] && s[p] == prev[p]) p++;
            data[dl++] = (unsigned char)p;
            strcpy((char *)data + dl, s + p);
            dl += (long)strlen(s + p) + 1;
        }
        prev = words[i];
    }
    out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }
    fwrite("EDWDIC1\n", 1, 8, out);
    put32(out, nu); put32(out, nb); put32(out, dl);
    for (i = 0; i < nb; i++) put32(out, offs[i]);
    fwrite(data, 1, dl, out);
    fclose(out);
    printf("%ld words, %ld blocks, %ld bytes of data\n", nu, nb, dl);
    return 0;
}
