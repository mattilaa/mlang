#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
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
typedef SOCKET mlang_ipc_socket_t;
#define MLANG_IPC_INVALID_SOCKET INVALID_SOCKET
#else
typedef int mlang_ipc_socket_t;
#define MLANG_IPC_INVALID_SOCKET (-1)
#endif

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
static int ipc_winsock_init(void)
{
    static int initialized = 0;
    if(initialized)
        return 0;
    WSADATA data;
    int rc = WSAStartup(MAKEWORD(2, 2), &data);
    if(rc != 0)
    {
        (void)snprintf(g_ipc_last_error, sizeof(g_ipc_last_error),
                       "std::ipc WSAStartup failed: Windows socket error %d", rc);
        return -1;
    }
    initialized = 1;
    return 0;
}

static void ipc_set_wsa_error(const char* prefix)
{
    int err = WSAGetLastError();
    (void)snprintf(g_ipc_last_error, sizeof(g_ipc_last_error), "%s: Windows socket error %d",
                   prefix ? prefix : "std::ipc", err);
}

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

static int local_socket_path(const char* path, struct sockaddr_un* sa)
{
    if(!path || !path[0] || !sa)
    {
        ipc_set_error("std::ipc local socket: path is empty");
        return -1;
    }

    size_t n = strlen(path);
    if(n >= sizeof(sa->sun_path))
    {
        ipc_set_error("std::ipc local socket: path is too long");
        return -1;
    }

    (void)memset(sa, 0, sizeof(*sa));
    sa->sun_family = AF_UNIX;
    (void)memcpy(sa->sun_path, path, n + 1u);
    return 0;
}

static int local_socket_close_raw(mlang_ipc_socket_t s)
{
#if defined(_WIN32)
    return closesocket(s) == 0 ? 0 : -1;
#else
    return close(s);
#endif
}

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

int64_t __mlang_std_ipc_local_socket_bind(const char* path)
{
    struct sockaddr_un sa;
    if(local_socket_path(path, &sa) != 0)
        return 0;

#if defined(_WIN32)
    if(ipc_winsock_init() != 0)
        return 0;
#else
    (void)unlink(path);
#endif

    mlang_ipc_socket_t s = socket(AF_UNIX, SOCK_STREAM, 0);
    if(s == MLANG_IPC_INVALID_SOCKET)
    {
#if defined(_WIN32)
        ipc_set_wsa_error("std::ipc local socket create failed");
#else
        ipc_set_errno_error("std::ipc local socket create failed");
#endif
        return 0;
    }

    if(bind(s, (struct sockaddr*)&sa, (socklen_t)sizeof(sa)) != 0)
    {
#if defined(_WIN32)
        ipc_set_wsa_error("std::ipc local socket bind failed");
#else
        ipc_set_errno_error("std::ipc local socket bind failed");
#endif
        (void)local_socket_close_raw(s);
        return 0;
    }

    if(listen(s, 16) != 0)
    {
#if defined(_WIN32)
        ipc_set_wsa_error("std::ipc local socket listen failed");
#else
        ipc_set_errno_error("std::ipc local socket listen failed");
#endif
        (void)local_socket_close_raw(s);
        return 0;
    }

    g_ipc_last_error[0] = '\0';
    return (int64_t)(intptr_t)s;
}

int64_t __mlang_std_ipc_local_socket_accept(int64_t listener)
{
    mlang_ipc_socket_t l = (mlang_ipc_socket_t)(intptr_t)listener;
    if(listener == 0 || l == MLANG_IPC_INVALID_SOCKET)
    {
        ipc_set_error("std::ipc local socket accept: invalid listener");
        return 0;
    }

    mlang_ipc_socket_t s = accept(l, NULL, NULL);
    if(s == MLANG_IPC_INVALID_SOCKET)
    {
#if defined(_WIN32)
        ipc_set_wsa_error("std::ipc local socket accept failed");
#else
        ipc_set_errno_error("std::ipc local socket accept failed");
#endif
        return 0;
    }

    g_ipc_last_error[0] = '\0';
    return (int64_t)(intptr_t)s;
}

int64_t __mlang_std_ipc_local_socket_connect(const char* path)
{
    struct sockaddr_un sa;
    if(local_socket_path(path, &sa) != 0)
        return 0;

#if defined(_WIN32)
    if(ipc_winsock_init() != 0)
        return 0;
#endif

    mlang_ipc_socket_t s = socket(AF_UNIX, SOCK_STREAM, 0);
    if(s == MLANG_IPC_INVALID_SOCKET)
    {
#if defined(_WIN32)
        ipc_set_wsa_error("std::ipc local socket create failed");
#else
        ipc_set_errno_error("std::ipc local socket create failed");
#endif
        return 0;
    }

    if(connect(s, (struct sockaddr*)&sa, (socklen_t)sizeof(sa)) != 0)
    {
#if defined(_WIN32)
        ipc_set_wsa_error("std::ipc local socket connect failed");
#else
        ipc_set_errno_error("std::ipc local socket connect failed");
#endif
        (void)local_socket_close_raw(s);
        return 0;
    }

    g_ipc_last_error[0] = '\0';
    return (int64_t)(intptr_t)s;
}

int64_t __mlang_std_ipc_local_socket_read(int64_t handle, char* buf, int64_t capacity)
{
    mlang_ipc_socket_t s = (mlang_ipc_socket_t)(intptr_t)handle;
    if(handle == 0 || s == MLANG_IPC_INVALID_SOCKET || !buf || capacity <= 1)
    {
        ipc_set_error("std::ipc local socket read: invalid arguments");
        return -1;
    }

#if defined(_WIN32)
    int n = recv(s, buf, (int)(capacity - 1), 0);
    if(n < 0)
    {
        ipc_set_wsa_error("std::ipc local socket read failed");
        return -1;
    }
#else
    ssize_t n = recv(s, buf, (size_t)(capacity - 1), 0);
    if(n < 0)
    {
        ipc_set_errno_error("std::ipc local socket read failed");
        return -1;
    }
#endif
    buf[n] = '\0';
    g_ipc_last_error[0] = '\0';
    return (int64_t)n;
}

int64_t __mlang_std_ipc_local_socket_write(int64_t handle, const char* str)
{
    mlang_ipc_socket_t s = (mlang_ipc_socket_t)(intptr_t)handle;
    if(handle == 0 || s == MLANG_IPC_INVALID_SOCKET || !str)
    {
        ipc_set_error("std::ipc local socket write: invalid arguments");
        return -1;
    }

    size_t len = strlen(str);
#if defined(_WIN32)
    int n = send(s, str, (int)len, 0);
    if(n < 0)
    {
        ipc_set_wsa_error("std::ipc local socket write failed");
        return -1;
    }
#else
    ssize_t n = send(s, str, len, 0);
    if(n < 0)
    {
        ipc_set_errno_error("std::ipc local socket write failed");
        return -1;
    }
#endif
    g_ipc_last_error[0] = '\0';
    return (int64_t)n;
}

int32_t __mlang_std_ipc_local_socket_close(int64_t handle)
{
    mlang_ipc_socket_t s = (mlang_ipc_socket_t)(intptr_t)handle;
    if(handle == 0 || s == MLANG_IPC_INVALID_SOCKET)
        return 0;

    if(local_socket_close_raw(s) != 0)
    {
#if defined(_WIN32)
        ipc_set_wsa_error("std::ipc local socket close failed");
#else
        ipc_set_errno_error("std::ipc local socket close failed");
#endif
        return -1;
    }
    g_ipc_last_error[0] = '\0';
    return 0;
}

int32_t __mlang_std_ipc_local_socket_remove(const char* path)
{
    if(!path || !path[0])
        return -1;
#if defined(_WIN32)
    if(DeleteFileA(path) == 0)
    {
        DWORD err = GetLastError();
        if(err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
        {
            ipc_set_last_error("std::ipc local socket remove failed");
            return -1;
        }
    }
    return 0;
#else
    if(unlink(path) != 0 && errno != ENOENT)
    {
        ipc_set_errno_error("std::ipc local socket remove failed");
        return -1;
    }
    return 0;
#endif
}
