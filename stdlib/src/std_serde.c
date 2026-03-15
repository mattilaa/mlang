#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct serde_buf_t
{
    unsigned char* data;
    size_t len;
    size_t cap;
} serde_buf_t;

typedef struct serde_reader_t
{
    unsigned char* data;
    size_t len;
    size_t pos;
} serde_reader_t;

static char g_serde_last_error[256] = "";
static int32_t g_serde_last_ok = 1;

static void serde_set_ok(void)
{
    g_serde_last_ok = 1;
    g_serde_last_error[0] = '\0';
}

static void serde_set_error(const char* msg)
{
    if(!msg)
        msg = "unknown error";
    g_serde_last_ok = 0;
    (void)snprintf(g_serde_last_error, sizeof(g_serde_last_error), "%s", msg);
}

static int serde_reserve(serde_buf_t* b, size_t target)
{
    if(!b)
    {
        serde_set_error("binary buffer is null");
        return 0;
    }
    if(target <= b->cap)
    {
        serde_set_ok();
        return 1;
    }

    size_t next = b->cap > 0 ? b->cap : 32u;
    while(next < target)
    {
        if(next > SIZE_MAX / 2u)
        {
            next = target;
            break;
        }
        next *= 2u;
    }

    unsigned char* grown = (unsigned char*)realloc(b->data, next);
    if(!grown)
    {
        serde_set_error("allocation failed");
        return 0;
    }
    b->data = grown;
    b->cap = next;
    serde_set_ok();
    return 1;
}

static int serde_buf_append(serde_buf_t* b, const void* src, size_t n)
{
    if(!b)
    {
        serde_set_error("binary buffer is null");
        return 0;
    }
    if(n > 0u && !src)
    {
        serde_set_error("append source is null");
        return 0;
    }
    if(!serde_reserve(b, b->len + n))
        return 0;
    if(n > 0u)
        (void)memcpy(b->data + b->len, src, n);
    b->len += n;
    serde_set_ok();
    return 1;
}

const char* __mlang_std_serde_last_error(void)
{
    return g_serde_last_error;
}

int32_t __mlang_std_serde_last_ok(void)
{
    return g_serde_last_ok;
}

int64_t __mlang_std_serde_binary_new(int64_t initial_capacity)
{
    if(initial_capacity < 0)
    {
        serde_set_error("initial_capacity must be >= 0");
        return 0;
    }

    serde_buf_t* b = (serde_buf_t*)malloc(sizeof(*b));
    if(!b)
    {
        serde_set_error("allocation failed");
        return 0;
    }

    size_t cap = (size_t)initial_capacity;
    if(cap < 32u)
        cap = 32u;

    b->data = (unsigned char*)malloc(cap);
    if(!b->data)
    {
        free(b);
        serde_set_error("allocation failed");
        return 0;
    }

    b->len = 0u;
    b->cap = cap;
    serde_set_ok();
    return (int64_t)(intptr_t)b;
}

int32_t __mlang_std_serde_binary_free(int64_t handle)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    free(b->data);
    b->data = NULL;
    free(b);
    serde_set_ok();
    return 0;
}

int64_t __mlang_std_serde_binary_len(int64_t handle)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    serde_set_ok();
    return (int64_t)b->len;
}

int64_t __mlang_std_serde_binary_capacity(int64_t handle)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    serde_set_ok();
    return (int64_t)b->cap;
}

int32_t __mlang_std_serde_binary_clear(int64_t handle)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    b->len = 0u;
    serde_set_ok();
    return 0;
}

int32_t __mlang_std_serde_binary_reserve(int64_t handle, int64_t min_capacity)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    if(min_capacity < 0)
    {
        serde_set_error("min_capacity must be >= 0");
        return -1;
    }
    return serde_reserve(b, (size_t)min_capacity) ? 0 : -1;
}

int32_t __mlang_std_serde_binary_append_u8(int64_t handle, int32_t value)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    if(value < 0 || value > 255)
    {
        serde_set_error("u8 value out of range");
        return -1;
    }
    unsigned char v = (unsigned char)value;
    return serde_buf_append(b, &v, 1u) ? 0 : -1;
}

int32_t __mlang_std_serde_binary_append_bool(int64_t handle, int32_t value)
{
    return __mlang_std_serde_binary_append_u8(handle, value ? 1 : 0);
}

int32_t __mlang_std_serde_binary_append_i32(int64_t handle, int32_t value)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    unsigned char raw[4];
    uint32_t v = (uint32_t)value;
    raw[0] = (unsigned char)(v & 0xFFu);
    raw[1] = (unsigned char)((v >> 8) & 0xFFu);
    raw[2] = (unsigned char)((v >> 16) & 0xFFu);
    raw[3] = (unsigned char)((v >> 24) & 0xFFu);
    return serde_buf_append(b, raw, sizeof(raw)) ? 0 : -1;
}

int32_t __mlang_std_serde_binary_append_i64(int64_t handle, int64_t value)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    unsigned char raw[8];
    uint64_t v = (uint64_t)value;
    raw[0] = (unsigned char)(v & 0xFFu);
    raw[1] = (unsigned char)((v >> 8) & 0xFFu);
    raw[2] = (unsigned char)((v >> 16) & 0xFFu);
    raw[3] = (unsigned char)((v >> 24) & 0xFFu);
    raw[4] = (unsigned char)((v >> 32) & 0xFFu);
    raw[5] = (unsigned char)((v >> 40) & 0xFFu);
    raw[6] = (unsigned char)((v >> 48) & 0xFFu);
    raw[7] = (unsigned char)((v >> 56) & 0xFFu);
    return serde_buf_append(b, raw, sizeof(raw)) ? 0 : -1;
}

int32_t __mlang_std_serde_binary_append_f32(int64_t handle, float value)
{
    uint32_t bits = 0u;
    (void)memcpy(&bits, &value, sizeof(bits));
    return __mlang_std_serde_binary_append_i32(handle, (int32_t)bits);
}

int32_t __mlang_std_serde_binary_append_f64(int64_t handle, double value)
{
    uint64_t bits = 0u;
    (void)memcpy(&bits, &value, sizeof(bits));
    return __mlang_std_serde_binary_append_i64(handle, (int64_t)bits);
}

int32_t __mlang_std_serde_binary_append_string(int64_t handle, const char* s)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    if(!s)
    {
        serde_set_error("string is null");
        return -1;
    }

    size_t n = strlen(s);
    if(n > INT64_MAX)
    {
        serde_set_error("string too large");
        return -1;
    }
    if(__mlang_std_serde_binary_append_i64(handle, (int64_t)n) != 0)
        return -1;
    return serde_buf_append(b, s, n) ? 0 : -1;
}

int32_t __mlang_std_serde_binary_get_u8(int64_t handle, int64_t index)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    if(index < 0)
    {
        serde_set_error("index out of range");
        return -1;
    }
    size_t i = (size_t)index;
    if(i >= b->len)
    {
        serde_set_error("index out of range");
        return -1;
    }
    serde_set_ok();
    return (int32_t)b->data[i];
}

int32_t __mlang_std_serde_binary_write_file(int64_t handle, const char* path)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return -1;
    }
    if(!path)
    {
        serde_set_error("path is null");
        return -1;
    }

    FILE* fp = fopen(path, "wb");
    if(!fp)
    {
        serde_set_error("open for write failed");
        return -1;
    }

    size_t w = fwrite(b->data, 1, b->len, fp);
    (void)fflush(fp);
    (void)fclose(fp);
    if(w != b->len)
    {
        serde_set_error("write failed");
        return -1;
    }

    serde_set_ok();
    return 0;
}

int64_t __mlang_std_serde_binary_read_file(const char* path)
{
    if(!path)
    {
        serde_set_error("path is null");
        return 0;
    }

    FILE* fp = fopen(path, "rb");
    if(!fp)
    {
        serde_set_error("open for read failed");
        return 0;
    }
    if(fseek(fp, 0, SEEK_END) != 0)
    {
        (void)fclose(fp);
        serde_set_error("seek failed");
        return 0;
    }

    long sz = ftell(fp);
    if(sz < 0)
    {
        (void)fclose(fp);
        serde_set_error("tell failed");
        return 0;
    }
    if(fseek(fp, 0, SEEK_SET) != 0)
    {
        (void)fclose(fp);
        serde_set_error("seek failed");
        return 0;
    }

    int64_t h = __mlang_std_serde_binary_new((int64_t)sz);
    if(h == 0)
    {
        (void)fclose(fp);
        return 0;
    }

    serde_buf_t* b = (serde_buf_t*)(intptr_t)h;
    size_t n = (size_t)sz;
    if(n > 0u)
    {
        size_t got = fread(b->data, 1, n, fp);
        if(got != n)
        {
            (void)fclose(fp);
            (void)__mlang_std_serde_binary_free(h);
            serde_set_error("read failed");
            return 0;
        }
    }
    (void)fclose(fp);

    b->len = n;
    serde_set_ok();
    return h;
}

int64_t __mlang_std_serde_reader_new(int64_t binary_handle)
{
    serde_buf_t* b = (serde_buf_t*)(intptr_t)binary_handle;
    if(!b)
    {
        serde_set_error("binary handle is null");
        return 0;
    }

    serde_reader_t* r = (serde_reader_t*)malloc(sizeof(*r));
    if(!r)
    {
        serde_set_error("allocation failed");
        return 0;
    }

    r->data = NULL;
    r->len = b->len;
    r->pos = 0u;
    if(b->len > 0u)
    {
        r->data = (unsigned char*)malloc(b->len);
        if(!r->data)
        {
            free(r);
            serde_set_error("allocation failed");
            return 0;
        }
        (void)memcpy(r->data, b->data, b->len);
    }

    serde_set_ok();
    return (int64_t)(intptr_t)r;
}

int32_t __mlang_std_serde_reader_free(int64_t reader_handle)
{
    serde_reader_t* r = (serde_reader_t*)(intptr_t)reader_handle;
    if(!r)
    {
        serde_set_error("reader handle is null");
        return -1;
    }
    free(r->data);
    r->data = NULL;
    free(r);
    serde_set_ok();
    return 0;
}

int64_t __mlang_std_serde_reader_remaining(int64_t reader_handle)
{
    serde_reader_t* r = (serde_reader_t*)(intptr_t)reader_handle;
    if(!r)
    {
        serde_set_error("reader handle is null");
        return -1;
    }
    serde_set_ok();
    return (int64_t)(r->len - r->pos);
}

static int serde_reader_read_bytes(serde_reader_t* r, void* out, size_t n)
{
    if(!r)
    {
        serde_set_error("reader handle is null");
        return 0;
    }
    if(!out && n > 0u)
    {
        serde_set_error("read target is null");
        return 0;
    }
    if(r->pos + n > r->len)
    {
        serde_set_error("unexpected end of binary");
        return 0;
    }

    if(n > 0u)
        (void)memcpy(out, r->data + r->pos, n);
    r->pos += n;
    serde_set_ok();
    return 1;
}

int32_t __mlang_std_serde_reader_read_u8(int64_t reader_handle)
{
    serde_reader_t* r = (serde_reader_t*)(intptr_t)reader_handle;
    unsigned char v = 0u;
    if(!serde_reader_read_bytes(r, &v, 1u))
        return -1;
    return (int32_t)v;
}

int32_t __mlang_std_serde_reader_read_bool(int64_t reader_handle)
{
    int32_t v = __mlang_std_serde_reader_read_u8(reader_handle);
    if(!g_serde_last_ok)
        return -1;
    serde_set_ok();
    return v == 0 ? 0 : 1;
}

int32_t __mlang_std_serde_reader_read_i32(int64_t reader_handle)
{
    serde_reader_t* r = (serde_reader_t*)(intptr_t)reader_handle;
    unsigned char raw[4];
    if(!serde_reader_read_bytes(r, raw, sizeof(raw)))
        return 0;

    uint32_t v = ((uint32_t)raw[0]) |
                 ((uint32_t)raw[1] << 8) |
                 ((uint32_t)raw[2] << 16) |
                 ((uint32_t)raw[3] << 24);
    serde_set_ok();
    return (int32_t)v;
}

int64_t __mlang_std_serde_reader_read_i64(int64_t reader_handle)
{
    serde_reader_t* r = (serde_reader_t*)(intptr_t)reader_handle;
    unsigned char raw[8];
    if(!serde_reader_read_bytes(r, raw, sizeof(raw)))
        return 0;

    uint64_t v = ((uint64_t)raw[0]) |
                 ((uint64_t)raw[1] << 8) |
                 ((uint64_t)raw[2] << 16) |
                 ((uint64_t)raw[3] << 24) |
                 ((uint64_t)raw[4] << 32) |
                 ((uint64_t)raw[5] << 40) |
                 ((uint64_t)raw[6] << 48) |
                 ((uint64_t)raw[7] << 56);
    serde_set_ok();
    return (int64_t)v;
}

float __mlang_std_serde_reader_read_f32(int64_t reader_handle)
{
    uint32_t bits = (uint32_t)__mlang_std_serde_reader_read_i32(reader_handle);
    if(!g_serde_last_ok)
        return 0.0f;

    float out = 0.0f;
    (void)memcpy(&out, &bits, sizeof(out));
    serde_set_ok();
    return out;
}

double __mlang_std_serde_reader_read_f64(int64_t reader_handle)
{
    uint64_t bits = (uint64_t)__mlang_std_serde_reader_read_i64(reader_handle);
    if(!g_serde_last_ok)
        return 0.0;

    double out = 0.0;
    (void)memcpy(&out, &bits, sizeof(out));
    serde_set_ok();
    return out;
}

char* __mlang_std_serde_reader_read_string(int64_t reader_handle)
{
    serde_reader_t* r = (serde_reader_t*)(intptr_t)reader_handle;
    if(!r)
    {
        serde_set_error("reader handle is null");
        return NULL;
    }

    int64_t n = __mlang_std_serde_reader_read_i64(reader_handle);
    if(!g_serde_last_ok)
        return NULL;
    if(n < 0)
    {
        serde_set_error("negative string length");
        return NULL;
    }
    if((uint64_t)n > (uint64_t)(r->len - r->pos))
    {
        serde_set_error("unexpected end of binary while reading string");
        return NULL;
    }

    char* out = (char*)malloc((size_t)n + 1u);
    if(!out)
    {
        serde_set_error("allocation failed");
        return NULL;
    }
    if(n > 0)
    {
        (void)memcpy(out, r->data + r->pos, (size_t)n);
        r->pos += (size_t)n;
    }
    out[n] = '\0';
    serde_set_ok();
    return out;
}
