# dsp::chorus

Module file: `modules/dsp/chorus.mla`

`JunoChorus` is a stereo ensemble chorus modelled on the Roland Juno-60/106.
A mono mix of the input goes into one bucket-brigade style delay line, read at
two taps swept in anti-phase by one triangle LFO. The left output is the dry
signal plus tap 1, the right the dry signal plus tap 2. Storage is allocated
only by `new()`; processing and parameter changes do not allocate or lock.

```mla
mod dsp::chorus;
use dsp::chorus::ChorusFrame;
use dsp::chorus::ChorusMode;
use dsp::chorus::JunoChorus;

var chorus: JunoChorus = JunoChorus::new(48000.0f);
chorus.set_mode(ChorusMode::Two);
chorus.set_mix(0.5f);        // the Juno: dry and each tap at full level
chorus.set_depth(1.0f);      // 0 .. 1.5 x the Juno's sweep
chorus.set_width(1.0f);      // 0 mono .. 1 Juno .. 1.5
chorus.set_tone_hz(9000.0f); // BBD filter cutoff
chorus.set_drive(0.3f);      // compander warmth
chorus.set_noise(true);      // BBD hiss through the line
chorus.set_noise_db(-62.0f);
chorus.set_output_db(0.0f);

let output: ChorusFrame = chorus.process_stereo(input_left, input_right);
```

## Modes

| `ChorusMode` | LFO | Tap delay sweep |
|---|---|---|
| `One` | 0.513 Hz | 1.66 .. 5.35 ms |
| `Two` | 0.863 Hz | 1.66 .. 5.35 ms |
| `Both` | 9.75 Hz | 3.3 .. 3.7 ms |
| `Off` | — | dry; the wet path fades out over 20 ms |

These are commonly cited measurements of the Juno chorus. The rate changes
at once; the sweep centre and depth glide over about 50 ms, so modes can be
switched while audio runs.

## Signal path

1. Mono sum of the input, plus white noise when `set_noise(true)` and the
   chorus is not `Off`.
2. Two one-pole low-passes at `set_tone_hz` (2 .. 18 kHz, kept below 0.45 × the
   sample rate), like the BBD's anti-alias filter.
3. Soft saturation `tanh(k·x)/k`, `k = 0.3 + 3·drive`, like the compander.
4. The delay line (16 ms), read at the two taps with Hermite interpolation.
5. Two one-pole low-passes per tap at the same cutoff, like the BBD's
   reconstruction filter.
6. Width scales the taps' difference; mix sets dry and wet gains: below 0.5
   the wet fades, above 0.5 the dry fades, and at 0.5 both are at full level.

`reset()` clears the line, filters and LFO and snaps the sweep and the Off
fade to their targets.
