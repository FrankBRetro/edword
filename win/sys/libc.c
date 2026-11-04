/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * libc.c - the small C library of the EdWord Windows port: start-up,
 * memory, strings, formatted output, buffered files and the clock, on
 * kernel32 alone, so EdWord.exe needs no C runtime DLL and runs on every
 * Windows from 95 on.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show);
void w_close_files(void);

static HANDLE heap;

/* where Windows starts EdWord */
void WinMainCRTStartup(void)
{
    STARTUPINFOA si;
    char *c = GetCommandLineA();
    int rc;
    heap = GetProcessHeap();
    /* the command line without the program's name */
    if (*c == '"') { c++; while (*c && *c != '"') c++; if (*c) c++; }
    else while (*c && *c != ' ' && *c != '\t') c++;
    while (*c == ' ' || *c == '\t') c++;
    si.cb = sizeof si;
    GetStartupInfoA(&si);
    rc = WinMain(GetModuleHandleA(NULL), NULL, c, (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT);
    w_close_files();
    ExitProcess((UINT)rc);
}

void exit(int code)
{
    w_close_files();
    ExitProcess((UINT)code);
}

/* ================================================================= */
/*  Memory                                                             */
/* ================================================================= */

void *malloc(size_t n)
{
    if (!heap) heap = GetProcessHeap();
    return HeapAlloc(heap, 0, n ? n : 1);
}

void free(void *p)
{
    if (p) HeapFree(heap, 0, p);
}

void *calloc(size_t n, size_t size)
{
    if (!heap) heap = GetProcessHeap();
    return HeapAlloc(heap, HEAP_ZERO_MEMORY, n * size ? n * size : 1);
}

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    return HeapReAlloc(heap, 0, p, n);
}

/* ================================================================= */
/*  Strings                                                            */
/* ================================================================= */

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (n >= 16 && !(((unsigned long)dp | (unsigned long)sp) & 3)) {
        unsigned long *dl = (unsigned long *)dp;
        const unsigned long *sl = (const unsigned long *)sp;
        size_t k = n >> 2;
        while (k--) *dl++ = *sl++;
        dp = (unsigned char *)dl;
        sp = (const unsigned char *)sl;
        n &= 3;
    }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp <= sp || dp >= sp + n) return memcpy(d, s, n);
    dp += n; sp += n;
    if (n >= 16 && !(((unsigned long)dp | (unsigned long)sp) & 3)) {
        unsigned long *dl = (unsigned long *)dp;
        const unsigned long *sl = (const unsigned long *)sp;
        size_t k = n >> 2;
        while (k--) *--dl = *--sl;
        dp = (unsigned char *)dl;
        sp = (const unsigned char *)sl;
        n &= 3;
    }
    while (n--) *--dp = *--sp;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dp = d;
    if (n >= 16) {
        unsigned long v = (unsigned char)c;
        v |= v << 8; v |= v << 16;
        while ((unsigned long)dp & 3) { *dp++ = (unsigned char)c; n--; }
        {
            unsigned long *dl = (unsigned long *)dp;
            size_t k = n >> 2;
            while (k--) *dl++ = v;
            dp = (unsigned char *)dl;
            n &= 3;
        }
    }
    while (n--) *dp++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p;
    return NULL;
}

size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != 0) continue; return r; }

char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n && *s) { *d++ = *s++; n--; }
    while (n--) *d++ = 0;
    return r;
}

char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }

char *strncat(char *d, const char *s, size_t n)
{
    char *e = d + strlen(d);
    while (n-- && *s) *e++ = *s++;
    *e = 0;
    return d;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}

char *strstr(const char *h, const char *n)
{
    size_t k = strlen(n);
    if (!k) return (char *)h;
    for (; *h; h++) if (*h == *n && !strncmp(h, n, k)) return (char *)h;
    return NULL;
}

int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+') p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
    else if (base == 0) base = *p == '0' ? 8 : 10;
    for (;; p++) {
        int c = (unsigned char)*p, dv;
        if (isdigit(c)) dv = c - '0';
        else if (c >= 'a' && c <= 'z') dv = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') dv = c - 'A' + 10;
        else break;
        if (dv >= base) break;
        v = v * base + dv;
    }
    if (end) *end = (char *)p;
    return v;
}

long strtol(const char *s, char **end, int base)
{
    const char *p = s;
    int neg = 0;
    long v;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '-') { neg = 1; p++; }
    v = (long)strtoul(p, end, base);
    if (end && *end == p) *end = (char *)s;
    return neg ? -v : v;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

/* a plain shell sort: EdWord sorts only short lists */
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    unsigned char *b = base, t;
    size_t gap, i, j, k;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++)
            for (j = i; j >= gap && cmp(b + (j - gap) * size, b + j * size) > 0; j -= gap)
                for (k = 0; k < size; k++) {
                    t = b[(j - gap) * size + k];
                    b[(j - gap) * size + k] = b[j * size + k];
                    b[j * size + k] = t;
                }
}

/* ================================================================= */
/*  Formatted output                                                   */
/* ================================================================= */

typedef struct { char *buf; size_t n, cap; FILE *f; } FMTOUT;

static void out_c(FMTOUT *o, char c)
{
    if (o->f) { fputc((unsigned char)c, o->f); o->n++; return; }
    if (o->n + 1 < o->cap) o->buf[o->n] = c;
    o->n++;
}

static int do_format(FMTOUT *o, const char *fmt, va_list ap)
{
    char num[40];
    for (; *fmt; fmt++) {
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1, lng = 0;
        const char *s;
        int len, i, neg = 0;
        unsigned long v;
        if (*fmt != '%') { out_c(o, *fmt); continue; }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '+') plus = 1;
            else if (*fmt == ' ') space = 1;
            else if (*fmt == '#') alt = 1;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
        else while (isdigit((unsigned char)*fmt)) width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (isdigit((unsigned char)*fmt)) prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l' || *fmt == 'h') { if (*fmt == 'l') lng = 1; fmt++; }
        switch (*fmt) {
        case 'c':
            num[0] = (char)va_arg(ap, int);
            s = num; len = 1;
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            for (len = 0; s[len] && (prec < 0 || len < prec); len++) ;
            break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            int base = *fmt == 'x' || *fmt == 'X' || *fmt == 'p' ? 16 : *fmt == 'o' ? 8 : 10;
            const char *dig = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char *e = num + sizeof num, *p = e;
            if (*fmt == 'p') { v = (unsigned long)va_arg(ap, void *); alt = 1; }
            else if (*fmt == 'd' || *fmt == 'i') {
                long sv = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
                if (sv < 0) { neg = 1; v = (unsigned long)-sv; } else v = (unsigned long)sv;
            } else v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            do { *--p = dig[v % base]; v /= base; } while (v);
            while (prec >= 0 && e - p < prec) *--p = '0';
            if (alt && base == 16 && (*fmt == 'p' || p[0] != '0')) { *--p = *fmt == 'X' ? 'X' : 'x'; *--p = '0'; }
            if (alt && base == 8 && *p != '0') *--p = '0';
            if (neg) *--p = '-';
            else if (plus && (*fmt == 'd' || *fmt == 'i')) *--p = '+';
            else if (space && (*fmt == 'd' || *fmt == 'i')) *--p = ' ';
            s = p; len = (int)(e - p);
            if (zero && !left && prec < 0) {
                int pre = (neg || plus || space) && (*fmt == 'd' || *fmt == 'i') ? 1 : 0;
                if (pre) { out_c(o, *s); s++; len--; width--; }
                for (i = len; i < width; i++) out_c(o, '0');
                width = 0;
            }
            break;
        }
        case '%':
            s = "%"; len = 1;
            break;
        case 0:
            fmt--;
            continue;
        default:
            num[0] = '%'; num[1] = *fmt;
            s = num; len = 2;
            break;
        }
        if (!left) for (i = len; i < width; i++) out_c(o, ' ');
        for (i = 0; i < len; i++) out_c(o, s[i]);
        if (left) for (i = len; i < width; i++) out_c(o, ' ');
    }
    return (int)o->n;
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    FMTOUT o;
    int r;
    o.buf = s; o.n = 0; o.cap = n; o.f = NULL;
    r = do_format(&o, fmt, ap);
    if (n) s[o.n < n ? o.n : n - 1] = 0;
    return r;
}

int vsprintf(char *s, const char *fmt, va_list ap) { return vsnprintf(s, 0x7fffffff, fmt, ap); }

int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(s, 0x7fffffff, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    FMTOUT o;
    o.buf = NULL; o.n = 0; o.cap = 0; o.f = f;
    return do_format(&o, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

/* ================================================================= */
/*  Files: buffered, on Win32 handles                                  */
/* ================================================================= */

#define FBUF 8192
struct w_file {
    HANDLE fh;
    int   writing, err, eof;
    long  pos, len;                  /* read: next byte and bytes in the buffer; write: bytes held */
    struct w_file *next;
    unsigned char buf[FBUF];
};
static struct w_file *files;

FILE *fopen(const char *path, const char *mode)
{
    FILE *f;
    HANDLE fh;
    DWORD acc, disp;
    switch (mode[0]) {
    case 'r': acc = GENERIC_READ; disp = OPEN_EXISTING; break;
    case 'w': acc = GENERIC_WRITE; disp = CREATE_ALWAYS; break;
    case 'a': acc = GENERIC_WRITE; disp = OPEN_ALWAYS; break;
    default: return NULL;
    }
    fh = CreateFileA(path, acc, FILE_SHARE_READ, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) return NULL;
    f = (FILE *)malloc(sizeof *f);
    if (!f) { CloseHandle(fh); return NULL; }
    memset(f, 0, sizeof *f - FBUF);
    f->fh = fh;
    f->writing = mode[0] != 'r';
    if (mode[0] == 'a') SetFilePointer(fh, 0, NULL, FILE_END);
    f->next = files;
    files = f;
    return f;
}

static int flush_w(FILE *f)
{
    DWORD done;
    if (f->writing && f->len) {
        if (!WriteFile(f->fh, f->buf, (DWORD)f->len, &done, NULL) || done != (DWORD)f->len) f->err = 1;
        f->len = 0;
    }
    return f->err ? EOF : 0;
}

int fflush(FILE *f) { return f ? flush_w(f) : 0; }

int fclose(FILE *f)
{
    struct w_file **pp;
    int r;
    if (!f) return EOF;
    r = flush_w(f);
    CloseHandle(f->fh);
    for (pp = &files; *pp; pp = &(*pp)->next) if (*pp == f) { *pp = f->next; break; }
    free(f);
    return r;
}

void w_close_files(void)
{
    while (files) fclose(files);
}

static int fill(FILE *f)
{
    DWORD n;
    if (f->eof || f->err) return 0;
    if (!ReadFile(f->fh, f->buf, FBUF, &n, NULL)) { f->err = 1; return 0; }
    if (n == 0) { f->eof = 1; return 0; }
    f->pos = 0;
    f->len = (long)n;
    return 1;
}

int fgetc(FILE *f)
{
    if (f->writing) return EOF;
    if (f->pos >= f->len && !fill(f)) return EOF;
    return f->buf[f->pos++];
}

size_t fread(void *p, size_t size, size_t n, FILE *f)
{
    unsigned char *d = p;
    size_t want = size * n, got = 0;
    if (!want || f->writing) return 0;
    while (got < want) {
        long avail = f->len - f->pos;
        if (avail <= 0) {
            if (want - got >= FBUF) {               /* big reads go straight to the caller */
                DWORD r;
                if (!ReadFile(f->fh, d + got, (DWORD)(want - got), &r, NULL)) { f->err = 1; break; }
                if (r == 0) { f->eof = 1; break; }
                got += r;
                continue;
            }
            if (!fill(f)) break;
            avail = f->len;
        }
        if ((size_t)avail > want - got) avail = (long)(want - got);
        memcpy(d + got, f->buf + f->pos, (size_t)avail);
        f->pos += avail;
        got += (size_t)avail;
    }
    return got / size;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0, c;
    while (i < n - 1) {
        c = fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (!i) return NULL;
    s[i] = 0;
    return s;
}

int fputc(int c, FILE *f)
{
    if (!f->writing) return EOF;
    if (f->len >= FBUF && flush_w(f)) return EOF;
    f->buf[f->len++] = (unsigned char)c;
    return (unsigned char)c;
}

size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    const unsigned char *s = p;
    size_t want = size * n, i;
    if (!want || !f->writing) return 0;
    if (want >= FBUF) {
        DWORD done;
        if (flush_w(f)) return 0;
        if (!WriteFile(f->fh, s, (DWORD)want, &done, NULL) || done != (DWORD)want) { f->err = 1; return 0; }
        return n;
    }
    for (i = 0; i < want; i++) if (fputc(s[i], f) == EOF) return i / size;
    return n;
}

int fputs(const char *s, FILE *f)
{
    while (*s) if (fputc((unsigned char)*s++, f) == EOF) return EOF;
    return 0;
}

long ftell(FILE *f)
{
    DWORD p = SetFilePointer(f->fh, 0, NULL, FILE_CURRENT);
    if (p == INVALID_SET_FILE_POINTER) return -1;
    return f->writing ? (long)p + f->len : (long)p - (f->len - f->pos);
}

int fseek(FILE *f, long off, int whence)
{
    DWORD how = whence == SEEK_SET ? FILE_BEGIN : whence == SEEK_CUR ? FILE_CURRENT : FILE_END;
    if (f->writing) { if (flush_w(f)) return -1; }
    else {
        if (whence == SEEK_CUR) off -= f->len - f->pos;
        f->pos = f->len = 0;
        f->eof = 0;
    }
    if (SetFilePointer(f->fh, off, NULL, how) == INVALID_SET_FILE_POINTER) return -1;
    return 0;
}

int ferror(FILE *f) { return f->err; }
int feof(FILE *f) { return f->eof && f->pos >= f->len; }

int remove(const char *path) { return DeleteFileA(path) ? 0 : -1; }
int rename(const char *from, const char *to) { return MoveFileA(from, to) ? 0 : -1; }

/* ================================================================= */
/*  The clock: local time, seconds from 1970                           */
/* ================================================================= */

static long days_from_civil(int y, int m, int d)     /* m 1..12 */
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

time_t time(time_t *t)
{
    SYSTEMTIME st;
    time_t v;
    GetLocalTime(&st);
    v = days_from_civil(st.wYear, st.wMonth, st.wDay) * 86400L + st.wHour * 3600L + st.wMinute * 60L + st.wSecond;
    if (t) *t = v;
    return v;
}

struct tm *localtime(const time_t *t)
{
    static struct tm tm;
    static const unsigned char mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    long days = *t / 86400L, secs = *t % 86400L;
    int y = 1970, m;
    tm.tm_sec = (int)(secs % 60);
    tm.tm_min = (int)(secs / 60 % 60);
    tm.tm_hour = (int)(secs / 3600);
    tm.tm_wday = (int)((days + 4) % 7);                /* 1 January 1970 was a Thursday */
    for (;;) {
        int yl = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        if (days < yl) break;
        days -= yl;
        y++;
    }
    tm.tm_yday = (int)days;
    for (m = 0; m < 12; m++) {
        int ml = mdays[m] + (m == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0);
        if (days < ml) break;
        days -= ml;
    }
    tm.tm_year = y - 1900;
    tm.tm_mon = m;
    tm.tm_mday = (int)days + 1;
    tm.tm_isdst = 0;
    return &tm;
}

/* ================================================================= */
/*  What the compiler calls on a 32-bit x86                            */
/* ================================================================= */

long long __divdi3(long long a, long long b);
long long __moddi3(long long a, long long b);
unsigned long long __udivdi3(unsigned long long a, unsigned long long b);
unsigned long long __umoddi3(unsigned long long a, unsigned long long b);

static unsigned long long udivmod(unsigned long long n, unsigned long long d, unsigned long long *rem)
{
    unsigned long long q = 0, r = 0;
    int i;
    if (d == 0) { if (rem) *rem = 0; return 0; }
    if ((n >> 32) == 0 && (d >> 32) == 0) {
        unsigned long nl = (unsigned long)n, dl = (unsigned long)d;
        if (rem) *rem = nl % dl;
        return nl / dl;
    }
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= 1ULL << i; }
    }
    if (rem) *rem = r;
    return q;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b) { return udivmod(a, b, NULL); }
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) { unsigned long long r; udivmod(a, b, &r); return r; }
long long __divdi3(long long a, long long b)
{
    int neg = (a < 0) != (b < 0);
    unsigned long long q = udivmod(a < 0 ? -(unsigned long long)a : (unsigned long long)a,
                                   b < 0 ? -(unsigned long long)b : (unsigned long long)b, NULL);
    return neg ? -(long long)q : (long long)q;
}
long long __moddi3(long long a, long long b)
{
    unsigned long long r;
    udivmod(a < 0 ? -(unsigned long long)a : (unsigned long long)a, b < 0 ? -(unsigned long long)b : (unsigned long long)b, &r);
    return a < 0 ? -(long long)r : (long long)r;
}
