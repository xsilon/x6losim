/*
 * time.cpp
 *
 *  Created on: 25 Jun 2015
 *      Author: martin
 */

#include "log/log.hpp"
#include "utils/time.hpp"


// _____________________________________________________ clock utility functions

clockid_t
get_highres_clock(void)
{
	struct timespec res;
	long resolution;

	//affected by setting time and NTP
	if (clock_getres(CLOCK_REALTIME, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_NOTICE, "CLOCK_REALTIME: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_REALTIME;
	}

	//affected by NTP
	if (clock_getres(CLOCK_MONOTONIC, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_NOTICE, "CLOCK_MONOTONIC: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_MONOTONIC;
	}

	if (clock_getres(CLOCK_PROCESS_CPUTIME_ID, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_NOTICE, "CLOCK_PROCESS_CPUTIME_ID: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_PROCESS_CPUTIME_ID;
	}

	return -1;
}

// __________________________________________________ timespec utility functions

int
compare_timespecs(const struct timespec* t1, const struct timespec* t2)
{
	if (t1->tv_sec < t2->tv_sec)
		return -1;
	else if (t1->tv_sec == t2->tv_sec) {
		if (t1->tv_nsec < t2->tv_nsec)
			return -1;
		else if (t1->tv_nsec == t2->tv_nsec)
			return 0;
		else
			return 1;
	}
	else
		return 1;
}

/*
 * Subtract the ‘struct timeval’ values X and Y,
 * storing the result in RESULT.
 * Return 1 if the difference is negative, otherwise 0.
 */
int
timespec_subtract(struct timespec *result, struct timespec *x, struct timespec *y)
{
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_nsec < y->tv_nsec) {
		int nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
		y->tv_nsec -= 1000000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_nsec - y->tv_nsec > 1000000000) {
		int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
		y->tv_nsec += 1000000000 * nsec;
		y->tv_sec -= nsec;
	}

	/* Compute the time remaining to wait. tv_nsec is certainly positive. */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_nsec = x->tv_nsec - y->tv_nsec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

void
timespec_add_ms(struct timespec *x, unsigned long ms)
{
	x->tv_nsec += ms * 1000000;
	if (x->tv_nsec >= 1000000000) {
		x->tv_sec += x->tv_nsec / 1000000000;
		x->tv_nsec %= 1000000000;
	}

}
