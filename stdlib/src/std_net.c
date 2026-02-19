#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

static char g_last_error[256];

static void set_error_from_errno(const char* op)
{
    if(!op)
        op = "net";
    const char* e = strerror(errno);
    if(!e)
        e = "unknown";
    (void)snprintf(g_last_error, sizeof(g_last_error), "std::net %s failed: %s",
                   op, e);
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

char* __mlang_std_net_last_error(void)
{
    if(g_last_error[0] == '\0')
        return dup_cstr("");
    return dup_cstr(g_last_error);
}

int64_t __mlang_std_net_tcp_bind(const char* addr, int64_t port)
{
    if(!addr || port < 0 || port > 65535)
    {
        errno = EINVAL;
        set_error_from_errno("bind");
        return 0;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        set_error_from_errno("socket");
        return 0;
    }

    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    (void)memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if(inet_pton(AF_INET, addr, &sa.sin_addr) != 1)
    {
        set_error_from_errno("inet_pton");
        (void)close(fd);
        return 0;
    }

    if(bind(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0)
    {
        set_error_from_errno("bind");
        (void)close(fd);
        return 0;
    }

    if(listen(fd, 16) != 0)
    {
        set_error_from_errno("listen");
        (void)close(fd);
        return 0;
    }

    clear_error();
    return (int64_t)fd;
}

int64_t __mlang_std_net_tcp_local_port(int64_t handle)
{
    int fd = (int)handle;
    if(fd <= 0)
    {
        errno = EINVAL;
        set_error_from_errno("local_port");
        return -1;
    }

    struct sockaddr_in sa;
    socklen_t n = (socklen_t)sizeof(sa);
    (void)memset(&sa, 0, sizeof(sa));
    if(getsockname(fd, (struct sockaddr*)&sa, &n) != 0)
    {
        set_error_from_errno("getsockname");
        return -1;
    }

    clear_error();
    return (int64_t)ntohs(sa.sin_port);
}

int64_t __mlang_std_net_tcp_accept(int64_t listener)
{
    int lfd = (int)listener;
    if(lfd <= 0)
    {
        errno = EINVAL;
        set_error_from_errno("accept");
        return 0;
    }

    struct sockaddr_in sa;
    socklen_t n = (socklen_t)sizeof(sa);
    int cfd = accept(lfd, (struct sockaddr*)&sa, &n);
    if(cfd < 0)
    {
        set_error_from_errno("accept");
        return 0;
    }

    clear_error();
    return (int64_t)cfd;
}

int64_t __mlang_std_net_tcp_connect(const char* addr, int64_t port)
{
    if(!addr || port < 0 || port > 65535)
    {
        errno = EINVAL;
        set_error_from_errno("connect");
        return 0;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        set_error_from_errno("socket");
        return 0;
    }

    struct sockaddr_in sa;
    (void)memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if(inet_pton(AF_INET, addr, &sa.sin_addr) != 1)
    {
        set_error_from_errno("inet_pton");
        (void)close(fd);
        return 0;
    }

    if(connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0)
    {
        set_error_from_errno("connect");
        (void)close(fd);
        return 0;
    }

    clear_error();
    return (int64_t)fd;
}

int64_t __mlang_std_net_tcp_read(int64_t handle, char* buf, int64_t capacity)
{
    int fd = (int)handle;
    if(fd <= 0 || !buf || capacity <= 1)
    {
        errno = EINVAL;
        set_error_from_errno("read");
        return -1;
    }

    ssize_t n = recv(fd, buf, (size_t)(capacity - 1), 0);
    if(n < 0)
    {
        set_error_from_errno("recv");
        return -1;
    }
    buf[n] = '\0';
    clear_error();
    return (int64_t)n;
}

int64_t __mlang_std_net_tcp_write(int64_t handle, const char* s)
{
    int fd = (int)handle;
    if(fd <= 0 || !s)
    {
        errno = EINVAL;
        set_error_from_errno("write");
        return -1;
    }

    size_t len = strlen(s);
    ssize_t n = send(fd, s, len, 0);
    if(n < 0)
    {
        set_error_from_errno("send");
        return -1;
    }
    clear_error();
    return (int64_t)n;
}

int __mlang_std_net_tcp_close(int64_t handle)
{
    int fd = (int)handle;
    if(fd <= 0)
    {
        errno = EINVAL;
        set_error_from_errno("close");
        return -1;
    }

    int rc = close(fd);
    if(rc != 0)
    {
        set_error_from_errno("close");
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_net_tcp_set_nonblocking(int64_t handle, int enabled)
{
    int fd = (int)handle;
    if(fd <= 0)
    {
        errno = EINVAL;
        set_error_from_errno("set_nonblocking");
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0)
    {
        set_error_from_errno("fcntl(F_GETFL)");
        return -1;
    }
    if(enabled)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    if(fcntl(fd, F_SETFL, flags) != 0)
    {
        set_error_from_errno("fcntl(F_SETFL)");
        return -1;
    }

    clear_error();
    return 0;
}

static int set_socket_timeout_ms(int fd, int optname, int64_t timeout_ms,
                                 const char* opname)
{
    if(fd <= 0 || timeout_ms < 0)
    {
        errno = EINVAL;
        set_error_from_errno(opname);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = (time_t)(timeout_ms / 1000);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);

    if(setsockopt(fd, SOL_SOCKET, optname, &tv, (socklen_t)sizeof(tv)) != 0)
    {
        set_error_from_errno(opname);
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_net_tcp_set_read_timeout_ms(int64_t handle, int64_t timeout_ms)
{
    return set_socket_timeout_ms((int)handle, SO_RCVTIMEO, timeout_ms,
                                 "set_read_timeout_ms");
}

int __mlang_std_net_tcp_set_write_timeout_ms(int64_t handle, int64_t timeout_ms)
{
    return set_socket_timeout_ms((int)handle, SO_SNDTIMEO, timeout_ms,
                                 "set_write_timeout_ms");
}
