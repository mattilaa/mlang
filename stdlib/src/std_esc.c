#include <stdio.h>

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define MLANG_ESC_TLS _Thread_local
#else
#define MLANG_ESC_TLS
#endif

static MLANG_ESC_TLS char g_mlang_esc_buf[32];

const char* __mlang_std_esc_reset(void)
{
    return "\x1b[0m";
}

const char* __mlang_std_esc_sgr_code(int code)
{
    snprintf(g_mlang_esc_buf, sizeof(g_mlang_esc_buf), "\x1b[%dm", code);
    return g_mlang_esc_buf;
}

const char* __mlang_std_esc_cursor_code(int code)
{
    switch(code)
    {
        case 0:
            return "\x1b[H"; // home
        case 1:
            return "\x1b[2J"; // clear screen
        case 2:
            return "\x1b[2K"; // clear line
        case 3:
            return "\x1b[?25l"; // hide cursor
        case 4:
            return "\x1b[?25h"; // show cursor
        case 5:
            return "\x1b[s"; // save cursor
        case 6:
            return "\x1b[u"; // restore cursor
        default:
            return "\x1b[0m";
    }
}

const char* __mlang_std_esc_cursor_move(int dir, int amount)
{
    char cmd = 'A';
    if(amount < 0)
    {
        amount = 0;
    }

    switch(dir)
    {
        case 0:
            cmd = 'A'; // up
            break;
        case 1:
            cmd = 'B'; // down
            break;
        case 2:
            cmd = 'C'; // right
            break;
        case 3:
            cmd = 'D'; // left
            break;
        default:
            cmd = 'A';
            break;
    }

    snprintf(g_mlang_esc_buf, sizeof(g_mlang_esc_buf), "\x1b[%d%c", amount,
             cmd);
    return g_mlang_esc_buf;
}
