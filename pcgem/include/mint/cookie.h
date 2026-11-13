/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* mint/cookie.h - PC GEM: there is no cookie jar, so no cookie is ever found */
#ifndef PCGEM_COOKIE_H
#define PCGEM_COOKIE_H
#define C_FOUND    0
#define C_NOTFOUND (-1)
int Getcookie(long id, long *val);
#endif
