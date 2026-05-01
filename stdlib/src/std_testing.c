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
static char** g_testing_failed_tests = NULL;
static int32_t g_testing_failed_tests_len = 0;
static int32_t g_testing_failed_tests_cap = 0;

static void testing_set_current_name(const char* name);
static void testing_record_failed_test(const char* name);
static void testing_clear_failed_tests(void);

/* Cardinality modes for EXPECT_CALL-style expectations.
 * Kept stable since it crosses the mlang/C ABI boundary. */
enum
{
    MOCK_CARD_NONE = 0,
    MOCK_CARD_EXACT = 1,
    MOCK_CARD_AT_LEAST = 2,
    MOCK_CARD_AT_MOST = 3,
    MOCK_CARD_NEVER = 4
};

/* Tagged union for programmed return values queued via will_return_*. */
enum
{
    MOCK_RV_NONE = 0,
    MOCK_RV_I32 = 1,
    MOCK_RV_I64 = 2,
    MOCK_RV_BOOL = 3,
    MOCK_RV_STR8 = 4,
    MOCK_RV_F32 = 5,
    MOCK_RV_F64 = 6
};

typedef struct
{
    int32_t kind;
    int32_t i32_val;
    int64_t i64_val;
    int32_t bool_val;
    char* str8_val;
    float f32_val;
    double f64_val;
} testing_mock_rv_t;

typedef struct
{
    char* name;
    int32_t expected_calls;
    int32_t actual_calls;
    int32_t has_expectation;
    int32_t cardinality;
    testing_mock_rv_t* return_queue;
    int32_t return_queue_len;
    int32_t return_queue_cap;
    int32_t return_queue_pos;
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

    if(strcmp(st, "FAIL") == 0)
        testing_record_failed_test(name);

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
    for(int32_t i = 0; i < g_testing_failed_tests_len; ++i)
    {
        const char* name = g_testing_failed_tests[i] ? g_testing_failed_tests[i]
                                                     : "";
        fprintf(stdout, "%s [SUMMARY-FAIL] %s\n", timestamp, name);
    }
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

static void testing_clear_failed_tests(void)
{
    for(int32_t i = 0; i < g_testing_failed_tests_len; ++i)
    {
        free(g_testing_failed_tests[i]);
        g_testing_failed_tests[i] = NULL;
    }
    free(g_testing_failed_tests);
    g_testing_failed_tests = NULL;
    g_testing_failed_tests_len = 0;
    g_testing_failed_tests_cap = 0;
}

static void testing_record_failed_test(const char* name)
{
    const char* src = name ? name : "";
    if(src[0] == '\0')
        return;

    for(int32_t i = 0; i < g_testing_failed_tests_len; ++i)
    {
        const char* existing = g_testing_failed_tests[i] ? g_testing_failed_tests[i]
                                                         : "";
        if(strcmp(existing, src) == 0)
            return;
    }

    if(g_testing_failed_tests_len >= g_testing_failed_tests_cap)
    {
        int32_t next = (g_testing_failed_tests_cap == 0)
                           ? 4
                           : (g_testing_failed_tests_cap * 2);
        char** grown = (char**)realloc(g_testing_failed_tests,
                                       (size_t)next * sizeof(*grown));
        if(!grown)
            return;
        g_testing_failed_tests = grown;
        g_testing_failed_tests_cap = next;
    }

    char* dup = testing_strdup(src);
    if(!dup)
        return;
    g_testing_failed_tests[g_testing_failed_tests_len++] = dup;
}

static void testing_mock_free_return_queue(testing_mock_entry_t* entry)
{
    if(!entry || !entry->return_queue)
        return;
    for(int32_t i = 0; i < entry->return_queue_len; ++i)
    {
        if(entry->return_queue[i].kind == MOCK_RV_STR8)
        {
            free(entry->return_queue[i].str8_val);
            entry->return_queue[i].str8_val = NULL;
        }
    }
    free(entry->return_queue);
    entry->return_queue = NULL;
    entry->return_queue_len = 0;
    entry->return_queue_cap = 0;
    entry->return_queue_pos = 0;
}

static void testing_mock_clear_entries(testing_mock_t* mock)
{
    if(!mock || !mock->entries)
        return;
    for(int32_t i = 0; i < mock->len; ++i)
    {
        free(mock->entries[i].name);
        mock->entries[i].name = NULL;
        testing_mock_free_return_queue(&mock->entries[i]);
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
    out->cardinality = MOCK_CARD_NONE;
    out->return_queue = NULL;
    out->return_queue_len = 0;
    out->return_queue_cap = 0;
    out->return_queue_pos = 0;
    return out;
}

static testing_mock_rv_t*
testing_mock_push_rv_slot(testing_mock_entry_t* entry)
{
    if(!entry)
        return NULL;
    if(entry->return_queue_len >= entry->return_queue_cap)
    {
        int32_t next = (entry->return_queue_cap == 0)
                           ? 4
                           : (entry->return_queue_cap * 2);
        testing_mock_rv_t* grown = (testing_mock_rv_t*)realloc(
            entry->return_queue, (size_t)next * sizeof(*grown));
        if(!grown)
            return NULL;
        entry->return_queue = grown;
        entry->return_queue_cap = next;
    }
    testing_mock_rv_t* slot = &entry->return_queue[entry->return_queue_len++];
    slot->kind = MOCK_RV_NONE;
    slot->i32_val = 0;
    slot->i64_val = 0;
    slot->bool_val = 0;
    slot->str8_val = NULL;
    slot->f32_val = 0.0f;
    slot->f64_val = 0.0;
    return slot;
}

static const char* testing_mock_card_label(int32_t cardinality)
{
    switch(cardinality)
    {
        case MOCK_CARD_EXACT:
            return "expected";
        case MOCK_CARD_AT_LEAST:
            return "expected at least";
        case MOCK_CARD_AT_MOST:
            return "expected at most";
        case MOCK_CARD_NEVER:
            return "expected (never)";
        default:
            return "expected";
    }
}

static int32_t testing_mock_cardinality_holds(int32_t cardinality,
                                              int32_t expected,
                                              int32_t actual)
{
    switch(cardinality)
    {
        case MOCK_CARD_EXACT:
            return actual == expected;
        case MOCK_CARD_AT_LEAST:
            return actual >= expected;
        case MOCK_CARD_AT_MOST:
            return actual <= expected;
        case MOCK_CARD_NEVER:
            return actual == 0;
        default:
            return 1;
    }
}

void __mlang_std_testing_reset(void)
{
    g_testing_checks = 0;
    g_testing_failures = 0;
    g_testing_quiet = 0;
    g_testing_current_index = 0;
    g_testing_current_total = 0;
    testing_set_current_name("");
    testing_clear_failed_tests();
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
    entry->cardinality = MOCK_CARD_EXACT;
}

void __mlang_std_testing_mock_expect_at_least(int64_t handle, const char* name,
                                              int32_t n)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    if(!entry)
        return;
    entry->expected_calls = n;
    entry->has_expectation = 1;
    entry->cardinality = MOCK_CARD_AT_LEAST;
}

void __mlang_std_testing_mock_expect_at_most(int64_t handle, const char* name,
                                             int32_t n)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    if(!entry)
        return;
    entry->expected_calls = n;
    entry->has_expectation = 1;
    entry->cardinality = MOCK_CARD_AT_MOST;
}

void __mlang_std_testing_mock_expect_never(int64_t handle, const char* name)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    if(!entry)
        return;
    entry->expected_calls = 0;
    entry->has_expectation = 1;
    entry->cardinality = MOCK_CARD_NEVER;
}

void __mlang_std_testing_mock_will_return_i32(int64_t handle, const char* name,
                                              int32_t value)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    testing_mock_rv_t* slot = testing_mock_push_rv_slot(entry);
    if(!slot)
        return;
    slot->kind = MOCK_RV_I32;
    slot->i32_val = value;
}

void __mlang_std_testing_mock_will_return_i64(int64_t handle, const char* name,
                                              int64_t value)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    testing_mock_rv_t* slot = testing_mock_push_rv_slot(entry);
    if(!slot)
        return;
    slot->kind = MOCK_RV_I64;
    slot->i64_val = value;
}

void __mlang_std_testing_mock_will_return_bool(int64_t handle, const char* name,
                                               int32_t value)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    testing_mock_rv_t* slot = testing_mock_push_rv_slot(entry);
    if(!slot)
        return;
    slot->kind = MOCK_RV_BOOL;
    slot->bool_val = value ? 1 : 0;
}

void __mlang_std_testing_mock_will_return_str8(int64_t handle, const char* name,
                                               const char* value)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    testing_mock_rv_t* slot = testing_mock_push_rv_slot(entry);
    if(!slot)
        return;
    slot->kind = MOCK_RV_STR8;
    slot->str8_val = testing_strdup(value ? value : "");
}

void __mlang_std_testing_mock_will_return_f32(int64_t handle, const char* name,
                                              float value)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    testing_mock_rv_t* slot = testing_mock_push_rv_slot(entry);
    if(!slot)
        return;
    slot->kind = MOCK_RV_F32;
    slot->f32_val = value;
}

void __mlang_std_testing_mock_will_return_f64(int64_t handle, const char* name,
                                              double value)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    testing_mock_rv_t* slot = testing_mock_push_rv_slot(entry);
    if(!slot)
        return;
    slot->kind = MOCK_RV_F64;
    slot->f64_val = value;
}

static testing_mock_rv_t* testing_mock_consume_rv(int64_t handle,
                                                  const char* name)
{
    testing_mock_t* mock = testing_find_mock(handle);
    if(!mock)
        return NULL;
    testing_mock_entry_t* entry = testing_get_or_create_entry(mock, name);
    if(!entry)
        return NULL;
    ++entry->actual_calls;
    if(entry->return_queue_pos < entry->return_queue_len)
        return &entry->return_queue[entry->return_queue_pos++];
    return NULL;
}

int32_t __mlang_std_testing_mock_record_and_return_i32(int64_t handle,
                                                       const char* name,
                                                       int32_t default_value)
{
    testing_mock_rv_t* slot = testing_mock_consume_rv(handle, name);
    if(!slot || slot->kind != MOCK_RV_I32)
        return default_value;
    return slot->i32_val;
}

int64_t __mlang_std_testing_mock_record_and_return_i64(int64_t handle,
                                                       const char* name,
                                                       int64_t default_value)
{
    testing_mock_rv_t* slot = testing_mock_consume_rv(handle, name);
    if(!slot || slot->kind != MOCK_RV_I64)
        return default_value;
    return slot->i64_val;
}

int32_t __mlang_std_testing_mock_record_and_return_bool(int64_t handle,
                                                        const char* name,
                                                        int32_t default_value)
{
    testing_mock_rv_t* slot = testing_mock_consume_rv(handle, name);
    if(!slot || slot->kind != MOCK_RV_BOOL)
        return default_value ? 1 : 0;
    return slot->bool_val ? 1 : 0;
}

const char*
__mlang_std_testing_mock_record_and_return_str8(int64_t handle,
                                                const char* name,
                                                const char* default_value)
{
    testing_mock_rv_t* slot = testing_mock_consume_rv(handle, name);
    if(!slot || slot->kind != MOCK_RV_STR8)
        return default_value ? default_value : "";
    return slot->str8_val ? slot->str8_val : "";
}

float __mlang_std_testing_mock_record_and_return_f32(int64_t handle,
                                                     const char* name,
                                                     float default_value)
{
    testing_mock_rv_t* slot = testing_mock_consume_rv(handle, name);
    if(!slot || slot->kind != MOCK_RV_F32)
        return default_value;
    return slot->f32_val;
}

double __mlang_std_testing_mock_record_and_return_f64(int64_t handle,
                                                     const char* name,
                                                     double default_value)
{
    testing_mock_rv_t* slot = testing_mock_consume_rv(handle, name);
    if(!slot || slot->kind != MOCK_RV_F64)
        return default_value;
    return slot->f64_val;
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
        int32_t cardinality =
            e->cardinality == 0 ? MOCK_CARD_EXACT : e->cardinality;
        if(testing_mock_cardinality_holds(cardinality, e->expected_calls,
                                          e->actual_calls))
            continue;
        ok = 0;
        ++g_testing_failures;
        if(!g_testing_quiet)
        {
            fprintf(stderr,
                    "[  FAILED  ] mock_expect_call('%s'): %s=%d actual=%d\n",
                    e->name ? e->name : "",
                    testing_mock_card_label(cardinality), e->expected_calls,
                    e->actual_calls);
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
