#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
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
    pthread_t thread;
    atomic_int running;
    atomic_int started;
} mlang_async_ticker_t;

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

static void* ticker_thread_main(void* arg)
{
    mlang_async_ticker_t* t = (mlang_async_ticker_t*)arg;
    if(!t)
        return NULL;

    while(atomic_load(&t->running))
    {
        sleep_ns_internal(t->interval_ns);
        if(!atomic_load(&t->running))
            break;
        (void)__mlang_std_sync_lfqueue_send(t->queue_handle, t->event_name ? t->event_name : "tick");
    }
    return NULL;
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
    atomic_store(&t->running, 1);
    atomic_store(&t->started, 0);

    if(pthread_create(&t->thread, NULL, ticker_thread_main, t) != 0)
    {
        free(t->event_name);
        free(t);
        return 0;
    }
    atomic_store(&t->started, 1);
    return (int64_t)(intptr_t)t;
}

int __mlang_std_timer_async_ticker_stop(int64_t handle)
{
    mlang_async_ticker_t* t = (mlang_async_ticker_t*)(intptr_t)handle;
    if(!t)
        return 0;

    atomic_store(&t->running, 0);
    if(atomic_load(&t->started))
    {
        (void)pthread_join(t->thread, NULL);
        atomic_store(&t->started, 0);
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

static int64_t floor_div_i64(int64_t a, int64_t b)
{
    int64_t q = a / b;
    int64_t r = a % b;
    if(r != 0 && ((r > 0) != (b > 0)))
        q -= 1;
    return q;
}

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2u;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2u ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int* y, int* m, int* d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned mm = mp + (mp < 10u ? 3u : -9u);
    yy += mm <= 2u;
    if(y)
        *y = (int)yy;
    if(m)
        *m = (int)mm;
    if(d)
        *d = (int)dd;
}

static int is_leap_year(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if(m < 1 || m > 12)
        return 0;
    if(m == 2 && is_leap_year(y))
        return 29;
    return days[m - 1];
}

static int valid_datetime(int year, int month, int day, int hour, int minute, int second)
{
    if(month < 1 || month > 12)
        return 0;
    if(day < 1 || day > days_in_month(year, month))
        return 0;
    if(hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return 0;
    return 1;
}

static void datetime_from_unix_offset(int64_t timestamp, int32_t offset_seconds,
                                      int* year, int* month, int* day,
                                      int* hour, int* minute, int* second)
{
    int64_t adjusted = timestamp + (int64_t)offset_seconds;
    int64_t days = floor_div_i64(adjusted, 86400);
    int64_t sod = adjusted - days * 86400;
    civil_from_days(days, year, month, day);
    if(hour)
        *hour = (int)(sod / 3600);
    if(minute)
        *minute = (int)((sod % 3600) / 60);
    if(second)
        *second = (int)(sod % 60);
}

static int date_part_from_unix_offset(int64_t timestamp, int32_t offset_seconds, int part)
{
    int y = 1970;
    int m = 1;
    int d = 1;
    int hh = 0;
    int mm = 0;
    int ss = 0;
    datetime_from_unix_offset(timestamp, offset_seconds, &y, &m, &d, &hh, &mm, &ss);
    switch(part)
    {
        case 0: return y;
        case 1: return m;
        case 2: return d;
        case 3: return hh;
        case 4: return mm;
        case 5: return ss;
        default: return 0;
    }
}

int64_t __mlang_std_date_unix_now(void)
{
    return (int64_t)time(NULL);
}

int __mlang_std_date_from_unix_year(int64_t timestamp, int32_t offset_seconds)
{
    return date_part_from_unix_offset(timestamp, offset_seconds, 0);
}

int __mlang_std_date_from_unix_month(int64_t timestamp, int32_t offset_seconds)
{
    return date_part_from_unix_offset(timestamp, offset_seconds, 1);
}

int __mlang_std_date_from_unix_day(int64_t timestamp, int32_t offset_seconds)
{
    return date_part_from_unix_offset(timestamp, offset_seconds, 2);
}

int __mlang_std_date_from_unix_hour(int64_t timestamp, int32_t offset_seconds)
{
    return date_part_from_unix_offset(timestamp, offset_seconds, 3);
}

int __mlang_std_date_from_unix_minute(int64_t timestamp, int32_t offset_seconds)
{
    return date_part_from_unix_offset(timestamp, offset_seconds, 4);
}

int __mlang_std_date_from_unix_second(int64_t timestamp, int32_t offset_seconds)
{
    return date_part_from_unix_offset(timestamp, offset_seconds, 5);
}

static int local_part_from_unix(int64_t timestamp, int part)
{
    time_t t = (time_t)timestamp;
    struct tm tmv;
#if defined(_WIN32)
    if(localtime_s(&tmv, &t) != 0)
        return date_part_from_unix_offset(timestamp, 0, part);
#else
    if(localtime_r(&t, &tmv) == NULL)
        return date_part_from_unix_offset(timestamp, 0, part);
#endif
    switch(part)
    {
        case 0: return tmv.tm_year + 1900;
        case 1: return tmv.tm_mon + 1;
        case 2: return tmv.tm_mday;
        case 3: return tmv.tm_hour;
        case 4: return tmv.tm_min;
        case 5: return tmv.tm_sec;
        default: return 0;
    }
}

int __mlang_std_date_from_unix_local_year(int64_t timestamp)
{
    return local_part_from_unix(timestamp, 0);
}

int __mlang_std_date_from_unix_local_month(int64_t timestamp)
{
    return local_part_from_unix(timestamp, 1);
}

int __mlang_std_date_from_unix_local_day(int64_t timestamp)
{
    return local_part_from_unix(timestamp, 2);
}

int __mlang_std_date_from_unix_local_hour(int64_t timestamp)
{
    return local_part_from_unix(timestamp, 3);
}

int __mlang_std_date_from_unix_local_minute(int64_t timestamp)
{
    return local_part_from_unix(timestamp, 4);
}

int __mlang_std_date_from_unix_local_second(int64_t timestamp)
{
    return local_part_from_unix(timestamp, 5);
}

int64_t __mlang_std_date_to_unix(int32_t year, int32_t month, int32_t day,
                                 int32_t hour, int32_t minute, int32_t second,
                                 int32_t offset_seconds)
{
    if(!valid_datetime(year, month, day, hour, minute, second))
        return INT64_MIN;
    int64_t days = days_from_civil((int64_t)year, (unsigned)month, (unsigned)day);
    return days * 86400LL + (int64_t)hour * 3600LL + (int64_t)minute * 60LL +
           (int64_t)second - (int64_t)offset_seconds;
}

int __mlang_std_date_local_offset_seconds_at(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm local_tm;
#if defined(_WIN32)
    if(localtime_s(&local_tm, &t) != 0)
        return 0;
#else
    if(localtime_r(&t, &local_tm) == NULL)
        return 0;
#endif
    int64_t local_as_utc = days_from_civil((int64_t)local_tm.tm_year + 1900,
                                           (unsigned)local_tm.tm_mon + 1u,
                                           (unsigned)local_tm.tm_mday) *
                               86400LL +
                           (int64_t)local_tm.tm_hour * 3600LL +
                           (int64_t)local_tm.tm_min * 60LL +
                           (int64_t)local_tm.tm_sec;
    int64_t diff = local_as_utc - timestamp;
    if(diff < INT_MIN || diff > INT_MAX)
        return 0;
    return (int)diff;
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
