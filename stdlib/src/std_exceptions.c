#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mlang_exception_frame
{
    jmp_buf env;
    struct mlang_exception_frame* prev;
} mlang_exception_frame;

typedef struct mlang_exception_record
{
    char* type_name;
    char* message;
    int32_t source_line;
} mlang_exception_record;

static _Thread_local mlang_exception_frame* g_exception_top = NULL;
static _Thread_local mlang_exception_record g_current_exception = {NULL, NULL, 0};

static char* mlang_exception_dup(const char* s)
{
    const char* src = s ? s : "";
    size_t len = strlen(src);
    char* out = (char*)malloc(len + 1u);
    if(!out)
        return NULL;
    memcpy(out, src, len + 1u);
    return out;
}

static void mlang_exception_clear_current(void)
{
    free(g_current_exception.type_name);
    free(g_current_exception.message);
    g_current_exception.type_name = NULL;
    g_current_exception.message = NULL;
    g_current_exception.source_line = 0;
}

static void mlang_exception_abort_uncaught(void)
{
    const char* type_name =
        g_current_exception.type_name ? g_current_exception.type_name : "Exception";
    const char* message =
        g_current_exception.message ? g_current_exception.message : "";
    if(g_current_exception.source_line > 0)
    {
        fprintf(stderr, "uncaught exception %s at line %d: %s\n", type_name,
                (int)g_current_exception.source_line, message);
    }
    else
    {
        fprintf(stderr, "uncaught exception %s: %s\n", type_name, message);
    }
    abort();
}

void __mlang_std_exceptions_rethrow_current(void);

int64_t __mlang_std_exceptions_push_frame(void)
{
    mlang_exception_frame* frame =
        (mlang_exception_frame*)malloc(sizeof(mlang_exception_frame));
    if(!frame)
    {
        fprintf(stderr, "mlang exceptions: out of memory while pushing frame\n");
        abort();
    }
    frame->prev = g_exception_top;
    g_exception_top = frame;
    return (int64_t)(intptr_t)frame;
}

void* __mlang_std_exceptions_frame_env(int64_t handle)
{
    mlang_exception_frame* frame = (mlang_exception_frame*)(intptr_t)handle;
    if(!frame)
        return NULL;
    return (void*)frame->env;
}

void __mlang_std_exceptions_pop_frame(int64_t handle)
{
    mlang_exception_frame* frame = (mlang_exception_frame*)(intptr_t)handle;
    if(!frame)
        return;

    if(g_exception_top == frame)
    {
        g_exception_top = frame->prev;
        free(frame);
        return;
    }

    mlang_exception_frame* prev = g_exception_top;
    while(prev && prev->prev != frame)
        prev = prev->prev;
    if(prev && prev->prev == frame)
    {
        prev->prev = frame->prev;
        free(frame);
    }
}

void __mlang_std_exceptions_throw(const char* type_name, const char* message,
                                  int32_t source_line)
{
    mlang_exception_clear_current();
    g_current_exception.type_name = mlang_exception_dup(type_name);
    g_current_exception.message = mlang_exception_dup(message);
    g_current_exception.source_line = source_line;
    __mlang_std_exceptions_rethrow_current();
}

void __mlang_std_exceptions_rethrow_current(void)
{
    if(!g_exception_top)
        mlang_exception_abort_uncaught();
    _longjmp(g_exception_top->env, 1);
}

char* __mlang_std_exceptions_take_type_name(void)
{
    char* out = g_current_exception.type_name;
    if(!out)
        out = mlang_exception_dup("");
    g_current_exception.type_name = NULL;
    return out;
}

char* __mlang_std_exceptions_take_message(void)
{
    char* out = g_current_exception.message;
    if(!out)
        out = mlang_exception_dup("");
    g_current_exception.message = NULL;
    return out;
}

int32_t __mlang_std_exceptions_take_source_line(void)
{
    int32_t out = g_current_exception.source_line;
    g_current_exception.source_line = 0;
    return out;
}

void __mlang_std_exceptions_string_free(char* s)
{
    free(s);
}
