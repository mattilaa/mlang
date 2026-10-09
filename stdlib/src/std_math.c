#include <math.h>
#include <stdint.h>

int32_t __mlang_std_math_midpoint_i32(int32_t a, int32_t b)
{
    if(a <= b)
    {
        uint32_t distance = (uint32_t)b - (uint32_t)a;
        return a + (int32_t)(distance / UINT32_C(2));
    }
    uint32_t distance = (uint32_t)a - (uint32_t)b;
    return a - (int32_t)(distance / UINT32_C(2));
}

int64_t __mlang_std_math_midpoint_i64(int64_t a, int64_t b)
{
    if(a <= b)
    {
        uint64_t distance = (uint64_t)b - (uint64_t)a;
        return a + (int64_t)(distance / UINT64_C(2));
    }
    uint64_t distance = (uint64_t)a - (uint64_t)b;
    return a - (int64_t)(distance / UINT64_C(2));
}

int32_t __mlang_std_math_add_i32(int32_t a, int32_t b)
{
    return a + b;
}

float __mlang_std_math_add_f32(float a, float b)
{
    return a + b;
}

double __mlang_std_math_add_f64(double a, double b)
{
    return a + b;
}

int32_t __mlang_std_math_subtract_i32(int32_t a, int32_t b)
{
    return a - b;
}

float __mlang_std_math_subtract_f32(float a, float b)
{
    return a - b;
}

double __mlang_std_math_subtract_f64(double a, double b)
{
    return a - b;
}

int32_t __mlang_std_math_multiply_i32(int32_t a, int32_t b)
{
    return a * b;
}

float __mlang_std_math_multiply_f32(float a, float b)
{
    return a * b;
}

double __mlang_std_math_multiply_f64(double a, double b)
{
    return a * b;
}

int32_t __mlang_std_math_square_i32(int32_t x)
{
    return x * x;
}

float __mlang_std_math_square_f32(float x)
{
    return x * x;
}

double __mlang_std_math_square_f64(double x)
{
    return x * x;
}

int32_t __mlang_std_math_sum_range_i32(int32_t start, int32_t end)
{
    int32_t total = 0;
    for(int32_t i = start; i < end; ++i)
    {
        total += i;
    }
    return total;
}

int32_t __mlang_std_math_factorial_i32(int32_t n)
{
    int32_t result = 1;
    for(int32_t i = 1; i < n + 1; ++i)
    {
        result *= i;
    }
    return result;
}

int32_t __mlang_std_math_abs_i32(int32_t x)
{
    return x < 0 ? -x : x;
}

float __mlang_std_math_abs_f32(float x)
{
    return fabsf(x);
}

double __mlang_std_math_abs_f64(double x)
{
    return fabs(x);
}

int32_t __mlang_std_math_min_i32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

float __mlang_std_math_min_f32(float a, float b)
{
    return a < b ? a : b;
}

double __mlang_std_math_min_f64(double a, double b)
{
    return a < b ? a : b;
}

int32_t __mlang_std_math_max_i32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

float __mlang_std_math_max_f32(float a, float b)
{
    return a > b ? a : b;
}

double __mlang_std_math_max_f64(double a, double b)
{
    return a > b ? a : b;
}

int32_t __mlang_std_math_clamp_i32(int32_t x, int32_t low, int32_t high)
{
    if(x < low)
        return low;
    if(x > high)
        return high;
    return x;
}

float __mlang_std_math_clamp_f32(float x, float low, float high)
{
    if(x < low)
        return low;
    if(x > high)
        return high;
    return x;
}

double __mlang_std_math_clamp_f64(double x, double low, double high)
{
    if(x < low)
        return low;
    if(x > high)
        return high;
    return x;
}

int64_t __mlang_std_math_clamp_i64(int64_t x, int64_t low, int64_t high)
{
    if(x < low)
        return low;
    if(x > high)
        return high;
    return x;
}

int32_t __mlang_std_math_pow_i32(int32_t a, int32_t b)
{
    int32_t result = 1;
    if(b < 0)
        return 0;
    for(int32_t i = 0; i < b; ++i)
        result *= a;
    return result;
}

float __mlang_std_math_pow_f32(float a, float b)
{
    return powf(a, b);
}

double __mlang_std_math_pow_f64(double a, double b)
{
    return pow(a, b);
}

int32_t __mlang_std_math_sqrt_i32(int32_t x)
{
    if(x <= 0)
        return 0;
    return (int32_t)sqrt((double)x);
}

float __mlang_std_math_sqrt_f32(float x)
{
    return sqrtf(x);
}

double __mlang_std_math_sqrt_f64(double x)
{
    return sqrt(x);
}

float __mlang_std_math_hypot_f32(float x, float y)
{
    return hypotf(x, y);
}

double __mlang_std_math_hypot_f64(double x, double y)
{
    return hypot(x, y);
}

float __mlang_std_math_fma_f32(float x, float y, float z)
{
    return fmaf(x, y, z);
}

double __mlang_std_math_fma_f64(double x, double y, double z)
{
    return fma(x, y, z);
}

float __mlang_std_math_nextafter_f32(float x, float y)
{
    return nextafterf(x, y);
}

double __mlang_std_math_nextafter_f64(double x, double y)
{
    return nextafter(x, y);
}

int32_t __mlang_std_math_isfinite_f32(float x) { return isfinite(x); }
int32_t __mlang_std_math_isfinite_f64(double x) { return isfinite(x); }
int32_t __mlang_std_math_isinf_f32(float x) { return isinf(x); }
int32_t __mlang_std_math_isinf_f64(double x) { return isinf(x); }
int32_t __mlang_std_math_isnan_f32(float x) { return isnan(x); }
int32_t __mlang_std_math_isnan_f64(double x) { return isnan(x); }
int32_t __mlang_std_math_signbit_f32(float x) { return signbit(x); }
int32_t __mlang_std_math_signbit_f64(double x) { return signbit(x); }

float __mlang_std_math_atan2_f32(float y, float x)
{
    return atan2f(y, x);
}

double __mlang_std_math_atan2_f64(double y, double x)
{
    return atan2(y, x);
}

float __mlang_std_math_sinh_f32(float x) { return sinhf(x); }
double __mlang_std_math_sinh_f64(double x) { return sinh(x); }
float __mlang_std_math_cosh_f32(float x) { return coshf(x); }
double __mlang_std_math_cosh_f64(double x) { return cosh(x); }

int32_t __mlang_std_math_sin_i32(int32_t x)
{
    return (int32_t)sin((double)x);
}

float __mlang_std_math_sin_f32(float x)
{
    return sinf(x);
}

double __mlang_std_math_sin_f64(double x)
{
    return sin(x);
}

int32_t __mlang_std_math_cos_i32(int32_t x)
{
    return (int32_t)cos((double)x);
}

float __mlang_std_math_cos_f32(float x)
{
    return cosf(x);
}

double __mlang_std_math_cos_f64(double x)
{
    return cos(x);
}

int32_t __mlang_std_math_tan_i32(int32_t x)
{
    return (int32_t)tan((double)x);
}

float __mlang_std_math_tan_f32(float x)
{
    return tanf(x);
}

double __mlang_std_math_tan_f64(double x)
{
    return tan(x);
}

int32_t __mlang_std_math_floor_i32(int32_t x)
{
    return x;
}

float __mlang_std_math_floor_f32(float x)
{
    return floorf(x);
}

double __mlang_std_math_floor_f64(double x)
{
    return floor(x);
}

int32_t __mlang_std_math_ceil_i32(int32_t x)
{
    return x;
}

float __mlang_std_math_ceil_f32(float x)
{
    return ceilf(x);
}

double __mlang_std_math_ceil_f64(double x)
{
    return ceil(x);
}

int32_t __mlang_std_math_round_i32(int32_t x)
{
    return x;
}

float __mlang_std_math_round_f32(float x)
{
    return roundf(x);
}

double __mlang_std_math_round_f64(double x)
{
    return round(x);
}

int32_t __mlang_std_math_log_i32(int32_t x)
{
    if(x <= 0)
        return 0;
    return (int32_t)log((double)x);
}

float __mlang_std_math_log_f32(float x)
{
    return logf(x);
}

double __mlang_std_math_log_f64(double x)
{
    return log(x);
}

int32_t __mlang_std_math_exp_i32(int32_t x)
{
    return (int32_t)exp((double)x);
}

float __mlang_std_math_exp_f32(float x)
{
    return expf(x);
}

double __mlang_std_math_exp_f64(double x)
{
    return exp(x);
}

int32_t __mlang_std_math_modulo_i32(int32_t a, int32_t b)
{
    return b == 0 ? 0 : (a % b);
}

float __mlang_std_math_modulo_f32(float a, float b)
{
    return fmodf(a, b);
}

double __mlang_std_math_modulo_f64(double a, double b)
{
    return fmod(a, b);
}
