# std::thread

Module file: `stdlib/std/thread.mla`

### Threads
- `thread`: stdlib-owned native thread handle
- `spawn(fn_ptr: ptr<void>) -> thread` (compiler-lowered for function names and closures)
- `join(handle: thread) -> i32`

### Atomics
- `atomic64`: stdlib-owned native 64-bit atomic handle
- `atomic_new(initial: i64) -> atomic64`
- `atomic_load_value(handle: &atomic64) -> i64`
- `atomic_store_value(handle: &atomic64, value: i64) -> i64`
- `atomic_add_value(handle: &atomic64, delta: i64) -> i64`
- `atomic_free_handle(handle: atomic64) -> void`

Mutexes are provided by `std::sync` as `mutex`.
