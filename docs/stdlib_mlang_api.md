# Mlang Stdlib Module API {#stdlib_mlang_api}

This page is the index for the **MLang-facing stdlib modules** under `stdlib/std/`. Detailed APIs are split by topic so audio, DSP, IO, collections, runtime, and other areas live on separate paths.

- [Language Built-ins](language_builtins.md) documents compiler-provided collection methods, enum behavior, aliases, `bit`, and `size_of`.

## Import List

These are the documented APIs you import in Mlang source via:

```mla
mod std::algorithm::fft;
mod std::algorithm::numeric;
mod std::algorithm::order;
mod std::algorithm::ranges;
mod std::argparser;
mod std::array;
mod std::audio;
mod std::bench;
mod std::bits;
mod std::bitset;
mod std::bytes;
mod std::chat;
mod std::compiler;
mod std::date;
mod std::env;
mod std::esc;
mod std::event_loop;
mod std::exceptions;
mod std::fs;
mod std::gps;
mod std::hash;
mod std::image;
mod std::io;
mod std::ipc;
mod std::json;
mod std::jsonrpc;
mod std::math;
mod std::net;
mod std::platform;
mod std::printf;
mod std::process;
mod std::protocol;
mod std::rand;
mod std::regex;
mod std::sed;
mod std::serde;
mod std::simd;
mod std::span;
mod std::strbuf;
mod std::sync;
mod std::term;
mod std::testing;
mod std::thread;
mod std::time;
mod std::timer;
mod std::unordered;
mod std::vec;
```

## Source Files

The source-of-truth implementation files are:
- `stdlib/std/algorithm/fft.mla`
- `stdlib/std/algorithm/numeric.mla`
- `stdlib/std/algorithm/order.mla`
- `stdlib/std/algorithm/ranges.mla`
- `stdlib/std/argparser.mla`
- `stdlib/std/array.mla`
- `stdlib/std/audio.mla`
- `stdlib/std/bench.mla`
- `stdlib/std/bits.mla`
- `stdlib/std/bitset.mla`
- `stdlib/std/bytes.mla`
- `stdlib/std/chat.mla`
- `stdlib/std/compiler.mla`
- `stdlib/std/date.mla`
- `stdlib/std/env.mla`
- `stdlib/std/esc.mla`
- `stdlib/std/event_loop.mla`
- `stdlib/std/exceptions.mla`
- `stdlib/std/fs.mla`
- `stdlib/std/gps.mla`
- `stdlib/std/hash.mla`
- `stdlib/std/image.mla`
- `stdlib/std/io.mla`
- `stdlib/std/ipc.mla`
- `stdlib/std/json.mla`
- `stdlib/std/jsonrpc.mla`
- `stdlib/std/math.mla`
- `stdlib/std/net.mla`
- `stdlib/std/platform.mla`
- `stdlib/std/printf.mla`
- `stdlib/std/process.mla`
- `stdlib/std/protocol.mla`
- `stdlib/std/rand.mla`
- `stdlib/std/regex.mla`
- `stdlib/std/sed.mla`
- `stdlib/std/serde.mla`
- `stdlib/std/simd.mla`
- `stdlib/std/span.mla`
- `stdlib/std/strbuf.mla`
- `stdlib/std/sync.mla`
- `stdlib/std/term.mla`
- `stdlib/std/testing.mla`
- `stdlib/std/thread.mla`
- `stdlib/std/time.mla`
- `stdlib/std/timer.mla`
- `stdlib/std/unordered.mla`
- `stdlib/std/vec.mla`

## Algorithms

- [std::algorithm::fft](stdlib/std_algorithm_fft.md)
- [std::algorithm::numeric](stdlib/std_algorithm_numeric.md)
- [std::algorithm::order](stdlib/std_algorithm_order.md)
- [std::algorithm::ranges](stdlib/std_algorithm_ranges.md)

## Audio and DSP

- [std::audio](stdlib/std_audio.md)
- [std::simd](stdlib/std_simd.md)

## Collections and Data

- [std::array](stdlib/std_array.md)
- [std::bits](stdlib/std_bits.md)
- [std::bitset](stdlib/std_bitset.md)
- [std::bytes](stdlib/std_bytes.md)
- [std::serde](stdlib/std_serde.md)
- [std::span](stdlib/std_span.md)
- [std::strbuf](stdlib/std_strbuf.md)
- [std::unordered](stdlib/std_unordered.md)
- [std::vec](stdlib/std_vec.md)

## Compiler, Runtime, and Testing

- [std::argparser](stdlib/std_argparser.md)
- [std::bench](stdlib/std_bench.md)
- [std::chat](stdlib/std_chat.md)
- [std::compiler](stdlib/std_compiler.md)
- [std::date](stdlib/std_date.md)
- [std::env](stdlib/std_env.md)
- [std::event_loop](stdlib/std_event_loop.md)
- [std::exceptions](stdlib/std_exceptions.md)
- [std::platform](stdlib/std_platform.md)
- [std::sync](stdlib/std_sync.md)
- [std::testing](stdlib/std_testing.md)
- [std::thread](stdlib/std_thread.md)
- [std::time](stdlib/std_time.md)
- [std::timer](stdlib/std_timer.md)

## IO, IPC, Networking, and Terminal

- [std::esc](stdlib/std_esc.md)
- [std::fs](stdlib/std_fs.md)
- [std::image](stdlib/std_image.md)
- [std::io](stdlib/std_io.md)
- [std::ipc](stdlib/std_ipc.md)
- [std::net](stdlib/std_net.md)
- [std::printf](stdlib/std_printf.md)
- [std::process](stdlib/std_process.md)
- [std::protocol](stdlib/std_protocol.md)
- [std::term](stdlib/std_term.md)

## Parsing, Text, Math, and Utilities

- [std::gps](stdlib/std_gps.md)
- [std::hash](stdlib/std_hash.md)
- [std::json](stdlib/std_json.md)
- [std::jsonrpc](stdlib/std_jsonrpc.md)
- [std::math](stdlib/std_math.md)
- [std::rand](stdlib/std_rand.md)
- [std::regex](stdlib/std_regex.md)
- [std::sed](stdlib/std_sed.md)
