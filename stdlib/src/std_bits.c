#include <float.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 &&
                   FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "f32 bit_cast requires IEEE-754 binary32");
_Static_assert(sizeof(double) == sizeof(uint64_t) && FLT_RADIX == 2 &&
                   DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "f64 bit_cast requires IEEE-754 binary64");

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

int64_t __mlang_std_bits_countl_zero_u64(uint64_t value)
{
    int64_t count = 0;
    uint64_t mask = UINT64_C(1) << 63;
    while(mask != 0 && (value & mask) == 0)
    {
        ++count;
        mask >>= 1;
    }
    return count;
}

int64_t __mlang_std_bits_countr_zero_u64(uint64_t value)
{
    if(value == 0)
        return 64;
    int64_t count = 0;
    while((value & UINT64_C(1)) == 0)
    {
        ++count;
        value >>= 1;
    }
    return count;
}

int64_t __mlang_std_bits_countl_one_u64(uint64_t value)
{
    return __mlang_std_bits_countl_zero_u64(~value);
}

int64_t __mlang_std_bits_countr_one_u64(uint64_t value)
{
    return __mlang_std_bits_countr_zero_u64(~value);
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

uint64_t __mlang_std_bits_bit_ceil_u64(uint64_t value)
{
    if(value <= 1)
        return 1;
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    ++value;
    // There is no representable 2^64 in the u64 domain.
    return value;
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

uint64_t __mlang_std_bits_byteswap_u64(uint64_t value)
{
    return ((value & UINT64_C(0x00000000000000FF)) << 56) |
           ((value & UINT64_C(0x000000000000FF00)) << 40) |
           ((value & UINT64_C(0x0000000000FF0000)) << 24) |
           ((value & UINT64_C(0x00000000FF000000)) << 8) |
           ((value & UINT64_C(0x000000FF00000000)) >> 8) |
           ((value & UINT64_C(0x0000FF0000000000)) >> 24) |
           ((value & UINT64_C(0x00FF000000000000)) >> 40) |
           ((value & UINT64_C(0xFF00000000000000)) >> 56);
}

uint32_t __mlang_std_bits_byteswap_u32(uint32_t value)
{
    return ((value & UINT32_C(0x000000FF)) << 24) |
           ((value & UINT32_C(0x0000FF00)) << 8) |
           ((value & UINT32_C(0x00FF0000)) >> 8) |
           ((value & UINT32_C(0xFF000000)) >> 24);
}

uint16_t __mlang_std_bits_byteswap_u16(uint16_t value)
{
    return (uint16_t)(((value & UINT16_C(0x00FF)) << 8) |
                      ((value & UINT16_C(0xFF00)) >> 8));
}

uint8_t __mlang_std_bits_byteswap_u8(uint8_t value)
{
    return value;
}

uint32_t __mlang_std_bits_f32_to_u32(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float __mlang_std_bits_u32_to_f32(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

uint64_t __mlang_std_bits_f64_to_u64(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double __mlang_std_bits_u64_to_f64(uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
