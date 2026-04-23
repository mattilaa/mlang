/*
 * std_process_win.c — Windows backend for std::process MLang bindings.
 *
 * Provides the same exported __mlang_std_process_* API as the POSIX
 * implementation in std_process.c but built on top of CreateProcess and
 * Win32 handles. PTY-related functions are stubbed (return error) since
 * a portable ConPTY backend is not yet implemented.
 */

#include "mlang_platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

typedef struct
{
    HANDLE process;
    DWORD  pid;
    HANDLE stdin_w;
    HANDLE stdout_r;
    HANDLE stderr_r;
    int    waited;
    int    status; /* (exit_code & 0xFF) << 8, matching POSIX layout */
} mlang_process_child_t;

typedef struct
{
    HANDLE handle;
    int    is_write;
} mlang_process_pipe_t;

static MLANG_THREAD_LOCAL char g_last_error[512];

static void set_error(const char* msg)
{
    if(!msg)
        msg = "std::process: unknown error";
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s", msg);
}

static void set_winerror(const char* prefix)
{
    const char* p = prefix ? prefix : "std::process";
    char emsg[256];
    DWORD err = GetLastError();
    DWORD r = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, 0, emsg, (DWORD)sizeof(emsg), NULL);
    if(r == 0)
        (void)snprintf(emsg, sizeof(emsg), "win32 error %lu", (unsigned long)err);
    else
    {
        while(r > 0 && (emsg[r-1] == '\r' || emsg[r-1] == '\n' ||
                        emsg[r-1] == '.'  || emsg[r-1] == ' '))
            emsg[--r] = '\0';
    }
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s: %s", p, emsg);
}

static void clear_error(void)
{
    g_last_error[0] = '\0';
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
        memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static mlang_process_child_t* as_child(int64_t handle)
{
    return (mlang_process_child_t*)(intptr_t)handle;
}

static mlang_process_pipe_t* as_pipe(int64_t handle)
{
    return (mlang_process_pipe_t*)(intptr_t)handle;
}

static int64_t make_pipe_handle(HANDLE h, int is_write)
{
    if(h == NULL || h == INVALID_HANDLE_VALUE)
        return 0;
    mlang_process_pipe_t* p = (mlang_process_pipe_t*)malloc(sizeof(*p));
    if(!p)
    {
        CloseHandle(h);
        return 0;
    }
    p->handle = h;
    p->is_write = is_write;
    return (int64_t)(intptr_t)p;
}

/* Quote a single argv entry per CommandLineToArgvW rules. */
static int append_quoted_arg(char** buf, size_t* cap, size_t* len,
                             const char* arg)
{
    int needs_quote = (arg[0] == '\0');
    for(const char* p = arg; *p && !needs_quote; ++p)
    {
        if(*p == ' ' || *p == '\t' || *p == '"')
            needs_quote = 1;
    }

    size_t need = strlen(arg) * 2 + 4;
    if(*len + need >= *cap)
    {
        size_t ncap = (*cap == 0) ? 256 : *cap * 2;
        while(ncap < *len + need) ncap *= 2;
        char* nb = (char*)realloc(*buf, ncap);
        if(!nb)
            return -1;
        *buf = nb;
        *cap = ncap;
    }

    char* out = *buf + *len;
    if(!needs_quote)
    {
        size_t n = strlen(arg);
        memcpy(out, arg, n);
        *len += n;
        return 0;
    }

    *out++ = '"';
    for(const char* p = arg; *p; )
    {
        size_t bs = 0;
        while(*p == '\\') { ++bs; ++p; }
        if(*p == '\0')
        {
            for(size_t i = 0; i < bs * 2; ++i) *out++ = '\\';
            break;
        }
        else if(*p == '"')
        {
            for(size_t i = 0; i < bs * 2 + 1; ++i) *out++ = '\\';
            *out++ = '"';
            ++p;
        }
        else
        {
            for(size_t i = 0; i < bs; ++i) *out++ = '\\';
            *out++ = *p++;
        }
    }
    *out++ = '"';
    *len = (size_t)(out - *buf);
    return 0;
}

static char* build_command_line(const char* program, mlang_list_t args)
{
    char* buf = NULL;
    size_t cap = 0, len = 0;
    if(append_quoted_arg(&buf, &cap, &len, program) != 0)
        goto oom;

    int argc = (args.size > 0 && args.data) ? (int)args.size : 0;
    char** in = (char**)args.data;
    for(int i = 0; i < argc; ++i)
    {
        if(len + 2 >= cap)
        {
            size_t ncap = cap * 2 + 16;
            char* nb = (char*)realloc(buf, ncap);
            if(!nb) goto oom;
            buf = nb; cap = ncap;
        }
        buf[len++] = ' ';
        if(append_quoted_arg(&buf, &cap, &len, in[i] ? in[i] : "") != 0)
            goto oom;
    }
    if(len + 1 >= cap)
    {
        char* nb = (char*)realloc(buf, len + 1);
        if(!nb) goto oom;
        buf = nb;
    }
    buf[len] = '\0';
    return buf;
oom:
    free(buf);
    set_error("std::process spawn: out of memory");
    return NULL;
}

char* __mlang_std_process_last_error(void)
{
    return dup_cstr(g_last_error);
}

static int64_t spawn_internal(const char* program, mlang_list_t args,
                              int pipe_stdin, int pipe_stdout, int pipe_stderr,
                              int inherit_std)
{
    if(!program || program[0] == '\0')
    {
        set_error("std::process spawn: empty program");
        return 0;
    }

    char* cmdline = build_command_line(program, args);
    if(!cmdline)
        return 0;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE in_r = NULL, in_w = NULL;
    HANDLE out_r = NULL, out_w = NULL;
    HANDLE err_r = NULL, err_w = NULL;

    if(pipe_stdin)
    {
        if(!CreatePipe(&in_r, &in_w, &sa, 0))
        { set_winerror("std::process spawn stdin pipe"); free(cmdline); return 0; }
        SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    }
    if(pipe_stdout)
    {
        if(!CreatePipe(&out_r, &out_w, &sa, 0))
        { set_winerror("std::process spawn stdout pipe"); goto fail; }
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    }
    if(pipe_stderr)
    {
        if(!CreatePipe(&err_r, &err_w, &sa, 0))
        { set_winerror("std::process spawn stderr pipe"); goto fail; }
        SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    BOOL inherit = FALSE;
    if(pipe_stdin || pipe_stdout || pipe_stderr || inherit_std)
    {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput  = pipe_stdin  ? in_r  : GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = pipe_stdout ? out_w : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError  = pipe_stderr ? err_w : GetStdHandle(STD_ERROR_HANDLE);
        inherit = TRUE;
    }

    BOOL ok = CreateProcessA(
        NULL, cmdline, NULL, NULL, inherit, 0, NULL, NULL, &si, &pi);
    if(!ok)
    {
        set_winerror("std::process CreateProcess failed");
        goto fail;
    }

    /* close child-side ends in parent */
    if(in_r)  { CloseHandle(in_r);  in_r  = NULL; }
    if(out_w) { CloseHandle(out_w); out_w = NULL; }
    if(err_w) { CloseHandle(err_w); err_w = NULL; }
    CloseHandle(pi.hThread);

    mlang_process_child_t* c =
        (mlang_process_child_t*)calloc(1, sizeof(*c));
    if(!c)
    {
        set_error("std::process spawn: out of memory");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        if(in_w)  CloseHandle(in_w);
        if(out_r) CloseHandle(out_r);
        if(err_r) CloseHandle(err_r);
        free(cmdline);
        return 0;
    }
    c->process = pi.hProcess;
    c->pid     = pi.dwProcessId;
    c->stdin_w = pipe_stdin  ? in_w  : NULL;
    c->stdout_r= pipe_stdout ? out_r : NULL;
    c->stderr_r= pipe_stderr ? err_r : NULL;

    free(cmdline);
    clear_error();
    return (int64_t)(intptr_t)c;

fail:
    if(in_r)  CloseHandle(in_r);
    if(in_w)  CloseHandle(in_w);
    if(out_r) CloseHandle(out_r);
    if(out_w) CloseHandle(out_w);
    if(err_r) CloseHandle(err_r);
    if(err_w) CloseHandle(err_w);
    free(cmdline);
    return 0;
}

int64_t __mlang_std_process_spawn(const char* program, mlang_list_t args,
                                  int pipe_stdin, int pipe_stdout,
                                  int pipe_stderr)
{
    return spawn_internal(program, args, pipe_stdin, pipe_stdout,
                          pipe_stderr, 0);
}

int64_t __mlang_std_process_spawn_foreground_inherit(const char* program,
                                                     mlang_list_t args)
{
    return spawn_internal(program, args, 0, 0, 0, 1);
}

int64_t __mlang_std_process_spawn_pty(const char* program, mlang_list_t args)
{
    (void)program; (void)args;
    set_error("std::process spawn_pty: PTY not supported on Windows");
    return 0;
}

static int64_t take_handle(HANDLE* slot, int is_write, const char* op)
{
    if(!slot || *slot == NULL)
    {
        set_error(op);
        return 0;
    }
    int64_t h = make_pipe_handle(*slot, is_write);
    if(h == 0)
    {
        set_error("std::process: out of memory");
        return 0;
    }
    *slot = NULL;
    clear_error();
    return h;
}

int64_t __mlang_std_process_take_stdin(int64_t child_handle)
{
    mlang_process_child_t* c = as_child(child_handle);
    if(!c) { set_error("std::process take_stdin: invalid child"); return 0; }
    return take_handle(&c->stdin_w, 1, "std::process take_stdin: not piped");
}

int64_t __mlang_std_process_take_stdout(int64_t child_handle)
{
    mlang_process_child_t* c = as_child(child_handle);
    if(!c) { set_error("std::process take_stdout: invalid child"); return 0; }
    return take_handle(&c->stdout_r, 0, "std::process take_stdout: not piped");
}

int64_t __mlang_std_process_take_stderr(int64_t child_handle)
{
    mlang_process_child_t* c = as_child(child_handle);
    if(!c) { set_error("std::process take_stderr: invalid child"); return 0; }
    return take_handle(&c->stderr_r, 0, "std::process take_stderr: not piped");
}

int64_t __mlang_std_process_take_pty(int64_t child_handle)
{
    (void)child_handle;
    set_error("std::process take_pty: PTY not supported on Windows");
    return 0;
}

int64_t __mlang_std_process_pipe_write(int64_t pipe_handle, const char* s)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p || !s) { set_error("std::process pipe_write: invalid pipe"); return -1; }
    DWORD written = 0;
    DWORD len = (DWORD)strlen(s);
    if(!WriteFile(p->handle, s, len, &written, NULL))
    {
        set_winerror("std::process pipe_write");
        return -1;
    }
    clear_error();
    return (int64_t)written;
}

int64_t __mlang_std_process_pipe_read(int64_t pipe_handle, char* buf,
                                      int64_t capacity)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p || !buf || capacity <= 1)
    {
        set_error("std::process pipe_read: invalid args");
        return -1;
    }
    DWORD got = 0;
    if(!ReadFile(p->handle, buf, (DWORD)(capacity - 1), &got, NULL))
    {
        DWORD err = GetLastError();
        if(err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF)
        {
            buf[0] = '\0';
            clear_error();
            return 0;
        }
        set_winerror("std::process pipe_read");
        return -1;
    }
    buf[got] = '\0';
    clear_error();
    return (int64_t)got;
}

int64_t __mlang_std_process_pipe_read_nonblocking(int64_t pipe_handle,
                                                  char* buf, int64_t capacity)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p || !buf || capacity <= 1)
    {
        set_error("std::process pipe_read_nonblocking: invalid args");
        return -1;
    }
    DWORD avail = 0;
    if(!PeekNamedPipe(p->handle, NULL, 0, NULL, &avail, NULL))
    {
        DWORD err = GetLastError();
        if(err == ERROR_BROKEN_PIPE)
        {
            buf[0] = '\0';
            clear_error();
            return 0;
        }
        set_winerror("std::process pipe_read_nonblocking peek");
        return -1;
    }
    if(avail == 0)
    {
        buf[0] = '\0';
        clear_error();
        return -2; /* would-block */
    }
    DWORD want = (DWORD)(capacity - 1);
    if(avail < want) want = avail;
    DWORD got = 0;
    if(!ReadFile(p->handle, buf, want, &got, NULL))
    {
        set_winerror("std::process pipe_read_nonblocking");
        return -1;
    }
    buf[got] = '\0';
    clear_error();
    return (int64_t)got;
}

int __mlang_std_process_wait_pty_events(int64_t pipe_handle, int timeout_ms)
{
    (void)pipe_handle; (void)timeout_ms;
    set_error("std::process wait_pty_events: PTY not supported on Windows");
    return -1;
}

int __mlang_std_process_pipe_close(int64_t pipe_handle)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p) return 0;
    if(p->handle) CloseHandle(p->handle);
    free(p);
    clear_error();
    return 0;
}

int __mlang_std_process_pty_resize(int64_t pipe_handle, int rows, int cols)
{
    (void)pipe_handle; (void)rows; (void)cols;
    set_error("std::process pty_resize: PTY not supported on Windows");
    return -1;
}

int __mlang_std_process_wait_raw(int64_t child_handle)
{
    mlang_process_child_t* c = as_child(child_handle);
    if(!c) { set_error("std::process wait: invalid child"); return -1; }
    if(c->waited) { clear_error(); return c->status; }

    DWORD wait = WaitForSingleObject(c->process, INFINITE);
    if(wait == WAIT_FAILED)
    {
        set_winerror("std::process wait failed");
        return -1;
    }
    DWORD code = 0;
    if(!GetExitCodeProcess(c->process, &code))
    {
        set_winerror("std::process GetExitCodeProcess failed");
        return -1;
    }
    c->waited = 1;
    c->status = (int)((code & 0xFF) << 8);
    clear_error();
    return c->status;
}

int __mlang_std_process_try_wait_raw(int64_t child_handle)
{
    mlang_process_child_t* c = as_child(child_handle);
    if(!c) { set_error("std::process try_wait: invalid child"); return -2; }
    if(c->waited) { clear_error(); return c->status; }

    DWORD wait = WaitForSingleObject(c->process, 0);
    if(wait == WAIT_TIMEOUT)
    {
        clear_error();
        return -1;
    }
    if(wait == WAIT_FAILED)
    {
        set_winerror("std::process try_wait failed");
        return -2;
    }
    DWORD code = 0;
    if(!GetExitCodeProcess(c->process, &code))
    {
        set_winerror("std::process GetExitCodeProcess failed");
        return -2;
    }
    c->waited = 1;
    c->status = (int)((code & 0xFF) << 8);
    clear_error();
    return c->status;
}

int __mlang_std_process_status_exited(int raw_status)
{
    (void)raw_status;
    return 1;
}

int __mlang_std_process_status_code(int raw_status)
{
    return (raw_status >> 8) & 0xFF;
}

int __mlang_std_process_status_signaled(int raw_status)
{
    (void)raw_status;
    return 0;
}

int __mlang_std_process_status_signal(int raw_status)
{
    (void)raw_status;
    return -1;
}

int __mlang_std_process_kill(int64_t child_handle, int sig)
{
    (void)sig;
    mlang_process_child_t* c = as_child(child_handle);
    if(!c) { set_error("std::process kill: invalid child"); return -1; }
    if(!TerminateProcess(c->process, 1))
    {
        set_winerror("std::process kill failed");
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_process_child_close(int64_t child_handle)
{
    mlang_process_child_t* c = as_child(child_handle);
    if(!c) return 0;
    if(c->stdin_w)  CloseHandle(c->stdin_w);
    if(c->stdout_r) CloseHandle(c->stdout_r);
    if(c->stderr_r) CloseHandle(c->stderr_r);
    if(c->process)  CloseHandle(c->process);
    free(c);
    return 0;
}
