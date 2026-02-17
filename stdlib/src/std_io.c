#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t g_stdout_mutex;
static pthread_once_t g_stdout_once = PTHREAD_ONCE_INIT;

static void init_stdout_mutex(void)
{
    (void)pthread_mutex_init(&g_stdout_mutex, NULL);
}

int __mlang_std_io_stdout_lock(void)
{
    (void)pthread_once(&g_stdout_once, init_stdout_mutex);
    return pthread_mutex_lock(&g_stdout_mutex);
}

int __mlang_std_io_stdout_unlock(void)
{
    (void)pthread_once(&g_stdout_once, init_stdout_mutex);
    return pthread_mutex_unlock(&g_stdout_mutex);
}

int __mlang_std_io_stdout_try_lock(void)
{
    (void)pthread_once(&g_stdout_once, init_stdout_mutex);
    int rc = pthread_mutex_trylock(&g_stdout_mutex);
    if(rc == 0)
    {
        (void)pthread_mutex_unlock(&g_stdout_mutex);
        return 1;
    }
    return 0;
}

int64_t __mlang_std_io_stdout_write(const char* s)
{
    if(!s)
        return -1;
    size_t n = strlen(s);
    size_t w = fwrite(s, 1, n, stdout);
    fflush(stdout);
    return (int64_t)w;
}

int64_t __mlang_std_io_stdout_writeln(const char* s)
{
    if(!s)
        return -1;
    size_t n = strlen(s);
    size_t w1 = fwrite(s, 1, n, stdout);
    size_t w2 = fwrite("\n", 1, 1, stdout);
    fflush(stdout);
    return (int64_t)(w1 + w2);
}

int64_t __mlang_std_io_stdin_read_line(char* buf, int64_t capacity)
{
    if(!buf || capacity <= 1)
        return -1;

    if(!fgets(buf, (int)capacity, stdin))
        return -1;

    size_t n = strlen(buf);
    if(n > 0 && buf[n - 1] == '\n')
    {
        buf[n - 1] = '\0';
        --n;
    }
    return (int64_t)n;
}

typedef int64_t (*mlang_fn0_i64_t)(void);

int64_t __mlang_std_io_with_stdout_lock_call0(int64_t func)
{
    if(__mlang_std_io_stdout_lock() != 0)
        return -1;

    int64_t result = -1;
    if(func != 0)
    {
        mlang_fn0_i64_t fn = (mlang_fn0_i64_t)(intptr_t)func;
        result = fn();
    }

    (void)__mlang_std_io_stdout_unlock();
    return result;
}
