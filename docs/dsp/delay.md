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
delay.set_mix_target(0.8f, 3840); // smooth 80 ms dry/wet change
delay.set_filter(FeedbackFilter::Lowpass);
delay.set_filter_wet_output(false); // feedback-only progressive damping
delay.set_filter_target(FeedbackFilter::Bandpass, 3840); // smooth 80 ms change
delay.set_filter_cutoff_hz(4200.0f);
delay.set_filter_cutoff_target_hz(1800.0f, 3840); // 80 ms at 48 kHz
delay.set_filter_resonance(0.35f);
delay.set_filter_resonance_target(0.8f, 3840);
delay.set_damping(0.75f);

let output: DelayFrame = delay.process_stereo(input_left, input_right);
```

The low-pass and high-pass modes use a topology-preserving two-pole filter.
`set_filter_resonance(0..1)` moves from a broadly damped response to a strong,
bounded peak around the selected cutoff. Resonant soft limiting controls the
peak without attenuating the filter's complete passband, keeping the wet signal
audible at high resonance settings. A separate unity-bounded filter feeds the
regenerative loop, so resonance can color the audible repeats without forcing
feedback values below `1` into permanent self-oscillation.

`set_filter_cutoff_target_hz(cutoff, ramp_samples)` linearly ramps the internal
filter coefficient from its current value. Retargeting during an active ramp
continues smoothly from the in-flight value; `set_filter_cutoff_hz()` remains
the immediate setter.

`set_filter_resonance_target(resonance, ramp_samples)` provides the same
sample-accurate, smoothly retargetable behavior for resonance. The immediate
`set_filter_resonance()` setter remains available.

`set_mix_target(mix, ramp_samples)` smoothly retargets the dry/wet crossfade
from its current in-flight value. `set_mix()` remains the immediate setter.

`FeedbackFilter` includes `None`, `Lowpass`, `Bandpass`, and `Highpass`.
`set_filter_target(type, ramp_samples)` crossfades their simultaneous
state-variable-filter taps without clearing filter history. This makes live
filter-type changes continuous; `set_filter()` remains the immediate setter.

`set_jitter_ms(depth)` adds slow, band-limited random modulation to the delay
time. Fractional delay interpolation keeps the movement smooth and creates
subtle analog-style pitch drift instead of abrupt whole-sample jumps. Use zero
to disable it. Allocate `max_delay_ms` with enough room for the base delay plus
the requested jitter depth.

Delay storage is allocated by `new(sample_rate_hz, max_delay_ms)`. Feedback is
clamped to `0..1.20`; mix and damping are clamped to `0..1`. Above `0.98`, the
feedback loop uses soft saturation so values above unity can produce bounded
dub-style self-oscillation. `reset()` clears delay and filter histories while
retaining parameters.

The WAV/AIFF delay example supports tempo-synchronized timing with `--bpm` and
`--delay-beats`. It calculates `60000 / bpm * delay_beats` milliseconds; use
`--bpm 0` to select the manual `--delay-ms` value instead. `--jitter-ms` exposes
the library's analog-style delay-time wander. `--dry-wet 0..1` crossfades from
the immediate source to the delayed signal; the package task spells this option
`dry_wet`.

During interactive playback, `z/x` lower or raise the feedback-filter cutoff
using the sample-accurate `cutoff_ramp_ms` smoothing time, `c/v` lower or raise
resonance using `resonance_ramp_ms`, and `a/s` lower or raise feedback. The
`d` key clears the loop and restores the example's safe `reset_feedback` value.
The example applies other updated parameters without stopping or resetting the
delay tail.

The example can write timestamped control actions with
`--record-session path.txt` and apply them again with
`--replay-session path.txt`. Package-task options use the names
`record_session` and `replay_session`. Elapsed-microsecond timestamps preserve
the automation timing when replaying through an output device with a different
sample rate; select that device with `audio_output`. Set the package option
`output_file=/path/to/take.wav` (or executable option `--output-file`) to stream
the processed stereo output, including automation and the delay tail, into a
16-bit PCM WAV file while it plays.

The example's `filter_scope=feedback` option calls
`set_filter_wet_output(false)`: the first repeat stays unfiltered while each
regeneration is damped continuously. `filter_scope=delay` also colors the wet
tap and therefore affects the first repeat.
