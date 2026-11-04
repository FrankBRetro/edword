/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * cpucheck.c - a version of EdWord made for a bigger processor (make CPU=...)
 * checks the processor before anything else runs, and on one it can't run on
 * says so in an alert and quits, instead of crashing with bombs.
 *
 * This file and lang.c are compiled for the plain 68000 in every 68k build
 * (see the Makefile), so the check and its alert run on any ST. It uses no
 * library code: the cookie jar through Supexec, and the AES called directly.
 */
#include <osbind.h>
#include "lang.h"

#ifdef ED_CPU

static long jar_addr(void) { return *(volatile long *)0x5A0L; }

static int cookie(long id, long *val)
{
    long *c = (long *)Supexec(jar_addr);
    if (!c) return 0;
    for (; c[0]; c += 2)
        if (c[0] == id) { *val = c[1]; return 1; }
    return 0;
}

/* the AES, without gemlib */
static short ctrl[5], glob[15], ain[16], aout[16];
static void *adin[2], *adout[2];
static void *apb[6] = { ctrl, glob, ain, aout, adin, adout };

static void aes(short op, short nin, short nout, short nadr)
{
    ctrl[0] = op; ctrl[1] = nin; ctrl[2] = nout; ctrl[3] = nadr; ctrl[4] = 0;
    __asm__ volatile ("move.l %0,%%d1\n\tmove.w #200,%%d0\n\ttrap #2"
                      : : "g"(apb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory");
}

static char *put(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }

static int cpu_ok(void)
{
#ifdef __mcoldfire__
    long v;
    return cookie(0x5F43465FL, &v);                 /* '_CF_' */
#else
    long cpu = 0, fpu = 0;
    (void)cookie(0x5F435055L, &cpu);                /* '_CPU' */
    (void)cookie(0x5F465055L, &fpu);                /* '_FPU' */
    /* this file is compiled for the 68000, so the Makefile says what the
     * rest needs: ED_CPUMIN 20 or 40 (a 68040 build's FPU code isn't all
     * there on a 68881/68882), and ED_FPU */
    if (cpu < ED_CPUMIN) return 0;
#ifdef ED_FPU
    if (!(fpu >> 16)) return 0;                     /* no FPU */
#endif
    return 1;
#endif
}

void cpu_check(void)
{
    static char a[200];
    const char *t;
    char *p;
    long akp;
    if (cpu_ok()) return;
    ui_lang = LANG_EN;
    if (cookie(0x5F414B50L, &akp))                  /* '_AKP': the TOS language */
        switch ((int)((akp >> 8) & 0xFF)) {
        case 1: case 8: ui_lang = LANG_DE; break;
        case 2: case 7: ui_lang = LANG_FR; break;
        case 4: ui_lang = LANG_ES; break;
        case 5: ui_lang = LANG_IT; break;
        }
    p = put(a, "[3][");
    for (t = L(AL_WRONG_CPU); *t; t++)
        if (t[0] == '%' && t[1] == 's') { p = put(p, ED_CPU); t++; }
        else *p++ = *t;
    p = put(p, "][");
    p = put(p, L(AB_QUIT));
    put(p, "]");
    aes(10, 0, 1, 0);                               /* appl_init */
    ain[0] = 1; adin[0] = a;
    aes(52, 1, 1, 1);                               /* form_alert */
    aes(19, 0, 1, 0);                               /* appl_exit */
    Pterm0();
}

#else
typedef int cpucheck_unused;
#endif
