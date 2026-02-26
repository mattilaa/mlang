#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t g_testing_checks = 0;
static int32_t g_testing_failures = 0;

void __mlang_std_testing_reset(void)
{
    g_testing_checks = 0;
    g_testing_failures = 0;
}

int32_t __mlang_std_testing_checks(void)
{
    return g_testing_checks;
}

int32_t __mlang_std_testing_failures(void)
{
    return g_testing_failures;
}

int32_t __mlang_std_testing_result(void)
{
    return g_testing_failures == 0 ? 0 : 1;
}

static void testing_fail(const char* label)
{
    ++g_testing_failures;
    fprintf(stderr, "[  FAILED  ] %s\n", label);
}

void __mlang_std_testing_expect_true(int32_t cond)
{
    ++g_testing_checks;
    if(cond)
        return;
    testing_fail("expect_true");
}

void __mlang_std_testing_expect_false(int32_t cond)
{
    ++g_testing_checks;
    if(!cond)
        return;
    testing_fail("expect_false");
}

void __mlang_std_testing_expect_eq_i32(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] expect_eq(i32): expected=%d actual=%d\n",
            expected, actual);
}

void __mlang_std_testing_expect_eq_i64(int64_t expected, int64_t actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] expect_eq(i64): expected=%lld actual=%lld\n",
            (long long)expected, (long long)actual);
}

void __mlang_std_testing_expect_eq_bool(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    expected = expected ? 1 : 0;
    actual = actual ? 1 : 0;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] expect_eq(bool): expected=%d actual=%d\n",
            expected, actual);
}

void __mlang_std_testing_expect_eq_string(const char* expected,
                                          const char* actual)
{
    ++g_testing_checks;
    const char* exp = expected ? expected : "";
    const char* act = actual ? actual : "";
    if(strcmp(exp, act) == 0)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] expect_eq(string): expected='%s' actual='%s'\n",
            exp, act);
}

void __mlang_std_testing_expect_eq_f32(float expected, float actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] expect_eq(f32): expected=%f actual=%f\n",
            expected, actual);
}

void __mlang_std_testing_expect_eq_f64(double expected, double actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] expect_eq(f64): expected=%f actual=%f\n",
            expected, actual);
}

void __mlang_std_testing_verify_true(int32_t cond)
{
    ++g_testing_checks;
    if(cond)
        return;
    testing_fail("verify_true");
    abort();
}

void __mlang_std_testing_verify_false(int32_t cond)
{
    ++g_testing_checks;
    if(!cond)
        return;
    testing_fail("verify_false");
    abort();
}

void __mlang_std_testing_verify_eq_i32(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_eq(i32): expected=%d actual=%d\n",
            expected, actual);
    abort();
}

void __mlang_std_testing_verify_eq_i64(int64_t expected, int64_t actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_eq(i64): expected=%lld actual=%lld\n",
            (long long)expected, (long long)actual);
    abort();
}

void __mlang_std_testing_verify_eq_bool(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    expected = expected ? 1 : 0;
    actual = actual ? 1 : 0;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_eq(bool): expected=%d actual=%d\n",
            expected, actual);
    abort();
}

void __mlang_std_testing_verify_eq_string(const char* expected,
                                          const char* actual)
{
    ++g_testing_checks;
    const char* exp = expected ? expected : "";
    const char* act = actual ? actual : "";
    if(strcmp(exp, act) == 0)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_eq(string): expected='%s' actual='%s'\n",
            exp, act);
    abort();
}

void __mlang_std_testing_verify_eq_f32(float expected, float actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_eq(f32): expected=%f actual=%f\n",
            expected, actual);
    abort();
}

void __mlang_std_testing_verify_eq_f64(double expected, double actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_eq(f64): expected=%f actual=%f\n",
            expected, actual);
    abort();
}
