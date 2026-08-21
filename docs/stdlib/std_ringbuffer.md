# std::ringbuffer

Module file: `stdlib/std/ringbuffer.mla`

`RingBufferF32` (lowercase alias `ring_buffer_f32`) is a fixed-capacity numeric
ring buffer intended for sample history, delay lines, and other realtime DSP
work. Construction allocates a zero-filled buffer; subsequent operations do
not allocate.

```mla
mod std::ringbuffer;
use std::ringbuffer::ring_buffer_f32;

var history: ring_buffer_f32 = ring_buffer_f32::new(1024);
history.write(0.5f);
let newest: f32 = history.read(1);
```

- `write(value)` appends and overwrites the oldest value when full.
- `read(delay)` reads history without removing it; `1` is the newest value.
- `process(value)` writes and returns the overwritten value, or zero until full.
- `capacity()`, `len()`, and `is_empty()` inspect state.
- `clear()` zeros storage without reallocating.
