# dsp::punch_limiter

Module file: `modules/dsp/punch_limiter.mla`

`PunchLimiter` is a linked-stereo look-ahead brickwall maximizer with auto
release, a punch control that hands the loudest transients to a soft clipper
instead of the limiter, and a fat control that saturates and lifts the low
end. Its controls follow classic mastering maximizers (threshold drive,
output ceiling, auto release) and smooth compressor/limiters; it is an
original implementation and contains no proprietary code. Storage is
allocated only by `new()`; processing and parameter changes do not allocate
or lock.

For a plain look-ahead limiter without the character stages, see
[dsp::limiter](limiter.md).

```mla
mod dsp::punch_limiter;
use dsp::punch_limiter::PunchLimiter;
use dsp::punch_limiter::PunchLimiterFrame;
use dsp::punch_limiter::PunchLimiterMode;

var limiter: PunchLimiter = PunchLimiter::new(48000.0f);
limiter.set_mode(PunchLimiterMode::Maximizer); // or Smooth
limiter.set_threshold_db(-6.0f);  // -30 .. 0: drives the input by -threshold dB
limiter.set_ceiling_db(-0.3f);    // -12 .. 0: no output sample exceeds it
limiter.set_release_ms(60.0f);    // 1 .. 1000
limiter.set_arc(true);            // auto release
limiter.set_punch(0.3f);          // 0 .. 1
limiter.set_fat(0.3f);            // 0 .. 1
limiter.set_attack_ms(5.0f);      // Smooth mode compressor, 0.1 .. 50
limiter.set_bypass(false);        // latency-aligned dry signal when true

let output: PunchLimiterFrame = limiter.process_stereo(input_left, input_right);
let reduction_db: f32 = limiter.gain_reduction_db();
let latency: i64 = limiter.latency_samples(); // 1.5 ms look-ahead + 1
```

## Signal path

1. **Fat**: per channel, a two-pole low band (~110 Hz) is saturated with
   `tanh` and lifted by up to about +4.6 dB for quiet bass (less as it
   saturates), then added back in place of the clean low band.
2. **Drive**: × −threshold dB, gliding over ~10 ms. The limiter works on a
   normalised scale where 1.0 becomes the ceiling.
3. **Smooth mode only**: a stereo-linked soft-knee (6 dB) 3:1 compressor 6 dB
   under the limit, with the attack control and the release time. It adds no
   make-up gain, so at the same threshold Smooth is denser and a little
   quieter than Maximizer.
4. **Limiter**: the linked peak sets the gain needed to keep it under
   `1 + punch`. That gain is held across the 1.5 ms look-ahead, then released;
   a boxcar over the look-ahead ramps each reduction in, so the gain reaches
   its target just as the peak arrives, without a step.
5. **Release**: with auto release off, one exponential time constant. With it
   on, while the gain is more than 0.5 dB under its 250 ms average (a short
   peak) it recovers at 0.3 × the release, otherwise at 3 × the release, so
   brief peaks do not leave holes and sustained reduction does not pump.
6. **Punch clipper**: whatever is left over 1.0 (up to +6 dB at punch 1) is
   rounded off by a soft clipper: linear to a knee of `1 − punch/2`, then a
   `tanh` shoulder that approaches 1. Only the clipper's distortion is
   anti-aliased (first-order antiderivative method), and it is added to the
   clean signal delayed by one sample, so below the knee the output is
   bit-exact.
7. × ceiling (gliding), with a final clamp at the ceiling.

`gain_reduction_db()` is the compressor plus limiter reduction, not counting
the clipper. `reset()` clears all delay lines, filters and envelopes and
snaps the glides to their targets.
