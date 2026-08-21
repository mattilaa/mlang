# MLang Reverb2 Trance Sequence

This example copies the bass-drum and clap source sounds from the earlier
`package_manager_reverb_techno` example, schedules them at 138 BPM, and sends
the clap through `dsp::reverb2`. The default is the more controllable gated
preset intended for tight trance claps.

Pattern:

```text
BD...BD+CL...BD...BD+CL
```

## Build and run

From this directory, run the gated-clap preset:

```sh
../../build/mlang pkg run demo \
  --option reverb_type=gated \
  --option clap_mix=0.82 \
  --option kick_gain_db=0 \
  --option clap_gain_db=3 \
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

`kick_gain_db` and `clap_gain_db` control the sample levels before the clap is
sent to the reverb. The clap defaults to `+3 dB`; both accept `-60..+24 dB`.

List or select an output device:

```sh
../../build/mlang pkg run list-devices

../../build/mlang pkg run demo \
  --option audio_output='Px7 S2' \
  --option reverb_type=gated
```

After linking, `./build/reverb2_trance_sequence --help` shows the corresponding
direct executable options.
