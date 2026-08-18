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
  q/Q    stop playback and exit
```

## Ping-pong delay

```sh
cd examples/package_manager_delay_audio
../../build/mlang pkg run demo \
  --option audio_path=/path/to/input.aiff \
  --option mode=pingpong \
  --option delay_ms=375 \
  --option feedback=0.58 \
  --option mix=0.45 \
  --option filter=lowpass \
  --option filter_cutoff=4200 \
  --option filter_resonance=0.35 \
  --option damping=0.75
```

Ping-pong mode sums the input to mono for the wet path, sends the first repeat
left, and cross-feeds later repeats between channels. The dry path remains
stereo.

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

List or select output devices:

```sh
../../build/mlang pkg run list-devices
../../build/mlang pkg run demo --option audio_output=0
```

The built executable exposes the same settings with hyphenated options:

```sh
./build/delay_audio_demo --help
./build/delay_audio_demo --audio-path /path/to/input.wav --mode pingpong
```
