# WAV/AIFF Delay Demo

This example decodes a mono or stereo WAV, AIFF, or uncompressed AIFF-C file
with `std::audio`, processes it through `dsp::delay::StereoDelay`, and streams
the result to the selected output device. Linear interpolation handles a file
sample rate that differs from the active output rate.

While it is running in an interactive terminal, press **Space** to pause or
resume the source file. Pausing sends silence into the delay but keeps delay
processing and feedback active, so you hear the existing repeats decay after
the source cuts out. Playback resumes from the same file position. Press
**q** or **Q** to stop playback, restore the terminal, and exit immediately.

The program prints this control summary when it starts:

```text
interactive controls:
  Space  pause/resume source input (delay feedback keeps running)
  z/x    lower/raise feedback-filter cutoff
  c/v    lower/raise feedback-filter resonance
  a/s    lower/raise delay feedback
  q/Q    stop playback and exit
```

Each `z` or `x` press divides or multiplies the cutoff by `1.25`, clamped from
20 Hz to just below the output Nyquist frequency. The DSP ramps sample by sample
to the new cutoff over `cutoff_ramp_ms` (80 ms by default), including when a new
keypress redirects a ramp already in progress. Set it to `0` for immediate
changes. Each `a` or `s` press changes feedback by `0.02`, clamped to the safe
`0..0.98` range. Uppercase keys work as well, and the program prints the new
value after every adjustment.

Each `c` or `v` press lowers or raises resonance by `0.05` in its `0..1` range.
The transition uses the sample-accurate `resonance_ramp_ms` duration (80 ms by
default), and rapid keypresses smoothly retarget an active ramp.

## Ping-pong delay

```sh
cd examples/package_manager_delay_audio
../../build/mlang pkg run demo \
  --option audio_path=/path/to/input.aiff \
  --option mode=pingpong \
  --option bpm=120 \
  --option delay_beats=0.75 \
  --option jitter_ms=1.2 \
  --option feedback=0.58 \
  --option dry_wet=0.45 \
  --option filter=lowpass \
  --option filter_cutoff=4200 \
  --option cutoff_ramp_ms=80 \
  --option filter_resonance=0.35 \
  --option resonance_ramp_ms=80 \
  --option damping=0.75
```

Ping-pong mode sums the input to mono for the wet path, sends the first repeat
left, and cross-feeds later repeats between channels. The dry path remains
stereo.

When `bpm` is greater than zero, the demo synchronizes the delay to the track
tempo and ignores `delay_ms`. `delay_beats` is measured in quarter-note beats:
`1` is a quarter note, `0.5` is an eighth note, `0.75` is a dotted eighth, and
approximately `0.333333` is a triplet eighth. The delay time is calculated as
`60000 / bpm * delay_beats`. Set `bpm=0` to use `delay_ms` directly.

`jitter_ms` adds slow, smoothly interpolated random movement around the synced
or manual delay time. Small values such as `0.5` to `2` ms give repeats a
subtle analog tape/BBD character; `0` keeps the delay mathematically exact.

`dry_wet` controls the output crossfade. At `0`, only the immediate source file
is heard. At `1`, the immediate source is removed and only the delayed signal
is heard, which makes the first repeat easy to distinguish. Intermediate values
mix the direct file and repeats. The executable also accepts the older `--mix`
name as a compatibility alias.

## Forward delay

```sh
../../build/mlang pkg run demo \
  --option audio_path=/path/to/input.wav \
  --option mode=forward \
  --option delay_ms=250 \
  --option feedback=0.42 \
  --option filter=highpass \
  --option filter_cutoff=650 \
  --option filter_resonance=0.2 \
  --option damping=0.55
```

Filters are `none`, `lowpass`, or `highpass`. `filter_resonance` ranges from
`0` for a broadly damped response to `1` for a strong cutoff peak. Damping `0` leaves feedback
unfiltered; `1` applies the selected filter fully. `tail_ms` controls how long
silence is processed after the source ends so repeats can decay naturally.
The filtered wet tap is used for output too, so filtering is audible beginning
with the first repeat rather than only after the signal recirculates.

List or select output devices:

```sh
../../build/mlang pkg run list-devices
../../build/mlang pkg run demo --option audio_output=0
```

The built executable exposes the same settings with hyphenated options:

```sh
./build/delay_audio_demo --help
./build/delay_audio_demo --audio-path /path/to/input.wav --mode pingpong --bpm 128 --delay-beats 0.75 --jitter-ms 1.2
```
