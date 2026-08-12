# MLang Reverb Techno Sequence

This MLang-only example loads two included mono WAV samples, schedules a
four-beat techno pattern at 130 BPM, sends the clap through a selectable
`dsp::reverb` type, and streams exactly 30 seconds through `std::audio`.

The musical render is exactly 30 seconds. A short silent queue guard remains
after it so the output stream can stop without reporting a shutdown underrun.

Pattern:

```text
BD...BD+CL...BD...BD+CL
```

- `samples/19_02_01.WAV`: bass drum on every quarter note
- `samples/19_02_08.WAV`: clap layered on beats 2 and 4

The samples are linearly resampled when the output device does not run at their
native 44.1 kHz rate. Beat boundaries are calculated from the absolute frame
position so rounding does not accumulate across the sequence.

## Build and run

From this directory:

```sh
../../build/mlang pkg run demo
```

The default package options are equivalent to:

```sh
../../build/mlang pkg run demo \
  --option bpm=130 \
  --option duration=30 \
  --option reverb_type=hall \
  --option clap_mix=0.45
```

Available reverb types are `hall`, `room`, `plate`, `gated`, `reverse`,
`nonlinear-short`, and `nonlinear-long`. The last four are inspired by Ensoniq
ASR-10 nonlinear effect types; they are not bit-exact ESP emulations.

Use `clap_mix=0` for a dry clap or `clap_mix=1` for a fully reverberated clap:

```sh
../../build/mlang pkg run demo \
  --option reverb_type=gated \
  --option clap_mix=1
```

List and select output devices by integer ID or name:

```sh
../../build/mlang pkg run list-devices

../../build/mlang pkg run demo \
  --option audio_output=0

../../build/mlang pkg run demo \
  --option audio_output='BlackHole 2ch' \
  --option reverb_type=gated \
  --option clap_mix=0.7
```

An empty `audio_output` or `-1` uses the default output device. A text value
first matches the complete CoreAudio/JACK device name, then a unique substring.
The copied WAV paths can be overridden with `kick_path` and `clap_path` package
options.

After building, run the executable directly for complete CLI help:

```sh
./build/reverb_techno_sequence --help
./build/reverb_techno_sequence --device 0 --reverb-type gated --clap-mix 0.7
./build/reverb_techno_sequence --device-name 'BlackHole 2ch' --reverb-type gated
```
