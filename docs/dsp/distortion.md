# dsp::distortion

Module file: `modules/dsp/distortion.mla`

`AnalogDistortion` is an allocation-free mono waveshaper for realtime audio.
It combines asymmetric `tanh` soft clipping, a one-pole tone filter, and a
coupling-capacitor-style DC blocker.

```mla
mod dsp::distortion;
use dsp::distortion::AnalogDistortion;

var distortion: AnalogDistortion = AnalogDistortion::new(48000.0f);
distortion.set_drive_db(15.0f);
distortion.set_bias(0.12f);
distortion.set_tone_hz(6000.0f, 48000.0f);
distortion.set_output_gain_db(-6.0f);
distortion.set_mix(1.0f);
let output: f32 = distortion.process(input);
```

Drive is clamped to `0..36 dB`, bias to `-0.5..0.5`, mix to `0..1`, and output
gain to `-24..+12 dB`. `reset()` clears the tone and DC-blocker histories while
retaining all parameter values.
