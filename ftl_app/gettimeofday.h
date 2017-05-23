#include <stdint.h>

#ifdef _WIN32
#include <Windows.h>
int gettimeofday(struct timeval * tp, struct timezone * tzp);
BOOLEAN nanosleep(LONGLONG ns);
#else
#include <sys/time.h>
#endif
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y);
float timeval_to_ms(struct timeval *tv);
uint64_t timeval_to_us(struct timeval *tv);
int64_t timeval_subtract_to_ms(const struct timeval *end, const struct timeval *start);
int64_t timeval_subtract_to_us(const struct timeval *end, const struct timeval *start);
float timeval_to_ms(struct timeval *tv);
uint64_t timeval_to_us(struct timeval *tv);

