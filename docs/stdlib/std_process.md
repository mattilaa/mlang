# std::process

Module file: `stdlib/std/process.mla`

### Types
- `Child`
- `ChildStdin`
- `ChildStdout`
- `ChildStderr`
- `ExitStatus`
- `WaitPoll`
- `PipeRead`

### Spawn
- `spawn(program: str8, args: list<str8>) -> Result<Child, str8>`
- `spawn_inherit(program: str8, args: list<str8>) -> Result<Child, str8>`
- `last_error() -> str8`

### Child and pipe API
- `Child::stdin(self: Child) -> Result<ChildStdin, str8>`
- `Child::stdout(self: Child) -> Result<ChildStdout, str8>`
- `Child::stderr(self: Child) -> Result<ChildStderr, str8>`
- `Child::wait(self: Child) -> Result<ExitStatus, str8>`
- `Child::try_wait(self: Child) -> Result<WaitPoll, str8>`
- `Child::kill(self: Child, sig: i32) -> Result<i32, str8>`
- `Child::close(self: Child) -> i32`
- `ChildStdin::write(self: ChildStdin, s: str8) -> Result<i64, str8>`
- `ChildStdin::close(self: ChildStdin) -> i32`
- `ChildStdout::read(self: ChildStdout, buf: str8, capacity: i64) -> Result<i64, str8>`
- `ChildStdout::read_nonblocking(self: ChildStdout, buf: str8, capacity: i64) -> Result<PipeRead, str8>`
- `ChildStdout::close(self: ChildStdout) -> i32`
- `ChildStderr::read(self: ChildStderr, buf: str8, capacity: i64) -> Result<i64, str8>`
- `ChildStderr::read_nonblocking(self: ChildStderr, buf: str8, capacity: i64) -> Result<PipeRead, str8>`
- `ChildStderr::close(self: ChildStderr) -> i32`

### Exit status
- `ExitStatus::success(self: ExitStatus) -> i32`
- `ExitStatus::exited(self: ExitStatus) -> i32`
- `ExitStatus::code(self: ExitStatus) -> i32`
- `ExitStatus::signaled(self: ExitStatus) -> i32`
- `ExitStatus::signal(self: ExitStatus) -> i32`
