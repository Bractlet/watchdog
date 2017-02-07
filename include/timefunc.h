/* > timefunc.h
 *
 */

#ifndef TIMEFUNC_H
#define TIMEFUNC_H

extern const long USEC; /* 1000000 => microseconds per second. */
#if 1
/*
 * If "1" functions provide stricter type-checking and better handling of
 * out of range value.
 * Otherwise if "0" then use the macros in <sys/time.h>
 */

void tv_norm(struct timeval *tv);
void tv_add(const struct timeval *a, const struct timeval *b,  struct timeval *res);
void tv_sub(const struct timeval *a, const struct timeval *b,  struct timeval *res);

#ifdef timeradd
#undef timeradd
#endif
#define timeradd tv_add

#ifdef timersub
#undef timersub
#endif
#define timersub tv_sub
#endif /* Use tv_? functions or not */

time_t time_mono(time_t *t);

#endif /*TIMEFUNC_H*/
