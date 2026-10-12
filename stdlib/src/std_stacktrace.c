#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define MLANG_STACKTRACE_NATIVE 1
#elif defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define MLANG_STACKTRACE_NATIVE 1
#endif
#endif

#ifndef MLANG_STACKTRACE_NATIVE
#define MLANG_STACKTRACE_NATIVE 0
#endif

#define MLANG_STACKTRACE_MAX_FRAMES 256
#define MLANG_STACKTRACE_INTERNAL_SKIP 2

typedef struct {
    int64_t count;
    void** frames;
#if !defined(_WIN32) && MLANG_STACKTRACE_NATIVE
    char** symbols;
#endif
    char** fallback_descriptions;
} mlang_stacktrace_t;

static void free_descriptions(mlang_stacktrace_t* trace)
{
    if(!trace || !trace->fallback_descriptions)
        return;
    for(int64_t i = 0; i < trace->count; ++i)
        free(trace->fallback_descriptions[i]);
    free(trace->fallback_descriptions);
    trace->fallback_descriptions = NULL;
}

int64_t __mlang_std_stacktrace_new(int64_t skip_frames, int64_t max_frames)
{
    if(max_frames <= 0)
        return 0;
    if(max_frames > MLANG_STACKTRACE_MAX_FRAMES)
        max_frames = MLANG_STACKTRACE_MAX_FRAMES;
    if(skip_frames < 0)
        skip_frames = 0;
    if(skip_frames > MLANG_STACKTRACE_MAX_FRAMES)
        skip_frames = MLANG_STACKTRACE_MAX_FRAMES;

    mlang_stacktrace_t* trace =
        (mlang_stacktrace_t*)calloc(1, sizeof(mlang_stacktrace_t));
    if(!trace)
        return 0;
    trace->frames = (void**)calloc((size_t)max_frames, sizeof(void*));
    if(!trace->frames) {
        free(trace);
        return 0;
    }

#if defined(_WIN32)
    USHORT captured = CaptureStackBackTrace(
        (ULONG)(MLANG_STACKTRACE_INTERNAL_SKIP + skip_frames),
        (ULONG)max_frames, trace->frames, NULL);
    trace->count = (int64_t)captured;
#elif MLANG_STACKTRACE_NATIVE
    int capacity = (int)(max_frames + MLANG_STACKTRACE_INTERNAL_SKIP +
                         skip_frames);
    if(capacity > MLANG_STACKTRACE_MAX_FRAMES + MLANG_STACKTRACE_INTERNAL_SKIP)
        capacity = MLANG_STACKTRACE_MAX_FRAMES + MLANG_STACKTRACE_INTERNAL_SKIP;
    void** captured = (void**)calloc((size_t)capacity, sizeof(void*));
    if(!captured) {
        free(trace->frames);
        free(trace);
        return 0;
    }
    int captured_count = backtrace(captured, capacity);
    int start = (int)skip_frames + MLANG_STACKTRACE_INTERNAL_SKIP;
    for(int i = start; i < captured_count && trace->count < max_frames; ++i)
        trace->frames[trace->count++] = captured[i];
    free(captured);
    if(trace->count > 0)
        trace->symbols = backtrace_symbols(trace->frames, (int)trace->count);
#else
    (void)skip_frames;
    (void)max_frames;
#endif

    trace->fallback_descriptions =
        (char**)calloc((size_t)trace->count, sizeof(char*));
    if(trace->count > 0 && !trace->fallback_descriptions) {
#if !defined(_WIN32) && MLANG_STACKTRACE_NATIVE
        free(trace->symbols);
#endif
        free(trace->frames);
        free(trace);
        return 0;
    }
    for(int64_t i = 0; i < trace->count; ++i) {
        char buffer[48];
        (void)snprintf(buffer, sizeof(buffer), "0x%llx",
                       (unsigned long long)(uintptr_t)trace->frames[i]);
        size_t length = strlen(buffer) + 1;
        trace->fallback_descriptions[i] = (char*)malloc(length);
        if(trace->fallback_descriptions[i])
            memcpy(trace->fallback_descriptions[i], buffer, length);
    }
    return (int64_t)(intptr_t)trace;
}

int64_t __mlang_std_stacktrace_count(int64_t handle)
{
    mlang_stacktrace_t* trace = (mlang_stacktrace_t*)(intptr_t)handle;
    return trace ? trace->count : 0;
}

uint64_t __mlang_std_stacktrace_address(int64_t handle, int64_t index)
{
    mlang_stacktrace_t* trace = (mlang_stacktrace_t*)(intptr_t)handle;
    if(!trace || index < 0 || index >= trace->count)
        return 0;
    return (uint64_t)(uintptr_t)trace->frames[index];
}

const char* __mlang_std_stacktrace_description(int64_t handle, int64_t index)
{
    mlang_stacktrace_t* trace = (mlang_stacktrace_t*)(intptr_t)handle;
    if(!trace || index < 0 || index >= trace->count)
        return "";
#if !defined(_WIN32) && MLANG_STACKTRACE_NATIVE
    if(trace->symbols && trace->symbols[index])
        return trace->symbols[index];
#endif
    if(trace->fallback_descriptions && trace->fallback_descriptions[index])
        return trace->fallback_descriptions[index];
    return "";
}

void __mlang_std_stacktrace_free(int64_t handle)
{
    mlang_stacktrace_t* trace = (mlang_stacktrace_t*)(intptr_t)handle;
    if(!trace)
        return;
#if !defined(_WIN32) && MLANG_STACKTRACE_NATIVE
    free(trace->symbols);
#endif
    free_descriptions(trace);
    free(trace->frames);
    free(trace);
}
