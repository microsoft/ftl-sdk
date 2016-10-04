#ifndef __GETTIMEOFDAY_H
#define __GETTIMEOFDAY_H

#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/time.h>
#endif

#ifdef _WIN32
int gettimeofday(struct timeval * tp, struct timezone * tzp);
#endif
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y);
float timeval_to_ms(struct timeval *tv);

#endif // __GETTIMEOFDAY_H
