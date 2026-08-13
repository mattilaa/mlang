# std::sync

Module file: `stdlib/std/sync.mla`

### Types
- `mutex`
- `condvar`
- `channel`
- `lock_free_queue` (SPSC str8 queue)

### mutex
- `mutex::new() -> result<mutex, str8>`
- `mutex::lock(self: mutex) -> result<i32, str8>`
- `mutex::unlock(self: mutex) -> result<i32, str8>`
- `mutex::close(self: mutex) -> i32`

### condvar
- `condvar::new() -> result<condvar, str8>`
- `condvar::wait(self: condvar, mutex: mutex) -> result<i32, str8>`
- `condvar::wait_timeout_ms(self: condvar, mutex: mutex, timeout_ms: i64) -> result<i32, str8>`
- `condvar::notify_one(self: condvar) -> result<i32, str8>`
- `condvar::notify_all(self: condvar) -> result<i32, str8>`
- `condvar::close(self: condvar) -> i32`

### channel (str8)
- `channel::new(capacity: i64) -> result<channel, str8>`
- `channel::send(self: channel, s: str8) -> result<i32, str8>`
- `channel::post(self: channel, s: str8) -> result<i32, str8>` (alias of `send`)
- `channel::recv(self: channel, buf: str8, capacity: i64) -> result<i64, str8>`
- `channel::try_recv(self: channel, buf: str8, capacity: i64) -> result<i64, str8>`
- `channel::close(self: channel) -> i32`
- `channel::free(self: channel) -> i32`

### lock_free_queue (str8, single-producer/single-consumer)
- `lock_free_queue::new(capacity: i64) -> result<lock_free_queue, str8>`
- `lock_free_queue::try_send(self: lock_free_queue, s: str8) -> result<i32, str8>`
  - returns `0` on success, `1` when full
- `lock_free_queue::try_recv(self: lock_free_queue, buf: str8, capacity: i64) -> result<i64, str8>`
  - returns bytes copied (>0), `-2` when empty, `0` when closed and drained
- `lock_free_queue::close(self: lock_free_queue) -> i32`
- `lock_free_queue::free(self: lock_free_queue) -> i32`
