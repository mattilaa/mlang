# std::sync

Module file: `stdlib/std/sync.mla`

### Types
- `Mutex`
- `Condvar`
- `Channel`
- `LockFreeQueue` (SPSC str8 queue)

### Mutex
- `Mutex::new() -> Result<Mutex, str8>`
- `Mutex::lock(self: Mutex) -> Result<i32, str8>`
- `Mutex::unlock(self: Mutex) -> Result<i32, str8>`
- `Mutex::close(self: Mutex) -> i32`

### Condvar
- `Condvar::new() -> Result<Condvar, str8>`
- `Condvar::wait(self: Condvar, mutex: Mutex) -> Result<i32, str8>`
- `Condvar::wait_timeout_ms(self: Condvar, mutex: Mutex, timeout_ms: i64) -> Result<i32, str8>`
- `Condvar::notify_one(self: Condvar) -> Result<i32, str8>`
- `Condvar::notify_all(self: Condvar) -> Result<i32, str8>`
- `Condvar::close(self: Condvar) -> i32`

### Channel (str8)
- `Channel::new(capacity: i64) -> Result<Channel, str8>`
- `Channel::send(self: Channel, s: str8) -> Result<i32, str8>`
- `Channel::post(self: Channel, s: str8) -> Result<i32, str8>` (alias of `send`)
- `Channel::recv(self: Channel, buf: str8, capacity: i64) -> Result<i64, str8>`
- `Channel::try_recv(self: Channel, buf: str8, capacity: i64) -> Result<i64, str8>`
- `Channel::close(self: Channel) -> i32`
- `Channel::free(self: Channel) -> i32`

### LockFreeQueue (str8, single-producer/single-consumer)
- `LockFreeQueue::new(capacity: i64) -> Result<LockFreeQueue, str8>`
- `LockFreeQueue::try_send(self: LockFreeQueue, s: str8) -> Result<i32, str8>`
  - returns `0` on success, `1` when full
- `LockFreeQueue::try_recv(self: LockFreeQueue, buf: str8, capacity: i64) -> Result<i64, str8>`
  - returns bytes copied (>0), `-2` when empty, `0` when closed and drained
- `LockFreeQueue::close(self: LockFreeQueue) -> i32`
- `LockFreeQueue::free(self: LockFreeQueue) -> i32`
