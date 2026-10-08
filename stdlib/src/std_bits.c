#include <stdint.h>

int64_t __mlang_std_bits_popcount_u64(uint64_t value)
{
    int64_t count = 0;
    while(value != 0)
    {
        value &= value - UINT64_C(1);
        ++count;
    }
    return count;
}

int32_t __mlang_std_bits_has_single_bit_u64(uint64_t value)
{
    return value != 0 && (value & (value - UINT64_C(1))) == 0;
}

int64_t __mlang_std_bits_bit_width_u64(uint64_t value)
{
    int64_t width = 0;
    while(value != 0)
    {
        ++width;
        value >>= 1;
    }
    return width;
}

uint64_t __mlang_std_bits_bit_floor_u64(uint64_t value)
{
    if(value == 0)
        return 0;
    uint64_t floor = 1;
    while(value >>= 1)
        floor <<= 1;
    return floor;
}

static uint32_t normalize_rotation(int64_t shift)
{
    int64_t normalized = shift % 64;
    if(normalized < 0)
        normalized += 64;
    return (uint32_t)normalized;
}

uint64_t __mlang_std_bits_rotl_u64(uint64_t value, int64_t shift)
{
    uint32_t amount = normalize_rotation(shift);
    if(amount == 0)
        return value;
    return (value << amount) | (value >> (64 - amount));
}

uint64_t __mlang_std_bits_rotr_u64(uint64_t value, int64_t shift)
{
    uint32_t amount = normalize_rotation(shift);
    if(amount == 0)
        return value;
    return (value >> amount) | (value << (64 - amount));
}
