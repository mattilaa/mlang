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

### SpscQueue<T> (typed events)

`SpscQueue<T>` transfers fixed-size values between one producer and one consumer.
For example, the producer can be a sequencer and the consumer an audio renderer.
Queue storage is allocated by `new` before playback. Push and pop use atomic
indices and byte copies, with no allocation, deallocation, mutex, or retry loop.
Construction rejects targets whose index atomics are not lock-free.

```mlang
mod std::sync;
use std::sync::SpscQueue;

struct Event { var line: i32; var note: i32; };

fn main() -> i32 {
    let q: SpscQueue<Event> = SpscQueue<Event>::new(256);
    if !q.is_valid() { return 1; } // std::sync::last_error() describes failure
    let posted: bool = q.try_push(Event { line: 16, note: 60 });
    var event: Event = Event { line: 0, note: 0 };
    let received: bool = q.try_pop_into(&event);
    q.release(); // only after both threads stop using the queue
    return posted && received && event.note == 60 ? 0 : 1;
}
```

`try_push(value)` returns false when full. `try_pop_into(&event)` returns false
when empty and leaves the output unchanged. The output pointer must refer to
writable storage for a complete `T`. Capacity is the number of usable events;
it must be positive and need not be a power of two. Delivery is FIFO.

T must be self-contained: scalar numbers, enums, or structs recursively composed
of those. Do not use strings, lists, arrays, pointers, references, or types with
copy/drop hooks. The queue performs shallow byte copies, does not manage payload
ownership, and does not enforce this payload restriction at compile time.

Handle copies refer to the same queue. When passing through `thread::spawn`, pass
`q.handle` and reconstruct the same `SpscQueue<T>` in the worker. Do not reinterpret
a handle as a different event type. Call `release()` exactly once after all users
stop; it is explicit because automatic cleanup of a borrowed handle would release
shared storage prematurely. There is no close operation or automatic wakeup.

In the audio callback, poll a bounded number of events, then dispatch on an enum
field with `match`. Choose a producer overflow policy; never wait for space in
the audio callback. Timestamps are payload data: the renderer must retain future
events and apply events at the correct sample offset. The queue does not schedule
events or guarantee the timing of the renderer's own functions.

The current compiler emits allocating exception frames for ordinary free
functions. The example therefore uses renderer methods for dispatch; inspect
generated code for the entire callback path before using it for real-time audio.

See `examples/spsc_sequencer_events.mla` for typed event dispatch and
`tests/std_spsc_tests.mla` for capacity, wraparound, and concurrent delivery tests.
The older `LockFreeQueue` is string-based and allocates on send/frees on receive;
use `SpscQueue<T>` for fixed-size audio commands.
