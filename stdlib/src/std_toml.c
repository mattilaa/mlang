#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char* key;
    char* value;
} toml_kv_t;

typedef struct
{
    toml_kv_t* items;
    int64_t len;
    int64_t cap;
} toml_doc_t;

static char* mlang_strdup(const char* s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static int streq(const char* a, const char* b)
{
    if(!a || !b)
        return a == b ? 1 : 0;
    return strcmp(a, b) == 0 ? 1 : 0;
}

static char* trim_copy(const char* s)
{
    if(!s)
        return mlang_strdup("");
    const char* start = s;
    while(*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;
    const char* end = start + strlen(start);
    while(end > start &&
          (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
           end[-1] == '\n'))
        --end;
    size_t n = (size_t)(end - start);
    char* out = (char*)malloc(n + 1);
    if(!out)
        return mlang_strdup("");
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

static char* unquote_copy(const char* s)
{
    char* t = trim_copy(s);
    size_t n = strlen(t);
    if(n >= 2 && t[0] == '"' && t[n - 1] == '"')
    {
        t[n - 1] = '\0';
        char* out = mlang_strdup(t + 1);
        free(t);
        return out ? out : mlang_strdup("");
    }
    return t;
}

static int toml_reserve(toml_doc_t* d, int64_t need)
{
    if(!d)
        return 0;
    if(d->cap >= need)
        return 1;
    int64_t next = d->cap <= 0 ? 8 : d->cap * 2;
    while(next < need)
        next *= 2;
    toml_kv_t* p = (toml_kv_t*)realloc(d->items, (size_t)next * sizeof(toml_kv_t));
    if(!p)
        return 0;
    d->items = p;
    d->cap = next;
    return 1;
}

static void toml_set_kv(toml_doc_t* d, const char* key, const char* value)
{
    if(!d || !key || !value)
        return;
    for(int64_t i = 0; i < d->len; ++i)
    {
        if(streq(d->items[i].key, key))
        {
            free(d->items[i].value);
            d->items[i].value = mlang_strdup(value);
            return;
        }
    }
    if(!toml_reserve(d, d->len + 1))
        return;
    d->items[d->len].key = mlang_strdup(key);
    d->items[d->len].value = mlang_strdup(value);
    d->len += 1;
}

static const char* toml_get_kv(const toml_doc_t* d, const char* key)
{
    if(!d || !key)
        return NULL;
    for(int64_t i = 0; i < d->len; ++i)
    {
        if(streq(d->items[i].key, key))
            return d->items[i].value;
    }
    return NULL;
}

static char* join_section_key(const char* sec, const char* key)
{
    if(!sec || !*sec)
        return mlang_strdup(key ? key : "");
    size_t a = strlen(sec);
    size_t b = key ? strlen(key) : 0;
    char* out = (char*)malloc(a + 1 + b + 1);
    if(!out)
        return mlang_strdup("");
    memcpy(out, sec, a);
    out[a] = '.';
    if(key && b > 0)
        memcpy(out + a + 1, key, b);
    out[a + 1 + b] = '\0';
    return out;
}

int64_t __toml_parse(const char* text)
{
    toml_doc_t* d = (toml_doc_t*)calloc(1, sizeof(toml_doc_t));
    if(!d)
        return 0;
    if(!text)
        return (int64_t)(intptr_t)d;

    char* src = mlang_strdup(text);
    if(!src)
        return (int64_t)(intptr_t)d;

    char* saveptr = NULL;
    char* line = strtok_r(src, "\n", &saveptr);
    char section[256];
    section[0] = '\0';
    while(line)
    {
        char* t = trim_copy(line);
        if(t[0] == '\0' || t[0] == '#')
        {
            free(t);
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        size_t tn = strlen(t);
        if(t[0] == '[' && tn > 2 && t[tn - 1] == ']')
        {
            t[tn - 1] = '\0';
            const char* sec = t + 1;
            size_t sn = strlen(sec);
            if(sn >= sizeof(section))
                sn = sizeof(section) - 1;
            memcpy(section, sec, sn);
            section[sn] = '\0';
            free(t);
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        char* eq = strchr(t, '=');
        if(eq)
        {
            *eq = '\0';
            char* k = trim_copy(t);
            char* v = unquote_copy(eq + 1);
            char* full = join_section_key(section, k);
            toml_set_kv(d, full, v);
            free(full);
            free(v);
            free(k);
        }
        free(t);
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(src);
    return (int64_t)(intptr_t)d;
}

char* __toml_get_string(int64_t obj, const char* key, const char* fallback)
{
    toml_doc_t* d = (toml_doc_t*)(intptr_t)obj;
    const char* v = toml_get_kv(d, key ? key : "");
    if(v)
        return mlang_strdup(v);
    return mlang_strdup(fallback ? fallback : "");
}

void __toml_set_inline_table(int64_t obj, const char* key, const char* field,
                             const char* value)
{
    toml_doc_t* d = (toml_doc_t*)(intptr_t)obj;
    if(!d || !key || !field)
        return;
    size_t a = strlen(key);
    size_t b = strlen(field);
    char* full = (char*)malloc(a + 1 + b + 1);
    if(!full)
        return;
    memcpy(full, key, a);
    full[a] = '.';
    memcpy(full + a + 1, field, b);
    full[a + 1 + b] = '\0';
    toml_set_kv(d, full, value ? value : "");
    free(full);
}

char* __toml_stringify(int64_t obj)
{
    toml_doc_t* d = (toml_doc_t*)(intptr_t)obj;
    if(!d || d->len <= 0)
        return mlang_strdup("");

    size_t cap = 256;
    char* out = (char*)malloc(cap);
    if(!out)
        return mlang_strdup("");
    out[0] = '\0';
    size_t len = 0;

    for(int64_t i = 0; i < d->len; ++i)
    {
        const char* k = d->items[i].key ? d->items[i].key : "";
        const char* v = d->items[i].value ? d->items[i].value : "";
        size_t need = strlen(k) + strlen(v) + 8;
        if(len + need + 1 > cap)
        {
            while(len + need + 1 > cap)
                cap *= 2;
            char* p = (char*)realloc(out, cap);
            if(!p)
                break;
            out = p;
        }
        int wrote = snprintf(out + len, cap - len, "%s = \"%s\"\n", k, v);
        if(wrote < 0)
            break;
        len += (size_t)wrote;
    }
    return out;
}

