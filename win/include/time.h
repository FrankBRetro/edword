/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* time.h - the small C library of the EdWord Windows port. time_t counts
 * seconds from 1 January 1970, in local time. */
#ifndef _TIME_H_
#define _TIME_H_
typedef long time_t;
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
time_t time(time_t *t);
struct tm *localtime(const time_t *t);
#endif
