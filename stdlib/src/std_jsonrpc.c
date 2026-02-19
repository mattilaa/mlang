#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static __thread char g_last_error[512];

static pthread_mutex_t g_write_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cancel_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    int64_t* ids;
    size_t len;
    size_t cap;
} cancel_registry_t;

static cancel_registry_t g_cancel_registry = {NULL, 0u, 0u};

typedef struct
{
    pthread_mutex_t mutex;
    char** uris;
    size_t len;
    size_t cap;
} uri_store_t;

static void set_error(const char* msg)
{
    const char* text = msg ? msg : "std::jsonrpc: unknown error";
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s", text);
}

static void set_errno_error(const char* prefix)
{
    const char* p = prefix ? prefix : "std::jsonrpc";
    const char* e = strerror(errno);
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s: %s", p,
                   e ? e : "unknown error");
}

static void clear_error(void)
{
    g_last_error[0] = '\0';
}

static char* dup_cstr(const char* s)
{
    const char* text = s ? s : "";
    size_t n = strlen(text);
    char* out = (char*)malloc(n + 1u);
    if(!out)
        return NULL;
    if(n > 0)
        (void)memcpy(out, text, n);
    out[n] = '\0';
    return out;
}

static int starts_with_ci(const char* text, const char* prefix)
{
    if(!text || !prefix)
        return 0;
    for(; *prefix != '\0'; ++prefix, ++text)
    {
        if(*text == '\0')
            return 0;
        if(tolower((unsigned char)*text) != tolower((unsigned char)*prefix))
            return 0;
    }
    return 1;
}

static int read_byte_with_timeout(int fd, char* out, int64_t timeout_ms)
{
    if(!out)
        return -1;

    if(timeout_ms >= 0)
    {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, (int)timeout_ms);
        if(pr == 0)
            return -2;
        if(pr < 0)
            return -1;
        if((pfd.revents & POLLIN) == 0)
            return -1;
    }

    ssize_t n = read(fd, out, 1u);
    if(n == 1)
        return 1;
    if(n == 0)
        return 0;
    return -1;
}

static int read_line_fd(int fd, char* line, size_t cap, int64_t timeout_ms)
{
    if(!line || cap < 2u)
        return -1;

    size_t len = 0u;
    for(;;)
    {
        char ch = '\0';
        int rc = read_byte_with_timeout(fd, &ch, timeout_ms);
        if(rc == -2)
            return -2;
        if(rc <= 0)
        {
            if(len == 0u)
                return rc;
            break;
        }
        if(ch == '\n')
            break;
        if(ch == '\r')
            continue;
        if(len + 1u >= cap)
        {
            set_error("std::jsonrpc: header line too long");
            return -1;
        }
        line[len++] = ch;
    }
    line[len] = '\0';
    return (int)len;
}

static int read_exact_fd(int fd, char* buf, size_t n, int64_t timeout_ms)
{
    size_t off = 0u;
    while(off < n)
    {
        if(timeout_ms >= 0)
        {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int pr = poll(&pfd, 1, (int)timeout_ms);
            if(pr == 0)
                return -2;
            if(pr < 0)
                return -1;
            if((pfd.revents & POLLIN) == 0)
                return -1;
        }

        ssize_t r = read(fd, buf + off, n - off);
        if(r == 0)
            return -1;
        if(r < 0)
            return -1;
        off += (size_t)r;
    }
    return 0;
}

static int write_all_fd(int fd, const char* buf, size_t n)
{
    size_t off = 0u;
    while(off < n)
    {
        ssize_t w = write(fd, buf + off, n - off);
        if(w < 0)
            return -1;
        if(w == 0)
            return -1;
        off += (size_t)w;
    }
    return 0;
}

static int64_t parse_content_length(const char* line)
{
    if(!starts_with_ci(line, "Content-Length:"))
        return -1;

    const char* p = line + 15;
    while(*p == ' ' || *p == '\t')
        ++p;
    if(!isdigit((unsigned char)*p))
        return -1;

    int64_t value = 0;
    while(isdigit((unsigned char)*p))
    {
        value = value * 10 + (int64_t)(*p - '0');
        ++p;
    }
    return value;
}

static int64_t read_frame_impl(char* buf, int64_t capacity, int64_t timeout_ms)
{
    if(!buf || capacity <= 1)
    {
        set_error("std::jsonrpc: invalid output buffer");
        return -1;
    }

    int fd = fileno(stdin);
    if(fd < 0)
    {
        set_error("std::jsonrpc: stdin is not readable");
        return -1;
    }

    int64_t content_length = -1;
    char line[1024];
    for(;;)
    {
        int n = read_line_fd(fd, line, sizeof(line), timeout_ms);
        if(n == -2)
            return -2;
        if(n < 0)
        {
            if(g_last_error[0] == '\0')
                set_error("std::jsonrpc: failed to read frame header");
            return -1;
        }
        if(n == 0)
            break;

        int64_t parsed = parse_content_length(line);
        if(parsed >= 0)
            content_length = parsed;
    }

    if(content_length < 0)
    {
        set_error("std::jsonrpc: missing Content-Length header");
        return -1;
    }

    if(content_length + 1 > capacity)
    {
        char drain[256];
        int64_t remaining = content_length;
        while(remaining > 0)
        {
            size_t chunk = (size_t)(remaining > (int64_t)sizeof(drain)
                                        ? (int64_t)sizeof(drain)
                                        : remaining);
            int rr = read_exact_fd(fd, drain, chunk, timeout_ms);
            if(rr != 0)
                break;
            remaining -= (int64_t)chunk;
        }
        set_error("std::jsonrpc: payload exceeds destination buffer");
        return -3;
    }

    if(read_exact_fd(fd, buf, (size_t)content_length, timeout_ms) != 0)
    {
        if(g_last_error[0] == '\0')
            set_error("std::jsonrpc: failed to read frame payload");
        return -1;
    }

    buf[content_length] = '\0';
    clear_error();
    return content_length;
}

char* __mlang_std_jsonrpc_last_error(void)
{
    return dup_cstr(g_last_error);
}

int64_t __mlang_std_jsonrpc_stdio_read_frame(char* buf, int64_t capacity)
{
    return read_frame_impl(buf, capacity, -1);
}

int64_t __mlang_std_jsonrpc_stdio_read_frame_timeout(char* buf, int64_t capacity,
                                                     int64_t timeout_ms)
{
    if(timeout_ms < 0)
        timeout_ms = 0;
    return read_frame_impl(buf, capacity, timeout_ms);
}

int __mlang_std_jsonrpc_stdio_write_frame(const char* json)
{
    if(!json)
    {
        set_error("std::jsonrpc: write_frame received null payload");
        return -1;
    }

    size_t body_len = strlen(json);
    char header[96];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n",
                        body_len);
    if(hlen <= 0 || (size_t)hlen >= sizeof(header))
    {
        set_error("std::jsonrpc: failed to build frame header");
        return -1;
    }

    if(pthread_mutex_lock(&g_write_mutex) != 0)
    {
        set_error("std::jsonrpc: failed to lock write mutex");
        return -1;
    }

    int fd = fileno(stdout);
    int ok = 0;
    if(fd < 0 ||
       write_all_fd(fd, header, (size_t)hlen) != 0 ||
       write_all_fd(fd, json, body_len) != 0)
    {
        ok = -1;
        set_errno_error("std::jsonrpc write_frame");
    }

    (void)pthread_mutex_unlock(&g_write_mutex);
    if(ok != 0)
        return -1;
    clear_error();
    return 0;
}

char* __mlang_std_jsonrpc_build_frame(const char* payload)
{
    if(!payload)
        return dup_cstr("");

    size_t body_len = strlen(payload);
    char header[96];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n",
                        body_len);
    if(hlen <= 0 || (size_t)hlen >= sizeof(header))
        return dup_cstr("");

    size_t total = (size_t)hlen + body_len;
    char* out = (char*)malloc(total + 1u);
    if(!out)
        return dup_cstr("");

    (void)memcpy(out, header, (size_t)hlen);
    if(body_len > 0)
        (void)memcpy(out + (size_t)hlen, payload, body_len);
    out[total] = '\0';
    return out;
}

int64_t __mlang_std_jsonrpc_parse_frame(const char* frame, char* out,
                                        int64_t capacity)
{
    if(!frame || !out || capacity <= 1)
    {
        set_error("std::jsonrpc: invalid parse_frame buffers");
        return -1;
    }

    const char* header_end = strstr(frame, "\r\n\r\n");
    size_t delim_len = 4u;
    if(!header_end)
    {
        header_end = strstr(frame, "\n\n");
        delim_len = 2u;
    }
    if(!header_end)
    {
        set_error("std::jsonrpc: invalid frame header delimiter");
        return -1;
    }

    int64_t content_length = -1;
    const char* p = frame;
    while(p < header_end)
    {
        const char* line_end = strstr(p, "\n");
        if(!line_end || line_end > header_end)
            line_end = header_end;
        size_t line_len = (size_t)(line_end - p);
        char line[256];
        size_t copy_len = line_len < (sizeof(line) - 1u) ? line_len : (sizeof(line) - 1u);
        if(copy_len > 0 && p[copy_len - 1u] == '\r')
            --copy_len;
        (void)memcpy(line, p, copy_len);
        line[copy_len] = '\0';
        int64_t parsed = parse_content_length(line);
        if(parsed >= 0)
            content_length = parsed;
        if(line_end == header_end)
            break;
        p = line_end + 1;
    }

    if(content_length < 0)
    {
        set_error("std::jsonrpc: missing Content-Length header");
        return -1;
    }

    const char* body = header_end + delim_len;
    size_t actual = strlen(body);
    if((int64_t)actual < content_length)
    {
        set_error("std::jsonrpc: truncated frame payload");
        return -1;
    }
    if(content_length + 1 > capacity)
    {
        set_error("std::jsonrpc: parse_frame destination buffer too small");
        return -1;
    }
    if(content_length > 0)
        (void)memcpy(out, body, (size_t)content_length);
    out[content_length] = '\0';
    clear_error();
    return content_length;
}

static int ensure_cancel_capacity(size_t need)
{
    if(g_cancel_registry.cap >= need)
        return 0;
    size_t next = g_cancel_registry.cap == 0u ? 16u : g_cancel_registry.cap;
    while(next < need)
        next *= 2u;
    int64_t* mem = (int64_t*)realloc(g_cancel_registry.ids, next * sizeof(int64_t));
    if(!mem)
        return -1;
    g_cancel_registry.ids = mem;
    g_cancel_registry.cap = next;
    return 0;
}

static int ensure_uri_store_capacity(uri_store_t* st, size_t need)
{
    if(!st)
        return -1;
    if(st->cap >= need)
        return 0;
    size_t next = st->cap == 0u ? 8u : st->cap;
    while(next < need)
        next *= 2u;
    char** mem = (char**)realloc(st->uris, next * sizeof(char*));
    if(!mem)
        return -1;
    st->uris = mem;
    st->cap = next;
    return 0;
}

int __mlang_std_jsonrpc_cancel_mark(int64_t request_id)
{
    if(pthread_mutex_lock(&g_cancel_mutex) != 0)
    {
        set_error("std::jsonrpc: cancel_mark lock failed");
        return -1;
    }

    for(size_t i = 0; i < g_cancel_registry.len; ++i)
    {
        if(g_cancel_registry.ids[i] == request_id)
        {
            (void)pthread_mutex_unlock(&g_cancel_mutex);
            clear_error();
            return 0;
        }
    }

    if(ensure_cancel_capacity(g_cancel_registry.len + 1u) != 0)
    {
        (void)pthread_mutex_unlock(&g_cancel_mutex);
        set_error("std::jsonrpc: cancel registry out of memory");
        return -1;
    }
    g_cancel_registry.ids[g_cancel_registry.len++] = request_id;
    (void)pthread_mutex_unlock(&g_cancel_mutex);
    clear_error();
    return 0;
}

int __mlang_std_jsonrpc_cancel_is_marked(int64_t request_id)
{
    if(pthread_mutex_lock(&g_cancel_mutex) != 0)
    {
        set_error("std::jsonrpc: cancel_is_marked lock failed");
        return 0;
    }
    for(size_t i = 0; i < g_cancel_registry.len; ++i)
    {
        if(g_cancel_registry.ids[i] == request_id)
        {
            (void)pthread_mutex_unlock(&g_cancel_mutex);
            clear_error();
            return 1;
        }
    }
    (void)pthread_mutex_unlock(&g_cancel_mutex);
    clear_error();
    return 0;
}

int __mlang_std_jsonrpc_cancel_take(int64_t request_id)
{
    if(pthread_mutex_lock(&g_cancel_mutex) != 0)
    {
        set_error("std::jsonrpc: cancel_take lock failed");
        return 0;
    }
    for(size_t i = 0; i < g_cancel_registry.len; ++i)
    {
        if(g_cancel_registry.ids[i] == request_id)
        {
            size_t remain = g_cancel_registry.len - i - 1u;
            if(remain > 0u)
            {
                (void)memmove(&g_cancel_registry.ids[i], &g_cancel_registry.ids[i + 1u],
                              remain * sizeof(int64_t));
            }
            g_cancel_registry.len--;
            (void)pthread_mutex_unlock(&g_cancel_mutex);
            clear_error();
            return 1;
        }
    }
    (void)pthread_mutex_unlock(&g_cancel_mutex);
    clear_error();
    return 0;
}

int __mlang_std_jsonrpc_cancel_clear(int64_t request_id)
{
    return __mlang_std_jsonrpc_cancel_take(request_id);
}

int __mlang_std_jsonrpc_cancel_clear_all(void)
{
    if(pthread_mutex_lock(&g_cancel_mutex) != 0)
    {
        set_error("std::jsonrpc: cancel_clear_all lock failed");
        return -1;
    }
    g_cancel_registry.len = 0u;
    (void)pthread_mutex_unlock(&g_cancel_mutex);
    clear_error();
    return 0;
}

static const char* skip_ws(const char* p)
{
    while(p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        ++p;
    return p;
}

static const char* find_key_after(const char* start, const char* key)
{
    if(!start || !key)
        return NULL;
    char pat[128];
    int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if(n <= 0 || (size_t)n >= sizeof(pat))
        return NULL;
    return strstr(start, pat);
}

static char* json_decode_string(const char* q1, const char** out_next)
{
    if(!q1 || *q1 != '"')
        return NULL;
    const char* p = q1 + 1;
    size_t cap = strlen(q1) + 1u;
    char* out = (char*)malloc(cap);
    if(!out)
        return NULL;
    size_t w = 0u;
    while(*p && *p != '"')
    {
        if(*p != '\\')
        {
            out[w++] = *p++;
            continue;
        }
        ++p;
        if(!*p)
            break;
        switch(*p)
        {
            case '"': out[w++] = '"'; break;
            case '\\': out[w++] = '\\'; break;
            case '/': out[w++] = '/'; break;
            case 'b': out[w++] = '\b'; break;
            case 'f': out[w++] = '\f'; break;
            case 'n': out[w++] = '\n'; break;
            case 'r': out[w++] = '\r'; break;
            case 't': out[w++] = '\t'; break;
            case 'u':
                /* Minimal handling: keep unicode escape as '?' for now. */
                out[w++] = '?';
                if(p[1]) ++p;
                if(p[1]) ++p;
                if(p[1]) ++p;
                if(p[1]) ++p;
                break;
            default:
                out[w++] = *p;
                break;
        }
        ++p;
    }
    if(*p != '"')
    {
        free(out);
        return NULL;
    }
    out[w] = '\0';
    if(out_next)
        *out_next = p + 1;
    return out;
}

static char* extract_string_field_in(const char* obj_start, const char* key)
{
    const char* k = find_key_after(obj_start, key);
    if(!k)
        return NULL;
    const char* colon = strchr(k, ':');
    if(!colon)
        return NULL;
    const char* v = skip_ws(colon + 1);
    if(!v || *v != '"')
        return NULL;
    return json_decode_string(v, NULL);
}

static int64_t extract_int_field_in(const char* obj_start, const char* key,
                                    int64_t fallback)
{
    const char* k = find_key_after(obj_start, key);
    if(!k)
        return fallback;
    const char* colon = strchr(k, ':');
    if(!colon)
        return fallback;
    const char* v = skip_ws(colon + 1);
    if(!v)
        return fallback;
    int sign = 1;
    if(*v == '-')
    {
        sign = -1;
        ++v;
    }
    if(!isdigit((unsigned char)*v))
        return fallback;
    int64_t out = 0;
    while(isdigit((unsigned char)*v))
    {
        out = out * 10 + (int64_t)(*v - '0');
        ++v;
    }
    return out * (int64_t)sign;
}

int64_t __mlang_std_jsonrpc_extract_cancel_id(const char* payload)
{
    if(!payload)
        return -1;

    const char* method_key = strstr(payload, "\"method\"");
    if(!method_key)
        return -1;
    const char* colon = strchr(method_key, ':');
    if(!colon)
        return -1;
    const char* val = skip_ws(colon + 1);
    if(!val || *val != '"')
        return -1;
    ++val;
    const char* end = strchr(val, '"');
    if(!end)
        return -1;
    size_t method_len = (size_t)(end - val);
    const char* cancel_name = "$/cancelRequest";
    if(method_len != strlen(cancel_name) ||
       strncmp(val, cancel_name, method_len) != 0)
    {
        return -1;
    }

    const char* params_key = strstr(end, "\"params\"");
    if(!params_key)
        return -2;
    const char* id_key = strstr(params_key, "\"id\"");
    if(!id_key)
        return -2;
    const char* id_colon = strchr(id_key, ':');
    if(!id_colon)
        return -2;
    const char* id_val = skip_ws(id_colon + 1);
    if(!id_val)
        return -2;
    int in_quotes = 0;
    if(*id_val == '"')
    {
        in_quotes = 1;
        ++id_val;
    }
    int sign = 1;
    if(*id_val == '-')
    {
        sign = -1;
        ++id_val;
    }
    if(!isdigit((unsigned char)*id_val))
        return -2;

    int64_t out = 0;
    while(isdigit((unsigned char)*id_val))
    {
        out = out * 10 + (int64_t)(*id_val - '0');
        ++id_val;
    }
    if(in_quotes && *id_val != '"')
        return -2;
    return out * (int64_t)sign;
}

int64_t __mlang_std_jsonrpc_extract_request_id(const char* payload)
{
    if(!payload)
        return -1;

    const char* id_key = strstr(payload, "\"id\"");
    if(!id_key)
        return -1;
    const char* id_colon = strchr(id_key, ':');
    if(!id_colon)
        return -1;
    const char* id_val = skip_ws(id_colon + 1);
    if(!id_val)
        return -1;

    int in_quotes = 0;
    if(*id_val == '"')
    {
        in_quotes = 1;
        ++id_val;
    }

    int sign = 1;
    if(*id_val == '-')
    {
        sign = -1;
        ++id_val;
    }
    if(!isdigit((unsigned char)*id_val))
        return -1;

    int64_t out = 0;
    while(isdigit((unsigned char)*id_val))
    {
        out = out * 10 + (int64_t)(*id_val - '0');
        ++id_val;
    }
    if(in_quotes && *id_val != '"')
        return -1;
    return out * (int64_t)sign;
}

char* __mlang_std_jsonrpc_extract_method(const char* payload)
{
    if(!payload)
        return dup_cstr("");
    char* s = extract_string_field_in(payload, "method");
    if(!s)
        return dup_cstr("");
    return s;
}

char* __mlang_std_jsonrpc_extract_text_document_uri(const char* payload)
{
    if(!payload)
        return dup_cstr("");
    const char* params = find_key_after(payload, "params");
    if(!params)
        return dup_cstr("");
    const char* td = find_key_after(params, "textDocument");
    if(!td)
        return dup_cstr("");
    char* s = extract_string_field_in(td, "uri");
    if(!s)
        return dup_cstr("");
    return s;
}

int64_t __mlang_std_jsonrpc_extract_position_line(const char* payload)
{
    if(!payload)
        return -1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return -1;
    const char* pos = find_key_after(params, "position");
    if(!pos)
        return -1;
    return extract_int_field_in(pos, "line", -1);
}

int64_t __mlang_std_jsonrpc_extract_position_character(const char* payload)
{
    if(!payload)
        return -1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return -1;
    const char* pos = find_key_after(params, "position");
    if(!pos)
        return -1;
    return extract_int_field_in(pos, "character", -1);
}

int64_t __mlang_std_jsonrpc_extract_text_document_version(const char* payload)
{
    if(!payload)
        return -1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return -1;
    const char* td = find_key_after(params, "textDocument");
    if(!td)
        return -1;
    return extract_int_field_in(td, "version", -1);
}

char* __mlang_std_jsonrpc_extract_text_document_language_id(const char* payload)
{
    if(!payload)
        return dup_cstr("");
    const char* params = find_key_after(payload, "params");
    if(!params)
        return dup_cstr("");
    const char* td = find_key_after(params, "textDocument");
    if(!td)
        return dup_cstr("");
    char* s = extract_string_field_in(td, "languageId");
    if(!s)
        return dup_cstr("");
    return s;
}

char* __mlang_std_jsonrpc_extract_text_document_text(const char* payload)
{
    if(!payload)
        return dup_cstr("");
    const char* params = find_key_after(payload, "params");
    if(!params)
        return dup_cstr("");
    const char* td = find_key_after(params, "textDocument");
    if(!td)
        return dup_cstr("");
    char* s = extract_string_field_in(td, "text");
    if(!s)
        return dup_cstr("");
    return s;
}

char* __mlang_std_jsonrpc_extract_first_change_text(const char* payload)
{
    if(!payload)
        return dup_cstr("");
    const char* params = find_key_after(payload, "params");
    if(!params)
        return dup_cstr("");
    const char* changes = find_key_after(params, "contentChanges");
    if(!changes)
        return dup_cstr("");
    const char* text_key = find_key_after(changes, "text");
    if(!text_key)
        return dup_cstr("");
    const char* colon = strchr(text_key, ':');
    if(!colon)
        return dup_cstr("");
    const char* v = skip_ws(colon + 1);
    if(!v || *v != '"')
        return dup_cstr("");
    char* s = json_decode_string(v, NULL);
    if(!s)
        return dup_cstr("");
    return s;
}

char* __mlang_std_jsonrpc_extract_new_name(const char* payload)
{
    if(!payload)
        return dup_cstr("");
    const char* params = find_key_after(payload, "params");
    if(!params)
        return dup_cstr("");
    char* s = extract_string_field_in(params, "newName");
    if(!s)
        return dup_cstr("");
    return s;
}

char* __mlang_std_jsonrpc_extract_workspace_query(const char* payload)
{
    if(!payload)
        return dup_cstr("");
    const char* params = find_key_after(payload, "params");
    if(!params)
        return dup_cstr("");
    char* s = extract_string_field_in(params, "query");
    if(!s)
        return dup_cstr("");
    return s;
}

int64_t __mlang_std_jsonrpc_uri_store_new(void)
{
    uri_store_t* st = (uri_store_t*)malloc(sizeof(uri_store_t));
    if(!st)
        return 0;
    st->uris = NULL;
    st->len = 0u;
    st->cap = 0u;
    if(pthread_mutex_init(&st->mutex, NULL) != 0)
    {
        free(st);
        return 0;
    }
    return (int64_t)(intptr_t)st;
}

int __mlang_std_jsonrpc_uri_store_free(int64_t handle)
{
    uri_store_t* st = (uri_store_t*)(intptr_t)handle;
    if(!st)
        return -1;
    if(pthread_mutex_lock(&st->mutex) != 0)
        return -1;
    for(size_t i = 0; i < st->len; ++i)
        free(st->uris[i]);
    free(st->uris);
    st->uris = NULL;
    st->len = 0u;
    st->cap = 0u;
    (void)pthread_mutex_unlock(&st->mutex);
    (void)pthread_mutex_destroy(&st->mutex);
    free(st);
    return 0;
}

int __mlang_std_jsonrpc_uri_store_add(int64_t handle, const char* uri)
{
    uri_store_t* st = (uri_store_t*)(intptr_t)handle;
    if(!st || !uri)
        return -1;
    if(pthread_mutex_lock(&st->mutex) != 0)
        return -1;
    for(size_t i = 0; i < st->len; ++i)
    {
        if(strcmp(st->uris[i], uri) == 0)
        {
            (void)pthread_mutex_unlock(&st->mutex);
            return 0;
        }
    }
    if(ensure_uri_store_capacity(st, st->len + 1u) != 0)
    {
        (void)pthread_mutex_unlock(&st->mutex);
        return -1;
    }
    char* copy = dup_cstr(uri);
    if(!copy)
    {
        (void)pthread_mutex_unlock(&st->mutex);
        return -1;
    }
    st->uris[st->len++] = copy;
    (void)pthread_mutex_unlock(&st->mutex);
    return 0;
}

int __mlang_std_jsonrpc_uri_store_remove(int64_t handle, const char* uri)
{
    uri_store_t* st = (uri_store_t*)(intptr_t)handle;
    if(!st || !uri)
        return -1;
    if(pthread_mutex_lock(&st->mutex) != 0)
        return -1;
    for(size_t i = 0; i < st->len; ++i)
    {
        if(strcmp(st->uris[i], uri) == 0)
        {
            free(st->uris[i]);
            size_t remain = st->len - i - 1u;
            if(remain > 0u)
            {
                (void)memmove(&st->uris[i], &st->uris[i + 1u],
                              remain * sizeof(char*));
            }
            st->len--;
            (void)pthread_mutex_unlock(&st->mutex);
            return 0;
        }
    }
    (void)pthread_mutex_unlock(&st->mutex);
    return 0;
}

int64_t __mlang_std_jsonrpc_uri_store_count(int64_t handle)
{
    uri_store_t* st = (uri_store_t*)(intptr_t)handle;
    if(!st)
        return -1;
    if(pthread_mutex_lock(&st->mutex) != 0)
        return -1;
    int64_t out = (int64_t)st->len;
    (void)pthread_mutex_unlock(&st->mutex);
    return out;
}

char* __mlang_std_jsonrpc_uri_store_get(int64_t handle, int64_t index)
{
    uri_store_t* st = (uri_store_t*)(intptr_t)handle;
    if(!st || index < 0)
        return dup_cstr("");
    if(pthread_mutex_lock(&st->mutex) != 0)
        return dup_cstr("");
    size_t i = (size_t)index;
    if(i >= st->len)
    {
        (void)pthread_mutex_unlock(&st->mutex);
        return dup_cstr("");
    }
    char* out = dup_cstr(st->uris[i]);
    (void)pthread_mutex_unlock(&st->mutex);
    if(!out)
        return dup_cstr("");
    return out;
}

__attribute__((weak)) char*
__mlang_std_jsonrpc_runtime_dispatch(const char* request_payload)
{
    (void)request_payload;
    return dup_cstr("");
}
