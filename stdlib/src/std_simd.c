#include <stdint.h>
#include <stdlib.h>

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

static mlang_list_t empty_list(void)
{
    mlang_list_t out;
    out.size = 0;
    out.data = NULL;
    return out;
}

static mlang_list_t alloc_list(int64_t count, size_t elem_size)
{
    mlang_list_t out = empty_list();
    if(count <= 0 || elem_size == 0)
        return out;
    out.data = malloc((size_t)count * elem_size);
    if(!out.data)
        return out;
    out.size = count;
    return out;
}

#if defined(__clang__) || defined(__GNUC__)
typedef int32_t v4i32 __attribute__((vector_size(16)));
typedef int64_t v2i64 __attribute__((vector_size(16)));
typedef float v4f32 __attribute__((vector_size(16)));
typedef double v2f64 __attribute__((vector_size(16)));
#define MLANG_SIMD_VECTOR_EXT 1
#else
#define MLANG_SIMD_VECTOR_EXT 0
#endif

#if MLANG_SIMD_VECTOR_EXT
#define DEFINE_BINARY_OP(NAME, SUFFIX, TYPE, VTYPE, LANES, OP)                 \
    mlang_list_t __mlang_std_simd_##NAME##_##SUFFIX(mlang_list_t lhs,          \
                                                  mlang_list_t rhs)            \
    {                                                                          \
        if(lhs.size != rhs.size || lhs.size <= 0 || !lhs.data || !rhs.data)     \
            return empty_list();                                               \
        mlang_list_t out = alloc_list(lhs.size, sizeof(TYPE));                 \
        if(!out.data)                                                          \
            return out;                                                        \
        const TYPE* a = (const TYPE*)lhs.data;                                 \
        const TYPE* b = (const TYPE*)rhs.data;                                 \
        TYPE* dst = (TYPE*)out.data;                                           \
        int64_t i = 0;                                                         \
        for(; i + (LANES) <= lhs.size; i += (LANES))                           \
        {                                                                      \
            VTYPE va;                                                          \
            VTYPE vb;                                                          \
            __builtin_memcpy(&va, a + i, sizeof(va));                          \
            __builtin_memcpy(&vb, b + i, sizeof(vb));                          \
            VTYPE vr = va OP vb;                                               \
            __builtin_memcpy(dst + i, &vr, sizeof(vr));                        \
        }                                                                      \
        for(; i < lhs.size; ++i)                                                \
            dst[i] = a[i] OP b[i];                                             \
        return out;                                                            \
    }
#else
#define DEFINE_BINARY_OP(NAME, SUFFIX, TYPE, VTYPE, LANES, OP)                 \
    mlang_list_t __mlang_std_simd_##NAME##_##SUFFIX(mlang_list_t lhs,          \
                                                  mlang_list_t rhs)            \
    {                                                                          \
        if(lhs.size != rhs.size || lhs.size <= 0 || !lhs.data || !rhs.data)     \
            return empty_list();                                               \
        mlang_list_t out = alloc_list(lhs.size, sizeof(TYPE));                 \
        if(!out.data)                                                          \
            return out;                                                        \
        const TYPE* a = (const TYPE*)lhs.data;                                 \
        const TYPE* b = (const TYPE*)rhs.data;                                 \
        TYPE* dst = (TYPE*)out.data;                                           \
        for(int64_t i = 0; i < lhs.size; ++i)                                  \
            dst[i] = a[i] OP b[i];                                             \
        return out;                                                            \
    }
#endif

DEFINE_BINARY_OP(add, i32, int32_t, v4i32, 4, +)
DEFINE_BINARY_OP(subtract, i32, int32_t, v4i32, 4, -)
DEFINE_BINARY_OP(multiply, i32, int32_t, v4i32, 4, *)

DEFINE_BINARY_OP(add, i64, int64_t, v2i64, 2, +)
DEFINE_BINARY_OP(subtract, i64, int64_t, v2i64, 2, -)
DEFINE_BINARY_OP(multiply, i64, int64_t, v2i64, 2, *)
DEFINE_BINARY_OP(bit_and, i32, int32_t, v4i32, 4, &)
DEFINE_BINARY_OP(bit_or, i32, int32_t, v4i32, 4, |)
DEFINE_BINARY_OP(bit_xor, i32, int32_t, v4i32, 4, ^)

DEFINE_BINARY_OP(bit_and, i64, int64_t, v2i64, 2, &)
DEFINE_BINARY_OP(bit_or, i64, int64_t, v2i64, 2, |)
DEFINE_BINARY_OP(bit_xor, i64, int64_t, v2i64, 2, ^)

DEFINE_BINARY_OP(add, f32, float, v4f32, 4, +)
DEFINE_BINARY_OP(subtract, f32, float, v4f32, 4, -)
DEFINE_BINARY_OP(multiply, f32, float, v4f32, 4, *)

DEFINE_BINARY_OP(add, f64, double, v2f64, 2, +)
DEFINE_BINARY_OP(subtract, f64, double, v2f64, 2, -)
DEFINE_BINARY_OP(multiply, f64, double, v2f64, 2, *)

#undef DEFINE_BINARY_OP

#if MLANG_SIMD_VECTOR_EXT
#define DEFINE_UNARY_OP(NAME, SUFFIX, TYPE, VTYPE, LANES, OP)                  \
    mlang_list_t __mlang_std_simd_##NAME##_##SUFFIX(mlang_list_t values)       \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return empty_list();                                               \
        mlang_list_t out = alloc_list(values.size, sizeof(TYPE));              \
        if(!out.data)                                                          \
            return out;                                                        \
        const TYPE* src = (const TYPE*)values.data;                            \
        TYPE* dst = (TYPE*)out.data;                                           \
        int64_t i = 0;                                                         \
        for(; i + (LANES) <= values.size; i += (LANES))                        \
        {                                                                      \
            VTYPE v;                                                           \
            __builtin_memcpy(&v, src + i, sizeof(v));                          \
            VTYPE r = OP v;                                                    \
            __builtin_memcpy(dst + i, &r, sizeof(r));                          \
        }                                                                      \
        for(; i < values.size; ++i)                                             \
            dst[i] = OP src[i];                                                \
        return out;                                                            \
    }

#define DEFINE_SHIFT_OP(NAME, SUFFIX, TYPE, VTYPE, LANES, OP)                  \
    mlang_list_t __mlang_std_simd_##NAME##_##SUFFIX(mlang_list_t values,       \
                                                    int32_t amount)            \
    {                                                                          \
        if(values.size <= 0 || !values.data || amount < 0)                     \
            return empty_list();                                               \
        mlang_list_t out = alloc_list(values.size, sizeof(TYPE));              \
        if(!out.data)                                                          \
            return out;                                                        \
        const TYPE* src = (const TYPE*)values.data;                            \
        TYPE* dst = (TYPE*)out.data;                                           \
        int64_t i = 0;                                                         \
        for(; i + (LANES) <= values.size; i += (LANES))                        \
        {                                                                      \
            VTYPE v;                                                           \
            __builtin_memcpy(&v, src + i, sizeof(v));                          \
            VTYPE r = v OP amount;                                             \
            __builtin_memcpy(dst + i, &r, sizeof(r));                          \
        }                                                                      \
        for(; i < values.size; ++i)                                             \
            dst[i] = src[i] OP amount;                                         \
        return out;                                                            \
    }
#else
#define DEFINE_UNARY_OP(NAME, SUFFIX, TYPE, VTYPE, LANES, OP)                  \
    mlang_list_t __mlang_std_simd_##NAME##_##SUFFIX(mlang_list_t values)       \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return empty_list();                                               \
        mlang_list_t out = alloc_list(values.size, sizeof(TYPE));              \
        if(!out.data)                                                          \
            return out;                                                        \
        const TYPE* src = (const TYPE*)values.data;                            \
        TYPE* dst = (TYPE*)out.data;                                           \
        for(int64_t i = 0; i < values.size; ++i)                               \
            dst[i] = OP src[i];                                                \
        return out;                                                            \
    }

#define DEFINE_SHIFT_OP(NAME, SUFFIX, TYPE, VTYPE, LANES, OP)                  \
    mlang_list_t __mlang_std_simd_##NAME##_##SUFFIX(mlang_list_t values,       \
                                                    int32_t amount)            \
    {                                                                          \
        if(values.size <= 0 || !values.data || amount < 0)                     \
            return empty_list();                                               \
        mlang_list_t out = alloc_list(values.size, sizeof(TYPE));              \
        if(!out.data)                                                          \
            return out;                                                        \
        const TYPE* src = (const TYPE*)values.data;                            \
        TYPE* dst = (TYPE*)out.data;                                           \
        for(int64_t i = 0; i < values.size; ++i)                               \
            dst[i] = src[i] OP amount;                                         \
        return out;                                                            \
    }
#endif

DEFINE_UNARY_OP(bit_not, i32, int32_t, v4i32, 4, ~)
DEFINE_UNARY_OP(bit_not, i64, int64_t, v2i64, 2, ~)

DEFINE_SHIFT_OP(shift_left, i32, int32_t, v4i32, 4, <<)
DEFINE_SHIFT_OP(shift_right, i32, int32_t, v4i32, 4, >>)
DEFINE_SHIFT_OP(shift_left, i64, int64_t, v2i64, 2, <<)
DEFINE_SHIFT_OP(shift_right, i64, int64_t, v2i64, 2, >>)

#undef DEFINE_UNARY_OP
#undef DEFINE_SHIFT_OP

int32_t __mlang_std_simd_sum_i32(mlang_list_t values)
{
    if(values.size <= 0 || !values.data)
        return 0;
    const int32_t* data = (const int32_t*)values.data;
    int64_t i = 0;
    int32_t total = 0;
#if MLANG_SIMD_VECTOR_EXT
    v4i32 acc = {0, 0, 0, 0};
    for(; i + 4 <= values.size; i += 4)
    {
        v4i32 chunk;
        __builtin_memcpy(&chunk, data + i, sizeof(chunk));
        acc += chunk;
    }
    total = acc[0] + acc[1] + acc[2] + acc[3];
#endif
    for(; i < values.size; ++i)
        total += data[i];
    return total;
}

int64_t __mlang_std_simd_sum_i64(mlang_list_t values)
{
    if(values.size <= 0 || !values.data)
        return 0;
    const int64_t* data = (const int64_t*)values.data;
    int64_t i = 0;
    int64_t total = 0;
#if MLANG_SIMD_VECTOR_EXT
    v2i64 acc = {0, 0};
    for(; i + 2 <= values.size; i += 2)
    {
        v2i64 chunk;
        __builtin_memcpy(&chunk, data + i, sizeof(chunk));
        acc += chunk;
    }
    total = acc[0] + acc[1];
#endif
    for(; i < values.size; ++i)
        total += data[i];
    return total;
}

float __mlang_std_simd_sum_f32(mlang_list_t values)
{
    if(values.size <= 0 || !values.data)
        return 0.0f;
    const float* data = (const float*)values.data;
    int64_t i = 0;
    float total = 0.0f;
#if MLANG_SIMD_VECTOR_EXT
    v4f32 acc = {0.0f, 0.0f, 0.0f, 0.0f};
    for(; i + 4 <= values.size; i += 4)
    {
        v4f32 chunk;
        __builtin_memcpy(&chunk, data + i, sizeof(chunk));
        acc += chunk;
    }
    total = acc[0] + acc[1] + acc[2] + acc[3];
#endif
    for(; i < values.size; ++i)
        total += data[i];
    return total;
}

double __mlang_std_simd_sum_f64(mlang_list_t values)
{
    if(values.size <= 0 || !values.data)
        return 0.0;
    const double* data = (const double*)values.data;
    int64_t i = 0;
    double total = 0.0;
#if MLANG_SIMD_VECTOR_EXT
    v2f64 acc = {0.0, 0.0};
    for(; i + 2 <= values.size; i += 2)
    {
        v2f64 chunk;
        __builtin_memcpy(&chunk, data + i, sizeof(chunk));
        acc += chunk;
    }
    total = acc[0] + acc[1];
#endif
    for(; i < values.size; ++i)
        total += data[i];
    return total;
}

#if MLANG_SIMD_VECTOR_EXT
#define DEFINE_PRODUCT(NAME, SUFFIX, TYPE, VTYPE, LANES, ONE)                  \
    TYPE __mlang_std_simd_##NAME##_##SUFFIX(mlang_list_t values)              \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return (TYPE)ONE;                                                  \
        const TYPE* data = (const TYPE*)values.data;                           \
        int64_t i = 0;                                                         \
        VTYPE acc;                                                             \
        for(int lane = 0; lane < (LANES); ++lane)                              \
            acc[lane] = (TYPE)ONE;                                             \
        for(; i + (LANES) <= values.size; i += (LANES))                        \
        {                                                                      \
            VTYPE chunk;                                                       \
            __builtin_memcpy(&chunk, data + i, sizeof(chunk));                 \
            acc *= chunk;                                                      \
        }                                                                      \
        TYPE total = (TYPE)ONE;                                                \
        for(int lane = 0; lane < (LANES); ++lane)                              \
            total *= acc[lane];                                                \
        for(; i < values.size; ++i)                                            \
            total *= data[i];                                                  \
        return total;                                                          \
    }
#else
#define DEFINE_PRODUCT(NAME, SUFFIX, TYPE, VTYPE, LANES, ONE)                  \
    TYPE __mlang_std_simd_##NAME##_##SUFFIX(mlang_list_t values)              \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return (TYPE)ONE;                                                  \
        const TYPE* data = (const TYPE*)values.data;                           \
        TYPE total = (TYPE)ONE;                                                \
        for(int64_t i = 0; i < values.size; ++i)                               \
            total *= data[i];                                                  \
        return total;                                                          \
    }
#endif

DEFINE_PRODUCT(product, i32, int32_t, v4i32, 4, 1)
DEFINE_PRODUCT(product, i64, int64_t, v2i64, 2, 1)
DEFINE_PRODUCT(product, f32, float, v4f32, 4, 1.0f)
DEFINE_PRODUCT(product, f64, double, v2f64, 2, 1.0)

#undef DEFINE_PRODUCT

#define DEFINE_PREFIX_SUM(SUFFIX, TYPE)                                        \
    mlang_list_t __mlang_std_simd_prefix_sum_##SUFFIX(mlang_list_t values)    \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return empty_list();                                               \
        mlang_list_t out = alloc_list(values.size, sizeof(TYPE));              \
        if(!out.data)                                                          \
            return out;                                                        \
        const TYPE* src = (const TYPE*)values.data;                            \
        TYPE* dst = (TYPE*)out.data;                                           \
        TYPE running = (TYPE)0;                                                \
        for(int64_t i = 0; i < values.size; ++i)                               \
        {                                                                      \
            running += src[i];                                                 \
            dst[i] = running;                                                  \
        }                                                                      \
        return out;                                                            \
    }

DEFINE_PREFIX_SUM(i32, int32_t)
DEFINE_PREFIX_SUM(i64, int64_t)
DEFINE_PREFIX_SUM(f32, float)
DEFINE_PREFIX_SUM(f64, double)

#undef DEFINE_PREFIX_SUM

#define DEFINE_MIN_MAX(SUFFIX, TYPE)                                           \
    TYPE __mlang_std_simd_min_##SUFFIX(mlang_list_t values)                   \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return (TYPE)0;                                                    \
        const TYPE* data = (const TYPE*)values.data;                           \
        TYPE best = data[0];                                                   \
        for(int64_t i = 1; i < values.size; ++i)                               \
        {                                                                      \
            if(data[i] < best)                                                 \
                best = data[i];                                                \
        }                                                                      \
        return best;                                                           \
    }                                                                          \
                                                                               \
    TYPE __mlang_std_simd_max_##SUFFIX(mlang_list_t values)                   \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return (TYPE)0;                                                    \
        const TYPE* data = (const TYPE*)values.data;                           \
        TYPE best = data[0];                                                   \
        for(int64_t i = 1; i < values.size; ++i)                               \
        {                                                                      \
            if(data[i] > best)                                                 \
                best = data[i];                                                \
        }                                                                      \
        return best;                                                           \
    }

DEFINE_MIN_MAX(i32, int32_t)
DEFINE_MIN_MAX(i64, int64_t)
DEFINE_MIN_MAX(f32, float)
DEFINE_MIN_MAX(f64, double)

#undef DEFINE_MIN_MAX

int32_t __mlang_std_simd_any_bool(mlang_list_t values)
{
    if(values.size <= 0 || !values.data)
        return 0;
    const uint8_t* data = (const uint8_t*)values.data;
    for(int64_t i = 0; i < values.size; ++i)
    {
        if(data[i] != 0)
            return 1;
    }
    return 0;
}

int32_t __mlang_std_simd_all_bool(mlang_list_t values)
{
    if(values.size <= 0 || !values.data)
        return 1;
    const uint8_t* data = (const uint8_t*)values.data;
    for(int64_t i = 0; i < values.size; ++i)
    {
        if(data[i] == 0)
            return 0;
    }
    return 1;
}

#define DEFINE_NONZERO(SUFFIX, TYPE)                                           \
    int32_t __mlang_std_simd_any_nonzero_##SUFFIX(mlang_list_t values)        \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return 0;                                                          \
        const TYPE* data = (const TYPE*)values.data;                           \
        for(int64_t i = 0; i < values.size; ++i)                               \
        {                                                                      \
            if(data[i] != (TYPE)0)                                             \
                return 1;                                                      \
        }                                                                      \
        return 0;                                                              \
    }                                                                          \
                                                                               \
    int32_t __mlang_std_simd_all_nonzero_##SUFFIX(mlang_list_t values)        \
    {                                                                          \
        if(values.size <= 0 || !values.data)                                   \
            return 1;                                                          \
        const TYPE* data = (const TYPE*)values.data;                           \
        for(int64_t i = 0; i < values.size; ++i)                               \
        {                                                                      \
            if(data[i] == (TYPE)0)                                             \
                return 0;                                                      \
        }                                                                      \
        return 1;                                                              \
    }

DEFINE_NONZERO(i32, int32_t)
DEFINE_NONZERO(i64, int64_t)
DEFINE_NONZERO(f32, float)
DEFINE_NONZERO(f64, double)

#undef DEFINE_NONZERO
