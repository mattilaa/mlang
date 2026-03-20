#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#elif !defined(_WIN32)
#include <pty.h>
#endif

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

typedef struct
{
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    int pty_fd;
    int waited;
    int status;
    int foreground_handoff;
    pid_t parent_pgrp;
} mlang_process_child_t;

typedef struct
{
    int fd;
} mlang_process_pipe_t;

static __thread char g_last_error[512];

static void set_error(const char* msg)
{
    if(!msg)
        msg = "std::process: unknown error";
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s", msg);
}

static void set_errno_error(const char* prefix)
{
    const char* p = prefix ? prefix : "std::process";
    const char* e = strerror(errno);
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s: %s", p, e ? e : "unknown error");
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
        (void)memcpy(out, s, n);
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

static int64_t make_pipe_handle(int fd)
{
    if(fd < 0)
        return 0;
    mlang_process_pipe_t* p = (mlang_process_pipe_t*)malloc(sizeof(mlang_process_pipe_t));
    if(!p)
    {
        (void)close(fd);
        return 0;
    }
    p->fd = fd;
    return (int64_t)(intptr_t)p;
}

static void close_if_open(int* fd)
{
    if(!fd || *fd < 0)
        return;
    (void)close(*fd);
    *fd = -1;
}

#if !defined(_WIN32)
static void set_ignored_job_signals_in_parent(void (*saved[3])(int));
static void restore_job_signals_in_parent(void (*saved[3])(int));
#endif

static void restore_foreground_terminal(mlang_process_child_t* child)
{
    if(!child || !child->foreground_handoff)
        return;
#if !defined(_WIN32)
    if(isatty(STDIN_FILENO) && child->parent_pgrp > 0)
    {
        void (*saved[3])(int);
        set_ignored_job_signals_in_parent(saved);
        (void)tcsetpgrp(STDIN_FILENO, child->parent_pgrp);
        restore_job_signals_in_parent(saved);
    }
#endif
    child->foreground_handoff = 0;
}

#if !defined(_WIN32)
static void restore_default_job_signals_in_child(void)
{
    (void)signal(SIGINT, SIG_DFL);
    (void)signal(SIGQUIT, SIG_DFL);
    (void)signal(SIGTSTP, SIG_DFL);
    (void)signal(SIGTTIN, SIG_DFL);
    (void)signal(SIGTTOU, SIG_DFL);
}

static void set_ignored_job_signals_in_parent(void (*saved[3])(int))
{
    saved[0] = signal(SIGTTOU, SIG_IGN);
    saved[1] = signal(SIGTTIN, SIG_IGN);
    saved[2] = signal(SIGTSTP, SIG_IGN);
}

static void restore_job_signals_in_parent(void (*saved[3])(int))
{
    (void)signal(SIGTTOU, saved[0]);
    (void)signal(SIGTTIN, saved[1]);
    (void)signal(SIGTSTP, saved[2]);
}

static void foreground_terminal_to_pgrp(pid_t pgrp)
{
    if(pgrp <= 0 || !isatty(STDIN_FILENO))
        return;

    void (*saved[3])(int);
    set_ignored_job_signals_in_parent(saved);
    for(int tries = 0; tries < 50; ++tries)
    {
        if(tcsetpgrp(STDIN_FILENO, pgrp) == 0)
            break;
        if(errno != EINTR)
            break;
    }
    restore_job_signals_in_parent(saved);
}
#endif

char* __mlang_std_process_last_error(void)
{
    return dup_cstr(g_last_error);
}

int64_t __mlang_std_process_spawn(const char* program, mlang_list_t args, int pipe_stdin, int pipe_stdout, int pipe_stderr)
{
    if(!program || program[0] == '\0')
    {
        set_error("std::process spawn: empty program");
        return 0;
    }

    int argc = (args.size > 0 && args.data) ? (int)args.size : 0;
    char** argv = (char**)calloc((size_t)argc + 2, sizeof(char*));
    if(!argv)
    {
        set_error("std::process spawn: out of memory");
        return 0;
    }

    argv[0] = (char*)program;
    if(argc > 0)
    {
        char** in = (char**)args.data;
        for(int i = 0; i < argc; ++i)
        {
            argv[i + 1] = in[i] ? in[i] : (char*)"";
        }
    }
    argv[argc + 1] = NULL;

    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};

    if(pipe_stdin && pipe(in_pipe) != 0)
    {
        set_errno_error("std::process spawn stdin pipe failed");
        free(argv);
        return 0;
    }
    if(pipe_stdout && pipe(out_pipe) != 0)
    {
        set_errno_error("std::process spawn stdout pipe failed");
        close_if_open(&in_pipe[0]);
        close_if_open(&in_pipe[1]);
        free(argv);
        return 0;
    }
    if(pipe_stderr && pipe(err_pipe) != 0)
    {
        set_errno_error("std::process spawn stderr pipe failed");
        close_if_open(&in_pipe[0]);
        close_if_open(&in_pipe[1]);
        close_if_open(&out_pipe[0]);
        close_if_open(&out_pipe[1]);
        free(argv);
        return 0;
    }

    pid_t pid = fork();
    if(pid < 0)
    {
        set_errno_error("std::process fork failed");
        close_if_open(&in_pipe[0]);
        close_if_open(&in_pipe[1]);
        close_if_open(&out_pipe[0]);
        close_if_open(&out_pipe[1]);
        close_if_open(&err_pipe[0]);
        close_if_open(&err_pipe[1]);
        free(argv);
        return 0;
    }

    if(pid == 0)
    {
        if(pipe_stdin && in_pipe[0] >= 0)
        {
            (void)dup2(in_pipe[0], STDIN_FILENO);
        }
        if(pipe_stdout && out_pipe[1] >= 0)
        {
            (void)dup2(out_pipe[1], STDOUT_FILENO);
        }
        if(pipe_stderr && err_pipe[1] >= 0)
        {
            (void)dup2(err_pipe[1], STDERR_FILENO);
        }

        close_if_open(&in_pipe[0]);
        close_if_open(&in_pipe[1]);
        close_if_open(&out_pipe[0]);
        close_if_open(&out_pipe[1]);
        close_if_open(&err_pipe[0]);
        close_if_open(&err_pipe[1]);

        execvp(program, argv);
        fprintf(stderr, "%s: %s\n", program, strerror(errno));
        fflush(stderr);
        _exit(127);
    }

    mlang_process_child_t* child = (mlang_process_child_t*)malloc(sizeof(mlang_process_child_t));
    if(!child)
    {
        set_error("std::process spawn: out of memory");
        close_if_open(&in_pipe[0]);
        close_if_open(&in_pipe[1]);
        close_if_open(&out_pipe[0]);
        close_if_open(&out_pipe[1]);
        close_if_open(&err_pipe[0]);
        close_if_open(&err_pipe[1]);
        free(argv);
        return 0;
    }

    child->pid = pid;
    child->stdin_fd = -1;
    child->stdout_fd = -1;
    child->stderr_fd = -1;
    child->pty_fd = -1;
    child->waited = 0;
    child->status = 0;
    child->foreground_handoff = 0;
    child->parent_pgrp = -1;

    if(pipe_stdin)
    {
        close_if_open(&in_pipe[0]);
        child->stdin_fd = in_pipe[1];
    }
    if(pipe_stdout)
    {
        close_if_open(&out_pipe[1]);
        child->stdout_fd = out_pipe[0];
    }
    if(pipe_stderr)
    {
        close_if_open(&err_pipe[1]);
        child->stderr_fd = err_pipe[0];
    }

    clear_error();
    free(argv);
    return (int64_t)(intptr_t)child;
}

int64_t __mlang_std_process_spawn_foreground_inherit(const char* program, mlang_list_t args)
{
    if(!program || program[0] == '\0')
    {
        set_error("std::process spawn_foreground_inherit: empty program");
        return 0;
    }

    int argc = (args.size > 0 && args.data) ? (int)args.size : 0;
    char** argv = (char**)calloc((size_t)argc + 2, sizeof(char*));
    if(!argv)
    {
        set_error("std::process spawn_foreground_inherit: out of memory");
        return 0;
    }

    argv[0] = (char*)program;
    if(argc > 0)
    {
        char** in = (char**)args.data;
        for(int i = 0; i < argc; ++i)
            argv[i + 1] = in[i] ? in[i] : (char*)"";
    }
    argv[argc + 1] = NULL;

    pid_t parent_pgrp = -1;
#if !defined(_WIN32)
    if(isatty(STDIN_FILENO))
        parent_pgrp = tcgetpgrp(STDIN_FILENO);
#endif

    pid_t pid = fork();
    if(pid < 0)
    {
        set_errno_error("std::process spawn_foreground_inherit fork failed");
        free(argv);
        return 0;
    }

    if(pid == 0)
    {
#if !defined(_WIN32)
        (void)setpgid(0, 0);
        restore_default_job_signals_in_child();
#endif
        execvp(program, argv);
        fprintf(stderr, "%s: %s\n", program, strerror(errno));
        fflush(stderr);
        _exit(127);
    }

#if !defined(_WIN32)
    for(int tries = 0; tries < 50; ++tries)
    {
        if(setpgid(pid, pid) == 0)
            break;
        if(errno == EACCES)
            break;
        if(errno != EINTR)
            break;
    }
    foreground_terminal_to_pgrp(pid);
#endif

    mlang_process_child_t* child = (mlang_process_child_t*)malloc(sizeof(mlang_process_child_t));
    if(!child)
    {
        set_error("std::process spawn_foreground_inherit: out of memory");
#if !defined(_WIN32)
        foreground_terminal_to_pgrp(parent_pgrp);
#endif
        free(argv);
        return 0;
    }

    child->pid = pid;
    child->stdin_fd = -1;
    child->stdout_fd = -1;
    child->stderr_fd = -1;
    child->pty_fd = -1;
    child->waited = 0;
    child->status = 0;
    child->foreground_handoff = 1;
    child->parent_pgrp = parent_pgrp;

    clear_error();
    free(argv);
    return (int64_t)(intptr_t)child;
}

int64_t __mlang_std_process_spawn_pty(const char* program, mlang_list_t args)
{
#if defined(_WIN32)
    (void)program;
    (void)args;
    set_error("std::process spawn_pty: unsupported on this platform");
    return 0;
#else
    if(!program || program[0] == '\0')
    {
        set_error("std::process spawn_pty: empty program");
        return 0;
    }

    int argc = (args.size > 0 && args.data) ? (int)args.size : 0;
    char** argv = (char**)calloc((size_t)argc + 2, sizeof(char*));
    if(!argv)
    {
        set_error("std::process spawn_pty: out of memory");
        return 0;
    }

    argv[0] = (char*)program;
    if(argc > 0)
    {
        char** in = (char**)args.data;
        for(int i = 0; i < argc; ++i)
            argv[i + 1] = in[i] ? in[i] : (char*)"";
    }
    argv[argc + 1] = NULL;

    int master_fd = -1;
    struct termios child_termios;
    struct termios* child_termios_ptr = NULL;
    if(tcgetattr(STDIN_FILENO, &child_termios) == 0)
        child_termios_ptr = &child_termios;

    struct winsize child_winsize;
    struct winsize* child_winsize_ptr = NULL;
    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &child_winsize) == 0 && child_winsize.ws_row > 0 && child_winsize.ws_col > 0)
        child_winsize_ptr = &child_winsize;

    pid_t pid = forkpty(&master_fd, NULL, child_termios_ptr, child_winsize_ptr);
    if(pid < 0)
    {
        set_errno_error("std::process spawn_pty forkpty failed");
        free(argv);
        return 0;
    }

    if(pid == 0)
    {
        restore_default_job_signals_in_child();
        execvp(program, argv);
        fprintf(stderr, "%s: %s\n", program, strerror(errno));
        fflush(stderr);
        _exit(127);
    }

    mlang_process_child_t* child = (mlang_process_child_t*)malloc(sizeof(mlang_process_child_t));
    if(!child)
    {
        set_error("std::process spawn_pty: out of memory");
        close_if_open(&master_fd);
        free(argv);
        return 0;
    }

    child->pid = pid;
    child->stdin_fd = -1;
    child->stdout_fd = -1;
    child->stderr_fd = -1;
    child->pty_fd = master_fd;
    child->waited = 0;
    child->status = 0;
    child->foreground_handoff = 0;
    child->parent_pgrp = -1;

    clear_error();
    free(argv);
    return (int64_t)(intptr_t)child;
#endif
}

int64_t __mlang_std_process_take_stdin(int64_t child_handle)
{
    mlang_process_child_t* child = as_child(child_handle);
    if(!child)
    {
        set_error("std::process stdin: invalid child handle");
        return 0;
    }
    if(child->stdin_fd < 0)
    {
        set_error("std::process stdin: pipe not available");
        return 0;
    }

    int fd = child->stdin_fd;
    child->stdin_fd = -1;
    int64_t out = make_pipe_handle(fd);
    if(out == 0)
    {
        set_error("std::process stdin: failed to create pipe handle");
        return 0;
    }
    clear_error();
    return out;
}

int64_t __mlang_std_process_take_stdout(int64_t child_handle)
{
    mlang_process_child_t* child = as_child(child_handle);
    if(!child)
    {
        set_error("std::process stdout: invalid child handle");
        return 0;
    }
    if(child->stdout_fd < 0)
    {
        set_error("std::process stdout: pipe not available");
        return 0;
    }

    int fd = child->stdout_fd;
    child->stdout_fd = -1;
    int64_t out = make_pipe_handle(fd);
    if(out == 0)
    {
        set_error("std::process stdout: failed to create pipe handle");
        return 0;
    }
    clear_error();
    return out;
}

int64_t __mlang_std_process_take_stderr(int64_t child_handle)
{
    mlang_process_child_t* child = as_child(child_handle);
    if(!child)
    {
        set_error("std::process stderr: invalid child handle");
        return 0;
    }
    if(child->stderr_fd < 0)
    {
        set_error("std::process stderr: pipe not available");
        return 0;
    }

    int fd = child->stderr_fd;
    child->stderr_fd = -1;
    int64_t out = make_pipe_handle(fd);
    if(out == 0)
    {
        set_error("std::process stderr: failed to create pipe handle");
        return 0;
    }
    clear_error();
    return out;
}

int64_t __mlang_std_process_take_pty(int64_t child_handle)
{
    mlang_process_child_t* child = as_child(child_handle);
    if(!child)
    {
        set_error("std::process pty: invalid child handle");
        return 0;
    }
    if(child->pty_fd < 0)
    {
        set_error("std::process pty: PTY not available");
        return 0;
    }

    int fd = child->pty_fd;
    child->pty_fd = -1;
    int64_t out = make_pipe_handle(fd);
    if(out == 0)
    {
        set_error("std::process pty: failed to create PTY handle");
        return 0;
    }
    clear_error();
    return out;
}

int64_t __mlang_std_process_pipe_write(int64_t pipe_handle, const char* s)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p || p->fd < 0 || !s)
    {
        set_error("std::process pipe_write: invalid handle or input");
        return -1;
    }

    size_t total = strlen(s);
    size_t off = 0;
    while(off < total)
    {
        ssize_t n = write(p->fd, s + off, total - off);
        if(n < 0)
        {
            if(errno == EINTR)
                continue;
            set_errno_error("std::process pipe_write failed");
            return -1;
        }
        off += (size_t)n;
    }

    clear_error();
    return (int64_t)off;
}

int64_t __mlang_std_process_pipe_read(int64_t pipe_handle, char* buf, int64_t capacity)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p || p->fd < 0 || !buf || capacity <= 1)
    {
        set_error("std::process pipe_read: invalid arguments");
        return -1;
    }

    for(;;)
    {
        ssize_t n = read(p->fd, buf, (size_t)(capacity - 1));
        if(n < 0)
        {
            if(errno == EINTR)
                continue;
            set_errno_error("std::process pipe_read failed");
            return -1;
        }
        buf[n] = '\0';
        clear_error();
        return (int64_t)n;
    }
}

int64_t __mlang_std_process_pipe_read_nonblocking(int64_t pipe_handle, char* buf, int64_t capacity)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p || p->fd < 0 || !buf || capacity <= 1)
    {
        set_error("std::process pipe_read_nonblocking: invalid arguments");
        return -2;
    }

    int flags = fcntl(p->fd, F_GETFL, 0);
    if(flags < 0)
    {
        set_errno_error("std::process pipe_read_nonblocking getfl failed");
        return -2;
    }
    if((flags & O_NONBLOCK) == 0 && fcntl(p->fd, F_SETFL, flags | O_NONBLOCK) != 0)
    {
        set_errno_error("std::process pipe_read_nonblocking setfl failed");
        return -2;
    }

    for(;;)
    {
        ssize_t n = read(p->fd, buf, (size_t)(capacity - 1));
        if(n < 0)
        {
            if(errno == EINTR)
                continue;
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                buf[0] = '\0';
                clear_error();
                return -1;
            }
            set_errno_error("std::process pipe_read_nonblocking failed");
            return -2;
        }
        buf[n] = '\0';
        clear_error();
        return (int64_t)n;
    }
}

int __mlang_std_process_wait_pty_events(int64_t pipe_handle, int timeout_ms)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p || p->fd < 0)
    {
        set_error("std::process wait_pty_events: invalid pipe handle");
        return -1;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(p->fd, &rfds);

    int maxfd = STDIN_FILENO > p->fd ? STDIN_FILENO : p->fd;
    struct timeval tv;
    struct timeval* tv_ptr = NULL;
    if(timeout_ms >= 0)
    {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    int rc = select(maxfd + 1, &rfds, NULL, NULL, tv_ptr);
    if(rc < 0)
    {
        if(errno == EINTR)
        {
            clear_error();
            return 0;
        }
        set_errno_error("std::process wait_pty_events failed");
        return -1;
    }

    int out = 0;
    if(FD_ISSET(STDIN_FILENO, &rfds))
        out |= 1;
    if(FD_ISSET(p->fd, &rfds))
        out |= 2;
    clear_error();
    return out;
}

int __mlang_std_process_pipe_close(int64_t pipe_handle)
{
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p)
        return 0;
    int rc = 0;
    if(p->fd >= 0)
        rc = close(p->fd);
    p->fd = -1;
    free(p);
    return rc;
}

int __mlang_std_process_pty_resize(int64_t pipe_handle, int rows, int cols)
{
#if defined(_WIN32)
    (void)pipe_handle;
    (void)rows;
    (void)cols;
    return -1;
#else
    mlang_process_pipe_t* p = as_pipe(pipe_handle);
    if(!p || p->fd < 0)
    {
        set_error("std::process pty_resize: invalid handle");
        return -1;
    }
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    if(ioctl(p->fd, TIOCSWINSZ, &ws) < 0)
    {
        set_errno_error("std::process pty_resize ioctl failed");
        return -1;
    }
    return 0;
#endif
}

int __mlang_std_process_wait_raw(int64_t child_handle)
{
    mlang_process_child_t* child = as_child(child_handle);
    if(!child)
    {
        set_error("std::process wait: invalid child handle");
        return -1;
    }

    if(child->waited)
    {
        clear_error();
        return child->status;
    }

    int status = 0;
    pid_t rc = waitpid(child->pid, &status, 0);
    if(rc < 0)
    {
        set_errno_error("std::process wait failed");
        return -1;
    }

    child->waited = 1;
    child->status = status;
    restore_foreground_terminal(child);
    clear_error();
    return status;
}

int __mlang_std_process_try_wait_raw(int64_t child_handle)
{
    mlang_process_child_t* child = as_child(child_handle);
    if(!child)
    {
        set_error("std::process try_wait: invalid child handle");
        return -2;
    }

    if(child->waited)
    {
        clear_error();
        return child->status;
    }

    int status = 0;
    pid_t rc = waitpid(child->pid, &status, WNOHANG);
    if(rc < 0)
    {
        set_errno_error("std::process try_wait failed");
        return -2;
    }
    if(rc == 0)
    {
        clear_error();
        return -1;
    }

    child->waited = 1;
    child->status = status;
    restore_foreground_terminal(child);
    clear_error();
    return status;
}

int __mlang_std_process_status_exited(int raw_status)
{
    return WIFEXITED(raw_status) ? 1 : 0;
}

int __mlang_std_process_status_code(int raw_status)
{
    if(!WIFEXITED(raw_status))
        return -1;
    return WEXITSTATUS(raw_status);
}

int __mlang_std_process_status_signaled(int raw_status)
{
    return WIFSIGNALED(raw_status) ? 1 : 0;
}

int __mlang_std_process_status_signal(int raw_status)
{
    if(!WIFSIGNALED(raw_status))
        return -1;
    return WTERMSIG(raw_status);
}

int __mlang_std_process_kill(int64_t child_handle, int sig)
{
    mlang_process_child_t* child = as_child(child_handle);
    if(!child)
    {
        set_error("std::process kill: invalid child handle");
        return -1;
    }
    if(kill(child->pid, sig) != 0)
    {
        set_errno_error("std::process kill failed");
        return -1;
    }
    clear_error();
    return 0;
}

int __mlang_std_process_child_close(int64_t child_handle)
{
    mlang_process_child_t* child = as_child(child_handle);
    if(!child)
        return 0;

    close_if_open(&child->stdin_fd);
    close_if_open(&child->stdout_fd);
    close_if_open(&child->stderr_fd);
    close_if_open(&child->pty_fd);

    if(!child->waited)
    {
        int status = 0;
        pid_t rc = waitpid(child->pid, &status, WNOHANG);
        if(rc == child->pid)
        {
            child->waited = 1;
            child->status = status;
        }
    }

    restore_foreground_terminal(child);
    free(child);
    return 0;
}
