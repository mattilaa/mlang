#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

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
    int waited;
    int status;
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
    child->waited = 0;
    child->status = 0;

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

    free(child);
    return 0;
}
