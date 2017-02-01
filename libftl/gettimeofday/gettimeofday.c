/*
* gettimeofday.c
*    Win32 gettimeofday() replacement
*
* src/port/gettimeofday.c
*
* Copyright (c) 2003 SRA, Inc.
* Copyright (c) 2003 SKC, Inc.
*
* Permission to use, copy, modify, and distribute this software and
* its documentation for any purpose, without fee, and without a
* written agreement is hereby granted, provided that the above
* copyright notice and this paragraph and the following two
* paragraphs appear in all copies.
*
* IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT,
* INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
* LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
* DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED
* OF THE POSSIBILITY OF SUCH DAMAGE.
*
* THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
* A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
* IS" BASIS, AND THE AUTHOR HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE,
* SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
*/

#include "gettimeofday.h"
#include <stdint.h>
#ifdef _WIN32
#include <Windows.h>

/* FILETIME of Jan 1 1970 00:00:00. */
static const unsigned __int64 epoch = ((unsigned __int64)116444736000000000ULL);

/*
* timezone information is stored outside the kernel so tzp isn't used anymore.
*
* Note: this function is not for Win32 high precision timing purpose. See
* elapsed_time().
*/
int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
	FILETIME    file_time;
	SYSTEMTIME  system_time;
	ULARGE_INTEGER ularge;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	ularge.LowPart = file_time.dwLowDateTime;
	ularge.HighPart = file_time.dwHighDateTime;

	tp->tv_sec = (long)((ularge.QuadPart - epoch) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);

	return 0;
}
#else
void timespec_add_ms(struct timespec *ts, int ms) {
	int sec_a, sec_b, usec;

	sec_a = ms / 1000;
	ms -= sec_a * 1000;

	sec_b = ts->tv_nsec / 1000000000;
	ts->tv_nsec -= sec_b * 1000000000;

	ts->tv_sec += sec_a + sec_b;
}
#endif // _WIN32

//result = end - start
int timeval_subtract(struct timeval *result, const struct timeval *end, const struct timeval *start)
{
	int d;

	d = timeval_subtract_to_ms(end, start);

	result->tv_sec = d / 1000;
	result->tv_usec = (d - result->tv_sec * 1000) * 1000;

	/* Return 1 if result is negative. */
	return d < 0;
}

//result = end - start
int timeval_subtract_to_ms(const struct timeval *end, const struct timeval *start)
{
	int64_t s, e, d;

	s = (int64_t)start->tv_sec * 1000 + (int64_t)start->tv_usec / 1000;
	e = (int64_t)end->tv_sec * 1000 + (int64_t)end->tv_usec / 1000;

	d = e - s;

	return (int)d;
}

void timeval_add_ms(struct timeval *tv, int ms)
{
	int sec_a, sec_b;

	sec_a = ms / 1000;
	ms -= sec_a * 1000;

	sec_b = tv->tv_usec / 1000000;
	tv->tv_usec -= sec_b * 1000000;

	tv->tv_sec += sec_a + sec_b;
}


float timeval_to_ms(struct timeval *tv) {
	double sec, usec;

	sec = (double)tv->tv_sec;
	usec = (double)tv->tv_usec;

	return (float)(sec * 1000 + usec / 1000);
}
