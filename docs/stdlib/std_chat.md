# std::chat

Module file: `stdlib/std/chat.mla`

### Types
- `ChatUi`

### API
- `line_normal() -> i32`
- `line_system() -> i32`
- `line_self() -> i32`
- `last_error() -> str8`
- `ChatUi::new(max_lines: i64) -> Result<ChatUi, str8>`
- `ChatUi::close(self: ChatUi) -> i32`
- `ChatUi::set_title(self: ChatUi, title: str8) -> i32`
- `ChatUi::set_server(self: ChatUi, server: str8) -> i32`
- `ChatUi::set_channel(self: ChatUi, channel: str8) -> i32`
- `ChatUi::set_nick(self: ChatUi, nick: str8) -> i32`
- `ChatUi::set_status(self: ChatUi, status: str8) -> i32`
- `ChatUi::set_prompt(self: ChatUi, prompt: str8) -> i32`
- `ChatUi::set_input(self: ChatUi, input: str8) -> i32`
- `ChatUi::input(self: ChatUi) -> str8`
- `ChatUi::push_line(self: ChatUi, prefix: str8, text: str8) -> i32`
- `ChatUi::push_system(self: ChatUi, text: str8) -> i32`
- `ChatUi::push_self(self: ChatUi, prefix: str8, text: str8) -> i32`
- `ChatUi::feed_keycode(self: ChatUi, keycode: i32) -> i32`
- `ChatUi::scroll(self: ChatUi, delta: i64) -> i32`
- `ChatUi::take_submitted(self: ChatUi) -> str8`
- `ChatUi::render(self: ChatUi, rows: i32, cols: i32) -> str8`
