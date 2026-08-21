# MLang Reverb2 Trance Sequence

This example schedules bass drum, clap, and closed/open TR-909-style hi-hat
samples at a configurable BPM and sends the clap through `dsp::reverb2`. The
default is the more controllable gated preset intended for tight trance claps.

Pattern:

```text
BD...BD+CL...BD...BD+CL
CH-OH-CH-CH-OH-CH-CH-OH-CH-OH-CH-CH-OH-CH-CH-OH
```

## Build and run

From this directory, run the gated-clap preset:

```sh
../../build/mlang pkg run demo \
  --option bpm=138 \
  --option reverb_type=gated \
  --option clap_mix=0.82 \
  --option kick_gain_db=0 \
  --option clap_gain_db=3 \
  --option hihat_gain_db=-3 \
  --option hihat_randomization=0.12 \
  --option hihat_seed=-1 \
  --option limiter_threshold_db=-3 \
  --option limiter_release_ms=120 \
  --option distortion_drive_db=6 \
  --option distortion_mix=0.12 \
  --option distortion_bias=0.04 \
  --option distortion_tone_hz=9000 \
  --option distortion_scope=master \
  --option gate_attack_ms=1 \
  --option gate_hold_ms=145 \
  --option gate_release_ms=45 \
  --option gate_threshold=0.01 \
  --option predelay_ms=20
```

Create a fully custom hall:

```sh
../../build/mlang pkg run demo \
  --option reverb_type=custom \
  --option clap_mix=0.72 \
  --option size=0.86 \
  --option decay_seconds=9 \
  --option damping=0.32 \
  --option bass_multiplier=1.08 \
  --option diffusion=0.9 \
  --option predelay_ms=28 \
  --option early_mix=0.12 \
  --option width=1 \
  --option mod_depth_ms=0.7 \
  --option mod_rate_hz=0.21
```

Run the persistent, ASR-10-inspired hall:

```sh
../../build/mlang pkg run demo \
  --option reverb_type=infinite-hall \
  --option clap_mix=0.75
```

This is an original infinite-hold effect, not a bit-exact Ensoniq emulation.
Because it intentionally does not decay, keep the input/output level moderate.

Other presets are `hall`, `room`, `plate`, `reverse`, `nonlinear-short`, and
`nonlinear-long`. Every custom option defaults to `-1`, meaning “keep the
preset value.” Gate controls are `gate`, `gate_attack_ms`, `gate_hold_ms`,
`gate_release_ms`, `gate_threshold`, and `gate_depth`. Additional controls cover
decay, damping, bass decay, diffusion, bandwidth, predelay, early/late balance,
stereo width, modulation, gain, and freeze.

`kick_gain_db`, `clap_gain_db`, and `hihat_gain_db` control sample levels. The
clap defaults to `+3 dB`, hats to `-3 dB`, and all accept `-60..+24 dB`.

The hi-hats run from the same `bpm` clock as the kick and clap, with one event
per sixteenth note. They share one centered mono voice: an open-hat step stops
the closed hat, and every closed-hat step chokes the open hat. The base pattern
is shown above. `hihat_randomization=0..1` is the probability of swapping CH
and OH on each step; `0` plays the exact pattern. `hihat_seed=-1` varies each
run, while a fixed integer makes the variations reproducible.

The complete kick, dry clap, and reverberated clap mix passes through a
linked-stereo `dsp::limiter`. `limiter_threshold_db` controls maximizer drive
from `-60..0 dB`: lower values make the result louder and cause more limiting.
`limiter_release_ms` controls gain recovery from `5..5000 ms`. Defaults are
`-3 dB` and `120 ms`; the limiter uses 5 ms look-ahead and a `-0.3 dB` ceiling.

Before limiting, the stereo master passes through two matched
`AnalogDistortion` processors for adjustable vinyl-like harmonic color. The
controls are `distortion_drive_db` (`0..36`), `distortion_mix` (`0..1`),
`distortion_bias` (`-0.5..0.5`), and `distortion_tone_hz`. The subtle defaults
are `6 dB`, `0.12`, `0.04`, and `9000 Hz`. Set `distortion_mix=0` for a clean
master. This stage adds saturation and tonal color, not record noise or clicks.

`distortion_scope` selects where the color is inserted:

- `master` processes the dry drums and completed reverb return before limiting.
- `clap-pre-reverb` processes the clap first, then sends that colored clap to
  both the dry clap path and `dsp::reverb2`. The resulting reverb return bypasses
  distortion and goes directly to the master limiter. `clap` is a short alias.
- `drums-pre-reverb` processes the kick, clap, and shared hi-hat voice with
  independent distortion state, but still sends only the colored clap into
  `dsp::reverb2`. The clean reverb return joins the colored drums before
  limiting. `drums` is an alias.

List or select an output device:

```sh
../../build/mlang pkg run list-devices

../../build/mlang pkg run demo \
  --option audio_output='Px7 S2' \
  --option reverb_type=gated
```

After linking, `./build/reverb2_trance_sequence --help` shows the corresponding
direct executable options.
