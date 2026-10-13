#include <float.h>
#include <math.h>

/* Avoid overflow when summing large values, while retaining the extra
 * precision of summing values that are safely below half the type's maximum.
 * Equal infinities naturally return themselves; opposite infinities produce
 * NaN, as the mathematical midpoint is indeterminate. */
float __mlang_std_midpoint_f32(float first, float second)
{
    if (first == second)
        return first;

    if (fabsf(first) <= FLT_MAX / 2.0f &&
        fabsf(second) <= FLT_MAX / 2.0f)
        return (first + second) / 2.0f;

    return first / 2.0f + second / 2.0f;
}

double __mlang_std_midpoint_f64(double first, double second)
{
    if (first == second)
        return first;

    if (fabs(first) <= DBL_MAX / 2.0 && fabs(second) <= DBL_MAX / 2.0)
        return (first + second) / 2.0;

    return first / 2.0 + second / 2.0;
}

/* C++20 std::lerp floating-point semantics. Split the computation by the
 * endpoint signs so finite endpoints don't overflow during interpolation. */
float __mlang_std_lerp_f32(float first, float second, float amount)
{
    if (amount == 0.0f)
        return first;
    if (amount == 1.0f)
        return second;
    if (first == second)
        return first;

    if ((first <= 0.0f && second >= 0.0f) ||
        (first >= 0.0f && second <= 0.0f))
        return amount * second + (1.0f - amount) * first;

    if (amount < 1.0f)
        return first + amount * (second - first);
    return second + (amount - 1.0f) * (second - first);
}

double __mlang_std_lerp_f64(double first, double second, double amount)
{
    if (amount == 0.0)
        return first;
    if (amount == 1.0)
        return second;
    if (first == second)
        return first;

    if ((first <= 0.0 && second >= 0.0) ||
        (first >= 0.0 && second <= 0.0))
        return amount * second + (1.0 - amount) * first;

    if (amount < 1.0)
        return first + amount * (second - first);
    return second + (amount - 1.0) * (second - first);
}
