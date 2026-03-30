#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char g_signal_last_error[256];
static void (*g_signal_handlers[NSIG])(int);
static volatile sig_atomic_t g_signal_counts[NSIG];

static void signal_set_error_from_errno(const char* prefix)
{
    int err = errno;
    const char* msg = strerror(err);
    if(!msg)
        msg = "unknown error";
    snprintf(g_signal_last_error, sizeof(g_signal_last_error), "%s: %s",
             prefix ? prefix : "signal error", msg);
}

static void signal_set_error_text(const char* text)
{
    if(!text)
        text = "signal error";
    snprintf(g_signal_last_error, sizeof(g_signal_last_error), "%s", text);
}

static int signal_is_valid(int signum)
{
    return signum > 0 && signum < NSIG;
}

static void mlang_signal_dispatch(int signum)
{
    if(signal_is_valid(signum))
    {
        g_signal_counts[signum] += 1;
        void (*handler)(int) = g_signal_handlers[signum];
        if(handler)
            handler(signum);
    }
}

const char* __mlang_std_signal_last_error(void)
{
    if(g_signal_last_error[0] == '\0')
        return "no signal error";
    return g_signal_last_error;
}

int __mlang_std_signal_set(int signum, void* handler)
{
    if(!signal_is_valid(signum))
    {
        signal_set_error_text("invalid signal number");
        return -1;
    }
    if(!handler)
    {
        signal_set_error_text("signal handler must not be null");
        return -1;
    }

    g_signal_handlers[signum] = (void (*)(int))handler;
    if(signal(signum, mlang_signal_dispatch) == SIG_ERR)
    {
        g_signal_handlers[signum] = NULL;
        signal_set_error_from_errno("signal");
        return -1;
    }
    return 0;
}

int __mlang_std_signal_ignore(int signum)
{
    if(!signal_is_valid(signum))
    {
        signal_set_error_text("invalid signal number");
        return -1;
    }
    g_signal_handlers[signum] = NULL;
    if(signal(signum, SIG_IGN) == SIG_ERR)
    {
        signal_set_error_from_errno("signal");
        return -1;
    }
    return 0;
}

int __mlang_std_signal_reset_default(int signum)
{
    if(!signal_is_valid(signum))
    {
        signal_set_error_text("invalid signal number");
        return -1;
    }
    g_signal_handlers[signum] = NULL;
    if(signal(signum, SIG_DFL) == SIG_ERR)
    {
        signal_set_error_from_errno("signal");
        return -1;
    }
    return 0;
}

int __mlang_std_signal_raise(int signum)
{
    if(!signal_is_valid(signum))
    {
        signal_set_error_text("invalid signal number");
        return -1;
    }
    if(raise(signum) != 0)
    {
        signal_set_error_from_errno("raise");
        return -1;
    }
    return 0;
}

int64_t __mlang_std_signal_received_count(int signum)
{
    if(!signal_is_valid(signum))
        return -1;
    return (int64_t)g_signal_counts[signum];
}

int __mlang_std_signal_clear_received(int signum)
{
    if(!signal_is_valid(signum))
    {
        signal_set_error_text("invalid signal number");
        return -1;
    }
    g_signal_counts[signum] = 0;
    return 0;
}

int __mlang_std_signal_sigint(void)
{
    return SIGINT;
}

int __mlang_std_signal_sighup(void)
{
#ifdef SIGHUP
    return SIGHUP;
#else
    return SIGTERM;
#endif
}

int __mlang_std_signal_sigquit(void)
{
#ifdef SIGQUIT
    return SIGQUIT;
#else
    return SIGTERM;
#endif
}

int __mlang_std_signal_sigterm(void)
{
    return SIGTERM;
}

int __mlang_std_signal_sigusr1(void)
{
#ifdef SIGUSR1
    return SIGUSR1;
#else
    return SIGTERM;
#endif
}

int __mlang_std_signal_sigusr2(void)
{
#ifdef SIGUSR2
    return SIGUSR2;
#else
    return SIGTERM;
#endif
}
