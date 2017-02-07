/* > timefunc.c
 *
 * Various functions to manipulate struct timeval, and a monotonic
 * equivalent of time().
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>		/* Needed to get clock_gettime() in time.h*/
#include <time.h>
#include <sys/time.h>

#include "extern.h"
#include "timefunc.h"

const long USEC = 1000000; /* microseconds per second. */

/*
 * Normalise 'struct timeval' so microseconds is 0 .. 999,999 and with the seconds
 * corrected to match.
 */

void tv_norm(struct timeval *tv)
{
	/* Reduce large values of tv_usec in one call if needed. */
	if (labs(tv->tv_usec) >= USEC) {
		ldiv_t d = ldiv(tv->tv_usec, USEC);
		tv->tv_usec = d.rem;
		tv->tv_sec += d.quot;
	}

	/* Restrict the remainder to positive only. */
	if (tv->tv_usec < 0) {
		tv->tv_usec += USEC;
		tv->tv_sec  -= 1;
	}
}

/*
 * Simple time maths - add two 'struct timeval' so res = a + b
 * Code is more portable and has proper type-checking compared to timeradd() macro.
 */

void tv_add(const struct timeval *a, const struct timeval *b,  struct timeval *res)
{
	res->tv_sec  = a->tv_sec  + b->tv_sec;
	res->tv_usec = a->tv_usec + b->tv_usec;
	tv_norm(res);
}

/*
 * Simple time maths - subtract two 'struct timeval' so res = a - b
 * Code is more portable and has proper type-checking compared to timersub() macro.
 */

void tv_sub(const struct timeval *a, const struct timeval *b,  struct timeval *res)
{
	res->tv_sec  = a->tv_sec  - b->tv_sec;
	res->tv_usec = a->tv_usec - b->tv_usec;
	tv_norm(res);
}

/*
 * Function like time() but hopefully decoupled from system time changes.
 */

time_t time_mono(time_t *t)
{
	time_t now = (time_t)(-1);

#if defined( _POSIX_MONOTONIC_CLOCK )
	struct timespec tmon;
	/*
	 * Use the monotonic timer so errors due to a jump in 'real time' (e.g. NTP adjustment)
	 * do not cause a timer to fail. Note: we need linking with librt.a in a lot of
	 * cases according to the man page for clock_gettime()
	 *
	 * We add 1 to the value as it is just possible that on a very fast start-up the
	 * uptime seconds could be 0 which is the same as the test case for "timer not set" used
	 * in other watchdog code.
	 */
	if (clock_gettime(CLOCK_MONOTONIC, &tmon) == 0) {
		now = 1+tmon.tv_sec;
	}
#else
#warning "No support for CLOCK_MONOTONIC"
	time(&now);
#endif /* !CLOCK_MONOTONIC */

	if (t != NULL)
		*t = now;

	return (now);
}


#if defined( MAIN )
/*
 * Compute basic maths for
 *		res = a / b
 * where b is an integer.
 * NOTE: Result is truncated to the microsecond (not rounded).
 */

int tv_idiv(const struct timeval *a, int b, struct timeval *res)
{
ldiv_t d;

	if (b == 0) {
		/* Error case if divide-by-zero. */
		res->tv_sec = 0;
		res->tv_usec = 0;
		return -1;
	}

	/* Compute seconds + remainder. */
	d = ldiv(a->tv_sec, b);
	res->tv_sec = d.quot;
	/* Compute microseconds, including the above remainder. */
	res->tv_usec = (d.rem * USEC + a->tv_usec) / b;
	tv_norm(res);
	return 0;
}

/*
 * Convert timeval structure 'a' to double-precision number of seconds.
 */

double tv_dbl(const struct timeval *a)
{
	return a->tv_sec + (1.0/USEC) * a->tv_usec;
}


// Test stuff: gcc -Wall -DMAIN -I../include timefunc.c -o test -lrt && ./test
#define PRT(x) printf("%4s has tv_sec = %10ld tv_usec = %10ld => %17.6f\n", #x, x.tv_sec, x.tv_usec, tv_dbl(&x));

int main(int argc, char *argv[])
{
struct timeval a, b, res;
struct timespec tmon;

// Show time() and time_mono() values
	a.tv_sec = time(NULL);
	a.tv_usec = 0;
	PRT(a);

	b.tv_sec = time_mono(NULL);
	b.tv_usec = 0;
	PRT(b);

// Show the call to get that.
	clock_gettime(CLOCK_MONOTONIC, &tmon);
	a.tv_sec  = tmon.tv_sec;
	a.tv_usec = tmon.tv_nsec/1000;
	PRT(a);

// Check the normalisation function, a should be same (as double) in both cases.
	a.tv_sec  = 100;
	a.tv_usec = -5000000;
	PRT(a);
	tv_norm(&a);
	PRT(a);

// Test addition
	a.tv_sec  = 100;
	a.tv_usec = -600000;
	b = a;
	tv_add(&a, &b, &res);
	PRT(a);
	PRT(b);
	PRT(res);

// Test subtraction
	tv_sub(&res, &a, &res);
	PRT(res);

// Test division so should have 9-0.1 = 8.9 / 5 = 1.78 also negative case.
	a.tv_sec  = 9;
	a.tv_usec = -100000;
	tv_idiv(&a, 5, &res);
	PRT(a);
	PRT(res);

	a.tv_sec  = 8;
	a.tv_usec = 900000;
	tv_idiv(&a, 5, &res);
	PRT(a);
	PRT(res);

	a.tv_sec  = -8;
	a.tv_usec = -900000;
	tv_idiv(&a, 5, &res);
	PRT(a);
	PRT(res);

return 0;
}
#endif /*MAIN*/
