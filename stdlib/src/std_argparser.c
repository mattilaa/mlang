#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

static char* ap_strdup(const char* s)
{
    if(!s) s = "";
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if(p) memcpy(p, s, n + 1);
    return p;
}

static char* ap_strndup(const char* s, size_t n)
{
    char* p = (char*)malloc(n + 1);
    if(p) { memcpy(p, s, n); p[n] = '\0'; }
    return p;
}

/* -----------------------------------------------------------------------
 * Argument definition: flag / option / positional
 * --------------------------------------------------------------------- */

typedef enum {
    AP_ARG_FLAG = 0,     /* boolean flag: --verbose / -v  */
    AP_ARG_OPTION = 1,   /* key=value option: --output <val> / -o <val> */
    AP_ARG_POSITIONAL = 2
} ArgKind;

typedef struct {
    ArgKind     kind;
    char*       long_name;   /* e.g. "verbose"  (without --) */
    char*       short_name;  /* e.g. "v"        (without -)  */
    char*       help;
    char*       default_val; /* for options only; NULL means no default */
} ArgDef;

/* -----------------------------------------------------------------------
 * ArgParser
 * --------------------------------------------------------------------- */

typedef struct {
    char*    prog;
    char*    desc;
    ArgDef*  defs;
    int      def_count;
    int      def_cap;
} ArgParser_t;

static void ap_def_push(ArgParser_t* p, ArgDef def)
{
    if(p->def_count >= p->def_cap) {
        int nc = p->def_cap ? p->def_cap * 2 : 8;
        ArgDef* nd = (ArgDef*)realloc(p->defs, (size_t)nc * sizeof(ArgDef));
        if(!nd) return;
        p->defs = nd;
        p->def_cap = nc;
    }
    p->defs[p->def_count++] = def;
}

/* -----------------------------------------------------------------------
 * ParseResult
 * --------------------------------------------------------------------- */

typedef struct {
    char*   key;
    char*   value;
} KVPair;

typedef struct {
    int       ok;              /* 1 = success, 0 = error */
    int       help_requested;
    char*     error_msg;
    KVPair*   flags;           /* flags that were set */
    int       flag_count;
    int       flag_cap;
    KVPair*   options;         /* options with their values */
    int       option_count;
    int       option_cap;
    char**    positionals;
    int       pos_count;
    int       pos_cap;
    /* raw arg staging for two-phase parse */
    char**    raw_args;
    int       raw_count;
    int       raw_cap;
} ParseResult_t;

static void pr_flag_set(ParseResult_t* r, const char* key, int val)
{
    for(int i = 0; i < r->flag_count; i++) {
        if(strcmp(r->flags[i].key, key) == 0) {
            free(r->flags[i].value);
            r->flags[i].value = ap_strdup(val ? "1" : "0");
            return;
        }
    }
    if(r->flag_count >= r->flag_cap) {
        int nc = r->flag_cap ? r->flag_cap * 2 : 8;
        KVPair* nd = (KVPair*)realloc(r->flags, (size_t)nc * sizeof(KVPair));
        if(!nd) return;
        r->flags = nd;
        r->flag_cap = nc;
    }
    r->flags[r->flag_count].key   = ap_strdup(key);
    r->flags[r->flag_count].value = ap_strdup(val ? "1" : "0");
    r->flag_count++;
}

static void pr_option_set(ParseResult_t* r, const char* key, const char* val)
{
    for(int i = 0; i < r->option_count; i++) {
        if(strcmp(r->options[i].key, key) == 0) {
            free(r->options[i].value);
            r->options[i].value = ap_strdup(val);
            return;
        }
    }
    if(r->option_count >= r->option_cap) {
        int nc = r->option_cap ? r->option_cap * 2 : 8;
        KVPair* nd = (KVPair*)realloc(r->options, (size_t)nc * sizeof(KVPair));
        if(!nd) return;
        r->options = nd;
        r->option_cap = nc;
    }
    r->options[r->option_count].key   = ap_strdup(key);
    r->options[r->option_count].value = ap_strdup(val ? val : "");
    r->option_count++;
}

static void pr_pos_push(ParseResult_t* r, const char* s)
{
    if(r->pos_count >= r->pos_cap) {
        int nc = r->pos_cap ? r->pos_cap * 2 : 8;
        char** nd = (char**)realloc(r->positionals, (size_t)nc * sizeof(char*));
        if(!nd) return;
        r->positionals = nd;
        r->pos_cap = nc;
    }
    r->positionals[r->pos_count++] = ap_strdup(s);
}

static void pr_raw_push(ParseResult_t* r, const char* s)
{
    if(r->raw_count >= r->raw_cap) {
        int nc = r->raw_cap ? r->raw_cap * 2 : 16;
        char** nd = (char**)realloc(r->raw_args, (size_t)nc * sizeof(char*));
        if(!nd) return;
        r->raw_args = nd;
        r->raw_cap = nc;
    }
    r->raw_args[r->raw_count++] = ap_strdup(s);
}

static void pr_set_error(ParseResult_t* r, const char* msg)
{
    r->ok = 0;
    free(r->error_msg);
    r->error_msg = ap_strdup(msg);
}

static ParseResult_t* pr_alloc(void)
{
    ParseResult_t* r = (ParseResult_t*)calloc(1, sizeof(ParseResult_t));
    if(r) r->ok = 1;
    return r;
}

static void pr_free(ParseResult_t* r)
{
    if(!r) return;
    free(r->error_msg);
    for(int i = 0; i < r->flag_count;   i++) { free(r->flags[i].key);   free(r->flags[i].value); }
    for(int i = 0; i < r->option_count; i++) { free(r->options[i].key); free(r->options[i].value); }
    for(int i = 0; i < r->pos_count;    i++) free(r->positionals[i]);
    for(int i = 0; i < r->raw_count;    i++) free(r->raw_args[i]);
    free(r->flags);
    free(r->options);
    free(r->positionals);
    free(r->raw_args);
    free(r);
}

/* -----------------------------------------------------------------------
 * Core parsing logic
 * --------------------------------------------------------------------- */

/* Find a definition by long or short name. Returns NULL if not found. */
static ArgDef* find_def(ArgParser_t* p, const char* name, int by_short)
{
    for(int i = 0; i < p->def_count; i++) {
        const char* cmp = by_short ? p->defs[i].short_name : p->defs[i].long_name;
        if(cmp && strcmp(cmp, name) == 0) return &p->defs[i];
    }
    return NULL;
}

static void do_parse(ArgParser_t* p, ParseResult_t* r)
{
    /* Seed defaults for all options and set all flags to 0 */
    for(int i = 0; i < p->def_count; i++) {
        ArgDef* d = &p->defs[i];
        if(d->kind == AP_ARG_FLAG) {
            pr_flag_set(r, d->long_name, 0);
        } else if(d->kind == AP_ARG_OPTION) {
            pr_option_set(r, d->long_name, d->default_val ? d->default_val : "");
        }
    }

    int n = r->raw_count;
    char** av = r->raw_args;
    int i = 0;
    int pos_def_idx = 0; /* which positional def we are on */

    while(i < n) {
        const char* arg = av[i];

        /* -- signals end of options */
        if(strcmp(arg, "--") == 0) {
            i++;
            while(i < n) {
                pr_pos_push(r, av[i]);
                i++;
                pos_def_idx++;
            }
            break;
        }

        /* --help / -h */
        if(strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            r->help_requested = 1;
            i++;
            continue;
        }

        /* --long-flag or --long-option[=value] */
        if(strncmp(arg, "--", 2) == 0) {
            const char* key_start = arg + 2;
            const char* eq = strchr(key_start, '=');
            char key_buf[256];
            const char* val = NULL;

            if(eq) {
                size_t klen = (size_t)(eq - key_start);
                if(klen >= sizeof(key_buf)) klen = sizeof(key_buf) - 1;
                memcpy(key_buf, key_start, klen);
                key_buf[klen] = '\0';
                val = eq + 1;
            } else {
                strncpy(key_buf, key_start, sizeof(key_buf) - 1);
                key_buf[sizeof(key_buf) - 1] = '\0';
            }

            ArgDef* d = find_def(p, key_buf, 0);
            if(!d) {
                char msg[512];
                snprintf(msg, sizeof(msg), "unknown option: --%s", key_buf);
                pr_set_error(r, msg);
                return;
            }
            if(d->kind == AP_ARG_FLAG) {
                pr_flag_set(r, d->long_name, 1);
                i++;
            } else {
                /* option: value is either --key=val or --key val */
                if(!val) {
                    if(i + 1 >= n) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "option --%s requires a value", key_buf);
                        pr_set_error(r, msg);
                        return;
                    }
                    val = av[++i];
                }
                pr_option_set(r, d->long_name, val);
                i++;
            }
            continue;
        }

        /* -short or cluster -abc */
        if(arg[0] == '-' && arg[1] != '\0') {
            const char* p_ch = arg + 1;
            while(*p_ch) {
                char sc[2] = { *p_ch, '\0' };
                ArgDef* d = find_def(p, sc, 1);
                if(!d) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "unknown option: -%c", *p_ch);
                    pr_set_error(r, msg);
                    return;
                }
                if(d->kind == AP_ARG_FLAG) {
                    pr_flag_set(r, d->long_name, 1);
                    p_ch++;
                } else {
                    /* option: rest of token is the value, or next token */
                    const char* val;
                    if(*(p_ch + 1)) {
                        val = p_ch + 1;
                        p_ch += strlen(p_ch); /* consume rest */
                    } else {
                        if(i + 1 >= n) {
                            char msg[512];
                            snprintf(msg, sizeof(msg), "option -%c requires a value", sc[0]);
                            pr_set_error(r, msg);
                            return;
                        }
                        val = av[++i];
                        p_ch += strlen(p_ch); /* end inner loop */
                    }
                    pr_option_set(r, d->long_name, val);
                }
            }
            i++;
            continue;
        }

        /* positional */
        pr_pos_push(r, arg);
        i++;
        pos_def_idx++;
    }
}

/* -----------------------------------------------------------------------
 * Help formatting
 * --------------------------------------------------------------------- */

static void ap_print_help(ArgParser_t* p)
{
    printf("Usage: %s", p->prog);
    /* Count flags and options */
    int has_opts = 0;
    int pos_count = 0;
    for(int i = 0; i < p->def_count; i++) {
        if(p->defs[i].kind == AP_ARG_FLAG || p->defs[i].kind == AP_ARG_OPTION) has_opts = 1;
        if(p->defs[i].kind == AP_ARG_POSITIONAL) pos_count++;
    }
    if(has_opts) printf(" [options]");
    for(int i = 0; i < p->def_count; i++) {
        if(p->defs[i].kind == AP_ARG_POSITIONAL)
            printf(" <%s>", p->defs[i].long_name);
    }
    printf("\n");
    if(p->desc && p->desc[0]) printf("\n%s\n", p->desc);

    if(has_opts) {
        printf("\nOptions:\n");
        int label_width = (int)strlen("-h, --help");
        for(int i = 0; i < p->def_count; i++) {
            ArgDef* d = &p->defs[i];
            if(d->kind == AP_ARG_POSITIONAL) continue;
            char label[160];
            if(d->short_name && d->short_name[0])
                snprintf(label, sizeof(label), "-%s, --%s%s", d->short_name, d->long_name,
                         d->kind == AP_ARG_OPTION ? " <val>" : "");
            else
                snprintf(label, sizeof(label), "    --%s%s", d->long_name,
                         d->kind == AP_ARG_OPTION ? " <val>" : "");
            int n = (int)strlen(label);
            if(n > label_width)
                label_width = n;
        }

        printf("  %-*s  %s\n", label_width, "-h, --help", "Show this help message");
        for(int i = 0; i < p->def_count; i++) {
            ArgDef* d = &p->defs[i];
            if(d->kind == AP_ARG_POSITIONAL) continue;
            char label[160];
            if(d->short_name && d->short_name[0])
                snprintf(label, sizeof(label), "-%s, --%s%s", d->short_name, d->long_name,
                         d->kind == AP_ARG_OPTION ? " <val>" : "");
            else
                snprintf(label, sizeof(label), "    --%s%s", d->long_name,
                         d->kind == AP_ARG_OPTION ? " <val>" : "");
            printf("  %-*s  %s", label_width, label, d->help ? d->help : "");
            if(d->kind == AP_ARG_OPTION && d->default_val && d->default_val[0])
                printf(" [default: %s]", d->default_val);
            printf("\n");
        }
    }

    if(pos_count) {
        printf("\nPositional arguments:\n");
        for(int i = 0; i < p->def_count; i++) {
            ArgDef* d = &p->defs[i];
            if(d->kind != AP_ARG_POSITIONAL) continue;
            printf("  %-20s %s\n", d->long_name, d->help ? d->help : "");
        }
    }
}

/* -----------------------------------------------------------------------
 * Public C API
 * --------------------------------------------------------------------- */

int64_t __mlang_std_argparser_new(const char* prog, const char* desc)
{
    ArgParser_t* p = (ArgParser_t*)calloc(1, sizeof(ArgParser_t));
    if(!p) return 0;
    p->prog = ap_strdup(prog);
    p->desc = ap_strdup(desc);
    return (int64_t)(uintptr_t)p;
}

void __mlang_std_argparser_free(int64_t handle)
{
    ArgParser_t* p = (ArgParser_t*)(uintptr_t)handle;
    if(!p) return;
    free(p->prog);
    free(p->desc);
    for(int i = 0; i < p->def_count; i++) {
        free(p->defs[i].long_name);
        free(p->defs[i].short_name);
        free(p->defs[i].help);
        free(p->defs[i].default_val);
    }
    free(p->defs);
    free(p);
}

void __mlang_std_argparser_add_flag(int64_t handle,
                                    const char* long_name,
                                    const char* short_name,
                                    const char* help)
{
    ArgParser_t* p = (ArgParser_t*)(uintptr_t)handle;
    if(!p) return;
    ArgDef d;
    d.kind       = AP_ARG_FLAG;
    d.long_name  = ap_strdup(long_name);
    d.short_name = (short_name && short_name[0]) ? ap_strdup(short_name) : NULL;
    d.help       = ap_strdup(help);
    d.default_val = NULL;
    ap_def_push(p, d);
}

void __mlang_std_argparser_add_option(int64_t handle,
                                      const char* long_name,
                                      const char* short_name,
                                      const char* help,
                                      const char* default_val)
{
    ArgParser_t* p = (ArgParser_t*)(uintptr_t)handle;
    if(!p) return;
    ArgDef d;
    d.kind        = AP_ARG_OPTION;
    d.long_name   = ap_strdup(long_name);
    d.short_name  = (short_name && short_name[0]) ? ap_strdup(short_name) : NULL;
    d.help        = ap_strdup(help);
    d.default_val = (default_val && default_val[0]) ? ap_strdup(default_val) : NULL;
    ap_def_push(p, d);
}

void __mlang_std_argparser_add_positional(int64_t handle,
                                          const char* name,
                                          const char* help)
{
    ArgParser_t* p = (ArgParser_t*)(uintptr_t)handle;
    if(!p) return;
    ArgDef d;
    d.kind       = AP_ARG_POSITIONAL;
    d.long_name  = ap_strdup(name);
    d.short_name = NULL;
    d.help       = ap_strdup(help);
    d.default_val = NULL;
    ap_def_push(p, d);
}

void __mlang_std_argparser_print_help(int64_t handle)
{
    ArgParser_t* p = (ArgParser_t*)(uintptr_t)handle;
    if(!p) return;
    ap_print_help(p);
}

/* Two-phase parse: begin -> add args -> finish */

int64_t __mlang_std_argparser_parse_begin(int64_t parser_handle)
{
    (void)parser_handle;
    ParseResult_t* r = pr_alloc();
    return (int64_t)(uintptr_t)r;
}

void __mlang_std_argparser_parse_add(int64_t result_handle, const char* arg)
{
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)result_handle;
    if(!r || !arg) return;
    pr_raw_push(r, arg);
}

void __mlang_std_argparser_parse_finish(int64_t parser_handle, int64_t result_handle)
{
    ArgParser_t*   p = (ArgParser_t*)(uintptr_t)parser_handle;
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)result_handle;
    if(!p || !r) return;
    do_parse(p, r);
}

/* ParseResult queries */

void __mlang_std_parseresult_free(int64_t handle)
{
    pr_free((ParseResult_t*)(uintptr_t)handle);
}

int32_t __mlang_std_parseresult_ok(int64_t handle)
{
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)handle;
    return r ? (int32_t)r->ok : 0;
}

int32_t __mlang_std_parseresult_help_requested(int64_t handle)
{
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)handle;
    return r ? (int32_t)r->help_requested : 0;
}

const char* __mlang_std_parseresult_error(int64_t handle)
{
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)handle;
    if(!r || !r->error_msg) return "";
    return r->error_msg;
}

int32_t __mlang_std_parseresult_flag(int64_t handle, const char* name)
{
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)handle;
    if(!r) return 0;
    for(int i = 0; i < r->flag_count; i++) {
        if(strcmp(r->flags[i].key, name) == 0) {
            return r->flags[i].value && strcmp(r->flags[i].value, "1") == 0 ? 1 : 0;
        }
    }
    return 0;
}

const char* __mlang_std_parseresult_get(int64_t handle, const char* name)
{
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)handle;
    if(!r) return "";
    for(int i = 0; i < r->option_count; i++) {
        if(strcmp(r->options[i].key, name) == 0)
            return r->options[i].value ? r->options[i].value : "";
    }
    return "";
}

int64_t __mlang_std_parseresult_get_i64(int64_t handle, const char* name)
{
    const char* s = __mlang_std_parseresult_get(handle, name);
    if(!s || !s[0]) return 0;
    char* end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if(errno || end == s) return 0;
    return (int64_t)v;
}

const char* __mlang_std_parseresult_positional(int64_t handle, int64_t idx)
{
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)handle;
    if(!r || idx < 0 || idx >= r->pos_count) return "";
    return r->positionals[idx] ? r->positionals[idx] : "";
}

int64_t __mlang_std_parseresult_positional_count(int64_t handle)
{
    ParseResult_t* r = (ParseResult_t*)(uintptr_t)handle;
    return r ? (int64_t)r->pos_count : 0;
}
