# dsp::gated_reverb

Module file: `modules/dsp/gated_reverb.mla`

`GatedVerb` is the 80s drum-machine gated reverb: a loud, dense, squashed
room that opens on each hit, holds, then is cut off by a short release that
darkens as it closes. It is the sound put on LinnDrum and Oberheim DMX
snares and toms with the gated programs of period units (AMS RMX16, Yamaha
REV7, the Ensoniq ASR-10's gated reverbs). Storage is allocated only by
`new()`; processing and parameter changes do not allocate or lock.

```mla
mod dsp::gated_reverb;
use dsp::gated_reverb::GatedVerb;
use dsp::gated_reverb::GatedVerbFrame;
use dsp::gated_reverb::GatedVerbType;

var verb: GatedVerb = GatedVerb::new(48000.0f);
verb.set_type(GatedVerbType::Room);
verb.set_size(0.5f);           // line lengths and tank decay
verb.set_hold_ms(250.0f);      // fully open time after the reverb starts
verb.set_release_ms(80.0f);    // closing decay
verb.set_damp(0.6f);           // how dark the tail gets as it closes
verb.set_tone_hz(9000.0f);     // brightness of the reverb
verb.set_threshold_db(-24.0f); // input level that opens the gate
verb.set_predelay_ms(8.0f);
verb.set_squash(0.6f);         // wet compression, flattens the tail
verb.set_width(1.0f);
verb.set_low_cut_hz(120.0f);   // keeps kicks out of the tank
verb.set_reverb_db(0.0f);      // reverb return level
verb.set_dry_db(0.0f);         // -60 mutes the dry path
verb.set_output_db(0.0f);

let output: GatedVerbFrame = verb.process_stereo(input_left, input_right);
```

## Types

| `GatedVerbType` | Character |
|---|---|
| `Room` | Bright live room with early reflections: the classic gated snare |
| `Plate` | Shorter, denser lines and more diffusion, no discrete reflections |
| `Reverse` | The reverb swells up (square-law) over the hold, then is cut |

## Gate

The gate is keyed from the dry input (peak follower, 10 ms release). When
the key crosses the threshold it opens at once, stays open for pre-delay +
hold, then releases over `release_ms` with an exponential curve forced to
reach zero, so the reverb ends exactly. A new hit re-arms once the key has
fallen 6 dB below the threshold and restarts the gate, even mid-release.
With nothing above the threshold the reverb stays shut. `envelope()` returns
the current gate gain and `is_open()` whether it is open or releasing.

During the release a two-pole low-pass on the wet signal sweeps from Tone
down by up to five octaves (`damp` = 1), so the tail dies dark instead of
just getting quieter. While closed the filter rests at its dark end; it jumps
back to Tone when the next hit opens the gate.

## Signal path

1. Mono sum, one-pole low cut and one-pole Tone low-pass.
2. Pre-delay (0 .. 100 ms). `Room` and `Reverse` add six early-reflection
   taps per side from the same line, 4 .. 40 ms past the pre-delay (scaled
   by size).
3. Four series allpass diffusers (1.4 .. 4.7 ms).
4. Eight-line feedback delay network (9 .. 54 ms lines) with a Householder
   mix, per-line damping at 1.5 × Tone and `tanh` on each write. Its decay is
   long (1.2 .. 3.6 s by size), so the tail stays dense for the whole hold.
5. Squash: a stereo-linked compressor with make-up gain. Above −24 dBFS the
   wet level is pulled toward about −9 dBFS with exponent `0.9 × squash`;
   at 1 it is almost flat, the dense wall of the 80s sound.
6. The closing low-pass, width (mid/side), then gate × reverb level, added to
   dry × dry level, times the output level. Level changes glide over ~10 ms.

`reset()` clears all lines, filters and the gate and snaps the level glides
to their targets.
