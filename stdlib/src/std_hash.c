#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash_mix_u64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static uint64_t hash_fnv1a_init(void)
{
    return 0xcbf29ce484222325ULL;
}

static uint64_t hash_fnv1a_step(uint64_t hash, uint8_t byte)
{
    hash ^= (uint64_t)byte;
    hash *= 0x100000001b3ULL;
    return hash;
}

static char* hash_hex_string(uint64_t value)
{
    static const char* digits = "0123456789abcdef";
    char* out = (char*)malloc(17);
    if(!out)
        return NULL;
    for(int i = 15; i >= 0; --i)
    {
        out[i] = digits[value & 0xFULL];
        value >>= 4;
    }
    out[16] = '\0';
    return out;
}

int64_t __mlang_std_hash_init(void)
{
    return (int64_t)hash_fnv1a_init();
}

int64_t __mlang_std_hash_i64(int64_t value)
{
    return (int64_t)hash_mix_u64((uint64_t)value);
}

int64_t __mlang_std_hash_bool(int32_t value)
{
    return (int64_t)hash_mix_u64(value ? 1ULL : 0ULL);
}

int64_t __mlang_std_hash_str(const char* text)
{
    uint64_t hash = hash_fnv1a_init();
    if(!text)
        return (int64_t)hash;
    for(size_t i = 0; text[i] != '\0'; ++i)
        hash = hash_fnv1a_step(hash, (uint8_t)text[i]);
    return (int64_t)hash;
}

int64_t __mlang_std_hash_str16(const uint16_t* text)
{
    uint64_t hash = hash_fnv1a_init();
    if(!text)
        return (int64_t)hash;
    for(size_t i = 0; text[i] != 0; ++i)
    {
        uint16_t unit = text[i];
        hash = hash_fnv1a_step(hash, (uint8_t)(unit & 0xFFu));
        hash = hash_fnv1a_step(hash, (uint8_t)((unit >> 8) & 0xFFu));
    }
    return (int64_t)hash;
}

int64_t __mlang_std_hash_combine(int64_t seed, int64_t value_hash)
{
    uint64_t s = (uint64_t)seed;
    uint64_t v = (uint64_t)value_hash;
    s ^= v + 0x9e3779b97f4a7c15ULL + (s << 6) + (s >> 2);
    return (int64_t)s;
}

char* __mlang_std_hash_to_hex(int64_t value)
{
    return hash_hex_string((uint64_t)value);
}
