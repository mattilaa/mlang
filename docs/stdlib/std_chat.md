# std::chat

Module file: `stdlib/std/chat.mla`

### Types
- `chat_ui`

### API
- `line_normal() -> i32`
- `line_system() -> i32`
- `line_self() -> i32`
- `last_error() -> str8`
- `chat_ui::new(max_lines: i64) -> Result<chat_ui, str8>`
- `chat_ui::close(self: chat_ui) -> i32`
- `chat_ui::set_title(self: chat_ui, title: str8) -> i32`
- `chat_ui::set_server(self: chat_ui, server: str8) -> i32`
- `chat_ui::set_channel(self: chat_ui, channel: str8) -> i32`
- `chat_ui::set_nick(self: chat_ui, nick: str8) -> i32`
- `chat_ui::set_status(self: chat_ui, status: str8) -> i32`
- `chat_ui::set_prompt(self: chat_ui, prompt: str8) -> i32`
- `chat_ui::set_input(self: chat_ui, input: str8) -> i32`
- `chat_ui::input(self: chat_ui) -> str8`
- `chat_ui::push_line(self: chat_ui, prefix: str8, text: str8) -> i32`
- `chat_ui::push_system(self: chat_ui, text: str8) -> i32`
- `chat_ui::push_self(self: chat_ui, prefix: str8, text: str8) -> i32`
- `chat_ui::feed_keycode(self: chat_ui, keycode: i32) -> i32`
- `chat_ui::scroll(self: chat_ui, delta: i64) -> i32`
- `chat_ui::take_submitted(self: chat_ui) -> str8`
- `chat_ui::render(self: chat_ui, rows: i32, cols: i32) -> str8`
