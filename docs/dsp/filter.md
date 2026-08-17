# dsp::filter

Module file: `modules/dsp/filter.mla`

Real-time-safe stateful filtering, parameter ramps, and interpolation helpers.
The filter coefficient design follows the Robert Bristow-Johnson biquad
formulas used by LinuxSampler. Resonance is expressed in decibels: `0 dB`
gives a Butterworth-like response and positive values increase resonance.

## 12 dB biquad filters

`Biquad` is a second-order, 12 dB/octave filter and supports allocation-free
sample processing with these coefficient design methods:

- `set_lowpass(cutoff_hz, resonance_db, sample_rate_hz)`
- `set_highpass(cutoff_hz, resonance_db, sample_rate_hz)`
- `set_bandpass(cutoff_hz, resonance_db, sample_rate_hz)`
- `set_bandreject(cutoff_hz, resonance_db, sample_rate_hz)`
- `process(input) -> f32`
- `reset()`

The explicit-Q variants `set_lowpass_q`, `set_highpass_q`, and
`set_bandpass_q` are available for constructing tuned cascades.

Cutoff is clamped to `1 Hz .. 0.495 * sample_rate` and resonance to
`0 .. 36 dB` before coefficient calculation.

```mla
mod dsp::filter;
use dsp::filter::Biquad;

var filter: Biquad = Biquad::new();
filter.set_lowpass(1200.0f, 6.0f, 48000.0f);
let output: f32 = filter.process(input);
```

## Moog-style ladder filter

`MoogLadder` is a topology-preserving four-stage low-pass ladder with a
zero-delay resonance feedback path. The second and fourth stage outputs give
12 dB/octave and 24 dB/octave responses from the same API:

```mla
use dsp::filter::MoogLadder;

var ladder: MoogLadder = MoogLadder::new();
ladder.set_lowpass(1200.0f, 12.0f, 48000.0f);
let output24: f32 = ladder.process_24db(input);
```

Call `process_12db` instead for the two-pole output. Use separate instances if
both slopes are needed for the same input stream, because each call advances
the ladder state by one sample.

`set_lowpass` preserves the four integrator states, so cutoff and resonance may
be changed for every sample by a `LinearRamp` without zipper noise. Resonance
uses `0 .. 36 dB`; the upper endpoint approaches the self-oscillation boundary.

## 24 dB filters

The fourth-order filters cascade two biquads. Low-pass and high-pass use the
stage Q values for a Butterworth response at `0 dB` resonance; band-pass uses
the same tuned stage pair around its center frequency:

- `Lowpass24::set_lowpass(cutoff_hz, resonance_db, sample_rate_hz)`
- `Highpass24::set_highpass(cutoff_hz, resonance_db, sample_rate_hz)`
- `Bandpass24::set_bandpass(center_hz, resonance_db, sample_rate_hz)`

Each type provides `new()`, `process(input)`, and `reset()`. The low-pass and
high-pass stop bands roll off at 24 dB/octave. Cascading both band-pass stages
gives a 24 dB/octave rolloff on either side of its pass band. Resonance gain is
distributed across both stages, so the argument describes the complete
cascade instead of being applied twice.

```mla
use dsp::filter::Highpass24;

var filter: Highpass24 = Highpass24::new();
filter.set_highpass(1200.0f, 6.0f, 48000.0f);
let output: f32 = filter.process(input);
```

## Smooth automation

`LinearRamp` reaches an exact target after a specified sample count.
`SmoothedLowpass` combines two ramps with a low-pass biquad so cutoff and
resonance can change without an abrupt coefficient jump:

```mla
var filter: SmoothedLowpass = SmoothedLowpass::new(
    48000.0f, 12000.0f, 0.0f);
filter.set_target_ms(180.0f, 12.0f, 5000.0f);

// Call once per audio sample.
let output: f32 = filter.process(input);
```

These processing methods allocate no lists, perform no I/O, and acquire no
locks. Trigonometric coefficient calculation is bounded but runs per sample in
`SmoothedLowpass`; use a control-rate update strategy when CPU cost is more
important than sample-accurate automation.

## Sampler interpolation

- `nearest_interpolate_f32(x0, x1, fraction)` selects the closest endpoint.
- `linear_interpolate_f32(x0, x1, fraction)` blends two adjacent samples.
- `hermite_interpolate_f32(xm1, x0, x1, x2, fraction)` implements optimized
  four-point third-order Hermite interpolation with smooth boundary slopes.
- `cubic_interpolate_f32(xm1, x0, x1, x2, fraction)` is the compatibility name
  for the same Hermite/Catmull-Rom polynomial used by LinuxSampler.
- `bicubic_interpolate_f32(samples, x_fraction, y_fraction)` applies the same
  polynomial over a row-major 4x4 neighborhood for two-dimensional tables.

Nearest has the lowest CPU cost and most distortion; linear is a low-cost
middle ground; Hermite gives smoother audio with four points. Waveform
interpolation is one-dimensional, so the demo's selectable bicubic mode
reduces to cubic. The full bicubic function remains available for
two-dimensional DSP parameter tables.

## CoreAudio example

[`examples/package_manager_coreaudio_filter`](../../examples/package_manager_coreaudio_filter)
loads `examples/fft_example/illusion.wav` and runs 12 or 24 dB/octave cutoff
sweeps. Select `moog12`, `moog24`, `lowpass12`, `highpass12`, `bandpass12`, `lowpass24`,
`highpass24`, or `bandpass24` with `--filter`; `ladder12` and `ladder24` are
aliases for the Moog-style modes. The processor keeps filter history across sweep sections
and crossfades from the dry reference to filtered output to avoid transition
clicks. All file decoding and buffer allocation happens before the CoreAudio
callback starts.

Sampler interpolation is selected independently with `--interpolation` using
`nearest`, `linear`, `hermite`, `cubic`, or `bicubic`. In waveform playback,
the bicubic option reports and uses its one-dimensional cubic reduction; the
full 4x4 bicubic API is intended for 2D tables. Use `--playback-rate RATE` in
the range `0.25..4` to exercise fractional source positions. The runtime
interface prints both the audio filter type and interpolation type.
It prints each sweep step and its time range only when playback enters that
step instead of dumping the complete schedule at startup.

The binary reports the 24 dB/octave filter slope separately from each
section's resonance target. Its realtime resonance peak defaults to `+12 dB`
and can be changed with `--max-resonance-db DB` in the range `0..36`. The
limited target is reached with a per-frame linear ramp, so changing the peak
does not introduce a sudden coefficient change. Values above the default can
produce strong peaks and require additional gain reduction.

Input may be mono or stereo 16-bit PCM WAV, AIFF, or uncompressed AIFF-C.
Pass a file with `--audio-path PATH`, or with `--option audio_path=PATH` when
using the package task. Input paths beginning with `~/` are expanded by
`std::audio`. Use `--list-devices` to enumerate CoreAudio output ids and
`--device ID` to select one. Device `-1` uses the current system default.
