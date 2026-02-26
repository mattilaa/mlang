#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    int64_t deadline_ns;
} mlang_time_timer_t;

static int64_t now_ns_internal(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static void sleep_ns_internal(int64_t ns)
{
    if(ns <= 0)
        return;

    struct timespec req;
    req.tv_sec = (time_t)(ns / 1000000000LL);
    req.tv_nsec = (long)(ns % 1000000000LL);

    while(nanosleep(&req, &req) != 0)
    {
        if(errno != EINTR)
            break;
    }
}

int64_t __mlang_std_time_now_ms(void)
{
    return now_ns_internal() / 1000000LL;
}

int64_t __mlang_std_time_now_ns(void)
{
    return now_ns_internal();
}

void __mlang_std_time_sleep_ms(int64_t ms)
{
    if(ms <= 0)
        return;
    sleep_ns_internal(ms * 1000000LL);
}

int64_t __mlang_std_time_timer_new(int64_t timeout_ms)
{
    if(timeout_ms < 0)
        timeout_ms = 0;

    mlang_time_timer_t* t = (mlang_time_timer_t*)malloc(sizeof(mlang_time_timer_t));
    if(!t)
        return 0;

    t->deadline_ns = now_ns_internal() + timeout_ms * 1000000LL;
    return (int64_t)(intptr_t)t;
}

int __mlang_std_time_timer_reset(int64_t handle, int64_t timeout_ms)
{
    mlang_time_timer_t* t = (mlang_time_timer_t*)(intptr_t)handle;
    if(!t)
        return -1;
    if(timeout_ms < 0)
        timeout_ms = 0;
    t->deadline_ns = now_ns_internal() + timeout_ms * 1000000LL;
    return 0;
}

int __mlang_std_time_timer_elapsed(int64_t handle)
{
    mlang_time_timer_t* t = (mlang_time_timer_t*)(intptr_t)handle;
    if(!t)
        return 1;
    return now_ns_internal() >= t->deadline_ns ? 1 : 0;
}

int64_t __mlang_std_time_timer_remaining_ms(int64_t handle)
{
    mlang_time_timer_t* t = (mlang_time_timer_t*)(intptr_t)handle;
    if(!t)
        return -1;
    int64_t now = now_ns_internal();
    if(now >= t->deadline_ns)
        return 0;
    int64_t rem_ns = t->deadline_ns - now;
    return (rem_ns + 999999LL) / 1000000LL;
}

int __mlang_std_time_timer_wait(int64_t handle)
{
    mlang_time_timer_t* t = (mlang_time_timer_t*)(intptr_t)handle;
    if(!t)
        return -1;

    int64_t now = now_ns_internal();
    if(now < t->deadline_ns)
        sleep_ns_internal(t->deadline_ns - now);
    return 0;
}

int __mlang_std_time_timer_free(int64_t handle)
{
    mlang_time_timer_t* t = (mlang_time_timer_t*)(intptr_t)handle;
    if(!t)
        return 0;
    free(t);
    return 0;
}

static int load_local_tm(struct tm* out)
{
    if(!out)
        return -1;
    time_t t = time(NULL);
#if defined(_WIN32)
    if(localtime_s(out, &t) != 0)
        return -1;
#else
    if(localtime_r(&t, out) == NULL)
        return -1;
#endif
    return 0;
}

int __mlang_std_date_now_year(void)
{
    struct tm tmv;
    if(load_local_tm(&tmv) != 0)
        return 1970;
    return tmv.tm_year + 1900;
}

int __mlang_std_date_now_month(void)
{
    struct tm tmv;
    if(load_local_tm(&tmv) != 0)
        return 1;
    return tmv.tm_mon + 1;
}

int __mlang_std_date_now_day(void)
{
    struct tm tmv;
    if(load_local_tm(&tmv) != 0)
        return 1;
    return tmv.tm_mday;
}

int __mlang_std_date_now_hour(void)
{
    struct tm tmv;
    if(load_local_tm(&tmv) != 0)
        return 0;
    return tmv.tm_hour;
}

int __mlang_std_date_now_minute(void)
{
    struct tm tmv;
    if(load_local_tm(&tmv) != 0)
        return 0;
    return tmv.tm_min;
}

int __mlang_std_date_now_second(void)
{
    struct tm tmv;
    if(load_local_tm(&tmv) != 0)
        return 0;
    return tmv.tm_sec;
}

const char* __mlang_std_time_local_datetime(void)
{
    static char buf[32];
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    if(localtime_s(&tmv, &t) != 0)
    {
        strcpy(buf, "01/01/1970 00:00:00");
        return buf;
    }
#else
    if(localtime_r(&t, &tmv) == NULL)
    {
        strcpy(buf, "01/01/1970 00:00:00");
        return buf;
    }
#endif

    if(strftime(buf, sizeof(buf), "%m/%d/%Y %H:%M:%S", &tmv) == 0)
        strcpy(buf, "01/01/1970 00:00:00");
    return buf;
}

const char* __mlang_std_time_test_timestamp(void)
{
    static char buf[20];
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    if(localtime_s(&tmv, &t) != 0)
    {
        strcpy(buf, "01/01/00/00/00");
        return buf;
    }
#else
    if(localtime_r(&t, &tmv) == NULL)
    {
        strcpy(buf, "01/01/00/00/00");
        return buf;
    }
#endif

    if(strftime(buf, sizeof(buf), "%d/%m/%H/%M/%S", &tmv) == 0)
        strcpy(buf, "01/01/00/00/00");
    return buf;
}

const char* __mlang_std_time_log_level_tag(int level, int color)
{
    if(!color)
    {
        switch(level)
        {
            case 0:
                return "[ERROR]";
            case 1:
                return "[WARN]";
            case 2:
                return "[INFO]";
            case 3:
                return "[DEBUG]";
            case 4:
                return "[VERBOSE]";
            default:
                return "[INFO]";
        }
    }

    switch(level)
    {
        case 0:
            return "\x1b[31m[ERROR]\x1b[0m";
        case 1:
            return "\x1b[33m[WARN]\x1b[0m";
        case 2:
            return "\x1b[32m[INFO]\x1b[0m";
        case 3:
            return "\x1b[37m[DEBUG]\x1b[0m";
        case 4:
            return "\x1b[37m[VERBOSE]\x1b[0m";
        default:
            return "\x1b[32m[INFO]\x1b[0m";
    }
}
