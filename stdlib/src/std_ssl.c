#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @file std_ssl.c
 * @brief OpenSSL-backed TLS client/server backend for std::ssl.
 */

typedef struct
{
    int fd;
    SSL_CTX* ctx;
} mlang_tls_listener_t;

typedef struct
{
    SSL* ssl;
} mlang_tls_stream_t;

static char g_last_error[512];

static void clear_error(void)
{
    g_last_error[0] = '\0';
}

static char* dup_cstr(const char* s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1u);
    if(!out)
        return NULL;
    if(n > 0u)
        (void)memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static void set_error_text(const char* text)
{
    if(!text)
        text = "std::ssl failed";
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s", text);
}

static void set_error_from_errno(const char* op)
{
    const char* msg = strerror(errno);
    if(!msg)
        msg = "unknown";
    (void)snprintf(g_last_error, sizeof(g_last_error),
                   "std::ssl %s failed: %s", op ? op : "operation", msg);
}

static void set_error_from_gai(const char* op, int rc)
{
    const char* msg = gai_strerror(rc);
    if(!msg)
        msg = "unknown";
    (void)snprintf(g_last_error, sizeof(g_last_error),
                   "std::ssl %s failed: %s", op ? op : "getaddrinfo", msg);
}

static void set_error_from_openssl(const char* op)
{
    unsigned long code = ERR_get_error();
    char buf[256];
    if(code == 0ul)
    {
        (void)snprintf(buf, sizeof(buf), "unknown OpenSSL error");
    }
    else
    {
        ERR_error_string_n(code, buf, sizeof(buf));
    }
    (void)snprintf(g_last_error, sizeof(g_last_error),
                   "std::ssl %s failed: %s", op ? op : "operation", buf);
}

static int ensure_ssl_ready(void)
{
    if(OPENSSL_init_ssl(0, NULL) != 1)
    {
        set_error_from_openssl("init");
        return -1;
    }
    return 0;
}

static int parse_port(int64_t port, char* out, size_t out_size)
{
    if(!out || out_size == 0u || port < 0 || port > 65535)
    {
        errno = EINVAL;
        set_error_from_errno("port");
        return -1;
    }
    (void)snprintf(out, out_size, "%lld", (long long)port);
    return 0;
}

static int socket_connect_host(const char* host, int64_t port)
{
    if(!host || host[0] == '\0')
    {
        errno = EINVAL;
        set_error_from_errno("connect");
        return -1;
    }

    char service[32];
    if(parse_port(port, service, sizeof(service)) != 0)
        return -1;

    struct addrinfo hints;
    struct addrinfo* result = NULL;
    struct addrinfo* it = NULL;
    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, service, &hints, &result);
    if(rc != 0)
    {
        set_error_from_gai("connect getaddrinfo", rc);
        return -1;
    }

    int fd = -1;
    for(it = result; it; it = it->ai_next)
    {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(fd < 0)
            continue;
        if(connect(fd, it->ai_addr, it->ai_addrlen) == 0)
            break;
        (void)close(fd);
        fd = -1;
    }

    if(fd < 0)
        set_error_from_errno("connect");
    else
        clear_error();

    freeaddrinfo(result);
    return fd;
}

static int socket_bind_listener(const char* addr, int64_t port)
{
    char service[32];
    if(parse_port(port, service, sizeof(service)) != 0)
        return -1;

    struct addrinfo hints;
    struct addrinfo* result = NULL;
    struct addrinfo* it = NULL;
    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    const char* bind_addr = (addr && addr[0] != '\0') ? addr : NULL;
    int rc = getaddrinfo(bind_addr, service, &hints, &result);
    if(rc != 0)
    {
        set_error_from_gai("bind getaddrinfo", rc);
        return -1;
    }

    int fd = -1;
    for(it = result; it; it = it->ai_next)
    {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(fd < 0)
            continue;

        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        if(bind(fd, it->ai_addr, it->ai_addrlen) == 0 && listen(fd, 16) == 0)
            break;

        (void)close(fd);
        fd = -1;
    }

    if(fd < 0)
        set_error_from_errno("bind");
    else
        clear_error();

    freeaddrinfo(result);
    return fd;
}

static int64_t local_port_for_fd(int fd, const char* op)
{
    if(fd < 0)
    {
        errno = EINVAL;
        set_error_from_errno(op);
        return -1;
    }

    struct sockaddr_storage sa;
    socklen_t n = (socklen_t)sizeof(sa);
    (void)memset(&sa, 0, sizeof(sa));
    if(getsockname(fd, (struct sockaddr*)&sa, &n) != 0)
    {
        set_error_from_errno("getsockname");
        return -1;
    }

    clear_error();
    if(sa.ss_family == AF_INET)
        return (int64_t)ntohs(((struct sockaddr_in*)&sa)->sin_port);
    if(sa.ss_family == AF_INET6)
        return (int64_t)ntohs(((struct sockaddr_in6*)&sa)->sin6_port);

    errno = EAFNOSUPPORT;
    set_error_from_errno("local_port");
    return -1;
}

char* __mlang_std_ssl_last_error(void)
{
    if(g_last_error[0] == '\0')
        return dup_cstr("");
    return dup_cstr(g_last_error);
}

int64_t __mlang_std_ssl_client_connect(const char* host, int64_t port,
                                       const char* server_name,
                                       const char* ca_file, int verify_peer)
{
    SSL_CTX* ctx = NULL;
    SSL* ssl = NULL;
    BIO* bio = NULL;
    int fd = -1;
    mlang_tls_stream_t* stream = NULL;
    const char* verify_name = NULL;

    if(ensure_ssl_ready() != 0)
        return 0;

    ctx = SSL_CTX_new(TLS_client_method());
    if(!ctx)
    {
        set_error_from_openssl("SSL_CTX_new");
        return 0;
    }

    if(verify_peer)
    {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        if(ca_file && ca_file[0] != '\0')
        {
            if(SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1)
            {
                set_error_from_openssl("load_verify_locations");
                goto fail;
            }
        }
        else if(SSL_CTX_set_default_verify_paths(ctx) != 1)
        {
            set_error_from_openssl("set_default_verify_paths");
            goto fail;
        }
    }
    else
    {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    fd = socket_connect_host(host, port);
    if(fd < 0)
        goto fail;

    ssl = SSL_new(ctx);
    if(!ssl)
    {
        set_error_from_openssl("SSL_new");
        goto fail;
    }

    bio = BIO_new_socket(fd, BIO_CLOSE);
    if(!bio)
    {
        set_error_from_openssl("BIO_new_socket");
        goto fail;
    }
    SSL_set_bio(ssl, bio, bio);
    bio = NULL;
    fd = -1;

    verify_name = (server_name && server_name[0] != '\0') ? server_name : host;
    if(verify_name && verify_name[0] != '\0')
    {
        if(SSL_set_tlsext_host_name(ssl, verify_name) != 1)
        {
            set_error_from_openssl("set_tlsext_host_name");
            goto fail;
        }
        if(verify_peer)
        {
            X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
            if(!param || X509_VERIFY_PARAM_set1_host(param, verify_name, 0) != 1)
            {
                set_error_from_openssl("set1_host");
                goto fail;
            }
        }
    }

    if(SSL_connect(ssl) != 1)
    {
        set_error_from_openssl("SSL_connect");
        goto fail;
    }

    if(verify_peer)
    {
        long verify_rc = SSL_get_verify_result(ssl);
        if(verify_rc != X509_V_OK)
        {
            (void)snprintf(g_last_error, sizeof(g_last_error),
                           "std::ssl verify failed: %s",
                           X509_verify_cert_error_string(verify_rc));
            goto fail;
        }
    }

    stream = (mlang_tls_stream_t*)malloc(sizeof(*stream));
    if(!stream)
    {
        errno = ENOMEM;
        set_error_from_errno("malloc");
        goto fail;
    }

    stream->ssl = ssl;
    SSL_CTX_free(ctx);
    clear_error();
    return (int64_t)(intptr_t)stream;

fail:
    if(bio)
        BIO_free_all(bio);
    if(ssl)
        SSL_free(ssl);
    if(fd >= 0)
        (void)close(fd);
    if(ctx)
        SSL_CTX_free(ctx);
    return 0;
}

int64_t __mlang_std_ssl_listener_bind(const char* addr, int64_t port,
                                      const char* cert_file,
                                      const char* key_file)
{
    SSL_CTX* ctx = NULL;
    int fd = -1;
    mlang_tls_listener_t* listener = NULL;

    if(!cert_file || cert_file[0] == '\0' || !key_file || key_file[0] == '\0')
    {
        errno = EINVAL;
        set_error_from_errno("bind");
        return 0;
    }

    if(ensure_ssl_ready() != 0)
        return 0;

    ctx = SSL_CTX_new(TLS_server_method());
    if(!ctx)
    {
        set_error_from_openssl("SSL_CTX_new");
        return 0;
    }

    if(SSL_CTX_use_certificate_chain_file(ctx, cert_file) != 1)
    {
        set_error_from_openssl("use_certificate_chain_file");
        goto fail;
    }

    if(SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1)
    {
        set_error_from_openssl("use_PrivateKey_file");
        goto fail;
    }

    if(SSL_CTX_check_private_key(ctx) != 1)
    {
        set_error_from_openssl("check_private_key");
        goto fail;
    }

    fd = socket_bind_listener(addr, port);
    if(fd < 0)
        goto fail;

    listener = (mlang_tls_listener_t*)malloc(sizeof(*listener));
    if(!listener)
    {
        errno = ENOMEM;
        set_error_from_errno("malloc");
        goto fail;
    }

    listener->fd = fd;
    listener->ctx = ctx;
    clear_error();
    return (int64_t)(intptr_t)listener;

fail:
    if(fd >= 0)
        (void)close(fd);
    if(ctx)
        SSL_CTX_free(ctx);
    return 0;
}

int64_t __mlang_std_ssl_listener_local_port(int64_t handle)
{
    mlang_tls_listener_t* listener = (mlang_tls_listener_t*)(intptr_t)handle;
    if(!listener || listener->fd < 0)
    {
        errno = EINVAL;
        set_error_from_errno("local_port");
        return -1;
    }
    return local_port_for_fd(listener->fd, "local_port");
}

int64_t __mlang_std_ssl_listener_accept(int64_t handle)
{
    mlang_tls_listener_t* listener = (mlang_tls_listener_t*)(intptr_t)handle;
    mlang_tls_stream_t* stream = NULL;
    SSL* ssl = NULL;
    BIO* bio = NULL;
    int fd = -1;

    if(!listener || listener->fd < 0 || !listener->ctx)
    {
        errno = EINVAL;
        set_error_from_errno("accept");
        return 0;
    }

    fd = accept(listener->fd, NULL, NULL);
    if(fd < 0)
    {
        set_error_from_errno("accept");
        return 0;
    }

    ssl = SSL_new(listener->ctx);
    if(!ssl)
    {
        set_error_from_openssl("SSL_new");
        goto fail;
    }

    bio = BIO_new_socket(fd, BIO_CLOSE);
    if(!bio)
    {
        set_error_from_openssl("BIO_new_socket");
        goto fail;
    }
    SSL_set_bio(ssl, bio, bio);
    bio = NULL;
    fd = -1;

    if(SSL_accept(ssl) != 1)
    {
        set_error_from_openssl("SSL_accept");
        goto fail;
    }

    stream = (mlang_tls_stream_t*)malloc(sizeof(*stream));
    if(!stream)
    {
        errno = ENOMEM;
        set_error_from_errno("malloc");
        goto fail;
    }

    stream->ssl = ssl;
    clear_error();
    return (int64_t)(intptr_t)stream;

fail:
    if(bio)
        BIO_free_all(bio);
    if(ssl)
        SSL_free(ssl);
    if(fd >= 0)
        (void)close(fd);
    return 0;
}

int __mlang_std_ssl_listener_close(int64_t handle)
{
    mlang_tls_listener_t* listener = (mlang_tls_listener_t*)(intptr_t)handle;
    if(!listener)
    {
        errno = EINVAL;
        set_error_from_errno("close");
        return -1;
    }

    int rc = 0;
    if(listener->fd >= 0 && close(listener->fd) != 0)
    {
        set_error_from_errno("close");
        rc = -1;
    }
    if(listener->ctx)
        SSL_CTX_free(listener->ctx);
    free(listener);
    if(rc == 0)
        clear_error();
    return rc;
}

int64_t __mlang_std_ssl_stream_read(int64_t handle, char* buf, int64_t capacity)
{
    mlang_tls_stream_t* stream = (mlang_tls_stream_t*)(intptr_t)handle;
    if(!stream || !stream->ssl || !buf || capacity <= 1)
    {
        errno = EINVAL;
        set_error_from_errno("read");
        return -1;
    }

    int n = SSL_read(stream->ssl, buf, (int)(capacity - 1));
    if(n <= 0)
    {
        int err = SSL_get_error(stream->ssl, n);
        if(err == SSL_ERROR_ZERO_RETURN)
        {
            buf[0] = '\0';
            clear_error();
            return 0;
        }
        set_error_from_openssl("SSL_read");
        return -1;
    }

    buf[n] = '\0';
    clear_error();
    return (int64_t)n;
}

int64_t __mlang_std_ssl_stream_write(int64_t handle, const char* s)
{
    mlang_tls_stream_t* stream = (mlang_tls_stream_t*)(intptr_t)handle;
    if(!stream || !stream->ssl || !s)
    {
        errno = EINVAL;
        set_error_from_errno("write");
        return -1;
    }

    size_t total = 0u;
    size_t len = strlen(s);
    while(total < len)
    {
        int n = SSL_write(stream->ssl, s + total, (int)(len - total));
        if(n <= 0)
        {
            set_error_from_openssl("SSL_write");
            return -1;
        }
        total += (size_t)n;
    }

    clear_error();
    return (int64_t)total;
}

int __mlang_std_ssl_stream_close(int64_t handle)
{
    mlang_tls_stream_t* stream = (mlang_tls_stream_t*)(intptr_t)handle;
    if(!stream)
    {
        errno = EINVAL;
        set_error_from_errno("close");
        return -1;
    }

    if(stream->ssl)
    {
        (void)SSL_shutdown(stream->ssl);
        SSL_free(stream->ssl);
    }
    free(stream);
    clear_error();
    return 0;
}
