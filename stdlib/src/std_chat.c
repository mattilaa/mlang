#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define MLANG_CHAT_TLS _Thread_local
#else
#define MLANG_CHAT_TLS
#endif

typedef struct
{
    char* text;
    int kind;
} chat_line_t;

typedef struct
{
    char* title;
    char* server;
    char* channel;
    char* nick;
    char* status;
    char* prompt;
    char* input;
    char* submitted;
    size_t cursor;
    chat_line_t* lines;
    size_t len;
    size_t cap;
    size_t max_lines;
    size_t scroll_rows;
    size_t viewport_body_rows;
    int esc_state;
} chat_ui_t;

typedef struct
{
    char* buf;
    size_t len;
    size_t cap;
} sb_t;

static MLANG_CHAT_TLS char g_chat_last_error[256];

int32_t __mlang_std_chat_free(int64_t handle);

static void set_last_error(const char* msg)
{
    if(msg == NULL)
    {
        msg = "std::chat error";
    }
    (void)snprintf(g_chat_last_error, sizeof(g_chat_last_error), "%s", msg);
}

static char* dup_cstr(const char* s)
{
    size_t n = s ? strlen(s) : 0u;
    char* out = (char*)malloc(n + 1u);
    if(out == NULL)
    {
        set_last_error("std::chat out of memory");
        return NULL;
    }
    if(n > 0u)
    {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

static int replace_cstr(char** dst, const char* src)
{
    char* next = dup_cstr(src ? src : "");
    if(next == NULL)
    {
        return -1;
    }
    free(*dst);
    *dst = next;
    return 0;
}

static int sb_reserve(sb_t* sb, size_t need)
{
    if(need <= sb->cap)
    {
        return 0;
    }
    size_t next_cap = sb->cap == 0u ? 256u : sb->cap;
    while(next_cap < need)
    {
        next_cap *= 2u;
    }
    char* next = (char*)realloc(sb->buf, next_cap);
    if(next == NULL)
    {
        set_last_error("std::chat out of memory");
        return -1;
    }
    sb->buf = next;
    sb->cap = next_cap;
    return 0;
}

static int sb_append_n(sb_t* sb, const char* s, size_t n)
{
    if(sb_reserve(sb, sb->len + n + 1u) != 0)
    {
        return -1;
    }
    if(n > 0u)
    {
        memcpy(sb->buf + sb->len, s, n);
    }
    sb->len += n;
    sb->buf[sb->len] = '\0';
    return 0;
}

static int sb_append(sb_t* sb, const char* s)
{
    return sb_append_n(sb, s, s ? strlen(s) : 0u);
}

static int sb_append_repeat(sb_t* sb, char ch, size_t count)
{
    if(sb_reserve(sb, sb->len + count + 1u) != 0)
    {
        return -1;
    }
    for(size_t i = 0; i < count; ++i)
    {
        sb->buf[sb->len++] = ch;
    }
    sb->buf[sb->len] = '\0';
    return 0;
}

static char* sb_take(sb_t* sb)
{
    if(sb->buf == NULL)
    {
        char* empty = (char*)malloc(1u);
        if(empty == NULL)
        {
            set_last_error("std::chat out of memory");
            return NULL;
        }
        empty[0] = '\0';
        return empty;
    }
    char* out = sb->buf;
    sb->buf = NULL;
    sb->len = 0u;
    sb->cap = 0u;
    return out;
}

static void free_lines(chat_ui_t* ui)
{
    if(ui->lines == NULL)
    {
        return;
    }
    for(size_t i = 0; i < ui->len; ++i)
    {
        free(ui->lines[i].text);
    }
    free(ui->lines);
    ui->lines = NULL;
    ui->len = 0u;
    ui->cap = 0u;
}

static chat_ui_t* as_ui(int64_t handle)
{
    return (chat_ui_t*)(intptr_t)handle;
}

static int ensure_line_capacity(chat_ui_t* ui, size_t need)
{
    if(need <= ui->cap)
    {
        return 0;
    }
    size_t next_cap = ui->cap == 0u ? 32u : ui->cap;
    while(next_cap < need)
    {
        next_cap *= 2u;
    }
    chat_line_t* next = (chat_line_t*)realloc(ui->lines, next_cap * sizeof(chat_line_t));
    if(next == NULL)
    {
        set_last_error("std::chat out of memory");
        return -1;
    }
    ui->lines = next;
    ui->cap = next_cap;
    return 0;
}

static size_t max_scroll_rows(const chat_ui_t* ui, size_t body_rows)
{
    if(ui->len == 0u || body_rows == 0u)
    {
        return 0u;
    }
    return ui->len > body_rows ? (ui->len - body_rows) : 0u;
}

static void trim_oldest(chat_ui_t* ui)
{
    if(ui->max_lines == 0u)
    {
        return;
    }
    while(ui->len > ui->max_lines)
    {
        free(ui->lines[0].text);
        for(size_t i = 1; i < ui->len; ++i)
        {
            ui->lines[i - 1u] = ui->lines[i];
        }
        ui->len -= 1u;
        if(ui->scroll_rows > 0u)
        {
            ui->scroll_rows -= 1u;
        }
    }
}

static int push_owned_line(chat_ui_t* ui, char* line, int kind)
{
    if(line == NULL)
    {
        return -1;
    }
    if(ensure_line_capacity(ui, ui->len + 1u) != 0)
    {
        free(line);
        return -1;
    }
    ui->lines[ui->len].text = line;
    ui->lines[ui->len].kind = kind;
    ui->len += 1u;
    trim_oldest(ui);
    ui->scroll_rows = 0u;
    return 0;
}

static int push_line(chat_ui_t* ui, const char* prefix, const char* text, int kind)
{
    sb_t sb = {0};
    if(prefix && prefix[0] != '\0')
    {
        if(sb_append(&sb, prefix) != 0 || sb_append(&sb, " ") != 0)
        {
            free(sb.buf);
            return -1;
        }
    }
    if(sb_append(&sb, text ? text : "") != 0)
    {
        free(sb.buf);
        return -1;
    }
    return push_owned_line(ui, sb_take(&sb), kind);
}

static size_t clamp_scroll(const chat_ui_t* ui, size_t body_rows)
{
    size_t max_scroll = max_scroll_rows(ui, body_rows);
    return ui->scroll_rows > max_scroll ? max_scroll : ui->scroll_rows;
}

static size_t normalize_scroll(chat_ui_t* ui)
{
    size_t scroll = clamp_scroll(ui, ui->viewport_body_rows);
    ui->scroll_rows = scroll;
    return scroll;
}

static void scroll_up_clamped(chat_ui_t* ui, size_t amount)
{
    size_t max_scroll = max_scroll_rows(ui, ui->viewport_body_rows);
    if(amount > max_scroll)
    {
        amount = max_scroll;
    }
    ui->scroll_rows += amount;
    if(ui->scroll_rows > max_scroll)
    {
        ui->scroll_rows = max_scroll;
    }
}

static void clear_submitted(chat_ui_t* ui)
{
    free(ui->submitted);
    ui->submitted = dup_cstr("");
}

static int is_utf8_continuation(unsigned char ch)
{
    return (ch & 0xC0u) == 0x80u;
}

static size_t utf8_prev_boundary(const char* s, size_t pos)
{
    if(pos == 0u)
    {
        return 0u;
    }
    pos -= 1u;
    while(pos > 0u && is_utf8_continuation((unsigned char)s[pos]))
    {
        pos -= 1u;
    }
    return pos;
}

static size_t utf8_next_boundary(const char* s, size_t len, size_t pos)
{
    if(pos >= len)
    {
        return len;
    }
    pos += 1u;
    while(pos < len && is_utf8_continuation((unsigned char)s[pos]))
    {
        pos += 1u;
    }
    return pos;
}

static size_t utf8_codepoint_count_n(const char* s, size_t start, size_t end)
{
    size_t count = 0u;
    if(end < start)
    {
        return 0u;
    }
    size_t i = start;
    while(i < end)
    {
        i = utf8_next_boundary(s, end, i);
        count += 1u;
    }
    return count;
}

static size_t utf8_advance_codepoints(const char* s, size_t len, size_t start, size_t count)
{
    size_t i = start;
    while(i < len && count > 0u)
    {
        i = utf8_next_boundary(s, len, i);
        count -= 1u;
    }
    return i;
}

static int insert_bytes(chat_ui_t* ui, const char* bytes, size_t bytes_len)
{
    size_t len = strlen(ui->input);
    char* next = (char*)malloc(len + bytes_len + 1u);
    if(next == NULL)
    {
        set_last_error("std::chat out of memory");
        return -1;
    }
    if(ui->cursor > len)
    {
        ui->cursor = len;
    }
    memcpy(next, ui->input, ui->cursor);
    memcpy(next + ui->cursor, bytes, bytes_len);
    memcpy(next + ui->cursor + bytes_len, ui->input + ui->cursor, len - ui->cursor + 1u);
    free(ui->input);
    ui->input = next;
    ui->cursor += bytes_len;
    return 0;
}

static int backspace_char(chat_ui_t* ui)
{
    size_t len = strlen(ui->input);
    if(ui->cursor == 0u || len == 0u)
    {
        return 0;
    }
    size_t prev = utf8_prev_boundary(ui->input, ui->cursor);
    char* next = (char*)malloc(len);
    if(next == NULL)
    {
        set_last_error("std::chat out of memory");
        return -1;
    }
    memcpy(next, ui->input, prev);
    memcpy(next + prev, ui->input + ui->cursor, len - ui->cursor + 1u);
    free(ui->input);
    ui->input = next;
    ui->cursor = prev;
    return 0;
}

static int delete_to_end(chat_ui_t* ui)
{
    if(ui->cursor > strlen(ui->input))
    {
        ui->cursor = strlen(ui->input);
    }
    ui->input[ui->cursor] = '\0';
    return 0;
}

static int submit_input(chat_ui_t* ui)
{
    if(replace_cstr(&ui->submitted, ui->input) != 0)
    {
        return -1;
    }
    ui->input[0] = '\0';
    ui->cursor = 0u;
    return 0;
}

static int feed_key(chat_ui_t* ui, int32_t keycode)
{
    if(ui->esc_state == 1)
    {
        if(keycode == '[')
        {
            ui->esc_state = 2;
            return 0;
        }
        ui->esc_state = 0;
    }
    else if(ui->esc_state == 2)
    {
        ui->esc_state = 0;
        if(keycode == 'A')
        {
            scroll_up_clamped(ui, 1u);
            return 0;
        }
        if(keycode == 'B')
        {
            if(ui->scroll_rows > 0u)
            {
                ui->scroll_rows -= 1u;
            }
            return 0;
        }
        if(keycode == 'C')
        {
            size_t len = strlen(ui->input);
            if(ui->cursor < len)
            {
                ui->cursor = utf8_next_boundary(ui->input, len, ui->cursor);
            }
            return 0;
        }
        if(keycode == 'D')
        {
            if(ui->cursor > 0u)
            {
                ui->cursor = utf8_prev_boundary(ui->input, ui->cursor);
            }
            return 0;
        }
    }

    if(keycode == 27)
    {
        ui->esc_state = 1;
        return 0;
    }
    if(keycode == 13 || keycode == 10)
    {
        return submit_input(ui);
    }
    if(keycode == 127 || keycode == 8)
    {
        return backspace_char(ui);
    }
    if(keycode == 1)
    {
        ui->cursor = 0u;
        return 0;
    }
    if(keycode == 5)
    {
        ui->cursor = strlen(ui->input);
        return 0;
    }
    if(keycode == 2)
    {
        if(ui->cursor > 0u)
        {
            ui->cursor = utf8_prev_boundary(ui->input, ui->cursor);
        }
        return 0;
    }
    if(keycode == 6)
    {
        size_t len = strlen(ui->input);
        if(ui->cursor < len)
        {
            ui->cursor = utf8_next_boundary(ui->input, len, ui->cursor);
        }
        return 0;
    }
    if(keycode == 21)
    {
        ui->input[0] = '\0';
        ui->cursor = 0u;
        return 0;
    }
    if(keycode == 11)
    {
        return delete_to_end(ui);
    }
    if(keycode == 16)
    {
        scroll_up_clamped(ui, 1u);
        return 0;
    }
    if(keycode == 14)
    {
        if(ui->scroll_rows > 0u)
        {
            ui->scroll_rows -= 1u;
        }
        return 0;
    }
    if((keycode >= 32 && keycode <= 126) || (keycode >= 128 && keycode <= 255))
    {
        unsigned char ch = (unsigned char)keycode;
        char bytes[1];
        bytes[0] = (char)ch;
        return insert_bytes(ui, bytes, 1u);
    }
    return 0;
}

static int append_padded_line(sb_t* out, const char* prefix, const char* text,
                              size_t width, const char* reset_suffix)
{
    size_t used = 0u;
    if(prefix && prefix[0] != '\0')
    {
        if(sb_append(out, prefix) != 0)
        {
            return -1;
        }
        used += strlen(prefix);
    }
    size_t text_len = text ? strlen(text) : 0u;
    if(used < width)
    {
        size_t avail = width - used;
        size_t take = text_len < avail ? text_len : avail;
        if(take > 0u && sb_append_n(out, text, take) != 0)
        {
            return -1;
        }
        used += take;
    }
    if(used < width && sb_append_repeat(out, ' ', width - used) != 0)
    {
        return -1;
    }
    if(reset_suffix && reset_suffix[0] != '\0' && sb_append(out, reset_suffix) != 0)
    {
        return -1;
    }
    return sb_append(out, "\r\n");
}

static int append_body_line(sb_t* out, const chat_line_t* line, size_t width)
{
    const char* kind_prefix = "\x1b[37m";
    if(line->kind == 1)
    {
        kind_prefix = "\x1b[1;33m";
    }
    else if(line->kind == 2)
    {
        kind_prefix = "\x1b[1;36m";
    }
    return append_padded_line(out, kind_prefix, line->text, width, "\x1b[0m");
}

const char* __mlang_std_chat_last_error(void)
{
    if(g_chat_last_error[0] == '\0')
    {
        return "std::chat ok";
    }
    return g_chat_last_error;
}

int64_t __mlang_std_chat_new(int64_t max_lines)
{
    if(max_lines <= 0)
    {
        max_lines = 512;
    }
    chat_ui_t* ui = (chat_ui_t*)calloc(1u, sizeof(chat_ui_t));
    if(ui == NULL)
    {
        set_last_error("std::chat out of memory");
        return 0;
    }
    ui->max_lines = (size_t)max_lines;
    ui->title = dup_cstr("mlang chat");
    ui->server = dup_cstr("");
    ui->channel = dup_cstr("");
    ui->nick = dup_cstr("");
    ui->status = dup_cstr("Connected");
    ui->prompt = dup_cstr("> ");
    ui->input = dup_cstr("");
    ui->submitted = dup_cstr("");
    if(ui->title == NULL || ui->server == NULL || ui->channel == NULL || ui->nick == NULL ||
       ui->status == NULL || ui->prompt == NULL || ui->input == NULL || ui->submitted == NULL)
    {
        __mlang_std_chat_free((int64_t)(intptr_t)ui);
        return 0;
    }
    return (int64_t)(intptr_t)ui;
}

int32_t __mlang_std_chat_free(int64_t handle)
{
    chat_ui_t* ui = as_ui(handle);
    if(ui == NULL)
    {
        return 0;
    }
    free(ui->title);
    free(ui->server);
    free(ui->channel);
    free(ui->nick);
    free(ui->status);
    free(ui->prompt);
    free(ui->input);
    free(ui->submitted);
    free_lines(ui);
    free(ui);
    return 0;
}

int32_t __mlang_std_chat_set_title(int64_t handle, const char* title)
{
    return replace_cstr(&as_ui(handle)->title, title);
}

int32_t __mlang_std_chat_set_server(int64_t handle, const char* server)
{
    return replace_cstr(&as_ui(handle)->server, server);
}

int32_t __mlang_std_chat_set_channel(int64_t handle, const char* channel)
{
    return replace_cstr(&as_ui(handle)->channel, channel);
}

int32_t __mlang_std_chat_set_nick(int64_t handle, const char* nick)
{
    return replace_cstr(&as_ui(handle)->nick, nick);
}

int32_t __mlang_std_chat_set_status(int64_t handle, const char* status)
{
    return replace_cstr(&as_ui(handle)->status, status);
}

int32_t __mlang_std_chat_set_prompt(int64_t handle, const char* prompt)
{
    return replace_cstr(&as_ui(handle)->prompt, prompt);
}

int32_t __mlang_std_chat_set_input(int64_t handle, const char* input)
{
    chat_ui_t* ui = as_ui(handle);
    if(replace_cstr(&ui->input, input) != 0)
    {
        return -1;
    }
    ui->cursor = strlen(ui->input);
    return 0;
}

char* __mlang_std_chat_input(int64_t handle)
{
    chat_ui_t* ui = as_ui(handle);
    return dup_cstr(ui->input);
}

int32_t __mlang_std_chat_push_line(int64_t handle, const char* prefix, const char* text, int32_t kind)
{
    chat_ui_t* ui = as_ui(handle);
    return push_line(ui, prefix, text, kind);
}

int32_t __mlang_std_chat_feed_keycode(int64_t handle, int32_t keycode)
{
    chat_ui_t* ui = as_ui(handle);
    return feed_key(ui, keycode);
}

int32_t __mlang_std_chat_scroll(int64_t handle, int64_t delta)
{
    chat_ui_t* ui = as_ui(handle);
    if(ui == NULL)
    {
        return -1;
    }
    if(delta > 0)
    {
        scroll_up_clamped(ui, (size_t)delta);
    }
    else
    {
        size_t down = (size_t)(-delta);
        ui->scroll_rows = ui->scroll_rows > down ? (ui->scroll_rows - down) : 0u;
    }
    return 0;
}

int64_t __mlang_std_chat_scroll_pos(int64_t handle)
{
    chat_ui_t* ui = as_ui(handle);
    if(ui == NULL)
    {
        return 0;
    }
    return (int64_t)normalize_scroll(ui);
}

int64_t __mlang_std_chat_scroll_max(int64_t handle)
{
    chat_ui_t* ui = as_ui(handle);
    if(ui == NULL)
    {
        return 0;
    }
    normalize_scroll(ui);
    return (int64_t)max_scroll_rows(ui, ui->viewport_body_rows);
}

int32_t __mlang_std_chat_scroll_to_bottom(int64_t handle)
{
    chat_ui_t* ui = as_ui(handle);
    if(ui == NULL)
    {
        return -1;
    }
    ui->scroll_rows = 0u;
    return 0;
}

char* __mlang_std_chat_take_submitted(int64_t handle)
{
    chat_ui_t* ui = as_ui(handle);
    char* out = dup_cstr(ui->submitted);
    if(out == NULL)
    {
        return NULL;
    }
    clear_submitted(ui);
    return out;
}

char* __mlang_std_chat_render(int64_t handle, int32_t rows, int32_t cols)
{
    chat_ui_t* ui = as_ui(handle);
    if(ui == NULL)
    {
        return dup_cstr("");
    }

    size_t width = cols <= 8 ? 8u : (size_t)cols;
    size_t total_rows = rows <= 4 ? 4u : (size_t)rows;
    size_t body_rows = total_rows > 4u ? (total_rows - 4u) : 0u;
    ui->viewport_body_rows = body_rows;
    size_t scroll = normalize_scroll(ui);
    size_t visible_count = ui->len > body_rows ? body_rows : ui->len;
    size_t start = ui->len > visible_count + scroll ? (ui->len - visible_count - scroll) : 0u;
    size_t end = start + visible_count;

    sb_t out = {0};
    char header[1024];
    char status[1024];
    (void)snprintf(header, sizeof(header), "%s", ui->title ? ui->title : "");
    (void)snprintf(status, sizeof(status), "%s%s%s%s%s%s%s",
                   ui->nick && ui->nick[0] ? ui->nick : "nick?",
                   ui->channel && ui->channel[0] ? "  " : "",
                   ui->channel && ui->channel[0] ? ui->channel : "",
                   ui->server && ui->server[0] ? "  @ " : "",
                   ui->server && ui->server[0] ? ui->server : "",
                   ui->status && ui->status[0] ? "  |  " : "",
                   ui->status && ui->status[0] ? ui->status : "");

    if(sb_append(&out, "\x1b[H\x1b[2J") != 0)
    {
        free(out.buf);
        return NULL;
    }
    if(append_padded_line(&out, "\x1b[1;37;44m", header, width, "\x1b[0m") != 0)
    {
        free(out.buf);
        return NULL;
    }
    if(append_padded_line(&out, "\x1b[36m", status, width, "\x1b[0m") != 0)
    {
        free(out.buf);
        return NULL;
    }
    if(sb_append(&out, "\x1b[90m") != 0 || sb_append_repeat(&out, '-', width) != 0 || sb_append(&out, "\x1b[0m\r\n") != 0)
    {
        free(out.buf);
        return NULL;
    }

    for(size_t i = start; i < end; ++i)
    {
        if(append_body_line(&out, &ui->lines[i], width) != 0)
        {
            free(out.buf);
            return NULL;
        }
    }
    for(size_t i = visible_count; i < body_rows; ++i)
    {
        if(sb_append_repeat(&out, ' ', width) != 0 || sb_append(&out, "\r\n") != 0)
        {
            free(out.buf);
            return NULL;
        }
    }

    size_t prompt_len = ui->prompt ? strlen(ui->prompt) : 0u;
    size_t input_len = strlen(ui->input);
    size_t avail = width > prompt_len ? (width - prompt_len) : 0u;
    size_t view_start = 0u;
    size_t cursor_cells = utf8_codepoint_count_n(ui->input, 0u, ui->cursor);
    if(cursor_cells > avail && avail > 0u)
    {
        view_start = utf8_advance_codepoints(ui->input, input_len, 0u, cursor_cells - avail);
    }
    if(view_start > input_len)
    {
        view_start = input_len;
    }
    size_t view_end = utf8_advance_codepoints(ui->input, input_len, view_start, avail);
    size_t visible_input = view_end > view_start ? (view_end - view_start) : 0u;
    size_t visible_cells = utf8_codepoint_count_n(ui->input, view_start, view_end);

    if(sb_append(&out, "\x1b[30;47m") != 0)
    {
        free(out.buf);
        return NULL;
    }
    if(prompt_len > 0u && sb_append_n(&out, ui->prompt, prompt_len) != 0)
    {
        free(out.buf);
        return NULL;
    }
    if(visible_input > 0u && sb_append_n(&out, ui->input + view_start, visible_input) != 0)
    {
        free(out.buf);
        return NULL;
    }
    size_t used = prompt_len + visible_cells;
    if(used < width && sb_append_repeat(&out, ' ', width - used) != 0)
    {
        free(out.buf);
        return NULL;
    }
    if(sb_append(&out, "\x1b[0m") != 0)
    {
        free(out.buf);
        return NULL;
    }

    size_t cursor_col = prompt_len + utf8_codepoint_count_n(ui->input, view_start, ui->cursor) + 2u;
    if(cursor_col < 1u)
    {
        cursor_col = 1u;
    }
    if(cursor_col > width)
    {
        cursor_col = width;
    }

    char cursor_pos[64];
    (void)snprintf(cursor_pos, sizeof(cursor_pos), "\x1b[%zu;%zuH", total_rows, cursor_col);
    if(sb_append(&out, cursor_pos) != 0)
    {
        free(out.buf);
        return NULL;
    }
    return sb_take(&out);
}
