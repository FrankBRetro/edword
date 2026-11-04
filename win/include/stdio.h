/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* stdio.h - the small C library of the EdWord Windows port (win/sys/libc.c) */
#ifndef _STDIO_H_
#define _STDIO_H_
#include <stddef.h>
#include <stdarg.h>

typedef struct w_file FILE;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 4096

FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int    fgetc(FILE *f);
#define getc(f) fgetc(f)
char  *fgets(char *s, int n, FILE *f);
int    fputc(int c, FILE *f);
#define putc(c, f) fputc(c, f)
int    fputs(const char *s, FILE *f);
int    fseek(FILE *f, long off, int whence);
long   ftell(FILE *f);
int    fflush(FILE *f);
int    ferror(FILE *f);
int    feof(FILE *f);
int    fprintf(FILE *f, const char *fmt, ...);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    sprintf(char *s, const char *fmt, ...);
int    vsprintf(char *s, const char *fmt, va_list ap);
int    snprintf(char *s, size_t n, const char *fmt, ...);
int    vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int    remove(const char *path);
int    rename(const char *from, const char *to);
#endif
