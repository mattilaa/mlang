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
