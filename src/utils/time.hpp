
#ifndef _INC_TIME_UTILS_H
#define _INC_TIME_UTILS_H

#include <time.h>

clockid_t
get_highres_clock(void);

int
compare_timespecs(const struct timespec* t1, const struct timespec* t2);

int
timespec_subtract(struct timespec *result, struct timespec *x, struct timespec *y);

void
timespec_add_ms(struct timespec *x, unsigned long ms);

#endif
