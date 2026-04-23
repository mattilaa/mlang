#ifdef _MSC_VER
  #include <intrin.h>
  #include <stdint.h>
  /* MSVC C mode lacks <stdatomic.h>; use Interlocked intrinsics instead. */
  typedef volatile uint64_t atomic_uint_fast64_t_msvc;
  #define atomic_load(p) (*(p))
  #define atomic_store(p, v) (*(p) = (v))
  #define atomic_compare_exchange_weak(p, expected, desired) \
      (_InterlockedCompareExchange64((volatile long long*)(p), \
          (long long)(desired), (long long)(*(expected))) == (long long)(*(expected)) \
       ? 1 : (*(expected) = *(p), 0))
  #define ATOMIC_VAR_INIT(x) (x)
  typedef atomic_uint_fast64_t_msvc atomic_uint_fast64_t;
#else
  #include <stdatomic.h>
#endif
#include <stdint.h>

static atomic_uint_fast64_t g_mlang_rand_state = UINT64_C(0x9E3779B97F4A7C15);

static uint64_t normalize_seed(uint64_t seed)
{
    if(seed == 0)
        return UINT64_C(0xA0761D6478BD642F);
    return seed;
}

static uint64_t next_u64_internal(void)
{
    uint64_t cur = atomic_load(&g_mlang_rand_state);
    for(;;)
    {
        uint64_t s = normalize_seed(cur);

        // xorshift64* core
        uint64_t x = s;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;

        uint64_t next_state = normalize_seed(x);
        if(atomic_compare_exchange_weak(&g_mlang_rand_state, &cur, next_state))
            return x * UINT64_C(2685821657736338717);
    }
}

void __mlang_std_rand_seed_i64(int64_t seed)
{
    atomic_store(&g_mlang_rand_state, normalize_seed((uint64_t)seed));
}

uint64_t __mlang_std_rand_next_u64(void)
{
    return next_u64_internal();
}

int64_t __mlang_std_rand_next_i64(void)
{
    return (int64_t)next_u64_internal();
}

int64_t __mlang_std_rand_range_i64(int64_t min_value, int64_t max_value)
{
    if(min_value > max_value)
    {
        int64_t t = min_value;
        min_value = max_value;
        max_value = t;
    }

    uint64_t span = (uint64_t)((max_value - min_value) + 1);
    if(span == 0)
        return (int64_t)next_u64_internal();

    uint64_t r = next_u64_internal();
    return min_value + (int64_t)(r % span);
}

double __mlang_std_rand_next_f64(void)
{
    // 53 random bits mapped to [0, 1)
    uint64_t r = next_u64_internal() >> 11;
    return (double)r * (1.0 / 9007199254740992.0);
}

double __mlang_std_rand_range_f64(double min_value, double max_value)
{
    if(min_value > max_value)
    {
        double t = min_value;
        min_value = max_value;
        max_value = t;
    }
    double unit = __mlang_std_rand_next_f64();
    return min_value + (max_value - min_value) * unit;
}
