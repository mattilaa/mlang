# std::audio

Module file: `stdlib/std/audio.mla`

Common audio hardware output helpers:
- macOS uses CoreAudio Audio Queue output.
- Linux uses JACK2 when `libjack` and a running JACK server are available.

Examples:
- `examples/std_audio_sine_demo.mla`
- `examples/std_audio_pcm_queue_demo.mla`
- `examples/std_audio_vst3_style_preview.mla`
- `examples/std_audio_simd_dsp_demo.mla`
- Full VST3/CoreAudio package demo: `examples/package_manager_vst3_coreaudio_synth`

### Types
- `audio_device`
- `pcm_audio`
- `pcm_block`

### API
- `backend_name() -> str8`
- `last_error() -> str8`
- `device_count() -> i64`
- `default_output_device_id() -> i64`
- `device_name(device_id: i64) -> str8`
- `pcm_audio::load(path: str8) -> result<pcm_audio, str8>`
- `pcm_audio::sample_rate(self: pcm_audio) -> i64`
- `pcm_audio::channels(self: pcm_audio) -> i64`
- `pcm_audio::frame_count(self: pcm_audio) -> i64`
- `pcm_audio::samples(self: pcm_audio) -> list<f32>`
- `pcm_audio::close(self: pcm_audio) -> i32`
- `pcm_block::new(capacity_frames: i64) -> result<pcm_block, str8>`
- `pcm_block::capacity_frames(self: pcm_block) -> i64`
- `pcm_block::set_stereo(self: pcm_block, frame: i64, left: f32, right: f32) -> i32`
- `pcm_block::clear(self: pcm_block) -> i32`
- `pcm_block::close(self: pcm_block) -> i32`
- `audio_device::open_default(client_name: str8) -> result<audio_device, str8>`
- `audio_device::open_default_with_config(client_name: str8, sample_rate: i64, buffer_frames: i64) -> result<audio_device, str8>`
- `audio_device::open(device_id: i64, client_name: str8) -> result<audio_device, str8>`
- `audio_device::open_with_config(device_id: i64, client_name: str8, sample_rate: i64, buffer_frames: i64) -> result<audio_device, str8>`
- `audio_device::start(self: audio_device) -> result<i32, str8>`
- `audio_device::stop(self: audio_device) -> i32`
- `audio_device::close(self: audio_device) -> i32`
- `audio_device::sample_rate(self: audio_device) -> i64`
- `audio_device::buffer_frames(self: audio_device) -> i64`
- `audio_device::pcm_capacity_frames(self: audio_device) -> i64`
- `audio_device::pcm_queued_frames(self: audio_device) -> i64`
- `audio_device::pcm_available_frames(self: audio_device) -> i64`
- `audio_device::pcm_underrun_count(self: audio_device) -> i64`
- `audio_device::clear_pcm_queue(self: audio_device) -> i32`
- `audio_device::queue_interleaved_f32(self: audio_device, samples: &list<f32>) -> result<i64, str8>`
- `audio_device::queue_pcm_block(self: audio_device, block: pcm_block, frames: i64) -> result<i64, str8>`
- `audio_device::play_sine(self: audio_device, frequency_hz: f64, gain: f64, duration_ms: i64) -> result<i32, str8>`

### PCM queue

`queue_interleaved_f32` copies `[L0, R0, L1, R1, ...]` stereo samples into a
preallocated single-producer/single-consumer ring. The call is nonblocking and
all-or-nothing: it returns an error if the list contains an odd sample count or
the full block does not fit. Queue initial audio before `start()`, then use
`pcm_available_frames()` to apply backpressure while producing later blocks.

The CoreAudio and JACK callbacks only read the ring and write their native
buffers; they do not allocate or lock. `pcm_underrun_count()` counts callbacks
that requested PCM after the ring became empty. Call `clear_pcm_queue()` while
the device is stopped.

`PcmAudio` decodes mono or stereo 16-bit PCM WAV, AIFF, and uncompressed
AIFF-C (`NONE`, `twos`, or `sowt`). `samples()` returns source-channel
interleaved `f32` samples. Paths beginning with `~/` are expanded before open.

`PcmBlock` is a fixed-size native stereo producer buffer. Allocate it before
starting playback, update frames with `set_stereo()`, and enqueue it with
`queue_pcm_block()`. Reusing one block avoids MLang list growth and allocation
in realtime producer loops.

### Build and test the PCM example

```sh
cmake --build build --target mlang_std
./build/mlang -o build/std_audio_pcm_queue_demo examples/std_audio_pcm_queue_demo.mla -L ./build -lmlang_std
./build/std_audio_pcm_queue_demo --help
./build/std_audio_pcm_queue_demo --device -1 --frequency 440 --duration 500
./build/mlang test tests/std_audio_tests.mla
```

On macOS, `--device -1` uses the CoreAudio default output. On Linux, start a
JACK2 server first; `std::audio` creates stereo JACK output ports and connects
them to the selected physical output pair.
