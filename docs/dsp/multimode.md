# dsp::multimode

Module file: `modules/dsp/multimode.mla`

The eight filter responses of the `package_manager_coreaudio_filter` example
as reusable, realtime-safe processors. Used by the Mla Filter plugin and by
the 24 dB/Moog feedback filters of `dsp::delay`.

## Models

`FilterModel` (index order, also `filter_model_from_index(i)`):

| Index | Model | Implementation |
|---|---|---|
| 0 | `Lowpass12` | one `Biquad` low-pass |
| 1 | `Lowpass24` | two Butterworth-tuned biquads |
| 2 | `Highpass12` | one `Biquad` high-pass |
| 3 | `Highpass24` | two Butterworth-tuned biquads |
| 4 | `Bandpass12` | one `Biquad` band-pass |
| 5 | `Bandpass24` | two cascaded band-pass biquads |
| 6 | `Moog12` | `MoogLadder` two-pole output |
| 7 | `Moog24` | `MoogLadder` four-pole output |

Resonance is `0 .. 36 dB` for every model: `0 dB` is a flat Butterworth (or
non-resonant ladder) response, and 36 dB approaches self-oscillation.

## MultimodeFilter (mono)

- `MultimodeFilter::new()`: Moog 24 at 1 kHz
- `set_model(model)`: switch and clear history
- `configure(cutoff_hz, resonance_db, sample_rate_hz)`: design coefficients
- `process(input) -> f32`, `reset()`, `model()`

## StereoMultimodeFilter

A stereo filter with sample-accurate ramps and click-free model changes:

- `StereoMultimodeFilter::new(sample_rate_hz, cutoff_hz)`
- `set_model_target(model, ramp_samples)`: loads the model into an idle second
  filter pair and crossfades to it (`0` switches immediately)
- `set_cutoff_target_hz(hz, ramp_samples)`: ramps linearly in log-frequency
- `set_resonance_target_db(db, ramp_samples)`
- `process_stereo(left, right) -> MultimodeFrame` (`left()`, `right()`)
- `reset()`, `model()`, `cutoff_hz()`, `resonance_db()`

Coefficients are redesigned per sample only while a ramp runs. Nothing
allocates.

```mla
mod dsp::multimode;
use dsp::multimode::FilterModel;
use dsp::multimode::MultimodeFrame;
use dsp::multimode::StereoMultimodeFilter;

var filter: StereoMultimodeFilter = StereoMultimodeFilter::new(48000.0f, 8000.0f);
filter.set_model_target(FilterModel::Moog24, 0);
filter.set_resonance_target_db(12.0f, 0);
filter.set_cutoff_target_hz(300.0f, 48000); // one-second sweep down
let frame: MultimodeFrame = filter.process_stereo(left, right);
```
