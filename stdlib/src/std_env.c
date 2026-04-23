#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mlang_platform.h"

#if defined(__APPLE__)
#include <crt_externs.h>
#endif

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

static char* mlang_strdup(const char* s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static void load_process_args(int* out_argc, char*** out_argv)
{
    if(out_argc)
        *out_argc = 0;
    if(out_argv)
        *out_argv = NULL;

#if defined(__APPLE__)
    if(out_argc)
        *out_argc = _NSGetArgc() ? *_NSGetArgc() : 0;
    if(out_argv)
        *out_argv = _NSGetArgv() ? *_NSGetArgv() : NULL;
#endif
}

mlang_list_t __env_args(void)
{
    int argc = 0;
    char** argv = NULL;
    load_process_args(&argc, &argv);
    mlang_list_t out;
    out.size = (int64_t)argc;
    out.data = argv;
    return out;
}

int64_t __env_len(mlang_list_t values)
{
    return values.size;
}

char* __env_get(mlang_list_t values, int64_t index)
{
    if(index < 0 || index >= values.size || !values.data)
        return mlang_strdup("");
    char** argv = (char**)values.data;
    const char* s = argv[index] ? argv[index] : "";
    return mlang_strdup(s);
}

char* __env_cwd(void)
{
#ifdef _WIN32
    char buf[_MAX_PATH];
    char* p = _getcwd(buf, sizeof(buf));
    if(!p)
        return mlang_strdup("");
    return mlang_strdup(buf);
#else
    char* p = getcwd(NULL, 0);
    if(!p)
        return mlang_strdup("");
    return p;
#endif
}

char* __env_get_var(const char* name)
{
    if(!name || name[0] == '\0')
        return mlang_strdup("");
    {
        const char* value = getenv(name);
        if(!value)
            return mlang_strdup("");
        return mlang_strdup(value);
    }
}

int32_t __env_set_var(const char* name, const char* value)
{
    if(!name || name[0] == '\0' || !value)
        return -1;
#ifdef _WIN32
    return _putenv_s(name, value) == 0 ? 0 : -1;
#else
    return setenv(name, value, 1) == 0 ? 0 : -1;
#endif
}

int32_t __env_unset_var(const char* name)
{
    if(!name || name[0] == '\0')
        return -1;
#ifdef _WIN32
    return _putenv_s(name, "") == 0 ? 0 : -1;
#else
    return unsetenv(name) == 0 ? 0 : -1;
#endif
}

void __env_println(const char* msg)
{
    fputs(msg ? msg : "", stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

void __env_stderrln(const char* msg)
{
    fputs(msg ? msg : "", stderr);
    fputc('\n', stderr);
    fflush(stderr);
}
