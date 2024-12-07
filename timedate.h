#ifndef UTILS_TD_H__
#define UTILS_TD_H__

#if defined(_WIN32) || defined(WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

#define MSEC_PER_SEC 1000
#define USEC_PER_SEC 1000000
#define NSEC_PER_SEC 1000000000
#define NSEC_PER_MSEC 1000000
#define NSEC_PER_USEC 1000

typedef struct timespec TD_V;

#define TD_GET(v) clock_gettime(CLOCK_MONOTONIC, &v)

#define TD_CALC_s(e, s) ((double)e.tv_sec + 1.0e-9 * e.tv_nsec) - ((double)s.tv_sec + 1.0e-9 * s.tv_nsec)
#define TD_CALC_ms(e, s) (1000 * e.tv_sec + e.tv_nsec / 1000000) - (1000 * s.tv_sec + s.tv_nsec / 1000000)
#define TD_CALC_us(e, s) (1000000 * e.tv_sec + e.tv_nsec / 1000) - (1000000 * s.tv_sec + s.tv_nsec / 1000)
#define TD_CALC_ns(e, s) (1000000000 * e.tv_sec + e.tv_nsec) - (1000000000 * s.tv_sec + s.tv_nsec)

#if !defined(_WIN32) && !defined(WIN32)
static inline int usleep2(uint32_t usec)
{
	int res;
	struct timespec ts = {.tv_sec = usec / USEC_PER_SEC, .tv_nsec = (usec % USEC_PER_SEC) * NSEC_PER_USEC};

	do
	{
		errno = 0;
		res = nanosleep(&ts, &ts);
	} while(res != 0 && errno == EINTR);

	return res;
}
#endif

static inline void delay_ms(uint32_t time_ms)
{
#if defined(_WIN32) || defined(WIN32)
	Sleep(time_ms);
#else
	usleep2(time_ms * 1000);
#endif
}

#endif // UTILS_TD_H__