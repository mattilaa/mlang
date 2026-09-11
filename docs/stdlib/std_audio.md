# std::audio

Module file: `stdlib/std/audio.mla`

Common audio output and duplex processing helpers:
- macOS uses CoreAudio Audio Queue input/output.
- Linux uses JACK2 when `libjack` and a running JACK server are available.

Examples:
- `examples/std_audio_sine_demo.mla`
- `examples/std_audio_pcm_queue_demo.mla`
- `examples/std_audio_vst3_style_preview.mla`
- `examples/std_audio_simd_dsp_demo.mla`
- `examples/std_audio_insert_stack_demo.mla`
- `examples/std_audio_mixer_routing_demo.mla`
- Full VST3/CoreAudio package demo: `examples/package_manager_vst3_coreaudio_synth`

### Types
- `audio_device`
- `pcm_audio`
- `pcm_block`
- `audio_insert_stack`
- `audio_effect_rack`
- `audio_mixer`
- `audio_track`
- `audio_return_track`

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
- `pcm_block::left(self: pcm_block, frame: i64) -> f32`
- `pcm_block::right(self: pcm_block, frame: i64) -> f32`
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
- `audio_insert_stack::new(sample_rate: i64, buffer_frames: i64) -> result<audio_insert_stack, str8>`
- `audio_insert_stack::open_default(client_name: str8, sample_rate: i64, buffer_frames: i64) -> result<audio_insert_stack, str8>`
- `audio_insert_stack::add_gain(gain: f64, wet: f64) -> result<i64, str8>`
- `audio_insert_stack::add_lowpass(cutoff_hz: f64, wet: f64) -> result<i64, str8>`
- `audio_insert_stack::add_distortion(drive: f64, wet: f64) -> result<i64, str8>`
- `audio_insert_stack::add_delay(delay_ms: f64, feedback: f64, wet: f64) -> result<i64, str8>`
- `audio_insert_stack::add_rack(dry: f64, wet: f64) -> result<audio_effect_rack, str8>`
- `audio_insert_stack::process_block(input: pcm_block, output: pcm_block, frames: i64) -> result<i32, str8>`
- `audio_insert_stack::start()`, `stop()`, and `close()`
- `audio_effect_rack::set_mix(dry: f64, wet: f64) -> result<i32, str8>`
- `audio_effect_rack::add_gain(gain: f64) -> result<i64, str8>`
- `audio_effect_rack::add_lowpass(cutoff_hz: f64) -> result<i64, str8>`
- `audio_effect_rack::add_distortion(drive: f64) -> result<i64, str8>`
- `audio_effect_rack::add_delay(delay_ms: f64, feedback: f64) -> result<i64, str8>`

### Insert stacks and effect racks

`AudioInsertStack` receives stereo input, passes every frame through its serial
inserts in insertion order, then sends that result to zero or more parallel
effect-rack tracks. Each serial insert has its own `wet` crossfade. Each rack
has independent dry and wet output gains, and all enabled rack tracks are
summed into the stereo output.

The built-in real-time effects are linear gain, one-pole low-pass, normalized
soft-clipping distortion, and stereo feedback delay. Graph configuration and
delay-buffer allocation happen while stopped; the CoreAudio and JACK callbacks
do not allocate. A stack supports 16 serial inserts, 8 racks, and 8 effects per
rack. Stop the stack before changing its graph or rack mix.

For one conventional dry/effect blend, create a rack with `dry=1.0` and the
desired wet return. For multiple auxiliary-style racks, keep dry at `1.0` on
one rack and use `dry=0.0` on additional racks so the original signal is not
summed more than once. Gain values are intentionally not normalized, so the
caller is responsible for headroom and clipping control.

```rust
let stack: AudioInsertStack =
    AudioInsertStack::open_default("my_processor", 48000, 256).unwrap();
stack.add_gain(1.2, 1.0);
stack.add_distortion(2.0, 0.15);

let delay: AudioEffectRack = stack.add_rack(1.0, 0.30).unwrap();
delay.add_delay(240.0, 0.38);
delay.add_lowpass(5500.0);

stack.start();
```

`new()` creates the same DSP graph without hardware. Use `process_block()` to
render preallocated `PcmBlock` values in tests, file renderers, or other offline
processing. The input and output block may be the same block.

### Ableton-style track routing

`AudioMixer` owns the complete routing graph and is the only object that opens
hardware. Its master bus writes to one stereo output device. Ordinary
`AudioTrack` values and send-only `AudioReturnTrack` values are lightweight
references owned by that mixer; they must not be used after `mixer.close()`.

Each ordinary track has:

- one selected **Audio From** source: the default stereo hardware input or
  another ordinary track;
- an ordered insert stack of up to 16 built-in effects;
- a click-free volume/pan stage and mute control;
- up to 8 pre-fader or post-fader sends to return tracks; and
- one **Audio To** destination: another ordinary track or the master bus.

A return track has no hardware or track input selector. It receives the sum of
sends, processes its own insert stack and fader, and is always routed directly
to master. It cannot be routed back into an ordinary track. All paths therefore
converge on the mixer's single master hardware output.

Routing is compiled into a topological processing order whenever configuration
changes. A connection that would create direct or indirect audio feedback is
rejected and the previous valid route is restored. Configure effects and routes
while stopped. Matching settings such as `bus.set_input_track(input)` and
`input.set_output_track(bus)` describe one connection and are not mixed twice.

Every ordinary audio track automatically gets an independent, initially
disabled send slot for each configured return track, regardless of whether the
audio or return track was created first. Changing the level for Return A does
not affect Return B or the track's main Audio To route. A pre-fader send is
taken after inserts but before volume, pan, and mute; a post-fader send includes
those controls. Return tracks do not send to other returns, and ordinary Audio
To routes cannot target a return, which keeps the graph equivalent to
Ableton-style effect returns and prevents return feedback loops.

`set_volume(volume, ramp_ms)` and `set_pan(pan, ramp_ms)` publish lock-free
automation commands to the audio callback and may be called while playback is
running. Each parameter has independent state, and the callback interpolates
linearly for the exact requested number of sample frames. Use `ramp_ms=0` for
an immediate change. Volume accepts `0..16`; pan accepts `-1..1`, where `-1`
mutes the right channel and `1` mutes the left channel. Ramps up to 60 seconds
are supported. `set_gain()` remains an immediate volume compatibility method.

```rust
let mixer: AudioMixer = AudioMixer::open_default("session", 48000, 256).unwrap();
let input: AudioTrack = mixer.add_audio_track("Input 1").unwrap();
let bus: AudioTrack = mixer.add_audio_track("Processing Bus").unwrap();
let return_a: AudioReturnTrack = mixer.add_return_track("Return A").unwrap();

bus.set_input_track(input);       // Audio From: Input 1
input.set_output_track(bus);      // Audio To: Processing Bus
bus.set_output_master();          // Audio To: Master
return_a.set_output_master();     // Return tracks have one output

input.add_lowpass(10000.0, 0.25);
bus.add_distortion(1.8, 0.12);
return_a.add_delay(260.0, 0.40, 1.0);
bus.set_send(return_a, 0.30, true); // 30% post-fader send
bus.set_volume(0.8, 20.0);         // short startup ramp
bus.set_pan(-0.15, 20.0);
mixer.start();

// Safe while the audio callback is active.
bus.set_pan(0.50, 250.0);
```

Use `AudioMixer::new()` and `process_block()` for offline rendering. A mixer
supports 32 total audio/return tracks and up to 8 returns, matching the 8
independent send slots on every ordinary track. Gain staging is explicit:
track, send, return, and master gains are not automatically normalized or
limited.

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

### Build and run the live insert-stack example

```sh
cmake --build build --target mlang_std
./build/mlang -o build/std_audio_insert_stack_demo examples/std_audio_insert_stack_demo.mla
./build/std_audio_insert_stack_demo
```

The example processes the default stereo input for ten seconds through gain,
low-pass, and distortion inserts, then mixes a filtered delay rack into the
default output. On macOS, grant microphone access when prompted. On Linux,
start JACK2 first; the demo creates and auto-connects stereo capture and
playback ports.

### Build and run the mixer-routing example

```sh
cmake --build build --target mlang_std
./build/mlang -o build/std_audio_mixer_routing_demo examples/std_audio_mixer_routing_demo.mla
./build/std_audio_mixer_routing_demo
```

The demo uses matching Audio From/Audio To track routing, serial inserts, a
post-fader send, a filtered delay return, live click-free volume/pan ramps, and
one master hardware output. Avoid
placing the microphone close to the speakers; headphones are recommended for
live-input testing.
