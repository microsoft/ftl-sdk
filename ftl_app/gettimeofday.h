#ifdef _WIN32
int gettimeofday(struct timeval * tp, struct timezone * tzp);
#else
#include <sys/time.h>
#endif
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y);
float timeval_to_ms(struct timeval *tv);
