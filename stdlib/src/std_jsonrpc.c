#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

typedef struct
{
    char* key;
    char* value;
} text_entry_t;

typedef struct
{
    pthread_mutex_t mutex;
    text_entry_t* entries;
    size_t len;
    size_t cap;
} text_store_t;

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

static int is_ident_start_char(char c);
static int is_ident_char(char c);
static void line_bounds_at(const char* text, int64_t line0,
                           const char** out_start, const char** out_end);

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

static int ensure_text_store_capacity(text_store_t* st, size_t need)
{
    if(!st)
        return -1;
    if(st->cap >= need)
        return 0;
    size_t next = st->cap == 0u ? 8u : st->cap;
    while(next < need)
        next *= 2u;
    text_entry_t* mem =
        (text_entry_t*)realloc(st->entries, next * sizeof(text_entry_t));
    if(!mem)
        return -1;
    st->entries = mem;
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

static int extract_bool_field_in(const char* obj_start, const char* key,
                                 int fallback)
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
    if(strncmp(v, "true", 4u) == 0)
        return 1;
    if(strncmp(v, "false", 5u) == 0)
        return 0;
    return fallback;
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

int __mlang_std_jsonrpc_extract_include_declaration(const char* payload)
{
    if(!payload)
        return 1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return 1;
    const char* context = find_key_after(params, "context");
    if(!context)
        return 1;
    return extract_bool_field_in(context, "includeDeclaration", 1);
}

int64_t __mlang_std_jsonrpc_extract_range_start_line(const char* payload)
{
    if(!payload)
        return -1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return -1;
    const char* range = find_key_after(params, "range");
    if(!range)
        return -1;
    const char* start = find_key_after(range, "start");
    if(!start)
        return -1;
    return extract_int_field_in(start, "line", -1);
}

int64_t __mlang_std_jsonrpc_extract_range_start_character(const char* payload)
{
    if(!payload)
        return -1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return -1;
    const char* range = find_key_after(params, "range");
    if(!range)
        return -1;
    const char* start = find_key_after(range, "start");
    if(!start)
        return -1;
    return extract_int_field_in(start, "character", -1);
}

int64_t __mlang_std_jsonrpc_extract_range_end_line(const char* payload)
{
    if(!payload)
        return -1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return -1;
    const char* range = find_key_after(params, "range");
    if(!range)
        return -1;
    const char* end = find_key_after(range, "end");
    if(!end)
        return -1;
    return extract_int_field_in(end, "line", -1);
}

int64_t __mlang_std_jsonrpc_extract_range_end_character(const char* payload)
{
    if(!payload)
        return -1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return -1;
    const char* range = find_key_after(params, "range");
    if(!range)
        return -1;
    const char* end = find_key_after(range, "end");
    if(!end)
        return -1;
    return extract_int_field_in(end, "character", -1);
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

int64_t __mlang_std_jsonrpc_extract_format_tab_size(const char* payload)
{
    if(!payload)
        return 4;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return 4;
    const char* opts = find_key_after(params, "options");
    if(!opts)
        return 4;
    int64_t tab = extract_int_field_in(opts, "tabSize", 4);
    if(tab <= 0)
        tab = 4;
    if(tab > 16)
        tab = 16;
    return tab;
}

int __mlang_std_jsonrpc_extract_format_insert_spaces(const char* payload)
{
    if(!payload)
        return 1;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return 1;
    const char* opts = find_key_after(params, "options");
    if(!opts)
        return 1;
    return extract_bool_field_in(opts, "insertSpaces", 1);
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

char* __mlang_std_jsonrpc_extract_root_uri(const char* payload)
{
    if(!payload)
        return dup_cstr("");
    char* s = extract_string_field_in(payload, "rootUri");
    if(!s)
        return dup_cstr("");
    return s;
}

static const char* find_first_diagnostic_object(const char* payload)
{
    if(!payload)
        return NULL;
    const char* params = find_key_after(payload, "params");
    if(!params)
        return NULL;
    const char* context = find_key_after(params, "context");
    if(!context)
        return NULL;
    const char* diagnostics = find_key_after(context, "diagnostics");
    if(!diagnostics)
        return NULL;
    const char* colon = strchr(diagnostics, ':');
    if(!colon)
        return NULL;
    const char* p = skip_ws(colon + 1);
    if(!p || *p != '[')
        return NULL;
    ++p;
    p = skip_ws(p);
    if(!p || *p != '{')
        return NULL;
    return p;
}

char* __mlang_std_jsonrpc_extract_first_diagnostic_message(const char* payload)
{
    const char* d = find_first_diagnostic_object(payload);
    if(!d)
        return dup_cstr("");
    char* s = extract_string_field_in(d, "message");
    if(!s)
        return dup_cstr("");
    return s;
}

int64_t __mlang_std_jsonrpc_extract_first_diagnostic_start_line(const char* payload)
{
    const char* d = find_first_diagnostic_object(payload);
    if(!d)
        return -1;
    const char* range = find_key_after(d, "range");
    if(!range)
        return -1;
    const char* start = find_key_after(range, "start");
    if(!start)
        return -1;
    return extract_int_field_in(start, "line", -1);
}

int64_t __mlang_std_jsonrpc_extract_first_diagnostic_start_character(const char* payload)
{
    const char* d = find_first_diagnostic_object(payload);
    if(!d)
        return -1;
    const char* range = find_key_after(d, "range");
    if(!range)
        return -1;
    const char* start = find_key_after(range, "start");
    if(!start)
        return -1;
    return extract_int_field_in(start, "character", -1);
}

int64_t __mlang_std_jsonrpc_extract_first_diagnostic_end_line(const char* payload)
{
    const char* d = find_first_diagnostic_object(payload);
    if(!d)
        return -1;
    const char* range = find_key_after(d, "range");
    if(!range)
        return -1;
    const char* end = find_key_after(range, "end");
    if(!end)
        return -1;
    return extract_int_field_in(end, "line", -1);
}

int64_t __mlang_std_jsonrpc_extract_first_diagnostic_end_character(const char* payload)
{
    const char* d = find_first_diagnostic_object(payload);
    if(!d)
        return -1;
    const char* range = find_key_after(d, "range");
    if(!range)
        return -1;
    const char* end = find_key_after(range, "end");
    if(!end)
        return -1;
    return extract_int_field_in(end, "character", -1);
}

int __mlang_std_jsonrpc_should_insert_semicolon(const char* text, int64_t line0,
                                                int64_t col0)
{
    if(!text || line0 < 0 || col0 < 0)
        return 0;
    const char* ls = NULL;
    const char* le = NULL;
    line_bounds_at(text, line0, &ls, &le);
    if(!ls || !le || le < ls)
        return 0;

    int64_t n = (int64_t)(le - ls);
    if(col0 > n)
        col0 = n;
    if(col0 >= 0 && col0 < n && ls[col0] == ';')
        return 0;
    if(col0 > 0 && ls[col0 - 1] == ';')
        return 0;

    int64_t i = n - 1;
    while(i >= 0 && (ls[i] == ' ' || ls[i] == '\t' || ls[i] == '\r'))
        --i;
    if(i < 0)
        return 0;
    if(ls[i] == ';' || ls[i] == '{' || ls[i] == '}' || ls[i] == ':')
        return 0;
    return 1;
}

char* __mlang_std_jsonrpc_signature_help_callee(const char* text,
                                                int64_t line0,
                                                int64_t column0)
{
    if(!text || line0 < 0)
        return dup_cstr("");
    const char* ls = NULL;
    const char* le = NULL;
    line_bounds_at(text, line0, &ls, &le);
    if(!ls || !le || le <= ls)
        return dup_cstr("");
    int64_t n = (int64_t)(le - ls);
    int64_t pos = column0;
    if(pos < 0)
        pos = 0;
    if(pos > n)
        pos = n;
    int depth = 0;
    for(int64_t i = pos - 1; i >= 0; --i)
    {
        char ch = ls[i];
        if(ch == ')')
        {
            depth++;
            continue;
        }
        if(ch == '(')
        {
            if(depth > 0)
            {
                depth--;
                continue;
            }
            int64_t j = i - 1;
            while(j >= 0 && isspace((unsigned char)ls[j]))
                --j;
            if(j < 0 || !is_ident_char(ls[j]))
                return dup_cstr("");
            int64_t end = j + 1;
            while(j >= 0 && is_ident_char(ls[j]))
                --j;
            int64_t start = j + 1;
            if(start >= end || !is_ident_start_char(ls[start]))
                return dup_cstr("");
            size_t len = (size_t)(end - start);
            char* out = (char*)malloc(len + 1u);
            if(!out)
                return dup_cstr("");
            memcpy(out, ls + start, len);
            out[len] = '\0';
            return out;
        }
    }
    return dup_cstr("");
}

typedef struct
{
    char* name;
    int line1;
    int col1;
    int kind;
    char path[PATH_MAX];
} ws_symbol_item_t;

typedef struct
{
    ws_symbol_item_t* items;
    size_t len;
    size_t cap;
} ws_symbol_list_t;

static void uri_to_path(const char* uri, char* out, size_t out_cap)
{
    if(!out || out_cap == 0u)
        return;
    out[0] = '\0';
    if(!uri)
        return;
    if(strncmp(uri, "file://", 7u) == 0)
    {
        snprintf(out, out_cap, "%s", uri + 7);
        return;
    }
    snprintf(out, out_cap, "%s", uri);
}

static int ends_with_mla(const char* path)
{
    if(!path)
        return 0;
    size_t n = strlen(path);
    return n >= 4u && strcmp(path + n - 4u, ".mla") == 0;
}

static int parse_decl_line(const char* line, char* out_name,
                           size_t out_name_cap, int* out_kind, int* out_col0)
{
    if(!line || !out_name || out_name_cap == 0u)
        return 0;
    out_name[0] = '\0';
    const struct
    {
        const char* kw;
        int kind;
        int paren_required;
    } rules[] = {{"fn", 12, 1}, {"struct", 23, 0}, {"enum", 10, 0},
                 {"mod", 2, 0}};
    for(size_t r = 0; r < sizeof(rules) / sizeof(rules[0]); ++r)
    {
        const char* p = line;
        while((p = strstr(p, rules[r].kw)) != NULL)
        {
            if(p > line && is_ident_char(*(p - 1)))
            {
                ++p;
                continue;
            }
            const char* k_end = p + strlen(rules[r].kw);
            if(*k_end && !isspace((unsigned char)*k_end))
            {
                ++p;
                continue;
            }
            while(*k_end && isspace((unsigned char)*k_end))
                ++k_end;
            if(!is_ident_start_char(*k_end))
            {
                ++p;
                continue;
            }
            const char* name_start = k_end;
            while(*k_end && is_ident_char(*k_end))
                ++k_end;
            size_t name_len = (size_t)(k_end - name_start);
            if(name_len == 0u || name_len + 1u > out_name_cap)
            {
                ++p;
                continue;
            }
            memcpy(out_name, name_start, name_len);
            out_name[name_len] = '\0';
            const char* after = k_end;
            while(*after && isspace((unsigned char)*after))
                ++after;
            if(rules[r].paren_required && *after != '(')
            {
                ++p;
                continue;
            }
            if(out_kind)
                *out_kind = rules[r].kind;
            if(out_col0)
                *out_col0 = (int)(name_start - line);
            return 1;
        }
    }
    return 0;
}

static int ensure_ws_symbol_capacity(ws_symbol_list_t* out, size_t need)
{
    if(!out)
        return -1;
    if(out->cap >= need)
        return 0;
    size_t next = out->cap == 0u ? 16u : out->cap;
    while(next < need)
        next *= 2u;
    ws_symbol_item_t* mem =
        (ws_symbol_item_t*)realloc(out->items, next * sizeof(ws_symbol_item_t));
    if(!mem)
        return -1;
    out->items = mem;
    out->cap = next;
    return 0;
}

static void push_ws_symbol(ws_symbol_list_t* out, const char* path,
                           const char* name, int line1, int col1, int kind)
{
    if(!out || !path || !name || name[0] == '\0')
        return;
    if(ensure_ws_symbol_capacity(out, out->len + 1u) != 0)
        return;
    ws_symbol_item_t* it = &out->items[out->len];
    it->name = dup_cstr(name);
    if(!it->name)
        return;
    it->line1 = line1;
    it->col1 = col1;
    it->kind = kind;
    snprintf(it->path, sizeof(it->path), "%s", path);
    out->len++;
}

static void free_ws_symbol_list(ws_symbol_list_t* list)
{
    if(!list)
        return;
    for(size_t i = 0; i < list->len; ++i)
        free(list->items[i].name);
    free(list->items);
    list->items = NULL;
    list->len = 0u;
    list->cap = 0u;
}

static int ws_symbol_item_cmp(const void* a, const void* b)
{
    const ws_symbol_item_t* ia = (const ws_symbol_item_t*)a;
    const ws_symbol_item_t* ib = (const ws_symbol_item_t*)b;
    int c = strcmp(ia->name ? ia->name : "", ib->name ? ib->name : "");
    if(c != 0)
        return c;
    c = strcmp(ia->path, ib->path);
    if(c != 0)
        return c;
    if(ia->line1 != ib->line1)
        return ia->line1 - ib->line1;
    return ia->col1 - ib->col1;
}

static char* json_escape_cstr(const char* s)
{
    if(!s)
        return dup_cstr("");
    size_t n = strlen(s);
    size_t cap = n * 2u + 16u;
    char* out = (char*)malloc(cap);
    if(!out)
        return dup_cstr("");
    size_t w = 0u;
    for(size_t i = 0; i < n; ++i)
    {
        char ch = s[i];
        if(ch == '"' || ch == '\\')
        {
            if(w + 2u >= cap)
                break;
            out[w++] = '\\';
            out[w++] = ch;
        }
        else if(ch == '\n')
        {
            if(w + 2u >= cap)
                break;
            out[w++] = '\\';
            out[w++] = 'n';
        }
        else if(ch == '\r')
        {
            if(w + 2u >= cap)
                break;
            out[w++] = '\\';
            out[w++] = 'r';
        }
        else
        {
            if(w + 1u >= cap)
                break;
            out[w++] = ch;
        }
    }
    out[w] = '\0';
    return out;
}

static void search_file_for_symbol(const char* path, const char* query,
                                   ws_symbol_list_t* out)
{
    if(!path || !query || !out)
        return;
    FILE* f = fopen(path, "rb");
    if(!f)
        return;
    char line[4096];
    int line1 = 1;
    while(fgets(line, (int)sizeof(line), f) != NULL)
    {
        char name[256];
        int kind = 0;
        int col0 = 0;
        if(parse_decl_line(line, name, sizeof(name), &kind, &col0))
        {
            if(strstr(name, query) != NULL)
                push_ws_symbol(out, path, name, line1, col0 + 1, kind);
        }
        line1++;
    }
    fclose(f);
}

static void search_dir_for_symbol(const char* root, const char* query,
                                  ws_symbol_list_t* out)
{
    if(!root || !query || !out)
        return;
    DIR* dir = opendir(root);
    if(!dir)
        return;
    struct dirent* ent = NULL;
    while((ent = readdir(dir)) != NULL)
    {
        if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char full[PATH_MAX];
        if(snprintf(full, sizeof(full), "%s/%s", root, ent->d_name) >=
           (int)sizeof(full))
            continue;
        struct stat st;
        if(stat(full, &st) != 0)
            continue;
        if(S_ISDIR(st.st_mode))
        {
            if(ent->d_name[0] == '.')
                continue;
            search_dir_for_symbol(full, query, out);
        }
        else if(S_ISREG(st.st_mode))
        {
            if(ends_with_mla(full))
                search_file_for_symbol(full, query, out);
        }
    }
    closedir(dir);
}

char* __mlang_std_jsonrpc_workspace_symbol_search(const char* root_uri,
                                                  const char* query)
{
    if(!root_uri || !query || query[0] == '\0')
        return dup_cstr("[]");
    char root[PATH_MAX];
    uri_to_path(root_uri, root, sizeof(root));
    if(root[0] == '\0')
        return dup_cstr("[]");
    ws_symbol_list_t matches;
    memset(&matches, 0, sizeof(matches));
    search_dir_for_symbol(root, query, &matches);
    if(matches.len == 0u)
    {
        free_ws_symbol_list(&matches);
        return dup_cstr("[]");
    }
    qsort(matches.items, matches.len, sizeof(ws_symbol_item_t), ws_symbol_item_cmp);
    size_t cap = matches.len * (PATH_MAX + 256u) + 32u;
    char* out = (char*)malloc(cap);
    if(!out)
    {
        free_ws_symbol_list(&matches);
        return dup_cstr("[]");
    }
    size_t w = 0u;
    out[w++] = '[';
    for(size_t i = 0; i < matches.len; ++i)
    {
        ws_symbol_item_t* m = &matches.items[i];
        char uri[PATH_MAX + 8];
        snprintf(uri, sizeof(uri), "file://%s", m->path);
        char* name_e = json_escape_cstr(m->name);
        char* uri_e = json_escape_cstr(uri);
        int wrote = snprintf(
            out + w, cap - w,
            "%s{\"name\":\"%s\",\"kind\":%d,\"location\":{\"uri\":\"%s\","
            "\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
            "\"end\":{\"line\":%d,\"character\":%d}}}}",
            (i == 0u ? "" : ","), name_e ? name_e : "", m->kind,
            uri_e ? uri_e : "", m->line1 - 1, m->col1 - 1, m->line1 - 1,
            m->col1 - 1);
        free(name_e);
        free(uri_e);
        if(wrote <= 0 || (size_t)wrote >= cap - w)
            break;
        w += (size_t)wrote;
    }
    if(w + 2u <= cap)
    {
        out[w++] = ']';
        out[w] = '\0';
    }
    else
    {
        out[cap - 1u] = '\0';
    }
    free_ws_symbol_list(&matches);
    return out;
}

int64_t __mlang_std_jsonrpc_text_store_new(void)
{
    text_store_t* st = (text_store_t*)malloc(sizeof(text_store_t));
    if(!st)
        return 0;
    st->entries = NULL;
    st->len = 0u;
    st->cap = 0u;
    if(pthread_mutex_init(&st->mutex, NULL) != 0)
    {
        free(st);
        return 0;
    }
    return (int64_t)(intptr_t)st;
}

int __mlang_std_jsonrpc_text_store_free(int64_t handle)
{
    text_store_t* st = (text_store_t*)(intptr_t)handle;
    if(!st)
        return -1;
    if(pthread_mutex_lock(&st->mutex) != 0)
        return -1;
    for(size_t i = 0; i < st->len; ++i)
    {
        free(st->entries[i].key);
        free(st->entries[i].value);
    }
    free(st->entries);
    st->entries = NULL;
    st->len = 0u;
    st->cap = 0u;
    (void)pthread_mutex_unlock(&st->mutex);
    (void)pthread_mutex_destroy(&st->mutex);
    free(st);
    return 0;
}

int __mlang_std_jsonrpc_text_store_upsert(int64_t handle, const char* key,
                                          const char* value)
{
    text_store_t* st = (text_store_t*)(intptr_t)handle;
    if(!st || !key || !value)
        return -1;
    if(pthread_mutex_lock(&st->mutex) != 0)
        return -1;
    for(size_t i = 0; i < st->len; ++i)
    {
        if(strcmp(st->entries[i].key, key) == 0)
        {
            char* nv = dup_cstr(value);
            if(!nv)
            {
                (void)pthread_mutex_unlock(&st->mutex);
                return -1;
            }
            free(st->entries[i].value);
            st->entries[i].value = nv;
            (void)pthread_mutex_unlock(&st->mutex);
            return 0;
        }
    }
    if(ensure_text_store_capacity(st, st->len + 1u) != 0)
    {
        (void)pthread_mutex_unlock(&st->mutex);
        return -1;
    }
    char* nk = dup_cstr(key);
    char* nv = dup_cstr(value);
    if(!nk || !nv)
    {
        free(nk);
        free(nv);
        (void)pthread_mutex_unlock(&st->mutex);
        return -1;
    }
    st->entries[st->len].key = nk;
    st->entries[st->len].value = nv;
    st->len++;
    (void)pthread_mutex_unlock(&st->mutex);
    return 0;
}

int __mlang_std_jsonrpc_text_store_remove(int64_t handle, const char* key)
{
    text_store_t* st = (text_store_t*)(intptr_t)handle;
    if(!st || !key)
        return -1;
    if(pthread_mutex_lock(&st->mutex) != 0)
        return -1;
    for(size_t i = 0; i < st->len; ++i)
    {
        if(strcmp(st->entries[i].key, key) == 0)
        {
            free(st->entries[i].key);
            free(st->entries[i].value);
            size_t remain = st->len - i - 1u;
            if(remain > 0u)
            {
                (void)memmove(&st->entries[i], &st->entries[i + 1u],
                              remain * sizeof(text_entry_t));
            }
            st->len--;
            (void)pthread_mutex_unlock(&st->mutex);
            return 0;
        }
    }
    (void)pthread_mutex_unlock(&st->mutex);
    return 0;
}

char* __mlang_std_jsonrpc_text_store_get(int64_t handle, const char* key)
{
    text_store_t* st = (text_store_t*)(intptr_t)handle;
    if(!st || !key)
        return dup_cstr("");
    if(pthread_mutex_lock(&st->mutex) != 0)
        return dup_cstr("");
    for(size_t i = 0; i < st->len; ++i)
    {
        if(strcmp(st->entries[i].key, key) == 0)
        {
            char* out = dup_cstr(st->entries[i].value);
            (void)pthread_mutex_unlock(&st->mutex);
            if(!out)
                return dup_cstr("");
            return out;
        }
    }
    (void)pthread_mutex_unlock(&st->mutex);
    return dup_cstr("");
}

static int line_is_blank(const char* s, size_t n)
{
    for(size_t i = 0; i < n; ++i)
    {
        if(!(s[i] == ' ' || s[i] == '\t' || s[i] == '\r'))
            return 0;
    }
    return 1;
}

static int line_starts_with_use(const char* s, size_t n)
{
    size_t i = 0u;
    while(i < n && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if(i + 4u > n)
        return 0;
    return s[i] == 'u' && s[i + 1u] == 's' && s[i + 2u] == 'e' &&
           s[i + 3u] == ' ';
}

int64_t __mlang_std_jsonrpc_line_count(const char* text)
{
    if(!text || *text == '\0')
        return 1;
    int64_t lines = 1;
    const char* p = text;
    while(*p)
    {
        if(*p == '\n')
            ++lines;
        ++p;
    }
    return lines;
}

int64_t __mlang_std_jsonrpc_line_length(const char* text, int64_t line0)
{
    if(!text || line0 < 0)
        return 0;
    const char* ls = NULL;
    const char* le = NULL;
    line_bounds_at(text, line0, &ls, &le);
    if(!ls || !le || le < ls)
        return 0;
    return (int64_t)(le - ls);
}

char* __mlang_std_jsonrpc_extract_lines_text(const char* text, int64_t start_line,
                                             int64_t end_line)
{
    if(!text || start_line < 0 || end_line < start_line)
        return dup_cstr("");
    const char* ss = NULL;
    const char* se = NULL;
    const char* es = NULL;
    const char* ee = NULL;
    line_bounds_at(text, start_line, &ss, &se);
    line_bounds_at(text, end_line, &es, &ee);
    if(!ss || !se || !es || !ee || ee < es || es < ss)
        return dup_cstr("");
    const char* out_end = ee;
    if(*out_end == '\n')
        ++out_end;
    size_t n = (size_t)(out_end - ss);
    char* out = (char*)malloc(n + 1u);
    if(!out)
        return dup_cstr("");
    if(n > 0u)
        memcpy(out, ss, n);
    out[n] = '\0';
    return out;
}

char* __mlang_std_jsonrpc_extract_range_text(const char* text, int64_t start_line,
                                             int64_t start_char, int64_t end_line,
                                             int64_t end_char)
{
    if(!text || start_line < 0 || end_line < start_line || start_char < 0 ||
       end_char < 0)
    {
        return dup_cstr("");
    }
    const char* sls = NULL;
    const char* sle = NULL;
    const char* els = NULL;
    const char* ele = NULL;
    line_bounds_at(text, start_line, &sls, &sle);
    line_bounds_at(text, end_line, &els, &ele);
    if(!sls || !sle || !els || !ele || sle < sls || ele < els)
        return dup_cstr("");

    int64_t slen = (int64_t)(sle - sls);
    int64_t elen = (int64_t)(ele - els);
    if(start_char > slen)
        start_char = slen;
    if(end_char > elen)
        end_char = elen;
    if(start_line == end_line && end_char < start_char)
        return dup_cstr("");

    const char* from = sls + start_char;
    const char* to = els + end_char;
    if(to < from)
        return dup_cstr("");
    size_t n = (size_t)(to - from);
    char* out = (char*)malloc(n + 1u);
    if(!out)
        return dup_cstr("");
    if(n > 0u)
        memcpy(out, from, n);
    out[n] = '\0';
    return out;
}

char* __mlang_std_jsonrpc_format_text_basic(const char* text)
{
    if(!text)
        return dup_cstr("");
    size_t in_n = strlen(text);
    size_t cap = in_n * 4u + 8u;
    char* out = (char*)malloc(cap);
    if(!out)
        return dup_cstr("");

    size_t w = 0u;
    const char* p = text;
    while(*p)
    {
        const char* ls = p;
        while(*p && *p != '\n')
            ++p;
        const char* le = p;
        while(le > ls && (le[-1] == ' ' || le[-1] == '\t' || le[-1] == '\r'))
            --le;

        for(const char* q = ls; q < le; ++q)
        {
            if(*q == '\t')
            {
                if(w + 4u >= cap)
                    break;
                out[w++] = ' ';
                out[w++] = ' ';
                out[w++] = ' ';
                out[w++] = ' ';
            }
            else
            {
                if(w + 1u >= cap)
                    break;
                out[w++] = *q;
            }
        }
        if(*p == '\n')
        {
            if(w + 1u >= cap)
                break;
            out[w++] = '\n';
            ++p;
        }
    }

    out[w] = '\0';
    return out;
}

char* __mlang_std_jsonrpc_format_text_with_options(const char* text,
                                                   int64_t tab_size,
                                                   int insert_spaces)
{
    if(!text)
        return dup_cstr("");
    if(tab_size <= 0)
        tab_size = 4;
    if(tab_size > 16)
        tab_size = 16;

    size_t in_n = strlen(text);
    size_t cap = in_n * (size_t)(tab_size + 1) + 8u;
    char* out = (char*)malloc(cap);
    if(!out)
        return dup_cstr("");

    size_t w = 0u;
    const char* p = text;
    while(*p)
    {
        const char* ls = p;
        while(*p && *p != '\n')
            ++p;
        const char* le = p;
        while(le > ls && (le[-1] == ' ' || le[-1] == '\t' || le[-1] == '\r'))
            --le;

        for(const char* q = ls; q < le; ++q)
        {
            if(*q == '\t' && insert_spaces == 1)
            {
                if(w + (size_t)tab_size >= cap)
                    break;
                for(int64_t i = 0; i < tab_size; ++i)
                    out[w++] = ' ';
            }
            else
            {
                if(w + 1u >= cap)
                    break;
                out[w++] = *q;
            }
        }
        if(*p == '\n')
        {
            if(w + 1u >= cap)
                break;
            out[w++] = '\n';
            ++p;
        }
    }

    out[w] = '\0';
    return out;
}

static int cmp_cstr_ptr(const void* a, const void* b)
{
    const char* const* sa = (const char* const*)a;
    const char* const* sb = (const char* const*)b;
    return strcmp(*sa, *sb);
}

static int is_ident_start_char(char c)
{
    unsigned char uc = (unsigned char)c;
    return (isalpha(uc) != 0) || c == '_';
}

static int is_ident_char(char c)
{
    unsigned char uc = (unsigned char)c;
    return (isalnum(uc) != 0) || c == '_';
}

static void line_bounds_at(const char* text, int64_t line0,
                           const char** out_start, const char** out_end)
{
    const char* start = text ? text : "";
    const char* p = start;
    int64_t line = 0;
    while(*p && line < line0)
    {
        if(*p == '\n')
            line++;
        p++;
    }
    start = p;
    while(*p && *p != '\n')
        p++;
    if(out_start)
        *out_start = start;
    if(out_end)
        *out_end = p;
}

static int parse_struct_decl_line(const char* s, size_t n, char* out_name,
                                  size_t out_name_cap, char* out_base,
                                  size_t out_base_cap)
{
    size_t i = 0u;
    while(i < n && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if(i + 6u > n)
        return 0;
    if(strncmp(s + i, "struct", 6u) != 0)
        return 0;
    i += 6u;
    if(i < n && is_ident_char(s[i]))
        return 0;
    while(i < n && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if(i >= n || !is_ident_start_char(s[i]))
        return 0;
    size_t name_start = i;
    ++i;
    while(i < n && is_ident_char(s[i]))
        ++i;
    size_t name_len = i - name_start;
    if(name_len + 1u > out_name_cap)
        return 0;
    memcpy(out_name, s + name_start, name_len);
    out_name[name_len] = '\0';
    if(out_base && out_base_cap > 0u)
        out_base[0] = '\0';
    while(i < n && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if(i < n && s[i] == ':')
    {
        ++i;
        while(i < n && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        if(i < n && is_ident_start_char(s[i]) && out_base && out_base_cap > 0u)
        {
            size_t base_start = i;
            ++i;
            while(i < n && is_ident_char(s[i]))
                ++i;
            size_t base_len = i - base_start;
            if(base_len + 1u <= out_base_cap)
            {
                memcpy(out_base, s + base_start, base_len);
                out_base[base_len] = '\0';
            }
        }
    }
    return 1;
}

static int identifier_at_line_column(const char* line, size_t n, int64_t col0,
                                     char* out, size_t out_cap)
{
    if(!line || n == 0u || !out || out_cap == 0u)
        return 0;
    int64_t pos = col0;
    if(pos < 0)
        pos = 0;
    if((size_t)pos >= n)
        pos = (int64_t)n - 1;
    if(!is_ident_char(line[pos]))
    {
        if(pos > 0 && is_ident_char(line[pos - 1]))
            pos--;
        else
            return 0;
    }
    int64_t start = pos;
    while(start > 0 && is_ident_char(line[start - 1]))
        --start;
    int64_t end = pos;
    while((size_t)(end + 1) < n && is_ident_char(line[end + 1]))
        ++end;
    if(!is_ident_start_char(line[start]))
        return 0;
    size_t len = (size_t)(end - start + 1);
    if(len + 1u > out_cap)
        return 0;
    memcpy(out, line + start, len);
    out[len] = '\0';
    return 1;
}

static int count_braces_line(const char* s, size_t n)
{
    int depth = 0;
    for(size_t i = 0; i < n; ++i)
    {
        if(s[i] == '{')
            depth++;
        else if(s[i] == '}')
            depth--;
    }
    return depth;
}

static int find_method_decl_in_line(const char* s, size_t n, const char* method,
                                    int* out_col0)
{
    if(!s || !method)
        return 0;
    size_t mlen = strlen(method);
    if(mlen == 0u)
        return 0;
    for(size_t i = 0; i + 2u < n; ++i)
    {
        if(s[i] != 'f' || s[i + 1u] != 'n')
            continue;
        if(i > 0u && is_ident_char(s[i - 1u]))
            continue;
        if(!isspace((unsigned char)s[i + 2u]))
            continue;
        size_t j = i + 2u;
        while(j < n && isspace((unsigned char)s[j]))
            ++j;
        if(j >= n || !is_ident_start_char(s[j]))
            continue;
        size_t name_start = j;
        ++j;
        while(j < n && is_ident_char(s[j]))
            ++j;
        size_t name_len = j - name_start;
        if(name_len != mlen)
            continue;
        if(strncmp(s + name_start, method, mlen) != 0)
            continue;
        while(j < n && isspace((unsigned char)s[j]))
            ++j;
        if(j >= n || s[j] != '(')
            continue;
        if(out_col0)
            *out_col0 = (int)name_start;
        return 1;
    }
    return 0;
}

static int detect_method_target(const char* query_text, int64_t line0, int64_t col0,
                                char* out_struct, size_t out_struct_cap,
                                char* out_method, size_t out_method_cap)
{
    if(!query_text || !out_struct || !out_method || out_struct_cap == 0u ||
       out_method_cap == 0u)
        return 0;
    const char* ls = NULL;
    const char* le = NULL;
    line_bounds_at(query_text, line0, &ls, &le);
    if(!ls || !le || le <= ls)
        return 0;
    if(!identifier_at_line_column(ls, (size_t)(le - ls), col0, out_method,
                                  out_method_cap))
        return 0;
    for(int64_t l = line0; l >= 0; --l)
    {
        const char* ss = NULL;
        const char* se = NULL;
        line_bounds_at(query_text, l, &ss, &se);
        if(!ss || !se || se <= ss)
            continue;
        if(parse_struct_decl_line(ss, (size_t)(se - ss), out_struct,
                                  out_struct_cap, NULL, 0u))
            return 1;
    }
    return 0;
}

static int find_implementation_in_text(const char* candidate_text,
                                       const char* base_struct,
                                       const char* method,
                                       int* out_line1, int* out_col1)
{
    if(!candidate_text || !base_struct || !method || !out_line1 || !out_col1)
        return 0;
    int64_t line = 0;
    const char* p = candidate_text;
    while(*p)
    {
        const char* line_start = p;
        while(*p && *p != '\n')
            ++p;
        const char* line_end = p;
        char struct_name[128];
        char base_name[128];
        if(parse_struct_decl_line(line_start, (size_t)(line_end - line_start),
                                  struct_name, sizeof(struct_name), base_name,
                                  sizeof(base_name)))
        {
            if(base_name[0] != '\0' && strcmp(base_name, base_struct) == 0)
            {
                int depth = count_braces_line(line_start,
                                              (size_t)(line_end - line_start));
                int started = (memchr(line_start, '{',
                                      (size_t)(line_end - line_start)) != NULL);
                int64_t inner_line = line + 1;
                const char* q = (*p == '\n') ? (p + 1) : p;
                while(*q)
                {
                    const char* s2 = q;
                    while(*q && *q != '\n')
                        ++q;
                    const char* e2 = q;
                    int col0 = 0;
                    if(find_method_decl_in_line(s2, (size_t)(e2 - s2), method,
                                                &col0))
                    {
                        *out_line1 = (int)(inner_line + 1);
                        *out_col1 = col0 + 1;
                        return 1;
                    }
                    if(memchr(s2, '{', (size_t)(e2 - s2)) != NULL)
                        started = 1;
                    depth += count_braces_line(s2, (size_t)(e2 - s2));
                    if(started && depth <= 0)
                        break;
                    if(*q == '\n')
                        ++q;
                    inner_line++;
                }
            }
        }
        if(*p == '\n')
        {
            ++p;
            line++;
        }
    }
    return 0;
}

int64_t __mlang_std_jsonrpc_organize_imports_line_count(const char* text)
{
    if(!text)
        return 0;
    const char* p = text;
    int64_t lines = 0;
    while(*p)
    {
        const char* start = p;
        while(*p && *p != '\n')
            ++p;
        size_t n = (size_t)(p - start);
        if(line_starts_with_use(start, n))
        {
            lines++;
        }
        else if(line_is_blank(start, n))
        {
            /* keep scanning */
        }
        else
        {
            break;
        }
        if(*p == '\n')
            ++p;
    }
    return lines;
}

char* __mlang_std_jsonrpc_organize_imports_text(const char* text)
{
    if(!text)
        return dup_cstr("");
    const char* p = text;
    char** imports = NULL;
    size_t ilen = 0u, icap = 0u;
    while(*p)
    {
        const char* start = p;
        while(*p && *p != '\n')
            ++p;
        size_t n = (size_t)(p - start);
        if(line_starts_with_use(start, n))
        {
            if(ilen == icap)
            {
                size_t next = icap == 0u ? 8u : icap * 2u;
                char** mem = (char**)realloc(imports, next * sizeof(char*));
                if(!mem)
                    goto fail;
                imports = mem;
                icap = next;
            }
            char* line = (char*)malloc(n + 2u);
            if(!line)
                goto fail;
            memcpy(line, start, n);
            line[n] = '\n';
            line[n + 1u] = '\0';
            imports[ilen++] = line;
        }
        else if(line_is_blank(start, n))
        {
            /* continue */
        }
        else
        {
            break;
        }
        if(*p == '\n')
            ++p;
    }
    if(ilen == 0u)
    {
        free(imports);
        return dup_cstr("");
    }
    qsort(imports, ilen, sizeof(char*), cmp_cstr_ptr);
    size_t uniq = 0u;
    for(size_t i = 0; i < ilen; ++i)
    {
        if(i == 0 || strcmp(imports[i], imports[i - 1u]) != 0)
            imports[uniq++] = imports[i];
        else
            free(imports[i]);
    }
    size_t total = 0u;
    for(size_t i = 0; i < uniq; ++i)
        total += strlen(imports[i]);
    char* out = (char*)malloc(total + 1u);
    if(!out)
        goto fail;
    size_t w = 0u;
    for(size_t i = 0; i < uniq; ++i)
    {
        size_t n = strlen(imports[i]);
        memcpy(out + w, imports[i], n);
        w += n;
        free(imports[i]);
    }
    out[w] = '\0';
    free(imports);
    return out;
fail:
    if(imports)
    {
        for(size_t i = 0; i < ilen; ++i)
            free(imports[i]);
        free(imports);
    }
    return dup_cstr("");
}

int64_t __mlang_std_jsonrpc_find_implementation_pos(const char* query_text,
                                                    int64_t line0,
                                                    int64_t column0,
                                                    const char* candidate_text)
{
    char base_struct[128];
    char method[128];
    if(!detect_method_target(query_text, line0, column0, base_struct,
                             sizeof(base_struct), method, sizeof(method)))
        return 0;
    int line1 = 0;
    int col1 = 0;
    if(!find_implementation_in_text(candidate_text, base_struct, method, &line1,
                                    &col1))
        return 0;
    return ((int64_t)line1 << 32) | (int64_t)(uint32_t)col1;
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
