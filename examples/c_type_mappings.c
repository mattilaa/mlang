#include <stdbool.h>
#include <stdint.h>

#include "../include/mlang_c_types.h"

int32_t c_add_i32(int32_t a, int32_t b)
{
    return a + b;
}

int64_t c_add_i64(int64_t a, int64_t b)
{
    return a + b;
}

uint32_t c_add_u32(uint32_t a, uint32_t b)
{
    return a + b;
}

bool c_is_true(bool v)
{
    return v;
}

float c_float(float x)
{
    return x;
}

double c_double(double x)
{
    return x;
}

int32_t c_str8(mlang_str8 s)
{
    return (s && s[0]) ? 1 : 0;
}

int32_t c_str16(mlang_str16 s)
{
    return (s && s[0]) ? 1 : 0;
}

int32_t c_string(mlang_string s)
{
    return (s && s[0]) ? 1 : 0;
}
