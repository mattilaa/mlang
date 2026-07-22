# std::audio

Module file: `stdlib/std/audio.mla`

Common audio hardware output helpers:
- macOS uses CoreAudio Audio Queue output.
- Linux uses JACK2 when `libjack` and a running JACK server are available.

Examples:
- `examples/std_audio_sine_demo.mla`
- `examples/std_audio_vst3_style_preview.mla`
- `examples/std_audio_simd_dsp_demo.mla`
- Full VST3/CoreAudio package demo: `examples/package_manager_vst3_coreaudio_synth`

### Types
- `AudioDevice`

### API
- `backend_name() -> str8`
- `last_error() -> str8`
- `device_count() -> i64`
- `default_output_device_id() -> i64`
- `device_name(device_id: i64) -> str8`
- `AudioDevice::open_default(client_name: str8) -> Result<AudioDevice, str8>`
- `AudioDevice::open_default_with_config(client_name: str8, sample_rate: i64, buffer_frames: i64) -> Result<AudioDevice, str8>`
- `AudioDevice::open(device_id: i64, client_name: str8) -> Result<AudioDevice, str8>`
- `AudioDevice::open_with_config(device_id: i64, client_name: str8, sample_rate: i64, buffer_frames: i64) -> Result<AudioDevice, str8>`
- `AudioDevice::start(self: AudioDevice) -> Result<i32, str8>`
- `AudioDevice::stop(self: AudioDevice) -> i32`
- `AudioDevice::close(self: AudioDevice) -> i32`
- `AudioDevice::sample_rate(self: AudioDevice) -> i64`
- `AudioDevice::buffer_frames(self: AudioDevice) -> i64`
- `AudioDevice::play_sine(self: AudioDevice, frequency_hz: f64, gain: f64, duration_ms: i64) -> Result<i32, str8>`
