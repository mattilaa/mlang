/**
 * @file std_array.c
 * @brief Runtime-checked fixed-capacity array support for MLang.
 *
 * array<T, N> uses the same runtime layout as list<T>:
 * { int64_t count; void* data; }. The compiler passes N to mutating
 * operations so the runtime can check capacity before reallocating or writing.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t count;
    void* data;
} mlang_array_t;

static void array_abort_capacity(int64_t count, int64_t add, int64_t capacity) {
    fprintf(stderr,
            "std::array capacity exceeded: len=%lld add=%lld capacity=%lld\n",
            (long long)count,
            (long long)add,
            (long long)capacity);
    abort();
}

static void array_abort_invalid(void) {
    fprintf(stderr, "std::array invalid runtime state\n");
    abort();
}

static void array_abort_oom(void) {
    fprintf(stderr, "std::array allocation failed\n");
    abort();
}

static int array_check_growth(mlang_array_t* arr, int64_t add, int64_t capacity) {
    if (!arr || add < 0 || capacity < 0) {
        array_abort_invalid();
    }
    if (arr->count < 0 || arr->count > capacity || add > capacity - arr->count) {
        array_abort_capacity(arr->count, add, capacity);
    }
    return add > 0;
}

static void* array_realloc_or_abort(void* data, int64_t count, size_t elem_size) {
    if (count <= 0) {
        return data;
    }
    void* next = realloc(data, (size_t)count * elem_size);
    if (!next) {
        array_abort_oom();
    }
    return next;
}

void __mlang_std_array_push_i32(void* arr_ptr, int32_t val, int64_t capacity) {
    mlang_array_t* arr = (mlang_array_t*)arr_ptr;
    if (!array_check_growth(arr, 1, capacity)) return;
    arr->data = array_realloc_or_abort(arr->data, arr->count + 1, sizeof(int32_t));
    ((int32_t*)arr->data)[arr->count] = val;
    arr->count++;
}

void __mlang_std_array_push_i64(void* arr_ptr, int64_t val, int64_t capacity) {
    mlang_array_t* arr = (mlang_array_t*)arr_ptr;
    if (!array_check_growth(arr, 1, capacity)) return;
    arr->data = array_realloc_or_abort(arr->data, arr->count + 1, sizeof(int64_t));
    ((int64_t*)arr->data)[arr->count] = val;
    arr->count++;
}

void __mlang_std_array_push_str(void* arr_ptr, const char* val, int64_t capacity) {
    mlang_array_t* arr = (mlang_array_t*)arr_ptr;
    if (!array_check_growth(arr, 1, capacity)) return;
    arr->data = array_realloc_or_abort(arr->data, arr->count + 1, sizeof(char*));
    ((const char**)arr->data)[arr->count] = val;
    arr->count++;
}

void __mlang_std_array_push_raw(void* arr_ptr,
                                const void* val_ptr,
                                int64_t elem_size,
                                int64_t capacity) {
    mlang_array_t* arr = (mlang_array_t*)arr_ptr;
    if (!val_ptr || elem_size <= 0) {
        array_abort_invalid();
    }
    if (!array_check_growth(arr, 1, capacity)) return;
    arr->data = array_realloc_or_abort(arr->data, arr->count + 1, (size_t)elem_size);
    char* dst = (char*)arr->data + ((size_t)arr->count * (size_t)elem_size);
    memcpy(dst, val_ptr, (size_t)elem_size);
    arr->count++;
}

void __mlang_std_array_fill_i32(void* arr_ptr, int32_t val, int64_t capacity) {
    mlang_array_t* arr = (mlang_array_t*)arr_ptr;
    if (!arr || capacity < 0) {
        array_abort_invalid();
    }
    if (capacity == 0) {
        arr->count = 0;
        return;
    }
    arr->data = array_realloc_or_abort(arr->data, capacity, sizeof(int32_t));
    for (int64_t i = 0; i < capacity; ++i) {
        ((int32_t*)arr->data)[i] = val;
    }
    arr->count = capacity;
}

void __mlang_std_array_fill_i64(void* arr_ptr, int64_t val, int64_t capacity) {
    mlang_array_t* arr = (mlang_array_t*)arr_ptr;
    if (!arr || capacity < 0) {
        array_abort_invalid();
    }
    if (capacity == 0) {
        arr->count = 0;
        return;
    }
    arr->data = array_realloc_or_abort(arr->data, capacity, sizeof(int64_t));
    for (int64_t i = 0; i < capacity; ++i) {
        ((int64_t*)arr->data)[i] = val;
    }
    arr->count = capacity;
}

void __mlang_std_array_fill_str(void* arr_ptr, const char* val, int64_t capacity) {
    mlang_array_t* arr = (mlang_array_t*)arr_ptr;
    if (!arr || capacity < 0) {
        array_abort_invalid();
    }
    if (capacity == 0) {
        arr->count = 0;
        return;
    }
    arr->data = array_realloc_or_abort(arr->data, capacity, sizeof(char*));
    for (int64_t i = 0; i < capacity; ++i) {
        ((const char**)arr->data)[i] = val;
    }
    arr->count = capacity;
}

void __mlang_std_array_fill_raw(void* arr_ptr,
                                const void* val_ptr,
                                int64_t elem_size,
                                int64_t capacity) {
    mlang_array_t* arr = (mlang_array_t*)arr_ptr;
    if (!arr || !val_ptr || elem_size <= 0 || capacity < 0) {
        array_abort_invalid();
    }
    if (capacity == 0) {
        arr->count = 0;
        return;
    }
    arr->data = array_realloc_or_abort(arr->data, capacity, (size_t)elem_size);
    for (int64_t i = 0; i < capacity; ++i) {
        char* dst = (char*)arr->data + ((size_t)i * (size_t)elem_size);
        memcpy(dst, val_ptr, (size_t)elem_size);
    }
    arr->count = capacity;
}

static void array_extend_bytes(void* dst_ptr,
                               const void* src_ptr,
                               size_t elem_size,
                               int64_t capacity) {
    mlang_array_t* dst = (mlang_array_t*)dst_ptr;
    const mlang_array_t* src = (const mlang_array_t*)src_ptr;
    if (!dst || !src || elem_size == 0) {
        array_abort_invalid();
    }
    if (src->count < 0 || (src->count > 0 && !src->data)) {
        array_abort_invalid();
    }
    if (!array_check_growth(dst, src->count, capacity)) return;

    int64_t old_count = dst->count;
    int64_t add_count = src->count;
    dst->data = array_realloc_or_abort(dst->data, old_count + add_count, elem_size);

    char* dst_bytes = (char*)dst->data;
    const char* src_bytes = (dst == src) ? dst_bytes : (const char*)src->data;
    memmove(dst_bytes + ((size_t)old_count * elem_size),
            src_bytes,
            (size_t)add_count * elem_size);
    dst->count = old_count + add_count;
}

void __mlang_std_array_extend_i32(void* dst_ptr, const void* src_ptr, int64_t capacity) {
    array_extend_bytes(dst_ptr, src_ptr, sizeof(int32_t), capacity);
}

void __mlang_std_array_extend_i64(void* dst_ptr, const void* src_ptr, int64_t capacity) {
    array_extend_bytes(dst_ptr, src_ptr, sizeof(int64_t), capacity);
}

void __mlang_std_array_extend_str(void* dst_ptr, const void* src_ptr, int64_t capacity) {
    array_extend_bytes(dst_ptr, src_ptr, sizeof(char*), capacity);
}

void __mlang_std_array_extend_raw(void* dst_ptr,
                                  const void* src_ptr,
                                  int64_t elem_size,
                                  int64_t capacity) {
    if (elem_size <= 0) {
        array_abort_invalid();
    }
    array_extend_bytes(dst_ptr, src_ptr, (size_t)elem_size, capacity);
}
