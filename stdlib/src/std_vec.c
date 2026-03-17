/**
 * @file std_vec.c
 * @brief Vec<T> runtime support for MLang (std::vec).
 *
 * All mutating operations receive a pointer to the on-stack list struct
 * { int64_t count; void* data; } and modify it in-place.
 * This matches the LLVM struct layout { i64, ptr } used by mlang list<T>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Internal header: mirrors the mlang list<T> struct layout. */
typedef struct {
    int64_t count;
    void*   data;
} mlang_vec_t;

/* ------------------------------------------------------------------ */
/* push                                                                 */
/* ------------------------------------------------------------------ */

/** Append an i32 element. */
void __mlang_std_vec_push_i32(void* vec_ptr, int32_t val) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    v->data = realloc(v->data, (size_t)(v->count + 1) * sizeof(int32_t));
    ((int32_t*)v->data)[v->count] = val;
    v->count++;
}

/** Append an i64 element. */
void __mlang_std_vec_push_i64(void* vec_ptr, int64_t val) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    v->data = realloc(v->data, (size_t)(v->count + 1) * sizeof(int64_t));
    ((int64_t*)v->data)[v->count] = val;
    v->count++;
}

/** Append a string element (stores the pointer, not a copy). */
void __mlang_std_vec_push_str(void* vec_ptr, const char* val) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    v->data = realloc(v->data, (size_t)(v->count + 1) * sizeof(char*));
    ((const char**)v->data)[v->count] = val;
    v->count++;
}

/** Append a raw-bytes element of size elem_size (for non-primitive list<T>). */
void __mlang_std_vec_push_raw(void* vec_ptr, const void* val_ptr, int64_t elem_size) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (!val_ptr || elem_size <= 0) return;
    size_t next_count = (size_t)(v->count + 1);
    size_t bytes = next_count * (size_t)elem_size;
    void* new_data = realloc(v->data, bytes);
    if (!new_data) return;
    v->data = new_data;
    char* dst = (char*)v->data + ((size_t)v->count * (size_t)elem_size);
    memcpy(dst, val_ptr, (size_t)elem_size);
    v->count++;
}

/* ------------------------------------------------------------------ */
/* pop                                                                  */
/* ------------------------------------------------------------------ */

/** Remove and return last i32 element (0 if empty). */
int32_t __mlang_std_vec_pop_i32(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (v->count <= 0) return 0;
    v->count--;
    return ((int32_t*)v->data)[v->count];
}

/** Remove and return last i64 element (0 if empty). */
int64_t __mlang_std_vec_pop_i64(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (v->count <= 0) return 0;
    v->count--;
    return ((int64_t*)v->data)[v->count];
}

/** Remove and return last string element (NULL if empty). */
const char* __mlang_std_vec_pop_str(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (v->count <= 0) return NULL;
    v->count--;
    return ((const char**)v->data)[v->count];
}

/** Remove and copy last raw element into out_ptr. Returns 1 on success, 0 if empty. */
int32_t __mlang_std_vec_pop_raw(void* vec_ptr, void* out_ptr, int64_t elem_size) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (!out_ptr || elem_size <= 0) return 0;
    if (v->count <= 0 || !v->data) return 0;
    v->count--;
    const char* src = (const char*)v->data + ((size_t)v->count * (size_t)elem_size);
    memcpy(out_ptr, src, (size_t)elem_size);
    return 1;
}

/* ------------------------------------------------------------------ */
/* clear                                                               */
/* ------------------------------------------------------------------ */

/** Reset element count to 0 (capacity kept). */
void __mlang_std_vec_clear(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    v->count = 0;
}

/* ------------------------------------------------------------------ */
/* contains / index_of                                                  */
/* ------------------------------------------------------------------ */

/** Return 1 if vec contains val, 0 otherwise. */
int __mlang_std_vec_contains_i32(void* vec_ptr, int32_t val) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    for (int64_t i = 0; i < v->count; i++)
        if (((int32_t*)v->data)[i] == val) return 1;
    return 0;
}

/** Return 1 if vec contains val, 0 otherwise. */
int __mlang_std_vec_contains_i64(void* vec_ptr, int64_t val) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    for (int64_t i = 0; i < v->count; i++)
        if (((int64_t*)v->data)[i] == val) return 1;
    return 0;
}

/** Return first index of val, -1 if not found. */
int64_t __mlang_std_vec_index_of_i32(void* vec_ptr, int32_t val) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    for (int64_t i = 0; i < v->count; i++)
        if (((int32_t*)v->data)[i] == val) return i;
    return -1;
}

/** Return first index of val, -1 if not found. */
int64_t __mlang_std_vec_index_of_i64(void* vec_ptr, int64_t val) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    for (int64_t i = 0; i < v->count; i++)
        if (((int64_t*)v->data)[i] == val) return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* sort                                                                 */
/* ------------------------------------------------------------------ */

static int _cmp_i32(const void* a, const void* b) {
    int32_t x = *(const int32_t*)a, y = *(const int32_t*)b;
    return (x > y) - (x < y);
}
static int _cmp_i64(const void* a, const void* b) {
    int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
    return (x > y) - (x < y);
}
static int _cmp_i32_desc(const void* a, const void* b) { return _cmp_i32(b, a); }
static int _cmp_i64_desc(const void* a, const void* b) { return _cmp_i64(b, a); }

/** Sort i32 elements in ascending order. */
void __mlang_std_vec_sort_i32(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (v->count > 1)
        qsort(v->data, (size_t)v->count, sizeof(int32_t), _cmp_i32);
}

/** Sort i64 elements in ascending order. */
void __mlang_std_vec_sort_i64(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (v->count > 1)
        qsort(v->data, (size_t)v->count, sizeof(int64_t), _cmp_i64);
}

/** Sort i32 elements in descending order. */
void __mlang_std_vec_sort_desc_i32(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (v->count > 1)
        qsort(v->data, (size_t)v->count, sizeof(int32_t), _cmp_i32_desc);
}

/** Sort i64 elements in descending order. */
void __mlang_std_vec_sort_desc_i64(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    if (v->count > 1)
        qsort(v->data, (size_t)v->count, sizeof(int64_t), _cmp_i64_desc);
}

/* ------------------------------------------------------------------ */
/* reverse                                                              */
/* ------------------------------------------------------------------ */

/** Reverse i32 elements in-place. */
void __mlang_std_vec_reverse_i32(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    int32_t* d = (int32_t*)v->data;
    for (int64_t i = 0, j = v->count - 1; i < j; i++, j--) {
        int32_t tmp = d[i]; d[i] = d[j]; d[j] = tmp;
    }
}

/** Reverse i64 elements in-place. */
void __mlang_std_vec_reverse_i64(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    int64_t* d = (int64_t*)v->data;
    for (int64_t i = 0, j = v->count - 1; i < j; i++, j--) {
        int64_t tmp = d[i]; d[i] = d[j]; d[j] = tmp;
    }
}

/** Reverse string elements in-place. */
void __mlang_std_vec_reverse_str(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    const char** d = (const char**)v->data;
    for (int64_t i = 0, j = v->count - 1; i < j; i++, j--) {
        const char* tmp = d[i]; d[i] = d[j]; d[j] = tmp;
    }
}

/* ------------------------------------------------------------------ */
/* dedup                                                                */
/* ------------------------------------------------------------------ */

/** Remove consecutive duplicate i32 elements (like Rust Vec::dedup). */
void __mlang_std_vec_dedup_i32(void* vec_ptr) {
    mlang_vec_t* v = (mlang_vec_t*)vec_ptr;
    int32_t* d = (int32_t*)v->data;
    if (v->count <= 1) return;
    int64_t write = 1;
    for (int64_t i = 1; i < v->count; i++)
        if (d[i] != d[write - 1]) d[write++] = d[i];
    v->count = write;
}
