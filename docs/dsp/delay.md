# dsp::delay

Module file: `modules/dsp/delay.mla`

`StereoDelay` provides allocation-free forward and ping-pong processing after
construction. Forward mode feeds each stereo channel back into itself.
Ping-pong mode sends mono input to the left repeat and cross-feeds subsequent
repeats between left and right.

The feedback path can use no filter, a low-pass filter for progressively
darker repeats, or a high-pass filter for progressively thinner repeats.
`set_damping(0..1)` blends between unfiltered and filtered feedback. The wet
output tap uses this same filtered signal, so the selected filter is audible
on the first repeat as well as later feedback repeats.

```mla
mod dsp::delay;
use dsp::delay::DelayFrame;
use dsp::delay::DelayMode;
use dsp::delay::FeedbackFilter;
use dsp::delay::StereoDelay;

var delay: StereoDelay = StereoDelay::new(48000.0f, 2000.0f);
delay.set_delay_ms(375.0f);
delay.set_jitter_ms(1.2f);
delay.set_mode(DelayMode::PingPong);
delay.set_feedback(0.58f);
delay.set_mix(0.45f);
delay.set_filter(FeedbackFilter::Lowpass);
delay.set_filter_cutoff_hz(4200.0f);
delay.set_filter_resonance(0.35f);
delay.set_damping(0.75f);

let output: DelayFrame = delay.process_stereo(input_left, input_right);
```

The low-pass and high-pass modes use a topology-preserving two-pole filter.
`set_filter_resonance(0..1)` moves from a broadly damped response to a strong,
bounded peak around the selected cutoff.

`set_jitter_ms(depth)` adds slow, band-limited random modulation to the delay
time. Fractional delay interpolation keeps the movement smooth and creates
subtle analog-style pitch drift instead of abrupt whole-sample jumps. Use zero
to disable it. Allocate `max_delay_ms` with enough room for the base delay plus
the requested jitter depth.

Delay storage is allocated by `new(sample_rate_hz, max_delay_ms)`. Feedback is
clamped to `0..0.98`; mix and damping are clamped to `0..1`. `reset()` clears
the delay and filter histories while retaining parameters.

The WAV/AIFF delay example supports tempo-synchronized timing with `--bpm` and
`--delay-beats`. It calculates `60000 / bpm * delay_beats` milliseconds; use
`--bpm 0` to select the manual `--delay-ms` value instead. `--jitter-ms` exposes
the library's analog-style delay-time wander. `--dry-wet 0..1` crossfades from
the immediate source to the delayed signal; the package task spells this option
`dry_wet`.
