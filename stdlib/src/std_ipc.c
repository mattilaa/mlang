#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

typedef struct
{
#if defined(_WIN32)
    HANDLE handle;
    int is_server;
    int connected;
#else
    int fd;
#endif
} mlang_ipc_named_pipe_t;

#if defined(_WIN32)
static __declspec(thread) char g_ipc_last_error[512];
#else
static __thread char g_ipc_last_error[512];
#endif

static void ipc_set_error(const char* msg)
{
    if(!msg)
        msg = "std::ipc: unknown error";
    (void)snprintf(g_ipc_last_error, sizeof(g_ipc_last_error), "%s", msg);
}

#if defined(_WIN32)
static void ipc_set_last_error(const char* prefix)
{
    DWORD err = GetLastError();
    (void)snprintf(g_ipc_last_error, sizeof(g_ipc_last_error), "%s: Windows error %lu",
                   prefix ? prefix : "std::ipc", (unsigned long)err);
}

static char* pipe_name_to_windows_path(const char* name)
{
    if(!name || !name[0])
        return NULL;
    if(strncmp(name, "\\\\.\\pipe\\", 9) == 0)
    {
        size_t n = strlen(name);
        char* out = (char*)malloc(n + 1u);
        if(out)
            memcpy(out, name, n + 1u);
        return out;
    }
    size_t n = strlen(name);
    const char* prefix = "\\\\.\\pipe\\";
    size_t prefix_n = strlen(prefix);
    char* out = (char*)malloc(prefix_n + n + 1u);
    if(!out)
        return NULL;
    memcpy(out, prefix, prefix_n);
    memcpy(out + prefix_n, name, n + 1u);
    return out;
}
#else
static void ipc_set_errno_error(const char* prefix)
{
    const char* e = strerror(errno);
    (void)snprintf(g_ipc_last_error, sizeof(g_ipc_last_error), "%s: %s",
                   prefix ? prefix : "std::ipc", e ? e : "unknown error");
}
#endif

const char* __mlang_std_ipc_last_error(void)
{
    return g_ipc_last_error;
}

static int64_t make_pipe_handle(void)
{
    mlang_ipc_named_pipe_t* p = (mlang_ipc_named_pipe_t*)calloc(1u, sizeof(*p));
    if(!p)
        ipc_set_error("std::ipc: allocation failed");
    return (int64_t)(intptr_t)p;
}

int64_t __mlang_std_ipc_named_pipe_create(const char* name)
{
    if(!name || !name[0])
    {
        ipc_set_error("std::ipc named pipe create: name is empty");
        return 0;
    }
    int64_t raw = make_pipe_handle();
    if(raw == 0)
        return 0;
    mlang_ipc_named_pipe_t* p = (mlang_ipc_named_pipe_t*)(intptr_t)raw;
#if defined(_WIN32)
    char* path = pipe_name_to_windows_path(name);
    if(!path)
    {
        free(p);
        ipc_set_error("std::ipc named pipe create: invalid name");
        return 0;
    }
    p->handle = CreateNamedPipeA(path,
                                 PIPE_ACCESS_DUPLEX,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
                                 PIPE_UNLIMITED_INSTANCES,
                                 4096,
                                 4096,
                                 0,
                                 NULL);
    free(path);
    if(p->handle == INVALID_HANDLE_VALUE)
    {
        free(p);
        ipc_set_last_error("std::ipc named pipe create failed");
        return 0;
    }
    p->is_server = 1;
    p->connected = 0;
#else
    if(mkfifo(name, 0600) != 0 && errno != EEXIST)
    {
        free(p);
        ipc_set_errno_error("std::ipc mkfifo failed");
        return 0;
    }
    p->fd = open(name, O_RDWR | O_NONBLOCK);
    if(p->fd < 0)
    {
        free(p);
        ipc_set_errno_error("std::ipc named pipe open failed");
        return 0;
    }
#endif
    g_ipc_last_error[0] = '\0';
    return raw;
}

int64_t __mlang_std_ipc_named_pipe_connect(const char* name)
{
    if(!name || !name[0])
    {
        ipc_set_error("std::ipc named pipe connect: name is empty");
        return 0;
    }
    int64_t raw = make_pipe_handle();
    if(raw == 0)
        return 0;
    mlang_ipc_named_pipe_t* p = (mlang_ipc_named_pipe_t*)(intptr_t)raw;
#if defined(_WIN32)
    char* path = pipe_name_to_windows_path(name);
    if(!path)
    {
        free(p);
        ipc_set_error("std::ipc named pipe connect: invalid name");
        return 0;
    }
    p->handle = CreateFileA(path,
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
    free(path);
    if(p->handle == INVALID_HANDLE_VALUE)
    {
        free(p);
        ipc_set_last_error("std::ipc named pipe connect failed");
        return 0;
    }
    p->is_server = 0;
    p->connected = 1;
#else
    p->fd = open(name, O_RDWR | O_NONBLOCK);
    if(p->fd < 0)
    {
        free(p);
        ipc_set_errno_error("std::ipc named pipe connect failed");
        return 0;
    }
#endif
    g_ipc_last_error[0] = '\0';
    return raw;
}

int64_t __mlang_std_ipc_named_pipe_read(int64_t handle, char* buf, int64_t capacity)
{
    mlang_ipc_named_pipe_t* p = (mlang_ipc_named_pipe_t*)(intptr_t)handle;
    if(!p || !buf || capacity <= 0)
    {
        ipc_set_error("std::ipc named pipe read: invalid arguments");
        return -1;
    }
#if defined(_WIN32)
    if(p->is_server && !p->connected)
    {
        BOOL connected = ConnectNamedPipe(p->handle, NULL);
        DWORD err = connected ? ERROR_SUCCESS : GetLastError();
        if(connected || err == ERROR_PIPE_CONNECTED)
            p->connected = 1;
        else if(err == ERROR_PIPE_LISTENING || err == ERROR_NO_DATA)
            return -2;
        else
        {
            ipc_set_last_error("std::ipc named pipe connect failed");
            return -1;
        }
    }
    DWORD got = 0;
    BOOL ok = ReadFile(p->handle, buf, (DWORD)(capacity - 1), &got, NULL);
    if(!ok)
    {
        DWORD err = GetLastError();
        if(err == ERROR_NO_DATA || err == ERROR_PIPE_LISTENING)
            return -2;
        ipc_set_last_error("std::ipc named pipe read failed");
        return -1;
    }
    buf[got < (DWORD)capacity ? got : (DWORD)capacity - 1u] = '\0';
    return (int64_t)got;
#else
    ssize_t n = read(p->fd, buf, (size_t)(capacity - 1));
    if(n < 0)
    {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            return -2;
        ipc_set_errno_error("std::ipc named pipe read failed");
        return -1;
    }
    buf[n < capacity ? n : capacity - 1] = '\0';
    return (int64_t)n;
#endif
}

int64_t __mlang_std_ipc_named_pipe_write(int64_t handle, const char* s)
{
    mlang_ipc_named_pipe_t* p = (mlang_ipc_named_pipe_t*)(intptr_t)handle;
    if(!p || !s)
    {
        ipc_set_error("std::ipc named pipe write: invalid arguments");
        return -1;
    }
    size_t n = strlen(s);
#if defined(_WIN32)
    if(p->is_server && !p->connected)
    {
        BOOL connected = ConnectNamedPipe(p->handle, NULL);
        DWORD err = connected ? ERROR_SUCCESS : GetLastError();
        if(connected || err == ERROR_PIPE_CONNECTED)
            p->connected = 1;
        else if(err == ERROR_PIPE_LISTENING || err == ERROR_NO_DATA)
            return -2;
        else
        {
            ipc_set_last_error("std::ipc named pipe connect failed");
            return -1;
        }
    }
    DWORD written = 0;
    if(!WriteFile(p->handle, s, (DWORD)n, &written, NULL))
    {
        ipc_set_last_error("std::ipc named pipe write failed");
        return -1;
    }
    return (int64_t)written;
#else
    ssize_t written = write(p->fd, s, n);
    if(written < 0)
    {
        ipc_set_errno_error("std::ipc named pipe write failed");
        return -1;
    }
    return (int64_t)written;
#endif
}

int32_t __mlang_std_ipc_named_pipe_close(int64_t handle)
{
    mlang_ipc_named_pipe_t* p = (mlang_ipc_named_pipe_t*)(intptr_t)handle;
    if(!p)
        return 0;
#if defined(_WIN32)
    if(p->handle != INVALID_HANDLE_VALUE && p->handle != NULL)
        CloseHandle(p->handle);
#else
    if(p->fd >= 0)
        close(p->fd);
#endif
    free(p);
    return 0;
}

int32_t __mlang_std_ipc_named_pipe_remove(const char* name)
{
    if(!name || !name[0])
        return -1;
#if defined(_WIN32)
    (void)name;
    return 0;
#else
    if(unlink(name) != 0 && errno != ENOENT)
    {
        ipc_set_errno_error("std::ipc named pipe remove failed");
        return -1;
    }
    return 0;
#endif
}
