/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* ctype.h - the small C library of the EdWord Amiga port (ASCII only) */
#ifndef _CTYPE_H_
#define _CTYPE_H_
#define isdigit(c)  ((unsigned)((c) - '0') < 10u)
#define isupper(c)  ((unsigned)((c) - 'A') < 26u)
#define islower(c)  ((unsigned)((c) - 'a') < 26u)
#define isalpha(c)  (isupper(c) || islower(c))
#define isalnum(c)  (isalpha(c) || isdigit(c))
#define isxdigit(c) (isdigit(c) || (unsigned)(((c) | 32) - 'a') < 6u)
#define isspace(c)  ((c) == ' ' || (unsigned)((c) - 9) < 5u)
#define isprint(c)  ((unsigned)((c) - 32) < 95u)
#define iscntrl(c)  ((unsigned)(c) < 32u || (c) == 127)
#define ispunct(c)  (isprint(c) && !isalnum(c) && (c) != ' ')
int tolower(int c);
int toupper(int c);
#endif
