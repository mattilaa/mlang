#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    pthread_mutex_t mu;
} mlang_sync_mutex_t;

typedef struct
{
    pthread_cond_t cv;
} mlang_sync_condvar_t;

typedef struct
{
    pthread_mutex_t mu;
    pthread_cond_t can_send;
    pthread_cond_t can_recv;
    int closed;
    size_t cap;
    size_t count;
    size_t head;
    size_t tail;
    char** items;
} mlang_sync_channel_t;

typedef struct
{
    _Atomic size_t head;
    _Atomic size_t tail;
    _Atomic int closed;
    size_t cap;
    char** items;
} mlang_sync_lfqueue_t;

static __thread char g_last_error[512];

static void set_error(const char* msg)
{
    if(!msg)
        msg = "std::sync: unknown error";
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s", msg);
}

static void set_errno_error(const char* prefix)
{
    const char* p = prefix ? prefix : "std::sync";
    const char* e = strerror(errno);
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s: %s", p, e ? e : "unknown error");
}

static void clear_error(void)
{
    g_last_error[0] = '\0';
}

static char* dup_cstr(const char* s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    if(n > 0)
        (void)memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static char* mlang_strdup(const char* s)
{
    return dup_cstr(s ? s : "");
}

char* __mlang_std_sync_last_error(void)
{
    return dup_cstr(g_last_error);
}

int64_t __mlang_std_sync_mutex_new(void)
{
    mlang_sync_mutex_t* m = (mlang_sync_mutex_t*)malloc(sizeof(mlang_sync_mutex_t));
    if(!m)
    {
        set_error("std::sync mutex_new: out of memory");
        return 0;
    }
    if(pthread_mutex_init(&m->mu, NULL) != 0)
    {
        free(m);
        set_error("std::sync mutex_new: pthread_mutex_init failed");
        return 0;
    }
    clear_error();
    return (int64_t)(intptr_t)m;
}

int __mlang_std_sync_mutex_lock(int64_t handle)
{
    mlang_sync_mutex_t* m = (mlang_sync_mutex_t*)(intptr_t)handle;
    if(!m)
    {
        set_error("std::sync mutex_lock: invalid handle");
        return -1;
    }
    int rc = pthread_mutex_lock(&m->mu);
    if(rc != 0)
    {
        set_error("std::sync mutex_lock failed");
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_sync_mutex_unlock(int64_t handle)
{
    mlang_sync_mutex_t* m = (mlang_sync_mutex_t*)(intptr_t)handle;
    if(!m)
    {
        set_error("std::sync mutex_unlock: invalid handle");
        return -1;
    }
    int rc = pthread_mutex_unlock(&m->mu);
    if(rc != 0)
    {
        set_error("std::sync mutex_unlock failed");
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_sync_mutex_free(int64_t handle)
{
    mlang_sync_mutex_t* m = (mlang_sync_mutex_t*)(intptr_t)handle;
    if(!m)
        return 0;
    (void)pthread_mutex_destroy(&m->mu);
    free(m);
    return 0;
}

int64_t __mlang_std_sync_condvar_new(void)
{
    mlang_sync_condvar_t* cv = (mlang_sync_condvar_t*)malloc(sizeof(mlang_sync_condvar_t));
    if(!cv)
    {
        set_error("std::sync condvar_new: out of memory");
        return 0;
    }
    if(pthread_cond_init(&cv->cv, NULL) != 0)
    {
        free(cv);
        set_error("std::sync condvar_new: pthread_cond_init failed");
        return 0;
    }
    clear_error();
    return (int64_t)(intptr_t)cv;
}

int __mlang_std_sync_condvar_wait(int64_t cond_handle, int64_t mutex_handle)
{
    mlang_sync_condvar_t* cv = (mlang_sync_condvar_t*)(intptr_t)cond_handle;
    mlang_sync_mutex_t* m = (mlang_sync_mutex_t*)(intptr_t)mutex_handle;
    if(!cv || !m)
    {
        set_error("std::sync condvar_wait: invalid handle");
        return -1;
    }
    int rc = pthread_cond_wait(&cv->cv, &m->mu);
    if(rc != 0)
    {
        set_error("std::sync condvar_wait failed");
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_sync_condvar_wait_timeout_ms(int64_t cond_handle, int64_t mutex_handle, int64_t timeout_ms)
{
    mlang_sync_condvar_t* cv = (mlang_sync_condvar_t*)(intptr_t)cond_handle;
    mlang_sync_mutex_t* m = (mlang_sync_mutex_t*)(intptr_t)mutex_handle;
    if(!cv || !m)
    {
        set_error("std::sync condvar_wait_timeout_ms: invalid handle");
        return -1;
    }
    if(timeout_ms < 0)
        timeout_ms = 0;

    struct timespec ts;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    int64_t add_ns = timeout_ms * 1000000LL;
    ts.tv_sec += (time_t)(add_ns / 1000000000LL);
    ts.tv_nsec += (long)(add_ns % 1000000000LL);
    if(ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    int rc = pthread_cond_timedwait(&cv->cv, &m->mu, &ts);
    if(rc == ETIMEDOUT)
    {
        clear_error();
        return 1;
    }
    if(rc != 0)
    {
        set_error("std::sync condvar_wait_timeout_ms failed");
        return -1;
    }

    clear_error();
    return 0;
}

int __mlang_std_sync_condvar_notify_one(int64_t cond_handle)
{
    mlang_sync_condvar_t* cv = (mlang_sync_condvar_t*)(intptr_t)cond_handle;
    if(!cv)
    {
        set_error("std::sync condvar_notify_one: invalid handle");
        return -1;
    }
    int rc = pthread_cond_signal(&cv->cv);
    if(rc != 0)
    {
        set_error("std::sync condvar_notify_one failed");
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_sync_condvar_notify_all(int64_t cond_handle)
{
    mlang_sync_condvar_t* cv = (mlang_sync_condvar_t*)(intptr_t)cond_handle;
    if(!cv)
    {
        set_error("std::sync condvar_notify_all: invalid handle");
        return -1;
    }
    int rc = pthread_cond_broadcast(&cv->cv);
    if(rc != 0)
    {
        set_error("std::sync condvar_notify_all failed");
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_sync_condvar_free(int64_t cond_handle)
{
    mlang_sync_condvar_t* cv = (mlang_sync_condvar_t*)(intptr_t)cond_handle;
    if(!cv)
        return 0;
    (void)pthread_cond_destroy(&cv->cv);
    free(cv);
    return 0;
}

int64_t __mlang_std_sync_channel_new(int64_t capacity)
{
    size_t cap = capacity > 0 ? (size_t)capacity : 64u;
    mlang_sync_channel_t* ch = (mlang_sync_channel_t*)malloc(sizeof(mlang_sync_channel_t));
    if(!ch)
    {
        set_error("std::sync channel_new: out of memory");
        return 0;
    }
    memset(ch, 0, sizeof(*ch));

    ch->items = (char**)calloc(cap, sizeof(char*));
    if(!ch->items)
    {
        free(ch);
        set_error("std::sync channel_new: out of memory");
        return 0;
    }
    ch->cap = cap;

    if(pthread_mutex_init(&ch->mu, NULL) != 0 ||
       pthread_cond_init(&ch->can_send, NULL) != 0 ||
       pthread_cond_init(&ch->can_recv, NULL) != 0)
    {
        (void)pthread_cond_destroy(&ch->can_send);
        (void)pthread_cond_destroy(&ch->can_recv);
        (void)pthread_mutex_destroy(&ch->mu);
        free(ch->items);
        free(ch);
        set_error("std::sync channel_new: pthread init failed");
        return 0;
    }

    clear_error();
    return (int64_t)(intptr_t)ch;
}

int __mlang_std_sync_channel_send(int64_t channel_handle, const char* s)
{
    mlang_sync_channel_t* ch = (mlang_sync_channel_t*)(intptr_t)channel_handle;
    if(!ch || !s)
    {
        set_error("std::sync channel_send: invalid handle or message");
        return -1;
    }

    if(pthread_mutex_lock(&ch->mu) != 0)
    {
        set_error("std::sync channel_send: mutex lock failed");
        return -1;
    }

    while(ch->count == ch->cap && !ch->closed)
        (void)pthread_cond_wait(&ch->can_send, &ch->mu);

    if(ch->closed)
    {
        (void)pthread_mutex_unlock(&ch->mu);
        set_error("std::sync channel_send: channel closed");
        return -1;
    }

    char* msg = mlang_strdup(s);
    if(!msg)
    {
        (void)pthread_mutex_unlock(&ch->mu);
        set_error("std::sync channel_send: out of memory");
        return -1;
    }

    ch->items[ch->tail] = msg;
    ch->tail = (ch->tail + 1) % ch->cap;
    ch->count++;
    (void)pthread_cond_signal(&ch->can_recv);
    (void)pthread_mutex_unlock(&ch->mu);

    clear_error();
    return 0;
}

static int64_t channel_pop_into_buf(mlang_sync_channel_t* ch, char* buf, int64_t capacity)
{
    char* msg = ch->items[ch->head];
    ch->items[ch->head] = NULL;
    ch->head = (ch->head + 1) % ch->cap;
    ch->count--;
    (void)pthread_cond_signal(&ch->can_send);

    if(!buf || capacity <= 1)
    {
        free(msg);
        return -1;
    }

    int64_t src_len = msg ? (int64_t)strlen(msg) : 0;
    int64_t n = src_len < (capacity - 1) ? src_len : (capacity - 1);
    if(n > 0 && msg)
        (void)memcpy(buf, msg, (size_t)n);
    buf[n] = '\0';
    free(msg);
    return n;
}

int64_t __mlang_std_sync_channel_recv(int64_t channel_handle, char* buf, int64_t capacity)
{
    mlang_sync_channel_t* ch = (mlang_sync_channel_t*)(intptr_t)channel_handle;
    if(!ch)
    {
        set_error("std::sync channel_recv: invalid handle");
        return -1;
    }

    if(pthread_mutex_lock(&ch->mu) != 0)
    {
        set_error("std::sync channel_recv: mutex lock failed");
        return -1;
    }

    while(ch->count == 0 && !ch->closed)
        (void)pthread_cond_wait(&ch->can_recv, &ch->mu);

    if(ch->count == 0 && ch->closed)
    {
        (void)pthread_mutex_unlock(&ch->mu);
        if(buf && capacity > 0)
            buf[0] = '\0';
        clear_error();
        return 0;
    }

    int64_t n = channel_pop_into_buf(ch, buf, capacity);
    (void)pthread_mutex_unlock(&ch->mu);
    if(n < 0)
    {
        set_error("std::sync channel_recv: invalid buffer");
        return -1;
    }
    clear_error();
    return n;
}

int64_t __mlang_std_sync_channel_try_recv(int64_t channel_handle, char* buf, int64_t capacity)
{
    mlang_sync_channel_t* ch = (mlang_sync_channel_t*)(intptr_t)channel_handle;
    if(!ch)
    {
        set_error("std::sync channel_try_recv: invalid handle");
        return -1;
    }

    if(pthread_mutex_lock(&ch->mu) != 0)
    {
        set_error("std::sync channel_try_recv: mutex lock failed");
        return -1;
    }

    if(ch->count == 0)
    {
        int closed = ch->closed;
        (void)pthread_mutex_unlock(&ch->mu);
        if(buf && capacity > 0)
            buf[0] = '\0';
        clear_error();
        return closed ? 0 : -2;
    }

    int64_t n = channel_pop_into_buf(ch, buf, capacity);
    (void)pthread_mutex_unlock(&ch->mu);
    if(n < 0)
    {
        set_error("std::sync channel_try_recv: invalid buffer");
        return -1;
    }
    clear_error();
    return n;
}

int __mlang_std_sync_channel_close(int64_t channel_handle)
{
    mlang_sync_channel_t* ch = (mlang_sync_channel_t*)(intptr_t)channel_handle;
    if(!ch)
        return 0;

    if(pthread_mutex_lock(&ch->mu) != 0)
    {
        set_error("std::sync channel_close: mutex lock failed");
        return -1;
    }
    ch->closed = 1;
    (void)pthread_cond_broadcast(&ch->can_recv);
    (void)pthread_cond_broadcast(&ch->can_send);
    (void)pthread_mutex_unlock(&ch->mu);
    clear_error();
    return 0;
}

int __mlang_std_sync_channel_free(int64_t channel_handle)
{
    mlang_sync_channel_t* ch = (mlang_sync_channel_t*)(intptr_t)channel_handle;
    if(!ch)
        return 0;

    (void)pthread_mutex_lock(&ch->mu);
    ch->closed = 1;
    for(size_t i = 0; i < ch->cap; ++i)
    {
        free(ch->items[i]);
        ch->items[i] = NULL;
    }
    (void)pthread_mutex_unlock(&ch->mu);

    (void)pthread_cond_destroy(&ch->can_send);
    (void)pthread_cond_destroy(&ch->can_recv);
    (void)pthread_mutex_destroy(&ch->mu);
    free(ch->items);
    free(ch);
    return 0;
}

int64_t __mlang_std_sync_lfqueue_new(int64_t capacity)
{
    size_t cap = capacity > 0 ? (size_t)capacity : 256u;
    mlang_sync_lfqueue_t* q = (mlang_sync_lfqueue_t*)malloc(sizeof(mlang_sync_lfqueue_t));
    if(!q)
    {
        set_error("std::sync lfqueue_new: out of memory");
        return 0;
    }
    memset(q, 0, sizeof(*q));

    q->items = (char**)calloc(cap, sizeof(char*));
    if(!q->items)
    {
        free(q);
        set_error("std::sync lfqueue_new: out of memory");
        return 0;
    }
    q->cap = cap;
    atomic_store_explicit(&q->head, 0u, memory_order_relaxed);
    atomic_store_explicit(&q->tail, 0u, memory_order_relaxed);
    atomic_store_explicit(&q->closed, 0, memory_order_relaxed);
    clear_error();
    return (int64_t)(intptr_t)q;
}

int __mlang_std_sync_lfqueue_send(int64_t queue_handle, const char* s)
{
    mlang_sync_lfqueue_t* q = (mlang_sync_lfqueue_t*)(intptr_t)queue_handle;
    if(!q || !s)
    {
        set_error("std::sync lfqueue_send: invalid handle or message");
        return -1;
    }
    if(atomic_load_explicit(&q->closed, memory_order_acquire) != 0)
    {
        set_error("std::sync lfqueue_send: queue closed");
        return -1;
    }

    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&q->head, memory_order_acquire);
    if((tail - head) >= q->cap)
    {
        clear_error();
        return 1; // full
    }

    char* msg = mlang_strdup(s);
    if(!msg)
    {
        set_error("std::sync lfqueue_send: out of memory");
        return -1;
    }

    q->items[tail % q->cap] = msg;
    atomic_store_explicit(&q->tail, tail + 1u, memory_order_release);
    clear_error();
    return 0;
}

int64_t __mlang_std_sync_lfqueue_try_recv(int64_t queue_handle, char* buf, int64_t capacity)
{
    mlang_sync_lfqueue_t* q = (mlang_sync_lfqueue_t*)(intptr_t)queue_handle;
    if(!q)
    {
        set_error("std::sync lfqueue_try_recv: invalid handle");
        return -1;
    }
    if(!buf || capacity <= 1)
    {
        set_error("std::sync lfqueue_try_recv: invalid buffer");
        return -1;
    }

    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    if(head == tail)
    {
        buf[0] = '\0';
        if(atomic_load_explicit(&q->closed, memory_order_acquire) != 0)
        {
            clear_error();
            return 0;
        }
        clear_error();
        return -2; // empty
    }

    size_t idx = head % q->cap;
    char* msg = q->items[idx];
    q->items[idx] = NULL;
    atomic_store_explicit(&q->head, head + 1u, memory_order_release);

    int64_t src_len = msg ? (int64_t)strlen(msg) : 0;
    int64_t n = src_len < (capacity - 1) ? src_len : (capacity - 1);
    if(n > 0 && msg)
        (void)memcpy(buf, msg, (size_t)n);
    buf[n] = '\0';
    free(msg);
    clear_error();
    return n;
}

int __mlang_std_sync_lfqueue_close(int64_t queue_handle)
{
    mlang_sync_lfqueue_t* q = (mlang_sync_lfqueue_t*)(intptr_t)queue_handle;
    if(!q)
        return 0;
    atomic_store_explicit(&q->closed, 1, memory_order_release);
    clear_error();
    return 0;
}

int __mlang_std_sync_lfqueue_free(int64_t queue_handle)
{
    mlang_sync_lfqueue_t* q = (mlang_sync_lfqueue_t*)(intptr_t)queue_handle;
    if(!q)
        return 0;
    atomic_store_explicit(&q->closed, 1, memory_order_release);
    if(q->items)
    {
        for(size_t i = 0; i < q->cap; ++i)
        {
            free(q->items[i]);
            q->items[i] = NULL;
        }
        free(q->items);
    }
    free(q);
    return 0;
}
