#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct mlang_bitset_t
{
    uint64_t* words;
    size_t len_bits;
    size_t cap_bits;
} mlang_bitset_t;

static char g_bitset_last_error[256] = "";

static void bitset_set_error(const char* msg)
{
    if(!msg)
        msg = "unknown error";
    snprintf(g_bitset_last_error, sizeof(g_bitset_last_error), "%s", msg);
}

static size_t words_for_bits(size_t bits)
{
    return bits == 0u ? 0u : (bits + 63u) / 64u;
}

static int bitset_ensure_capacity(mlang_bitset_t* b, size_t target_bits)
{
    if(!b)
        return 0;
    if(target_bits <= b->cap_bits)
        return 1;

    size_t next_bits = b->cap_bits > 0u ? b->cap_bits : 64u;
    while(next_bits < target_bits)
    {
        if(next_bits > SIZE_MAX / 2u)
        {
            next_bits = target_bits;
            break;
        }
        next_bits *= 2u;
    }

    size_t old_words = words_for_bits(b->cap_bits);
    size_t new_words = words_for_bits(next_bits);
    uint64_t* grown = (uint64_t*)realloc(b->words, new_words * sizeof(uint64_t));
    if(!grown)
    {
        bitset_set_error("allocation failed");
        return 0;
    }
    if(new_words > old_words)
        memset(grown + old_words, 0, (new_words - old_words) * sizeof(uint64_t));
    b->words = grown;
    b->cap_bits = next_bits;
    return 1;
}

static int bitset_set_bit(mlang_bitset_t* b, size_t idx, int value)
{
    if(!b || idx >= b->len_bits)
        return -1;
    size_t word = idx / 64u;
    uint64_t mask = (uint64_t)1u << (idx % 64u);
    if(value)
        b->words[word] |= mask;
    else
        b->words[word] &= ~mask;
    return 0;
}

static int bitset_get_bit(const mlang_bitset_t* b, size_t idx)
{
    if(!b || idx >= b->len_bits)
        return -1;
    size_t word = idx / 64u;
    uint64_t mask = (uint64_t)1u << (idx % 64u);
    return (b->words[word] & mask) != 0u ? 1 : 0;
}

static void bitset_mask_tail(mlang_bitset_t* b)
{
    if(!b || b->len_bits == 0u)
        return;
    size_t rem = b->len_bits % 64u;
    if(rem == 0u)
        return;
    size_t last = words_for_bits(b->len_bits) - 1u;
    uint64_t mask = ((uint64_t)1u << rem) - 1u;
    b->words[last] &= mask;
}

const char* __mlang_std_bitset_last_error(void)
{
    return g_bitset_last_error;
}

int64_t __mlang_std_bitset_new(int64_t bit_capacity)
{
    if(bit_capacity < 0)
    {
        bitset_set_error("bit_capacity must be >= 0");
        return 0;
    }
    mlang_bitset_t* b = (mlang_bitset_t*)malloc(sizeof(*b));
    if(!b)
    {
        bitset_set_error("allocation failed");
        return 0;
    }
    b->words = NULL;
    b->len_bits = 0u;
    b->cap_bits = 0u;
    if(!bitset_ensure_capacity(b, (size_t)bit_capacity))
    {
        free(b);
        return 0;
    }
    g_bitset_last_error[0] = '\0';
    return (int64_t)(intptr_t)b;
}

int32_t __mlang_std_bitset_free(int64_t handle)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b)
        return -1;
    free(b->words);
    b->words = NULL;
    free(b);
    return 0;
}

int64_t __mlang_std_bitset_len(int64_t handle)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b)
        return -1;
    return (int64_t)b->len_bits;
}

int64_t __mlang_std_bitset_capacity(int64_t handle)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b)
        return -1;
    return (int64_t)b->cap_bits;
}

int32_t __mlang_std_bitset_clear(int64_t handle)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b)
        return -1;
    b->len_bits = 0u;
    return 0;
}

int32_t __mlang_std_bitset_resize(int64_t handle, int64_t new_len, int32_t fill_bit)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b || new_len < 0)
        return -1;
    if(fill_bit != 0 && fill_bit != 1)
    {
        bitset_set_error("fill_bit must be 0 or 1");
        return -1;
    }
    size_t old_len = b->len_bits;
    size_t next_len = (size_t)new_len;
    if(!bitset_ensure_capacity(b, next_len))
        return -1;
    b->len_bits = next_len;
    if(next_len > old_len)
    {
        for(size_t i = old_len; i < next_len; ++i)
            bitset_set_bit(b, i, fill_bit);
    }
    bitset_mask_tail(b);
    return 0;
}

int32_t __mlang_std_bitset_set(int64_t handle, int64_t index, int32_t value)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b || index < 0)
        return -1;
    if(value != 0 && value != 1)
    {
        bitset_set_error("bit value must be 0 or 1");
        return -1;
    }
    if(bitset_set_bit(b, (size_t)index, value) != 0)
    {
        bitset_set_error("index out of range");
        return -1;
    }
    return 0;
}

int32_t __mlang_std_bitset_get(int64_t handle, int64_t index)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b || index < 0)
        return -1;
    int v = bitset_get_bit(b, (size_t)index);
    if(v < 0)
    {
        bitset_set_error("index out of range");
        return -1;
    }
    return v;
}

int32_t __mlang_std_bitset_toggle(int64_t handle, int64_t index)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b || index < 0)
        return -1;
    int v = bitset_get_bit(b, (size_t)index);
    if(v < 0)
    {
        bitset_set_error("index out of range");
        return -1;
    }
    return bitset_set_bit(b, (size_t)index, v == 0 ? 1 : 0);
}

int32_t __mlang_std_bitset_push(int64_t handle, int32_t value)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b)
        return -1;
    if(value != 0 && value != 1)
    {
        bitset_set_error("bit value must be 0 or 1");
        return -1;
    }
    if(!bitset_ensure_capacity(b, b->len_bits + 1u))
        return -1;
    b->len_bits += 1u;
    return bitset_set_bit(b, b->len_bits - 1u, value);
}

int32_t __mlang_std_bitset_pop(int64_t handle)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b || b->len_bits == 0u)
    {
        bitset_set_error("bitset is empty");
        return -1;
    }
    int v = bitset_get_bit(b, b->len_bits - 1u);
    b->len_bits -= 1u;
    bitset_mask_tail(b);
    return v;
}

int64_t __mlang_std_bitset_count_ones(int64_t handle)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b)
        return -1;
    size_t words = words_for_bits(b->len_bits);
    uint64_t total = 0u;
    for(size_t i = 0u; i < words; ++i)
        total += (uint64_t)__builtin_popcountll((unsigned long long)b->words[i]);
    return (int64_t)total;
}

int32_t __mlang_std_bitset_and_eq(int64_t lhs_handle, int64_t rhs_handle)
{
    mlang_bitset_t* lhs = (mlang_bitset_t*)(intptr_t)lhs_handle;
    mlang_bitset_t* rhs = (mlang_bitset_t*)(intptr_t)rhs_handle;
    if(!lhs || !rhs)
        return -1;
    if(lhs->len_bits != rhs->len_bits)
    {
        bitset_set_error("bitset lengths must match");
        return -1;
    }
    size_t words = words_for_bits(lhs->len_bits);
    for(size_t i = 0u; i < words; ++i)
        lhs->words[i] &= rhs->words[i];
    bitset_mask_tail(lhs);
    return 0;
}

int32_t __mlang_std_bitset_or_eq(int64_t lhs_handle, int64_t rhs_handle)
{
    mlang_bitset_t* lhs = (mlang_bitset_t*)(intptr_t)lhs_handle;
    mlang_bitset_t* rhs = (mlang_bitset_t*)(intptr_t)rhs_handle;
    if(!lhs || !rhs)
        return -1;
    if(lhs->len_bits != rhs->len_bits)
    {
        bitset_set_error("bitset lengths must match");
        return -1;
    }
    size_t words = words_for_bits(lhs->len_bits);
    for(size_t i = 0u; i < words; ++i)
        lhs->words[i] |= rhs->words[i];
    bitset_mask_tail(lhs);
    return 0;
}

int32_t __mlang_std_bitset_xor_eq(int64_t lhs_handle, int64_t rhs_handle)
{
    mlang_bitset_t* lhs = (mlang_bitset_t*)(intptr_t)lhs_handle;
    mlang_bitset_t* rhs = (mlang_bitset_t*)(intptr_t)rhs_handle;
    if(!lhs || !rhs)
        return -1;
    if(lhs->len_bits != rhs->len_bits)
    {
        bitset_set_error("bitset lengths must match");
        return -1;
    }
    size_t words = words_for_bits(lhs->len_bits);
    for(size_t i = 0u; i < words; ++i)
        lhs->words[i] ^= rhs->words[i];
    bitset_mask_tail(lhs);
    return 0;
}

int32_t __mlang_std_bitset_not_eq(int64_t handle)
{
    mlang_bitset_t* b = (mlang_bitset_t*)(intptr_t)handle;
    if(!b)
        return -1;
    size_t words = words_for_bits(b->len_bits);
    for(size_t i = 0u; i < words; ++i)
        b->words[i] = ~b->words[i];
    bitset_mask_tail(b);
    return 0;
}
