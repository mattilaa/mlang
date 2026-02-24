#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

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

int64_t __mlang_std_io_stderr_write(const char* s)
{
    if(!s)
        return -1;
    size_t n = strlen(s);
    size_t w = fwrite(s, 1, n, stderr);
    fflush(stderr);
    return (int64_t)w;
}

int64_t __mlang_std_io_stderr_writeln(const char* s)
{
    if(!s)
        return -1;
    size_t n = strlen(s);
    size_t w1 = fwrite(s, 1, n, stderr);
    size_t w2 = fwrite("\n", 1, 1, stderr);
    fflush(stderr);
    return (int64_t)(w1 + w2);
}

int __mlang_std_io_stdout_flush(void)
{
    return fflush(stdout);
}

int __mlang_std_io_stderr_flush(void)
{
    return fflush(stderr);
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

// mode: 0=unbuffered, 1=line buffered, 2=fully buffered.
static int set_stream_buffering(FILE* fp, int mode, int64_t size)
{
    if(!fp)
        return -1;
    int c_mode = _IONBF;
    if(mode == 1)
        c_mode = _IOLBF;
    else if(mode == 2)
        c_mode = _IOFBF;

    size_t cap = (size > 0) ? (size_t)size : 0;
    return setvbuf(fp, NULL, c_mode, cap);
}

int __mlang_std_io_set_stdin_buffering(int mode, int64_t size)
{
    return set_stream_buffering(stdin, mode, size);
}

int __mlang_std_io_set_stdout_buffering(int mode, int64_t size)
{
    return set_stream_buffering(stdout, mode, size);
}

int __mlang_std_io_set_stderr_buffering(int mode, int64_t size)
{
    return set_stream_buffering(stderr, mode, size);
}

// Returns:
//  >0 bytes read (newline stripped)
//   0 no data currently available (would block)
//  -1 EOF/error
int64_t __mlang_std_io_stdin_read_line_nonblocking(char* buf, int64_t capacity)
{
    if(!buf || capacity <= 1)
        return -1;

    int fd = fileno(stdin);
    if(fd < 0)
        return -1;

    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0)
        return -1;
    if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
        return -1;

    int64_t out_n = 0;
    int saw_any = 0;
    while(out_n < (capacity - 1))
    {
        unsigned char ch = 0;
        ssize_t n = read(fd, &ch, 1);
        if(n == 1)
        {
            saw_any = 1;
            if(ch == '\n')
                break;
            if(ch == '\r')
                continue;
            buf[out_n++] = (char)ch;
            continue;
        }
        if(n == 0)
        {
            (void)fcntl(fd, F_SETFL, flags);
            if(!saw_any)
                return -1;
            break;
        }
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            (void)fcntl(fd, F_SETFL, flags);
            if(!saw_any)
            {
                buf[0] = '\0';
                return 0;
            }
            break;
        }
        (void)fcntl(fd, F_SETFL, flags);
        return -1;
    }

    (void)fcntl(fd, F_SETFL, flags);
    buf[out_n] = '\0';
    return out_n;
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

typedef struct
{
    char* data;
    int64_t len;
    int64_t cap;
    int64_t pos;
} mlang_io_cursor_t;

static int64_t cursor_grow(mlang_io_cursor_t* c, int64_t needed)
{
    if(!c || needed <= c->cap)
        return 0;

    int64_t next = c->cap > 0 ? c->cap : 16;
    while(next < needed)
        next *= 2;

    char* p = (char*)realloc(c->data, (size_t)next);
    if(!p)
        return -1;

    c->data = p;
    c->cap = next;
    return 0;
}

static mlang_io_cursor_t* cursor_from_handle(int64_t handle)
{
    return (mlang_io_cursor_t*)(intptr_t)handle;
}

int64_t __mlang_std_io_cursor_new(int64_t capacity)
{
    int64_t cap = capacity > 1 ? capacity : 16;
    mlang_io_cursor_t* c = (mlang_io_cursor_t*)malloc(sizeof(mlang_io_cursor_t));
    if(!c)
        return 0;

    c->data = (char*)malloc((size_t)cap);
    if(!c->data)
    {
        free(c);
        return 0;
    }

    c->data[0] = '\0';
    c->len = 0;
    c->cap = cap;
    c->pos = 0;
    return (int64_t)(intptr_t)c;
}

void __mlang_std_io_cursor_free(int64_t handle)
{
    mlang_io_cursor_t* c = cursor_from_handle(handle);
    if(!c)
        return;
    free(c->data);
    free(c);
}

int64_t __mlang_std_io_cursor_read(int64_t handle, char* buf, int64_t capacity)
{
    mlang_io_cursor_t* c = cursor_from_handle(handle);
    if(!c || !buf || capacity <= 1)
        return -1;

    int64_t remaining = c->len - c->pos;
    if(remaining < 0)
        remaining = 0;

    int64_t n = remaining < (capacity - 1) ? remaining : (capacity - 1);
    if(n > 0)
        (void)memcpy(buf, c->data + c->pos, (size_t)n);
    buf[n] = '\0';
    c->pos += n;
    return n;
}

int64_t __mlang_std_io_cursor_write(int64_t handle, const char* s)
{
    mlang_io_cursor_t* c = cursor_from_handle(handle);
    if(!c || !s)
        return -1;

    int64_t n = (int64_t)strlen(s);
    int64_t needed = c->pos + n + 1;
    if(cursor_grow(c, needed) != 0)
        return -1;

    if(n > 0)
        (void)memcpy(c->data + c->pos, s, (size_t)n);
    c->pos += n;
    if(c->pos > c->len)
        c->len = c->pos;
    c->data[c->len] = '\0';
    return n;
}

int64_t __mlang_std_io_cursor_seek(int64_t handle, int64_t offset, int whence)
{
    mlang_io_cursor_t* c = cursor_from_handle(handle);
    if(!c)
        return -1;

    int64_t base = 0;
    if(whence == 0)
        base = 0;
    else if(whence == 1)
        base = c->pos;
    else if(whence == 2)
        base = c->len;
    else
        return -1;

    int64_t next = base + offset;
    if(next < 0)
        next = 0;
    if(next > c->len)
        next = c->len;
    c->pos = next;
    return c->pos;
}

int64_t __mlang_std_io_cursor_read_line(int64_t handle, char* buf, int64_t capacity)
{
    mlang_io_cursor_t* c = cursor_from_handle(handle);
    if(!c || !buf || capacity <= 1)
        return -1;

    if(c->pos >= c->len)
    {
        buf[0] = '\0';
        return -1;
    }

    int64_t max_n = capacity - 1;
    int64_t out_n = 0;
    while(c->pos < c->len && out_n < max_n)
    {
        char ch = c->data[c->pos];
        if(ch == '\n')
        {
            c->pos++;
            break;
        }
        buf[out_n++] = ch;
        c->pos++;
    }
    buf[out_n] = '\0';
    return out_n;
}

/* -----------------------------------------------------------------------
 * get_char / printf
 * --------------------------------------------------------------------- */

int32_t __mlang_std_io_get_char(void)
{
    return (int32_t)getchar();
}

void __mlang_std_io_printf(const char* s)
{
    if(s) { fputs(s, stdout); fflush(stdout); }
}

void __mlang_std_io_eprintf(const char* s)
{
    if(s) { fputs(s, stderr); fflush(stderr); }
}

void __mlang_std_io_fprintf(int32_t fd, const char* s)
{
    if(!s) return;
    FILE* f = (fd == 2) ? stderr : stdout;
    fputs(s, f);
    fflush(f);
}

char* __mlang_std_io_cursor_to_string(int64_t handle)
{
    mlang_io_cursor_t* c = cursor_from_handle(handle);
    if(!c)
        return NULL;

    char* out = (char*)malloc((size_t)(c->len + 1));
    if(!out)
        return NULL;

    if(c->len > 0)
        (void)memcpy(out, c->data, (size_t)c->len);
    out[c->len] = '\0';
    return out;
}
