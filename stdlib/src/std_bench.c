#include <stdint.h>

/**
 * @file std_bench.c
 * @brief Benchmark helpers to reduce dead-code elimination.
 */

static volatile int64_t g_sink_i64 = 0;
static volatile int32_t g_sink_i32 = 0;

void __mlang_std_bench_do_not_optimize_i64(int64_t v)
{
    g_sink_i64 = v;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r,m"(g_sink_i64) : "memory");
#endif
}

void __mlang_std_bench_do_not_optimize_i32(int32_t v)
{
    g_sink_i32 = v;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r,m"(g_sink_i32) : "memory");
#endif
}

void __mlang_std_bench_clobber_memory(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : : "memory");
#endif
}
