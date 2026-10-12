# std::stacktrace

Module file: `stdlib/std/stacktrace.mla`

`std::stacktrace` captures a bounded snapshot of the current native call stack,
inspired by C++23 `std::stacktrace`. POSIX targets use `backtrace`; Windows
uses `CaptureStackBackTrace`. Captured addresses and platform descriptions are
owned by a native snapshot, so call `close()` when the snapshot is no longer
needed.

```mla
mod std::stacktrace;
use std::stacktrace::capture;
use std::stacktrace::StackTrace;

fn inspect_call_stack() {
    var trace: StackTrace = capture(32);
    for frame in 0..trace.len() {
        let address: option<u64> = trace.address(frame);
        let description: option<str8> = trace.description(frame);
        if address.is_some() && description.is_some() {
            println(format!("{}: {} ({})", frame,
                            description.unwrap(), address.unwrap()));
        }
    }
    trace.close();
}
```

- `capture(max_frames)` captures up to 256 frames (a lower positive limit may
  be requested); zero or negative limits produce an empty trace.
- `capture_skipping(skip_frames, max_frames)` omits additional caller frames.
- `len`, `is_empty`, `address(index)`, and `description(index)` inspect the
  snapshot. Invalid frame indices return `None`.
- `close()` releases the native snapshot and is safe to call more than once.

The implementation provides raw instruction addresses and platform-provided
symbol strings. It does not promise source file/line resolution, demangling, or
identical symbol formatting across operating systems.
