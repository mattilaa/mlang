#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define MLANG_STD_REGEX_UNSUPPORTED 1
typedef struct
{
    int rm_so;
    int rm_eo;
} regmatch_t;
#else
#include <regex.h>
#define MLANG_STD_REGEX_UNSUPPORTED 0
#endif

/**
 * @file std_regex.c
 * @brief POSIX regex backend for std::regex MLang bindings.
 */

typedef struct RegexHandle
{
#if !MLANG_STD_REGEX_UNSUPPORTED
    regex_t regex;
    int is_compiled;
#endif
    int dummy;
} RegexHandle;

static char g_last_error[512];

static void clear_error(void)
{
    g_last_error[0] = '\0';
}

static void set_errorf(const char* msg)
{
    if(!msg)
        msg = "unknown error";
    (void)snprintf(g_last_error, sizeof(g_last_error), "std::regex: %s", msg);
}

static char* dup_cstr(const char* s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    if(n > 0)
        (void)memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

char* __mlang_std_regex_last_error(void)
{
    if(g_last_error[0] == '\0')
        return dup_cstr("");
    return dup_cstr(g_last_error);
}

int64_t __mlang_std_regex_compile(const char* pattern)
{
    if(!pattern)
    {
        set_errorf("compile: null pattern");
        return 0;
    }

#if MLANG_STD_REGEX_UNSUPPORTED
    (void)pattern;
    set_errorf("regex is not supported on this platform");
    return 0;
#else
    RegexHandle* h = (RegexHandle*)calloc(1, sizeof(RegexHandle));
    if(!h)
    {
        set_errorf("compile: out of memory");
        return 0;
    }

    int rc = regcomp(&h->regex, pattern, REG_EXTENDED);
    if(rc != 0)
    {
        char errbuf[256];
        (void)regerror(rc, &h->regex, errbuf, sizeof(errbuf));
        set_errorf(errbuf);
        free(h);
        return 0;
    }

    h->is_compiled = 1;
    clear_error();
    return (int64_t)(intptr_t)h;
#endif
}

int __mlang_std_regex_free(int64_t handle)
{
    RegexHandle* h = (RegexHandle*)(intptr_t)handle;
    if(!h)
    {
        set_errorf("free: invalid handle");
        return -1;
    }

#if !MLANG_STD_REGEX_UNSUPPORTED
    if(h->is_compiled)
    {
        regfree(&h->regex);
        h->is_compiled = 0;
    }
#endif
    free(h);
    clear_error();
    return 0;
}

static int run_group_match(RegexHandle* h, const char* text, int64_t group_index,
                           regmatch_t* out_match)
{
    if(!h)
    {
        set_errorf("invalid handle");
        return -1;
    }
    if(!text)
    {
        set_errorf("null input text");
        return -1;
    }
    if(group_index < 0)
    {
        set_errorf("group index must be >= 0");
        return -1;
    }

#if MLANG_STD_REGEX_UNSUPPORTED
    (void)out_match;
    set_errorf("regex is not supported on this platform");
    return -1;
#else
    size_t nmatch = (size_t)group_index + 1u;
    regmatch_t* matches = (regmatch_t*)calloc(nmatch, sizeof(regmatch_t));
    if(!matches)
    {
        set_errorf("out of memory");
        return -1;
    }

    int rc = regexec(&h->regex, text, nmatch, matches, 0);
    if(rc == REG_NOMATCH)
    {
        free(matches);
        clear_error();
        return 0;
    }
    if(rc != 0)
    {
        char errbuf[256];
        (void)regerror(rc, &h->regex, errbuf, sizeof(errbuf));
        set_errorf(errbuf);
        free(matches);
        return -1;
    }

    regmatch_t m = matches[(size_t)group_index];
    free(matches);
    if(m.rm_so < 0 || m.rm_eo < 0)
    {
        clear_error();
        return 0;
    }
    if(out_match)
        *out_match = m;
    clear_error();
    return 1;
#endif
}

int __mlang_std_regex_is_match(int64_t handle, const char* text)
{
    RegexHandle* h = (RegexHandle*)(intptr_t)handle;
#if MLANG_STD_REGEX_UNSUPPORTED
    (void)h;
    (void)text;
    set_errorf("regex is not supported on this platform");
    return 0;
#else
    if(!h || !text)
    {
        set_errorf("is_match: invalid arguments");
        return 0;
    }
    int rc = regexec(&h->regex, text, 0, NULL, 0);
    if(rc == REG_NOMATCH)
    {
        clear_error();
        return 0;
    }
    if(rc != 0)
    {
        char errbuf[256];
        (void)regerror(rc, &h->regex, errbuf, sizeof(errbuf));
        set_errorf(errbuf);
        return 0;
    }
    clear_error();
    return 1;
#endif
}

int64_t __mlang_std_regex_match_start(int64_t handle, const char* text,
                                      int64_t group_index)
{
    RegexHandle* h = (RegexHandle*)(intptr_t)handle;
    regmatch_t m;
    int rc = run_group_match(h, text, group_index, &m);
    if(rc <= 0)
        return -1;
    return (int64_t)m.rm_so;
}

int64_t __mlang_std_regex_match_end(int64_t handle, const char* text,
                                    int64_t group_index)
{
    RegexHandle* h = (RegexHandle*)(intptr_t)handle;
    regmatch_t m;
    int rc = run_group_match(h, text, group_index, &m);
    if(rc <= 0)
        return -1;
    return (int64_t)m.rm_eo;
}
