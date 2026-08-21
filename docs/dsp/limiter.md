# dsp::limiter

Module file: `modules/dsp/limiter.mla`

`dsp::limiter` provides an allocation-free linked-stereo look-ahead peak
limiter for master outputs. It is an original implementation using general
dynamics-processing techniques; it does not reproduce proprietary Waves code.

The threshold works like a maximizer drive control. Lowering it adds makeup
gain before limiting, while the output ceiling remains fixed. A shared detector
and gain envelope preserve the stereo image. The detector has immediate gain
reduction, a look-ahead hold, exponential release, and a final sample-peak
ceiling guard.

## API

- `StereoLimiter::new(sample_rate_hz)`
- `process_stereo(left, right) -> LimiterFrame`
- `set_threshold_db(value)` — maximizer threshold from `-60..0 dB`
- `set_release_ms(value)` — recovery time from `5..5000 ms`
- `set_ceiling_db(value)` — sample-peak ceiling from `-24..0 dB`
- `set_lookahead_ms(value)` — look-ahead latency from `0..20 ms`
- `current_gain()` — current linked linear envelope gain
- `reset()` — clear delayed audio and restore unity envelope gain

Defaults are a `-3 dB` threshold, `-0.3 dB` ceiling, `120 ms` release, and
`5 ms` look-ahead.

```mla
mod dsp::limiter;

use dsp::limiter::LimiterFrame;
use dsp::limiter::StereoLimiter;

var limiter: StereoLimiter = StereoLimiter::new(48000.0f);
limiter.set_threshold_db(-6.0f);
limiter.set_release_ms(180.0f);

let frame: LimiterFrame = limiter.process_stereo(master_left, master_right);
let output_left: f32 = frame.left();
let output_right: f32 = frame.right();
```

Storage is allocated by `new()`. Processing and parameter setters do not
allocate or lock. Look-ahead adds the configured latency to the output.
