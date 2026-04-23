#include "mlang_platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int __mlang_std_sync_lfqueue_send(int64_t queue_handle, const char* s);

typedef struct
{
    int64_t deadline_ns;
} mlang_time_timer_t;

typedef struct
{
    int64_t interval_ns;
    int64_t next_deadline_ns;
} mlang_interval_timer_t;

typedef struct
{
    int64_t queue_handle;
    int64_t interval_ns;
    char* event_name;
    mlang_thread_t thread;
    mlang_atomic_int running;
    mlang_atomic_int started;
} mlang_async_ticker_t;

static int64_t now_ns_internal(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static void sleep_ns_internal(int64_t ns)
{
    mlang_sleep_ns((long long)ns);
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

int64_t __mlang_std_timer_interval_new(int64_t interval_ms)
{
    if(interval_ms <= 0)
        interval_ms = 1;

    mlang_interval_timer_t* t = (mlang_interval_timer_t*)malloc(sizeof(mlang_interval_timer_t));
    if(!t)
        return 0;

    t->interval_ns = interval_ms * 1000000LL;
    t->next_deadline_ns = now_ns_internal() + t->interval_ns;
    return (int64_t)(intptr_t)t;
}

int __mlang_std_timer_interval_reset(int64_t handle)
{
    mlang_interval_timer_t* t = (mlang_interval_timer_t*)(intptr_t)handle;
    if(!t)
        return -1;
    t->next_deadline_ns = now_ns_internal() + t->interval_ns;
    return 0;
}

int64_t __mlang_std_timer_interval_remaining_ms(int64_t handle)
{
    mlang_interval_timer_t* t = (mlang_interval_timer_t*)(intptr_t)handle;
    if(!t)
        return -1;
    int64_t now = now_ns_internal();
    if(now >= t->next_deadline_ns)
        return 0;
    int64_t rem_ns = t->next_deadline_ns - now;
    return (rem_ns + 999999LL) / 1000000LL;
}

int __mlang_std_timer_interval_wait_next(int64_t handle)
{
    mlang_interval_timer_t* t = (mlang_interval_timer_t*)(intptr_t)handle;
    if(!t)
        return -1;

    int64_t now = now_ns_internal();
    if(now < t->next_deadline_ns)
    {
        sleep_ns_internal(t->next_deadline_ns - now);
        now = now_ns_internal();
    }

    while(now >= t->next_deadline_ns)
        t->next_deadline_ns += t->interval_ns;
    return 0;
}

int __mlang_std_timer_interval_poll(int64_t handle)
{
    mlang_interval_timer_t* t = (mlang_interval_timer_t*)(intptr_t)handle;
    if(!t)
        return -1;

    int64_t now = now_ns_internal();
    if(now < t->next_deadline_ns)
        return 0;

    while(now >= t->next_deadline_ns)
        t->next_deadline_ns += t->interval_ns;
    return 1;
}

int __mlang_std_timer_interval_free(int64_t handle)
{
    mlang_interval_timer_t* t = (mlang_interval_timer_t*)(intptr_t)handle;
    if(!t)
        return 0;
    free(t);
    return 0;
}

static MLANG_THREAD_RETURN MLANG_THREAD_CALL ticker_thread_main(void* arg)
{
    mlang_async_ticker_t* t = (mlang_async_ticker_t*)arg;
    if(!t)
        return MLANG_THREAD_RETURN_VALUE;

    while(mlang_atomic_load(&t->running))
    {
        sleep_ns_internal(t->interval_ns);
        if(!mlang_atomic_load(&t->running))
            break;
        (void)__mlang_std_sync_lfqueue_send(t->queue_handle, t->event_name ? t->event_name : "tick");
    }
    return MLANG_THREAD_RETURN_VALUE;
}

int64_t __mlang_std_timer_async_ticker_new(int64_t queue_handle, int64_t interval_ms, const char* event_name)
{
    if(queue_handle == 0)
        return 0;
    if(interval_ms <= 0)
        interval_ms = 1;

    mlang_async_ticker_t* t = (mlang_async_ticker_t*)calloc(1, sizeof(mlang_async_ticker_t));
    if(!t)
        return 0;

    t->queue_handle = queue_handle;
    t->interval_ns = interval_ms * 1000000LL;
    t->event_name = strdup((event_name && event_name[0]) ? event_name : "tick");
    if(!t->event_name)
    {
        free(t);
        return 0;
    }
    mlang_atomic_store(&t->running, 1);
    mlang_atomic_store(&t->started, 0);

    if(mlang_thread_create(&t->thread, ticker_thread_main, t) != 0)
    {
        free(t->event_name);
        free(t);
        return 0;
    }
    mlang_atomic_store(&t->started, 1);
    return (int64_t)(intptr_t)t;
}

int __mlang_std_timer_async_ticker_stop(int64_t handle)
{
    mlang_async_ticker_t* t = (mlang_async_ticker_t*)(intptr_t)handle;
    if(!t)
        return 0;

    mlang_atomic_store(&t->running, 0);
    if(mlang_atomic_load(&t->started))
    {
        (void)mlang_thread_join(t->thread);
        mlang_atomic_store(&t->started, 0);
    }
    return 0;
}

int __mlang_std_timer_async_ticker_free(int64_t handle)
{
    mlang_async_ticker_t* t = (mlang_async_ticker_t*)(intptr_t)handle;
    if(!t)
        return 0;

    (void)__mlang_std_timer_async_ticker_stop(handle);
    free(t->event_name);
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

static void append_text(char* out, size_t cap, size_t* len, const char* s)
{
    if(!out || !len || !s || cap == 0)
        return;
    while(*s && *len + 1 < cap)
    {
        out[*len] = *s;
        ++(*len);
        ++s;
    }
    out[*len] = '\0';
}

static void append_char(char* out, size_t cap, size_t* len, char c)
{
    if(!out || !len || cap == 0)
        return;
    if(*len + 1 >= cap)
        return;
    out[*len] = c;
    ++(*len);
    out[*len] = '\0';
}

static void append_i32_pad(char* out, size_t cap, size_t* len, int value, int width)
{
    char tmp[32];
    if(width <= 0)
        (void)snprintf(tmp, sizeof(tmp), "%d", value);
    else
        (void)snprintf(tmp, sizeof(tmp), "%0*d", width, value);
    append_text(out, cap, len, tmp);
}

static void append_i64_pad(char* out, size_t cap, size_t* len, int64_t value, int width)
{
    char tmp[48];
    if(width <= 0)
        (void)snprintf(tmp, sizeof(tmp), "%lld", (long long)value);
    else
        (void)snprintf(tmp, sizeof(tmp), "%0*lld", width, (long long)value);
    append_text(out, cap, len, tmp);
}

const char* __mlang_std_time_format_local(const char* pattern)
{
    static char out[128];
    out[0] = '\0';
    size_t len = 0;

    struct timespec ts;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    time_t t = ts.tv_sec;

    struct tm tmv;
#if defined(_WIN32)
    if(localtime_s(&tmv, &t) != 0)
#else
    if(localtime_r(&t, &tmv) == NULL)
#endif
    {
        append_text(out, sizeof(out), &len, "01/01/1970:00:00:00.000");
        return out;
    }

    int mm_seen = 0;
    const char* p = pattern;
    if(!p || !*p)
        p = "MM/DD/YYYY:HH:MM:SS";

    while(*p)
    {
        if(strncmp(p, "YYYY", 4) == 0)
        {
            append_i32_pad(out, sizeof(out), &len, tmv.tm_year + 1900, 4);
            p += 4;
            continue;
        }
        if(strncmp(p, "DD", 2) == 0)
        {
            append_i32_pad(out, sizeof(out), &len, tmv.tm_mday, 2);
            p += 2;
            continue;
        }
        if(strncmp(p, "HH", 2) == 0)
        {
            append_i32_pad(out, sizeof(out), &len, tmv.tm_hour, 2);
            p += 2;
            continue;
        }
        if(strncmp(p, "SS", 2) == 0)
        {
            append_i32_pad(out, sizeof(out), &len, tmv.tm_sec, 2);
            p += 2;
            continue;
        }
        if(strncmp(p, "MS", 2) == 0)
        {
            append_i64_pad(out, sizeof(out), &len, ts.tv_nsec / 1000000LL, 3);
            p += 2;
            continue;
        }
        if(strncmp(p, "NS", 2) == 0)
        {
            append_i64_pad(out, sizeof(out), &len, ts.tv_nsec, 9);
            p += 2;
            continue;
        }
        if(strncmp(p, "MM", 2) == 0)
        {
            if(mm_seen == 0)
                append_i32_pad(out, sizeof(out), &len, tmv.tm_mon + 1, 2);
            else
                append_i32_pad(out, sizeof(out), &len, tmv.tm_min, 2);
            ++mm_seen;
            p += 2;
            continue;
        }

        append_char(out, sizeof(out), &len, *p);
        ++p;
    }

    return out;
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
