#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t utf8_decode_one(const unsigned char* s, uint32_t* cp)
{
    if(!s || !cp)
        return 0;

    unsigned char b0 = s[0];
    if(b0 < 0x80)
    {
        *cp = b0;
        return 1;
    }
    if((b0 & 0xE0) == 0xC0)
    {
        unsigned char b1 = s[1];
        if((b1 & 0xC0) != 0x80)
            goto invalid;
        uint32_t v = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        if(v < 0x80)
            goto invalid;
        *cp = v;
        return 2;
    }
    if((b0 & 0xF0) == 0xE0)
    {
        unsigned char b1 = s[1];
        unsigned char b2 = s[2];
        if((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
            goto invalid;
        uint32_t v = ((uint32_t)(b0 & 0x0F) << 12) |
                     ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
        if(v < 0x800 || (v >= 0xD800 && v <= 0xDFFF))
            goto invalid;
        *cp = v;
        return 3;
    }
    if((b0 & 0xF8) == 0xF0)
    {
        unsigned char b1 = s[1];
        unsigned char b2 = s[2];
        unsigned char b3 = s[3];
        if((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 ||
           (b3 & 0xC0) != 0x80)
            goto invalid;
        uint32_t v = ((uint32_t)(b0 & 0x07) << 18) |
                     ((uint32_t)(b1 & 0x3F) << 12) |
                     ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
        if(v < 0x10000 || v > 0x10FFFF)
            goto invalid;
        *cp = v;
        return 4;
    }

invalid:
    *cp = 0xFFFD;
    return 1;
}

static size_t utf16_encode_one(uint32_t cp, uint16_t* out)
{
    if(cp <= 0xFFFF)
    {
        if(cp >= 0xD800 && cp <= 0xDFFF)
            cp = 0xFFFD;
        out[0] = (uint16_t)cp;
        return 1;
    }

    cp -= 0x10000;
    out[0] = (uint16_t)(0xD800u + ((cp >> 10) & 0x3FFu));
    out[1] = (uint16_t)(0xDC00u + (cp & 0x3FFu));
    return 2;
}

static size_t utf8_encode_one(uint32_t cp, unsigned char* out)
{
    if(cp <= 0x7F)
    {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if(cp <= 0x7FF)
    {
        out[0] = (unsigned char)(0xC0u | ((cp >> 6) & 0x1Fu));
        out[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if(cp <= 0xFFFF)
    {
        if(cp >= 0xD800 && cp <= 0xDFFF)
            cp = 0xFFFD;
        out[0] = (unsigned char)(0xE0u | ((cp >> 12) & 0x0Fu));
        out[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
        return 3;
    }

    if(cp > 0x10FFFF)
        cp = 0xFFFD;
    out[0] = (unsigned char)(0xF0u | ((cp >> 18) & 0x07u));
    out[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 4;
}

int64_t __mlang_std_strbuf_len(const char* s)
{
    if(!s)
        return 0;
    return (int64_t)strlen(s);
}

int __mlang_std_strbuf_is_empty(const char* s)
{
    if(!s)
        return 1;
    return s[0] == '\0' ? 1 : 0;
}

char* __mlang_std_strbuf_clone(const char* s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    memcpy(out, s, n + 1);
    return out;
}

int __mlang_std_strbuf_eq(const char* a, const char* b)
{
    if(!a || !b)
        return a == b ? 1 : 0;
    return strcmp(a, b) == 0 ? 1 : 0;
}

int64_t __mlang_std_strbuf_compare(const char* a, const char* b)
{
    if(!a && !b)
        return 0;
    if(!a)
        return -1;
    if(!b)
        return 1;
    return (int64_t)strcmp(a, b);
}

int __mlang_std_strbuf_eq16(const uint16_t* a, const uint16_t* b)
{
    if(!a || !b)
        return a == b ? 1 : 0;
    size_t i = 0;
    while(a[i] != 0 && b[i] != 0)
    {
        if(a[i] != b[i])
            return 0;
        ++i;
    }
    return a[i] == b[i] ? 1 : 0;
}

int64_t __mlang_std_strbuf_compare16(const uint16_t* a, const uint16_t* b)
{
    if(!a && !b)
        return 0;
    if(!a)
        return -1;
    if(!b)
        return 1;

    size_t i = 0;
    while(a[i] != 0 && b[i] != 0)
    {
        if(a[i] < b[i])
            return -1;
        if(a[i] > b[i])
            return 1;
        ++i;
    }
    if(a[i] == b[i])
        return 0;
    return a[i] == 0 ? -1 : 1;
}

int __mlang_std_strbuf_starts_with(const char* s, const char* prefix)
{
    if(!s || !prefix)
        return 0;
    size_t pn = strlen(prefix);
    size_t sn = strlen(s);
    if(pn > sn)
        return 0;
    return strncmp(s, prefix, pn) == 0 ? 1 : 0;
}

int __mlang_std_strbuf_ends_with(const char* s, const char* suffix)
{
    if(!s || !suffix)
        return 0;
    size_t sn = strlen(s);
    size_t xn = strlen(suffix);
    if(xn > sn)
        return 0;
    return strcmp(s + (sn - xn), suffix) == 0 ? 1 : 0;
}

int __mlang_std_strbuf_contains(const char* s, const char* needle)
{
    if(!s || !needle)
        return 0;
    return strstr(s, needle) ? 1 : 0;
}

int64_t __mlang_std_strbuf_find(const char* s, const char* needle)
{
    if(!s || !needle)
        return -1;
    const char* p = strstr(s, needle);
    if(!p)
        return -1;
    return (int64_t)(p - s);
}

int64_t __mlang_std_strbuf_rfind(const char* s, const char* needle)
{
    if(!s || !needle)
        return -1;

    size_t needleLen = strlen(needle);
    if(needleLen == 0)
        return (int64_t)strlen(s);

    int64_t last = -1;
    const char* cur = s;
    while(1)
    {
        const char* p = strstr(cur, needle);
        if(!p)
            break;
        last = (int64_t)(p - s);
        cur = p + 1;
    }
    return last;
}

/**
 * @brief Return a newly allocated substring by byte offsets.
 *
 * The slice starts at `start` (0-based byte index) and contains up to
 * `count` bytes. Indices are clamped so out-of-range values are safe:
 * negative `start` is treated as `0`, negative `count` yields an empty
 * string, and ranges past the end are truncated.
 *
 * @param s Input UTF-8 string (treated as raw bytes for slicing).
 * @param start Start byte offset (0-based).
 * @param count Maximum number of bytes to copy.
 * @return Newly allocated NUL-terminated string (caller frees), or `NULL`
 * on allocation failure / NULL input.
 */
char* __mlang_std_strbuf_sub(const char* s, int64_t start, int64_t count)
{
    if(!s)
        return NULL;

    size_t n = strlen(s);
    size_t begin = 0u;
    if(start > 0)
        begin = (size_t)start;
    if(begin > n)
        begin = n;

    size_t take = 0u;
    if(count > 0)
        take = (size_t)count;
    if(begin + take > n)
        take = n - begin;

    char* out = (char*)malloc(take + 1u);
    if(!out)
        return NULL;
    if(take > 0u)
        memcpy(out, s + begin, take);
    out[take] = '\0';
    return out;
}

char* __mlang_std_strbuf_concat(const char* a, const char* b)
{
    if(!a && !b)
        return NULL;
    if(!a)
        return __mlang_std_strbuf_clone(b);
    if(!b)
        return __mlang_std_strbuf_clone(a);

    size_t an = strlen(a);
    size_t bn = strlen(b);
    char* out = (char*)malloc(an + bn + 1);
    if(!out)
        return NULL;
    memcpy(out, a, an);
    memcpy(out + an, b, bn + 1);
    return out;
}

uint16_t* __mlang_std_strbuf_concat16(const uint16_t* a, const uint16_t* b)
{
    static const uint16_t empty16[] = {0};
    if(!a)
        a = empty16;
    if(!b)
        b = empty16;

    size_t an = 0;
    while(a[an] != 0)
        ++an;
    size_t bn = 0;
    while(b[bn] != 0)
        ++bn;

    uint16_t* out = (uint16_t*)malloc((an + bn + 1u) * sizeof(uint16_t));
    if(!out)
        return NULL;
    memcpy(out, a, an * sizeof(uint16_t));
    memcpy(out + an, b, bn * sizeof(uint16_t));
    out[an + bn] = 0;
    return out;
}

char* __mlang_std_strbuf_repeat(const char* s, int64_t count)
{
    if(count <= 0)
    {
        char* empty = (char*)malloc(1);
        if(empty)
            empty[0] = '\0';
        return empty;
    }

    if(!s)
        return NULL;

    size_t n = strlen(s);
    size_t times = (size_t)count;
    size_t total = n * times;
    char* out = (char*)malloc(total + 1);
    if(!out)
        return NULL;

    char* w = out;
    for(size_t i = 0; i < times; ++i)
    {
        memcpy(w, s, n);
        w += n;
    }
    out[total] = '\0';
    return out;
}

char* __mlang_std_strbuf_align(const char* s, int64_t width, int32_t align)
{
    if(width <= 0)
        return __mlang_std_strbuf_clone("");

    if(!s)
        s = "";

    size_t n = strlen(s);
    size_t out_len = (size_t)width;
    char* out = (char*)malloc(out_len + 1);
    if(!out)
        return NULL;

    if(n >= out_len)
    {
        memcpy(out, s, out_len);
        out[out_len] = '\0';
        return out;
    }

    size_t total_pad = out_len - n;
    size_t left_pad = 0;
    size_t right_pad = total_pad;

    if(align == 2)
    {
        left_pad = total_pad;
        right_pad = 0;
    }
    else if(align == 1)
    {
        left_pad = total_pad / 2;
        right_pad = total_pad - left_pad;
    }

    memset(out, ' ', left_pad);
    memcpy(out + left_pad, s, n);
    memset(out + left_pad + n, ' ', right_pad);
    out[out_len] = '\0';
    return out;
}

static int is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

char* __mlang_std_strbuf_ltrim(const char* s)
{
    if(!s)
        return NULL;

    const char* p = s;
    while(*p && is_ascii_space(*p))
        ++p;
    return __mlang_std_strbuf_clone(p);
}

char* __mlang_std_strbuf_rtrim(const char* s)
{
    if(!s)
        return NULL;

    size_t n = strlen(s);
    while(n > 0 && is_ascii_space(s[n - 1]))
        --n;

    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

char* __mlang_std_strbuf_trim(const char* s)
{
    if(!s)
        return NULL;

    const char* start = s;
    while(*start && is_ascii_space(*start))
        ++start;

    size_t n = strlen(start);
    while(n > 0 && is_ascii_space(start[n - 1]))
        --n;

    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

uint16_t* __mlang_std_strbuf_utf8_to_utf16(const char* s)
{
    if(!s)
        return NULL;

    size_t units = 0;
    const unsigned char* p = (const unsigned char*)s;
    while(*p)
    {
        uint32_t cp = 0;
        size_t n = utf8_decode_one(p, &cp);
        if(n == 0)
            break;
        p += n;
        units += (cp <= 0xFFFFu) ? 1u : 2u;
    }

    uint16_t* out = (uint16_t*)malloc((units + 1u) * sizeof(uint16_t));
    if(!out)
        return NULL;

    p = (const unsigned char*)s;
    size_t w = 0;
    while(*p)
    {
        uint32_t cp = 0;
        size_t n = utf8_decode_one(p, &cp);
        if(n == 0)
            break;
        p += n;
        w += utf16_encode_one(cp, out + w);
    }
    out[w] = 0;
    return out;
}

char* __mlang_std_strbuf_utf16_to_utf8(const uint16_t* s)
{
    if(!s)
        return NULL;

    size_t bytes = 0;
    for(size_t i = 0; s[i] != 0; ++i)
    {
        uint32_t cp = s[i];
        if(cp >= 0xD800u && cp <= 0xDBFFu && s[i + 1] != 0)
        {
            uint32_t lo = s[i + 1];
            if(lo >= 0xDC00u && lo <= 0xDFFFu)
            {
                cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
                ++i;
            }
            else
            {
                cp = 0xFFFDu;
            }
        }
        else if(cp >= 0xDC00u && cp <= 0xDFFFu)
        {
            cp = 0xFFFDu;
        }

        if(cp <= 0x7Fu)
            bytes += 1u;
        else if(cp <= 0x7FFu)
            bytes += 2u;
        else if(cp <= 0xFFFFu)
            bytes += 3u;
        else
            bytes += 4u;
    }

    char* out = (char*)malloc(bytes + 1u);
    if(!out)
        return NULL;

    size_t w = 0;
    for(size_t i = 0; s[i] != 0; ++i)
    {
        uint32_t cp = s[i];
        if(cp >= 0xD800u && cp <= 0xDBFFu && s[i + 1] != 0)
        {
            uint32_t lo = s[i + 1];
            if(lo >= 0xDC00u && lo <= 0xDFFFu)
            {
                cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
                ++i;
            }
            else
            {
                cp = 0xFFFDu;
            }
        }
        else if(cp >= 0xDC00u && cp <= 0xDFFFu)
        {
            cp = 0xFFFDu;
        }

        w += utf8_encode_one(cp, (unsigned char*)(out + w));
    }
    out[w] = '\0';
    return out;
}

void __mlang_std_strbuf_free_str16(uint16_t* s)
{
    free(s);
}

char* __mlang_std_strbuf_json_escape(const char* s)
{
    if(!s)
        return NULL;

    size_t in_len = strlen(s);
    size_t out_cap = (in_len * 6u) + 1u;
    char* out = (char*)malloc(out_cap);
    if(!out)
        return NULL;

    size_t w = 0;
    for(size_t i = 0; i < in_len; ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        switch(c)
        {
            case '\"':
                out[w++] = '\\';
                out[w++] = '\"';
                break;
            case '\\':
                out[w++] = '\\';
                out[w++] = '\\';
                break;
            case '\b':
                out[w++] = '\\';
                out[w++] = 'b';
                break;
            case '\f':
                out[w++] = '\\';
                out[w++] = 'f';
                break;
            case '\n':
                out[w++] = '\\';
                out[w++] = 'n';
                break;
            case '\r':
                out[w++] = '\\';
                out[w++] = 'r';
                break;
            case '\t':
                out[w++] = '\\';
                out[w++] = 't';
                break;
            default:
                if(c < 0x20u)
                {
                    static const char hex[] = "0123456789abcdef";
                    out[w++] = '\\';
                    out[w++] = 'u';
                    out[w++] = '0';
                    out[w++] = '0';
                    out[w++] = hex[(c >> 4) & 0x0Fu];
                    out[w++] = hex[c & 0x0Fu];
                }
                else
                {
                    out[w++] = (char)c;
                }
                break;
        }
    }

    out[w] = '\0';
    return out;
}

typedef struct mlang_strbuilder_t
{
    char* buf;
    size_t len;
    size_t cap;
    size_t page_size;
} mlang_strbuilder_t;

static size_t sb_default_page_size(void)
{
    return 256u;
}

static int sb_round_up_to_page(size_t need, size_t page, size_t* out)
{
    if(!out || page == 0u)
        return 0;
    size_t rem = need % page;
    if(rem == 0u)
    {
        *out = need;
        return 1;
    }
    size_t add = page - rem;
    if(need > SIZE_MAX - add)
        return 0;
    *out = need + add;
    return 1;
}

static int sb_ensure_capacity(mlang_strbuilder_t* sb, size_t additional)
{
    if(!sb || !sb->buf || sb->cap == 0u)
        return 0;

    if(additional > SIZE_MAX - sb->len - 1u)
        return 0;
    size_t need = sb->len + additional + 1u;
    if(need <= sb->cap)
        return 1;

    size_t doubled = sb->cap;
    if(doubled <= SIZE_MAX / 2u)
        doubled = doubled * 2u;
    else
        doubled = SIZE_MAX;

    size_t rounded_need = 0u;
    if(!sb_round_up_to_page(need, sb->page_size, &rounded_need))
        return 0;

    size_t next_cap = doubled > rounded_need ? doubled : rounded_need;
    if(next_cap < need)
        return 0;

    char* grown = (char*)realloc(sb->buf, next_cap);
    if(!grown)
        return 0;
    sb->buf = grown;
    sb->cap = next_cap;
    return 1;
}

int64_t __mlang_std_strbuf_builder_rt_new(int64_t initial_capacity, int64_t page_size)
{
    size_t page = sb_default_page_size();
    if(page_size > 0)
        page = (size_t)page_size;
    if(page == 0u)
        page = 1u;

    size_t requested = 1u;
    if(initial_capacity > 0)
    {
        size_t init = (size_t)initial_capacity;
        if(init > SIZE_MAX - 1u)
            return 0;
        requested = init + 1u;
    }

    size_t cap = 0u;
    if(!sb_round_up_to_page(requested, page, &cap))
        return 0;
    if(cap == 0u)
        cap = 1u;

    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)malloc(sizeof(*sb));
    if(!sb)
        return 0;
    sb->buf = (char*)malloc(cap);
    if(!sb->buf)
    {
        free(sb);
        return 0;
    }
    sb->buf[0] = '\0';
    sb->len = 0u;
    sb->cap = cap;
    sb->page_size = page;
    return (int64_t)(intptr_t)sb;
}

int32_t __mlang_std_strbuf_builder_rt_free(int64_t handle)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb)
        return -1;
    free(sb->buf);
    sb->buf = NULL;
    free(sb);
    return 0;
}

int64_t __mlang_std_strbuf_builder_rt_len(int64_t handle)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb)
        return -1;
    return (int64_t)sb->len;
}

int64_t __mlang_std_strbuf_builder_rt_capacity(int64_t handle)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || sb->cap == 0u)
        return -1;
    return (int64_t)(sb->cap - 1u);
}

int32_t __mlang_std_strbuf_builder_rt_set_page_size(int64_t handle, int64_t page_size)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || page_size <= 0)
        return -1;
    sb->page_size = (size_t)page_size;
    if(sb->page_size == 0u)
        sb->page_size = 1u;
    return 0;
}

int32_t __mlang_std_strbuf_builder_rt_clear(int64_t handle)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || !sb->buf)
        return -1;
    sb->len = 0u;
    sb->buf[0] = '\0';
    return 0;
}

int32_t __mlang_std_strbuf_builder_rt_reserve(int64_t handle, int64_t min_capacity)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || min_capacity < 0)
        return -1;

    size_t target = (size_t)min_capacity;
    if(target <= sb->len)
        return 0;
    size_t additional = target - sb->len;
    return sb_ensure_capacity(sb, additional) ? 0 : -1;
}

int64_t __mlang_std_strbuf_builder_rt_append(int64_t handle, const char* s)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || !sb->buf || !s)
        return -1;

    size_t n = strlen(s);
    if(!sb_ensure_capacity(sb, n))
        return -1;
    if(n > 0u)
        memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
    return (int64_t)n;
}

int64_t __mlang_std_strbuf_builder_rt_append_len(int64_t handle, const char* s, int64_t n)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || !sb->buf || !s || n < 0)
        return -1;

    size_t count = (size_t)n;
    if(!sb_ensure_capacity(sb, count))
        return -1;
    if(count > 0u)
        memcpy(sb->buf + sb->len, s, count);
    sb->len += count;
    sb->buf[sb->len] = '\0';
    return n;
}

int64_t __mlang_std_strbuf_builder_rt_append_char(int64_t handle, int32_t ch)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || !sb->buf || ch < 0 || ch > 255)
        return -1;

    if(!sb_ensure_capacity(sb, 1u))
        return -1;
    sb->buf[sb->len] = (char)(unsigned char)ch;
    sb->len += 1u;
    sb->buf[sb->len] = '\0';
    return 1;
}

char* __mlang_std_strbuf_builder_rt_data(int64_t handle)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || !sb->buf)
        return NULL;
    return sb->buf;
}

char* __mlang_std_strbuf_builder_rt_to_string(int64_t handle)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || !sb->buf)
        return NULL;
    return __mlang_std_strbuf_clone(sb->buf);
}

char* __mlang_std_strbuf_builder_rt_take_string(int64_t handle)
{
    mlang_strbuilder_t* sb = (mlang_strbuilder_t*)(intptr_t)handle;
    if(!sb || !sb->buf)
        return NULL;

    char* out = sb->buf;

    size_t cap = sb->page_size;
    if(cap == 0u)
        cap = 1u;
    char* fresh = (char*)malloc(cap);
    if(!fresh)
        return NULL;
    fresh[0] = '\0';

    sb->buf = fresh;
    sb->len = 0u;
    sb->cap = cap;
    return out;
}
