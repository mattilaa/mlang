#include <stdio.h>

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(_MSC_VER)
#define MLANG_ESC_TLS _Thread_local
#elif defined(_MSC_VER)
#define MLANG_ESC_TLS __declspec(thread)
#else
#define MLANG_ESC_TLS
#endif

static MLANG_ESC_TLS char g_mlang_esc_bufs[8][32];
static MLANG_ESC_TLS int g_mlang_esc_buf_idx = 0;

static char* next_esc_buf(void)
{
    g_mlang_esc_buf_idx = (g_mlang_esc_buf_idx + 1) & 7;
    return g_mlang_esc_bufs[g_mlang_esc_buf_idx];
}

const char* __mlang_std_esc_reset(void)
{
    return "\x1b[0m";
}

const char* __mlang_std_esc_sgr_code(int code)
{
    char* buf = next_esc_buf();
    snprintf(buf, 32, "\x1b[%dm", code);
    return buf;
}

const char* __mlang_std_esc_sgr_palette(int is_bg, int idx)
{
    if(idx < 0)
    {
        idx = 0;
    }
    if(idx > 255)
    {
        idx = 255;
    }
    char* buf = next_esc_buf();
    snprintf(buf, 32, "\x1b[%d;5;%dm", is_bg ? 48 : 38, idx);
    return buf;
}

const char* __mlang_std_esc_sgr_rgb(int is_bg, int r, int g, int b)
{
    if(r < 0) r = 0;
    if(g < 0) g = 0;
    if(b < 0) b = 0;
    if(r > 255) r = 255;
    if(g > 255) g = 255;
    if(b > 255) b = 255;
    char* buf = next_esc_buf();
    snprintf(buf, 32, "\x1b[%d;2;%d;%d;%dm", is_bg ? 48 : 38, r, g, b);
    return buf;
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

    char* buf = next_esc_buf();
    snprintf(buf, 32, "\x1b[%d%c", amount, cmd);
    return buf;
}

const char* __mlang_std_esc_cursor_pos(int row1, int col1)
{
    if(row1 < 1)
    {
        row1 = 1;
    }
    if(col1 < 1)
    {
        col1 = 1;
    }
    char* buf = next_esc_buf();
    snprintf(buf, 32, "\x1b[%d;%dH", row1, col1);
    return buf;
}

const char* __mlang_std_esc_cursor_style(int style)
{
    if(style < 0)
    {
        style = 0;
    }
    char* buf = next_esc_buf();
    snprintf(buf, 32, "\x1b[%d q", style);
    return buf;
}

const char* __mlang_std_esc_cursor_color_rgb(int r, int g, int b)
{
    if(r < 0) r = 0;
    if(g < 0) g = 0;
    if(b < 0) b = 0;
    if(r > 255) r = 255;
    if(g > 255) g = 255;
    if(b > 255) b = 255;
    char* buf = next_esc_buf();
    snprintf(buf, 32, "\x1b]12;#%02x%02x%02x\x07", r, g, b);
    return buf;
}

const char* __mlang_std_esc_cursor_color_reset(void)
{
    return "\x1b]112\x07";
}
