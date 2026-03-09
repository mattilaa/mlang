#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct mlang_bytes_t
{
    unsigned char* data;
    size_t len;
    size_t cap;
} mlang_bytes_t;

static char g_bytes_last_error[256] = "";

static void bytes_set_error(const char* msg)
{
    if(!msg)
        msg = "unknown error";
    snprintf(g_bytes_last_error, sizeof(g_bytes_last_error), "%s", msg);
}

static int bytes_ensure_capacity(mlang_bytes_t* b, size_t target)
{
    if(!b)
        return 0;
    if(target <= b->cap)
        return 1;

    size_t next = b->cap > 0 ? b->cap : 16u;
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
        bytes_set_error("allocation failed");
        return 0;
    }
    b->data = grown;
    b->cap = next;
    return 1;
}

const char* __mlang_std_bytes_last_error(void)
{
    return g_bytes_last_error;
}

int64_t __mlang_std_bytes_new(int64_t initial_capacity)
{
    if(initial_capacity < 0)
    {
        bytes_set_error("initial_capacity must be >= 0");
        return 0;
    }

    mlang_bytes_t* b = (mlang_bytes_t*)malloc(sizeof(*b));
    if(!b)
    {
        bytes_set_error("allocation failed");
        return 0;
    }

    size_t cap = (size_t)initial_capacity;
    if(cap < 16u)
        cap = 16u;

    b->data = (unsigned char*)malloc(cap);
    if(!b->data)
    {
        free(b);
        bytes_set_error("allocation failed");
        return 0;
    }

    b->len = 0u;
    b->cap = cap;
    g_bytes_last_error[0] = '\0';
    return (int64_t)(intptr_t)b;
}

int64_t __mlang_std_bytes_from_string(const char* s)
{
    if(!s)
    {
        bytes_set_error("string is null");
        return 0;
    }

    size_t n = strlen(s);
    int64_t h = __mlang_std_bytes_new((int64_t)n);
    if(h == 0)
        return 0;

    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)h;
    if(n > 0u)
        memcpy(b->data, s, n);
    b->len = n;
    return h;
}

int32_t __mlang_std_bytes_free(int64_t handle)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b)
        return -1;
    free(b->data);
    b->data = NULL;
    free(b);
    return 0;
}

int64_t __mlang_std_bytes_len(int64_t handle)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b)
        return -1;
    return (int64_t)b->len;
}

int64_t __mlang_std_bytes_capacity(int64_t handle)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b)
        return -1;
    return (int64_t)b->cap;
}

int32_t __mlang_std_bytes_clear(int64_t handle)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b)
        return -1;
    b->len = 0u;
    return 0;
}

int32_t __mlang_std_bytes_reserve(int64_t handle, int64_t min_capacity)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b || min_capacity < 0)
        return -1;
    return bytes_ensure_capacity(b, (size_t)min_capacity) ? 0 : -1;
}

int32_t __mlang_std_bytes_append_byte(int64_t handle, int32_t value)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b)
        return -1;
    if(value < 0 || value > 255)
    {
        bytes_set_error("byte value out of range");
        return -1;
    }
    if(!bytes_ensure_capacity(b, b->len + 1u))
        return -1;
    b->data[b->len++] = (unsigned char)value;
    return 0;
}

int64_t __mlang_std_bytes_append_string(int64_t handle, const char* s)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b || !s)
        return -1;

    size_t n = strlen(s);
    if(!bytes_ensure_capacity(b, b->len + n))
        return -1;
    if(n > 0u)
        memcpy(b->data + b->len, s, n);
    b->len += n;
    return (int64_t)n;
}

int64_t __mlang_std_bytes_append_bytes(int64_t handle, int64_t other_handle)
{
    mlang_bytes_t* dst = (mlang_bytes_t*)(intptr_t)handle;
    mlang_bytes_t* src = (mlang_bytes_t*)(intptr_t)other_handle;
    if(!dst || !src)
        return -1;

    if(!bytes_ensure_capacity(dst, dst->len + src->len))
        return -1;
    if(src->len > 0u)
        memcpy(dst->data + dst->len, src->data, src->len);
    dst->len += src->len;
    return (int64_t)src->len;
}

int32_t __mlang_std_bytes_get(int64_t handle, int64_t index)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b || index < 0)
        return -1;
    size_t i = (size_t)index;
    if(i >= b->len)
    {
        bytes_set_error("index out of range");
        return -1;
    }
    return (int32_t)b->data[i];
}

int32_t __mlang_std_bytes_set(int64_t handle, int64_t index, int32_t value)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b || index < 0 || value < 0 || value > 255)
        return -1;
    size_t i = (size_t)index;
    if(i >= b->len)
    {
        bytes_set_error("index out of range");
        return -1;
    }
    b->data[i] = (unsigned char)value;
    return 0;
}

char* __mlang_std_bytes_to_string(int64_t handle)
{
    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b)
        return NULL;

    char* out = (char*)malloc(b->len + 1u);
    if(!out)
    {
        bytes_set_error("allocation failed");
        return NULL;
    }
    if(b->len > 0u)
        memcpy(out, b->data, b->len);
    out[b->len] = '\0';
    return out;
}

char* __mlang_std_bytes_to_hex(int64_t handle)
{
    static const char* HEX = "0123456789abcdef";

    mlang_bytes_t* b = (mlang_bytes_t*)(intptr_t)handle;
    if(!b)
        return NULL;

    if(b->len > (SIZE_MAX - 1u) / 2u)
    {
        bytes_set_error("payload too large");
        return NULL;
    }

    size_t out_len = b->len * 2u;
    char* out = (char*)malloc(out_len + 1u);
    if(!out)
    {
        bytes_set_error("allocation failed");
        return NULL;
    }

    for(size_t i = 0; i < b->len; ++i)
    {
        unsigned char v = b->data[i];
        out[i * 2u] = HEX[(v >> 4) & 0x0Fu];
        out[i * 2u + 1u] = HEX[v & 0x0Fu];
    }
    out[out_len] = '\0';
    return out;
}
