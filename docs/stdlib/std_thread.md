# std::thread

Module file: `stdlib/std/thread.mla`

### Thread and mutex
- `join(handle: Handle<Thread>) -> i32`
- `mutex_new() -> Handle<mutex>`
- `mutex_lock_handle(handle: Handle<mutex>) -> i32`
- `mutex_unlock_handle(handle: Handle<mutex>) -> i32`
- `mutex_free(handle: Handle<mutex>) -> i32`

### Atomics
- `atomic_new(initial: i64) -> Handle<Atomic64>`
- `atomic_load_value(handle: Handle<Atomic64>) -> i64`
- `atomic_store_value(handle: Handle<Atomic64>, value: i64) -> i64`
- `atomic_add_value(handle: Handle<Atomic64>, delta: i64) -> i64`
- `atomic_free_handle(handle: Handle<Atomic64>) -> void`
