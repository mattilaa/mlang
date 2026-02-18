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
