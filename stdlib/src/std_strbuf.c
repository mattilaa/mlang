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
