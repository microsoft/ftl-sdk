#ifndef __GETTIMEOFDAY_H
#define __GETTIMEOFDAY_H

#ifndef _WIN32
#include <sys/time.h>
#endif

#ifdef _WIN32
int gettimeofday(struct timeval * tp, struct timezone * tzp);
#endif
int timeval_subtract(struct timeval *result, const struct timeval *end, const struct timeval *start);
int timeval_subtract_to_ms(const struct timeval *end, const struct timeval *start);
void timeval_add_ms(struct timeval *tv, int ms);
void timespec_add_ms(struct timespec *tv, int ms);
float timeval_to_ms(struct timeval *tv);

#endif // __GETTIMEOFDAY_H
