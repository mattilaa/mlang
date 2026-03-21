#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static int32_t g_testing_checks = 0;
static int32_t g_testing_failures = 0;
static int32_t g_testing_quiet = 0;
static int32_t g_testing_current_index = 0;
static int32_t g_testing_current_total = 0;
static char* g_testing_current_name = NULL;

static void testing_set_current_name(const char* name);

typedef struct
{
    char* name;
    int32_t expected_calls;
    int32_t actual_calls;
    int32_t has_expectation;
} testing_mock_entry_t;

typedef struct
{
    int64_t handle;
    testing_mock_entry_t* entries;
    int32_t len;
    int32_t cap;
} testing_mock_t;

static testing_mock_t* g_testing_mocks = NULL;
static int32_t g_testing_mocks_len = 0;
static int32_t g_testing_mocks_cap = 0;
static int64_t g_testing_next_mock_handle = 1;

static int testing_digits_i32(int32_t value)
{
    int digits = 1;
    while(value >= 10)
    {
        value /= 10;
        ++digits;
    }
    return digits;
}

static void testing_timestamp_now(char* out, size_t outSize)
{
    if(!out || outSize == 0)
        return;

    struct timeval tv;
    gettimeofday(&tv, NULL);

    time_t secs = (time_t)tv.tv_sec;
    struct tm tmNow;
#if defined(_WIN32)
    localtime_s(&tmNow, &secs);
#else
    localtime_r(&secs, &tmNow);
#endif

    snprintf(out, outSize, "%02d/%02d/%04d %02d:%02d:%02d:%03d",
             tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_year + 1900,
             tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec,
             (int)(tv.tv_usec / 1000));
}

void __mlang_std_testing_report_test(int32_t index, int32_t total,
                                     const char* status,
                                     const char* test_name, int32_t rc)
{
    char timestamp[32];
    testing_timestamp_now(timestamp, sizeof(timestamp));

    int digits = testing_digits_i32(total > 0 ? total : index);
    const char* st = status ? status : "UNKNOWN";
    const char* name = test_name ? test_name : "";

    if(total > 0)
    {
        if(strcmp(st, "FAIL") == 0)
        {
            fprintf(stdout, "[%*d/%d] %s [%s] %s rc=%d\n", digits, index, total,
                    timestamp, st, name, rc);
        }
        else
        {
            fprintf(stdout, "[%*d/%d] %s [%s] %s\n", digits, index, total,
                    timestamp, st, name);
        }
        return;
    }

    if(strcmp(st, "FAIL") == 0)
        fprintf(stdout, "[%*d] %s [%s] %s rc=%d\n", digits, index, timestamp,
                st, name, rc);
    else
        fprintf(stdout, "[%*d] %s [%s] %s\n", digits, index, timestamp, st,
                name);
}

void __mlang_std_testing_report_summary(int32_t total, int32_t pass,
                                        int32_t fail)
{
    char timestamp[32];
    testing_timestamp_now(timestamp, sizeof(timestamp));
    fprintf(stdout, "%s [SUMMARY] total=%d pass=%d fail=%d\n", timestamp, total,
            pass, fail);
}

void __mlang_std_testing_set_current_test(int32_t index, int32_t total,
                                          const char* test_name)
{
    g_testing_current_index = index;
    g_testing_current_total = total;
    testing_set_current_name(test_name);
}

static char* testing_strdup(const char* s)
{
    const char* src = s ? s : "";
    size_t n = strlen(src);
    char* out = (char*)malloc(n + 1u);
    if(!out)
        return NULL;
    memcpy(out, src, n + 1u);
    return out;
}

static void testing_set_current_name(const char* name)
{
    free(g_testing_current_name);
    g_testing_current_name = testing_strdup(name ? name : "");
}

static void testing_mock_clear_entries(testing_mock_t* mock)
{
    if(!mock || !mock->entries)
        return;
    for(int32_t i = 0; i < mock->len; ++i)
    {
        free(mock->entries[i].name);
        mock->entries[i].name = NULL;
    }
    mock->len = 0;
}

static testing_mock_t* testing_find_mock(int64_t handle)
{
    for(int32_t i = 0; i < g_testing_mocks_len; ++i)
    {
        if(g_testing_mocks[i].handle == handle)
            return &g_testing_mocks[i];
    }
    return NULL;
}

static testing_mock_entry_t* testing_find_entry(testing_mock_t* mock,
                                                const char* name)
{
    if(!mock)
        return NULL;
    const char* needle = name ? name : "";
    for(int32_t i = 0; i < mock->len; ++i)
    {
        if(strcmp(mock->entries[i].name ? mock->entries[i].name : "", needle) ==
           0)
        {
            return &mock->entries[i];
        }
    }
    return NULL;
}

static testing_mock_entry_t* testing_get_or_create_entry(testing_mock_t* mock,
                                                         const char* name)
{
    testing_mock_entry_t* existing = testing_find_entry(mock, name);
    if(existing)
        return existing;

    if(mock->len >= mock->cap)
    {
        int32_t next = (mock->cap == 0) ? 4 : (mock->cap * 2);
        testing_mock_entry_t* grown =
            (testing_mock_entry_t*)realloc(mock->entries,
                                           (size_t)next * sizeof(*grown));
        if(!grown)
            return NULL;
        mock->entries = grown;
        mock->cap = next;
    }

    testing_mock_entry_t* out = &mock->entries[mock->len++];
    out->name = testing_strdup(name);
    if(!out->name)
    {
        --mock->len;
        return NULL;
    }
    out->expected_calls = 0;
    out->actual_calls = 0;
    out->has_expectation = 0;
    return out;
}

void __mlang_std_testing_reset(void)
{
    g_testing_checks = 0;
    g_testing_failures = 0;
    g_testing_quiet = 0;
    g_testing_current_index = 0;
    g_testing_current_total = 0;
    testing_set_current_name("");
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

void __mlang_std_testing_set_quiet(int32_t enabled)
{
    g_testing_quiet = enabled ? 1 : 0;
}

int64_t __mlang_std_testing_mock_create(void)
{
    if(g_testing_mocks_len >= g_testing_mocks_cap)
    {
        int32_t next = (g_testing_mocks_cap == 0) ? 4 : (g_testing_mocks_cap * 2);
        testing_mock_t* grown = (testing_mock_t*)realloc(
            g_testing_mocks, (size_t)next * sizeof(*grown));
        if(!grown)
            return 0;
        g_testing_mocks = grown;
        g_testing_mocks_cap = next;
    }

    testing_mock_t* mock = &g_testing_mocks[g_testing_mocks_len++];
    mock->handle = g_testing_next_mock_handle++;
    mock->entries = NULL;
    mock->len = 0;
    mock->cap = 0;
    return mock->handle;
}

void __mlang_std_testing_mock_destroy(int64_t handle)
{
    for(int32_t i = 0; i < g_testing_mocks_len; ++i)
    {
        if(g_testing_mocks[i].handle != handle)
            continue;

        testing_mock_clear_entries(&g_testing_mocks[i]);
        free(g_testing_mocks[i].entries);
        g_testing_mocks[i].entries = NULL;
        g_testing_mocks[i].cap = 0;

        if(i != g_testing_mocks_len - 1)
            g_testing_mocks[i] = g_testing_mocks[g_testing_mocks_len - 1];
        --g_testing_mocks_len;
        return;
    }
}

void __mlang_std_testing_mock_reset(int64_t handle)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_clear_entries(mock);
}

void __mlang_std_testing_mock_expect_called(int64_t handle, const char* name,
                                            int32_t expected_calls)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    if(!entry)
        return;
    entry->expected_calls = expected_calls;
    entry->has_expectation = 1;
}

void __mlang_std_testing_mock_called(int64_t handle, const char* name)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    if(!entry)
        return;
    ++entry->actual_calls;
}

int32_t __mlang_std_testing_mock_actual_calls(int64_t handle, const char* name)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return 0;
    testing_mock_entry_t* entry = testing_find_entry(mock, name);
    if(!entry)
        return 0;
    return entry->actual_calls;
}

int32_t __mlang_std_testing_mock_verify(int64_t handle)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return 0;

    int32_t ok = 1;
    for(int32_t i = 0; i < mock->len; ++i)
    {
        testing_mock_entry_t* e = &mock->entries[i];
        if(!e->has_expectation)
            continue;
        ++g_testing_checks;
        if(e->actual_calls == e->expected_calls)
            continue;
        ok = 0;
        ++g_testing_failures;
        if(!g_testing_quiet)
        {
            fprintf(stderr,
                    "[  FAILED  ] mock_expect_call('%s'): expected=%d actual=%d\n",
                    e->name ? e->name : "", e->expected_calls, e->actual_calls);
        }
    }
    return ok;
}

static void testing_fail(const char* label)
{
    ++g_testing_failures;
    if(!g_testing_quiet)
        fprintf(stderr, "[  FAILED  ] %s\n", label);
}

static void testing_report_fatal_detail(const char* detail)
{
    char timestamp[32];
    int digits = testing_digits_i32(g_testing_current_total > 0
                                        ? g_testing_current_total
                                        : g_testing_current_index);
    const char* name = g_testing_current_name ? g_testing_current_name : "";
    const char* msg = detail ? detail : "verify failure";

    testing_timestamp_now(timestamp, sizeof(timestamp));
    if(g_testing_current_total > 0 && g_testing_current_index > 0)
    {
        fprintf(stdout, "[%*d/%d] %s [FAIL] %s - %s\n", digits,
                g_testing_current_index, g_testing_current_total, timestamp,
                name, msg);
    }
    else
    {
        fprintf(stdout, "%s [FAIL] %s - %s\n", timestamp, name, msg);
    }
    fflush(stdout);
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
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_eq(i32): expected=%d actual=%d\n",
                expected, actual);
    }
}

void __mlang_std_testing_expect_not_eq_i32(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    if(expected != actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_not_eq(i32): left=%d right=%d (values are equal)\n",
                expected, actual);
    }
}

void __mlang_std_testing_expect_eq_i64(int64_t expected, int64_t actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_eq(i64): expected=%lld actual=%lld\n",
                (long long)expected, (long long)actual);
    }
}

void __mlang_std_testing_expect_not_eq_i64(int64_t expected, int64_t actual)
{
    ++g_testing_checks;
    if(expected != actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_not_eq(i64): left=%lld right=%lld (values are equal)\n",
                (long long)expected, (long long)actual);
    }
}

void __mlang_std_testing_expect_eq_bool(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    expected = expected ? 1 : 0;
    actual = actual ? 1 : 0;
    if(expected == actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_eq(bool): expected=%d actual=%d\n",
                expected, actual);
    }
}

void __mlang_std_testing_expect_not_eq_bool(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    expected = expected ? 1 : 0;
    actual = actual ? 1 : 0;
    if(expected != actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_not_eq(bool): left=%d right=%d (values are equal)\n",
                expected, actual);
    }
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
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_eq(string): expected='%s' actual='%s'\n",
                exp, act);
    }
}

void __mlang_std_testing_expect_not_eq_string(const char* expected,
                                              const char* actual)
{
    ++g_testing_checks;
    const char* exp = expected ? expected : "";
    const char* act = actual ? actual : "";
    if(strcmp(exp, act) != 0)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_not_eq(string): left='%s' right='%s' (values are equal)\n",
                exp, act);
    }
}

void __mlang_std_testing_expect_eq_f32(float expected, float actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_eq(f32): expected=%f actual=%f\n",
                expected, actual);
    }
}

void __mlang_std_testing_expect_not_eq_f32(float expected, float actual)
{
    ++g_testing_checks;
    if(expected != actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_not_eq(f32): left=%f right=%f (values are equal)\n",
                expected, actual);
    }
}

void __mlang_std_testing_expect_eq_f64(double expected, double actual)
{
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_eq(f64): expected=%f actual=%f\n",
                expected, actual);
    }
}

void __mlang_std_testing_expect_not_eq_f64(double expected, double actual)
{
    ++g_testing_checks;
    if(expected != actual)
        return;
    ++g_testing_failures;
    if(!g_testing_quiet)
    {
        fprintf(stderr,
                "[  FAILED  ] expect_not_eq(f64): left=%f right=%f (values are equal)\n",
                expected, actual);
    }
}

void __mlang_std_testing_verify_true(int32_t cond)
{
    ++g_testing_checks;
    if(cond)
        return;
    testing_report_fatal_detail("verify_true");
    testing_fail("verify_true");
    abort();
}

void __mlang_std_testing_verify_false(int32_t cond)
{
    ++g_testing_checks;
    if(!cond)
        return;
    testing_report_fatal_detail("verify_false");
    testing_fail("verify_false");
    abort();
}

void __mlang_std_testing_verify_eq_i32(int32_t expected, int32_t actual)
{
    char detail[128];
    ++g_testing_checks;
    if(expected == actual)
        return;
    ++g_testing_failures;
    snprintf(detail, sizeof(detail), "verify_eq(i32): expected=%d actual=%d",
             expected, actual);
    testing_report_fatal_detail(detail);
    fprintf(stderr,
            "[  FAILED  ] verify_eq(i32): expected=%d actual=%d\n",
            expected, actual);
    abort();
}

void __mlang_std_testing_verify_not_eq_i32(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    if(expected != actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_not_eq(i32): left=%d right=%d (values are equal)\n",
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

void __mlang_std_testing_verify_not_eq_i64(int64_t expected, int64_t actual)
{
    ++g_testing_checks;
    if(expected != actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_not_eq(i64): left=%lld right=%lld (values are equal)\n",
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

void __mlang_std_testing_verify_not_eq_bool(int32_t expected, int32_t actual)
{
    ++g_testing_checks;
    expected = expected ? 1 : 0;
    actual = actual ? 1 : 0;
    if(expected != actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_not_eq(bool): left=%d right=%d (values are equal)\n",
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

void __mlang_std_testing_verify_not_eq_string(const char* expected,
                                              const char* actual)
{
    ++g_testing_checks;
    const char* exp = expected ? expected : "";
    const char* act = actual ? actual : "";
    if(strcmp(exp, act) != 0)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_not_eq(string): left='%s' right='%s' (values are equal)\n",
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

void __mlang_std_testing_verify_not_eq_f32(float expected, float actual)
{
    ++g_testing_checks;
    if(expected != actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_not_eq(f32): left=%f right=%f (values are equal)\n",
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

void __mlang_std_testing_verify_not_eq_f64(double expected, double actual)
{
    ++g_testing_checks;
    if(expected != actual)
        return;
    ++g_testing_failures;
    fprintf(stderr,
            "[  FAILED  ] verify_not_eq(f64): left=%f right=%f (values are equal)\n",
            expected, actual);
    abort();
}
