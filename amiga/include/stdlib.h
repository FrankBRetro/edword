/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* stdlib.h - the small C library of the EdWord Amiga port */
#ifndef _STDLIB_H_
#define _STDLIB_H_
#include <stddef.h>
#ifndef NULL
#define NULL ((void *)0)
#endif
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void  free(void *p);
int   atoi(const char *s);
long  atol(const char *s);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int   abs(int v);
long  labs(long v);
void  qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void  exit(int code);
#endif
