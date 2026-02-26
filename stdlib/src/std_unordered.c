#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file std_unordered.c
 * @brief Open-addressing hash table/set runtime for std::unordered.
 */

typedef struct
{
    int64_t len;
    void* data;
} mlang_list_t;

typedef struct
{
    uint8_t state; /* 0 empty, 1 full, 2 tombstone */
    int64_t key;
    int64_t value;
} bucket_t;

typedef struct
{
    int64_t len;
    int64_t cap;
    bucket_t* buckets;
} map_i64_i64_t;

static uint64_t hash_u64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static uint64_t hash_i64(int64_t k)
{
    return hash_u64((uint64_t)k);
}

static int ensure_capacity(map_i64_i64_t* m, int64_t min_cap)
{
    if(!m)
        return -1;

    int64_t target = m->cap;
    if(target < 8)
        target = 8;
    while(target < min_cap)
        target *= 2;

    if(m->cap >= target)
        return 0;

    bucket_t* newb = (bucket_t*)calloc((size_t)target, sizeof(bucket_t));
    if(!newb)
        return -1;

    bucket_t* old = m->buckets;
    int64_t old_cap = m->cap;

    m->buckets = newb;
    m->cap = target;
    m->len = 0;

    for(int64_t i = 0; i < old_cap; ++i)
    {
        if(old[i].state != 1)
            continue;

        uint64_t h = hash_i64(old[i].key);
        int64_t mask = m->cap - 1;
        int64_t idx = (int64_t)(h & (uint64_t)mask);

        for(;;)
        {
            if(m->buckets[idx].state != 1)
            {
                m->buckets[idx].state = 1;
                m->buckets[idx].key = old[i].key;
                m->buckets[idx].value = old[i].value;
                m->len += 1;
                break;
            }
            idx = (idx + 1) & mask;
        }
    }

    free(old);
    return 0;
}

static int maybe_grow(map_i64_i64_t* m)
{
    if(!m)
        return -1;
    if(m->cap < 8)
        return ensure_capacity(m, 8);

    /* grow at 75% */
    if((m->len + 1) * 4 >= m->cap * 3)
        return ensure_capacity(m, m->cap * 2);
    return 0;
}

int64_t __mlang_std_unordered_map_i64_i64_new(void)
{
    map_i64_i64_t* m = (map_i64_i64_t*)calloc(1, sizeof(map_i64_i64_t));
    if(!m)
        return 0;
    if(ensure_capacity(m, 8) != 0)
    {
        free(m);
        return 0;
    }
    return (int64_t)(intptr_t)m;
}

int __mlang_std_unordered_map_i64_i64_free(int64_t handle)
{
    map_i64_i64_t* m = (map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return -1;
    free(m->buckets);
    free(m);
    return 0;
}

int __mlang_std_unordered_map_i64_i64_insert(int64_t handle, int64_t key,
                                              int64_t value)
{
    map_i64_i64_t* m = (map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return -1;
    if(maybe_grow(m) != 0)
        return -1;

    int64_t mask = m->cap - 1;
    int64_t idx = (int64_t)(hash_i64(key) & (uint64_t)mask);
    int64_t first_tomb = -1;

    for(;;)
    {
        bucket_t* b = &m->buckets[idx];
        if(b->state == 0)
        {
            if(first_tomb >= 0)
                b = &m->buckets[first_tomb];
            b->state = 1;
            b->key = key;
            b->value = value;
            m->len += 1;
            return 1; /* inserted */
        }
        if(b->state == 2)
        {
            if(first_tomb < 0)
                first_tomb = idx;
        }
        else if(b->key == key)
        {
            b->value = value;
            return 0; /* updated */
        }
        idx = (idx + 1) & mask;
    }
}

int __mlang_std_unordered_map_i64_i64_contains(int64_t handle, int64_t key)
{
    map_i64_i64_t* m = (map_i64_i64_t*)(intptr_t)handle;
    if(!m || m->cap <= 0)
        return 0;

    int64_t mask = m->cap - 1;
    int64_t idx = (int64_t)(hash_i64(key) & (uint64_t)mask);

    for(;;)
    {
        bucket_t* b = &m->buckets[idx];
        if(b->state == 0)
            return 0;
        if(b->state == 1 && b->key == key)
            return 1;
        idx = (idx + 1) & mask;
    }
}

int64_t __mlang_std_unordered_map_i64_i64_get_or(int64_t handle, int64_t key,
                                                  int64_t default_value)
{
    map_i64_i64_t* m = (map_i64_i64_t*)(intptr_t)handle;
    if(!m || m->cap <= 0)
        return default_value;

    int64_t mask = m->cap - 1;
    int64_t idx = (int64_t)(hash_i64(key) & (uint64_t)mask);

    for(;;)
    {
        bucket_t* b = &m->buckets[idx];
        if(b->state == 0)
            return default_value;
        if(b->state == 1 && b->key == key)
            return b->value;
        idx = (idx + 1) & mask;
    }
}

int __mlang_std_unordered_map_i64_i64_remove(int64_t handle, int64_t key)
{
    map_i64_i64_t* m = (map_i64_i64_t*)(intptr_t)handle;
    if(!m || m->cap <= 0)
        return 0;

    int64_t mask = m->cap - 1;
    int64_t idx = (int64_t)(hash_i64(key) & (uint64_t)mask);

    for(;;)
    {
        bucket_t* b = &m->buckets[idx];
        if(b->state == 0)
            return 0;
        if(b->state == 1 && b->key == key)
        {
            b->state = 2;
            m->len -= 1;
            return 1;
        }
        idx = (idx + 1) & mask;
    }
}

int64_t __mlang_std_unordered_map_i64_i64_len(int64_t handle)
{
    map_i64_i64_t* m = (map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return 0;
    return m->len;
}

mlang_list_t __mlang_std_unordered_map_i64_i64_keys(int64_t handle)
{
    mlang_list_t out;
    out.len = 0;
    out.data = NULL;

    map_i64_i64_t* m = (map_i64_i64_t*)(intptr_t)handle;
    if(!m || m->len <= 0)
        return out;

    int64_t* keys = (int64_t*)malloc((size_t)m->len * sizeof(int64_t));
    if(!keys)
        return out;

    int64_t n = 0;
    for(int64_t i = 0; i < m->cap; ++i)
    {
        if(m->buckets[i].state == 1)
            keys[n++] = m->buckets[i].key;
    }

    out.len = n;
    out.data = keys;
    return out;
}

mlang_list_t __mlang_std_unordered_map_i64_i64_values(int64_t handle)
{
    mlang_list_t out;
    out.len = 0;
    out.data = NULL;

    map_i64_i64_t* m = (map_i64_i64_t*)(intptr_t)handle;
    if(!m || m->len <= 0)
        return out;

    int64_t* vals = (int64_t*)malloc((size_t)m->len * sizeof(int64_t));
    if(!vals)
        return out;

    int64_t n = 0;
    for(int64_t i = 0; i < m->cap; ++i)
    {
        if(m->buckets[i].state == 1)
            vals[n++] = m->buckets[i].value;
    }

    out.len = n;
    out.data = vals;
    return out;
}

/* HashSet<i64> shares map backend with dummy value 1. */
int64_t __mlang_std_unordered_set_i64_new(void)
{
    return __mlang_std_unordered_map_i64_i64_new();
}

int __mlang_std_unordered_set_i64_free(int64_t handle)
{
    return __mlang_std_unordered_map_i64_i64_free(handle);
}

int __mlang_std_unordered_set_i64_insert(int64_t handle, int64_t key)
{
    return __mlang_std_unordered_map_i64_i64_insert(handle, key, 1);
}

int __mlang_std_unordered_set_i64_contains(int64_t handle, int64_t key)
{
    return __mlang_std_unordered_map_i64_i64_contains(handle, key);
}

int __mlang_std_unordered_set_i64_remove(int64_t handle, int64_t key)
{
    return __mlang_std_unordered_map_i64_i64_remove(handle, key);
}

int64_t __mlang_std_unordered_set_i64_len(int64_t handle)
{
    return __mlang_std_unordered_map_i64_i64_len(handle);
}

mlang_list_t __mlang_std_unordered_set_i64_keys(int64_t handle)
{
    return __mlang_std_unordered_map_i64_i64_keys(handle);
}

/* QuickMapVec<i64, i64>: vector-backed map (linear search). */
typedef struct
{
    int64_t key;
    int64_t value;
} kv_i64_i64_t;

typedef struct
{
    int64_t len;
    int64_t cap;
    kv_i64_i64_t* items;
} vec_map_i64_i64_t;

static int vec_map_grow(vec_map_i64_i64_t* m, int64_t min_cap)
{
    if(!m)
        return -1;
    int64_t target = m->cap;
    if(target < 8)
        target = 8;
    while(target < min_cap)
        target *= 2;
    if(target <= m->cap)
        return 0;
    kv_i64_i64_t* next =
        (kv_i64_i64_t*)realloc(m->items, (size_t)target * sizeof(kv_i64_i64_t));
    if(!next)
        return -1;
    m->items = next;
    m->cap = target;
    return 0;
}

int64_t __mlang_std_unordered_vecmap_i64_i64_new(void)
{
    vec_map_i64_i64_t* m =
        (vec_map_i64_i64_t*)calloc(1, sizeof(vec_map_i64_i64_t));
    if(!m)
        return 0;
    if(vec_map_grow(m, 8) != 0)
    {
        free(m);
        return 0;
    }
    return (int64_t)(intptr_t)m;
}

int __mlang_std_unordered_vecmap_i64_i64_free(int64_t handle)
{
    vec_map_i64_i64_t* m = (vec_map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return -1;
    free(m->items);
    free(m);
    return 0;
}

int __mlang_std_unordered_vecmap_i64_i64_set(int64_t handle, int64_t key,
                                              int64_t value)
{
    vec_map_i64_i64_t* m = (vec_map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return -1;
    for(int64_t i = 0; i < m->len; ++i)
    {
        if(m->items[i].key == key)
        {
            m->items[i].value = value;
            return 0; /* updated */
        }
    }
    if(vec_map_grow(m, m->len + 1) != 0)
        return -1;
    m->items[m->len].key = key;
    m->items[m->len].value = value;
    m->len += 1;
    return 1; /* inserted */
}

int __mlang_std_unordered_vecmap_i64_i64_has(int64_t handle, int64_t key)
{
    vec_map_i64_i64_t* m = (vec_map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return 0;
    for(int64_t i = 0; i < m->len; ++i)
    {
        if(m->items[i].key == key)
            return 1;
    }
    return 0;
}

int64_t __mlang_std_unordered_vecmap_i64_i64_get_or(int64_t handle, int64_t key,
                                                     int64_t default_value)
{
    vec_map_i64_i64_t* m = (vec_map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return default_value;
    for(int64_t i = 0; i < m->len; ++i)
    {
        if(m->items[i].key == key)
            return m->items[i].value;
    }
    return default_value;
}

int __mlang_std_unordered_vecmap_i64_i64_del(int64_t handle, int64_t key)
{
    vec_map_i64_i64_t* m = (vec_map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return 0;
    for(int64_t i = 0; i < m->len; ++i)
    {
        if(m->items[i].key == key)
        {
            m->items[i] = m->items[m->len - 1];
            m->len -= 1;
            return 1;
        }
    }
    return 0;
}

int64_t __mlang_std_unordered_vecmap_i64_i64_len(int64_t handle)
{
    vec_map_i64_i64_t* m = (vec_map_i64_i64_t*)(intptr_t)handle;
    if(!m)
        return 0;
    return m->len;
}

mlang_list_t __mlang_std_unordered_vecmap_i64_i64_keys(int64_t handle)
{
    mlang_list_t out;
    out.len = 0;
    out.data = NULL;
    vec_map_i64_i64_t* m = (vec_map_i64_i64_t*)(intptr_t)handle;
    if(!m || m->len <= 0)
        return out;
    int64_t* keys = (int64_t*)malloc((size_t)m->len * sizeof(int64_t));
    if(!keys)
        return out;
    for(int64_t i = 0; i < m->len; ++i)
        keys[i] = m->items[i].key;
    out.len = m->len;
    out.data = keys;
    return out;
}

mlang_list_t __mlang_std_unordered_vecmap_i64_i64_values(int64_t handle)
{
    mlang_list_t out;
    out.len = 0;
    out.data = NULL;
    vec_map_i64_i64_t* m = (vec_map_i64_i64_t*)(intptr_t)handle;
    if(!m || m->len <= 0)
        return out;
    int64_t* vals = (int64_t*)malloc((size_t)m->len * sizeof(int64_t));
    if(!vals)
        return out;
    for(int64_t i = 0; i < m->len; ++i)
        vals[i] = m->items[i].value;
    out.len = m->len;
    out.data = vals;
    return out;
}
