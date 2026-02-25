#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#define isatty _isatty
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
static struct termios g_saved_stdin_termios;
static int g_saved_stdin_termios_valid = 0;
#endif

static int env_flag_enabled(const char* name)
{
    const char* v = getenv(name);
    if(!v || !*v)
        return 0;
    if(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 ||
       strcmp(v, "FALSE") == 0 || strcmp(v, "no") == 0 ||
       strcmp(v, "NO") == 0)
    {
        return 0;
    }
    return 1;
}

static int contains_ci(const char* haystack, const char* needle)
{
    if(!haystack || !needle || !*needle)
        return 0;

    size_t n = strlen(needle);
    for(const char* p = haystack; *p; ++p)
    {
        size_t i = 0;
        while(i < n && p[i] &&
              tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
        {
            ++i;
        }
        if(i == n)
            return 1;
    }
    return 0;
}

static int detect_color_level_for_fd(int fd)
{
    if(!isatty(fd))
        return 0;

    if(env_flag_enabled("NO_COLOR"))
        return 0;

    if(env_flag_enabled("FORCE_COLOR"))
    {
        const char* fc = getenv("FORCE_COLOR");
        if(fc && strcmp(fc, "3") == 0)
            return 16777216;
        if(fc && strcmp(fc, "2") == 0)
            return 256;
        return 16;
    }

    const char* colorterm = getenv("COLORTERM");
    if(contains_ci(colorterm, "truecolor") || contains_ci(colorterm, "24bit"))
        return 16777216;

    const char* term = getenv("TERM");
    if(contains_ci(term, "256color"))
        return 256;
    if(contains_ci(term, "xterm") || contains_ci(term, "screen") ||
       contains_ci(term, "tmux") || contains_ci(term, "rxvt") ||
       contains_ci(term, "ansi") || contains_ci(term, "color") ||
       contains_ci(term, "cygwin") || contains_ci(term, "linux"))
    {
        return 16;
    }

    return 0;
}

static int parse_env_int(const char* name, int fallback)
{
    const char* v = getenv(name);
    if(!v || !*v)
        return fallback;
    char* end = NULL;
    long out = strtol(v, &end, 10);
    if(end == v || out <= 0 || out > 1000000)
        return fallback;
    return (int)out;
}

static int get_rows_for_fd(int fd)
{
#if defined(_WIN32)
    (void)fd;
    return parse_env_int("LINES", -1);
#else
    struct winsize ws;
    if(ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return (int)ws.ws_row;
    return parse_env_int("LINES", -1);
#endif
}

static int get_cols_for_fd(int fd)
{
#if defined(_WIN32)
    (void)fd;
    return parse_env_int("COLUMNS", -1);
#else
    struct winsize ws;
    if(ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    return parse_env_int("COLUMNS", -1);
#endif
}

const char* __mlang_std_term_term_name(void)
{
    const char* term = getenv("TERM");
    if(term && *term)
        return term;
    return "unknown";
}

int __mlang_std_term_stdin_is_tty(void)
{
    return isatty(STDIN_FILENO) ? 1 : 0;
}

int __mlang_std_term_stdout_is_tty(void)
{
    return isatty(STDOUT_FILENO) ? 1 : 0;
}

int __mlang_std_term_stderr_is_tty(void)
{
    return isatty(STDERR_FILENO) ? 1 : 0;
}

int __mlang_std_term_stdout_rows(void)
{
    return get_rows_for_fd(STDOUT_FILENO);
}

int __mlang_std_term_stdout_cols(void)
{
    return get_cols_for_fd(STDOUT_FILENO);
}

int __mlang_std_term_stderr_rows(void)
{
    return get_rows_for_fd(STDERR_FILENO);
}

int __mlang_std_term_stderr_cols(void)
{
    return get_cols_for_fd(STDERR_FILENO);
}

int __mlang_std_term_stdout_color_level(void)
{
    return detect_color_level_for_fd(STDOUT_FILENO);
}

int __mlang_std_term_stderr_color_level(void)
{
    return detect_color_level_for_fd(STDERR_FILENO);
}

int __mlang_std_term_stdout_truecolor(void)
{
    return detect_color_level_for_fd(STDOUT_FILENO) >= 16777216 ? 1 : 0;
}

int __mlang_std_term_stderr_truecolor(void)
{
    return detect_color_level_for_fd(STDERR_FILENO) >= 16777216 ? 1 : 0;
}

int __mlang_std_term_stdout_supports_ansi(void)
{
#if defined(_WIN32)
    return __mlang_std_term_stdout_is_tty() ? 1 : 0;
#else
    if(__mlang_std_term_stdout_is_tty() == 0)
        return 0;
    if(env_flag_enabled("NO_COLOR"))
        return 0;
    return 1;
#endif
}

int __mlang_std_term_stdin_enable_raw(void)
{
#if defined(_WIN32)
    return -1;
#else
    struct termios t;
    if(tcgetattr(STDIN_FILENO, &t) != 0)
        return -1;

    if(!g_saved_stdin_termios_valid)
    {
        g_saved_stdin_termios = t;
        g_saved_stdin_termios_valid = 1;
    }

    struct termios raw = t;
#if defined(__APPLE__) || defined(__linux__)
    cfmakeraw(&raw);
#else
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
#endif
    return tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif
}

int __mlang_std_term_stdin_restore(void)
{
#if defined(_WIN32)
    return -1;
#else
    if(!g_saved_stdin_termios_valid)
        return -1;
    int rc = tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_stdin_termios);
    if(rc == 0)
        g_saved_stdin_termios_valid = 0;
    return rc;
#endif
}
