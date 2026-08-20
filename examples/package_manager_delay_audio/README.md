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

On an ANSI-capable interactive terminal, the demo uses `std::esc` alternate
screen mode for a fullscreen performance view. It redraws the current cutoff,
resonance, feedback, dry/wet, damping, delay time, jitter, and source position
as sliders, and shows the active filter and recording states. The cursor and
original terminal screen are restored on exit. When output is redirected or
the terminal has no ANSI support, playback continues with plain text output.

The program prints this control summary when it starts:

```text
interactive controls:
  Space  pause/resume source input (delay feedback keeps running)
  z/x    lower/raise feedback-filter cutoff
  c/v    lower/raise feedback-filter resonance
  l/b/h  select low-pass/band-pass/high-pass smoothly
  n/m    lower/raise dry-wet mix smoothly
  j/k    lower/raise analog jitter smoothly
  u/i    lower/raise delay time smoothly
  a/s    lower/raise delay feedback
  d/D    clear the loop and restore safe feedback
  q/Q    stop playback and exit
```

Each `z` or `x` press divides or multiplies the cutoff by `1.25`, clamped from
20 Hz to just below the output Nyquist frequency. The DSP ramps sample by sample
to the new cutoff over `cutoff_ramp_ms` (80 ms by default), including when a new
keypress redirects a ramp already in progress. Set it to `0` for immediate
changes. Each `a` or `s` press changes feedback by `0.02`, clamped to `0..1.20`.
Values near `1` create very long dub repeats; values above `1` drive bounded
self-oscillation through a soft-saturated loop. When feedback is above `0.98`,
one `a` press jumps directly to `0.96` so the loop starts decaying immediately.
Press `d` to clear the current loop and restore `reset_feedback` (0.68 by
default), then use `s` to build regeneration again. Uppercase keys work as
well, and the program prints the new value after every adjustment.

Each `c` or `v` press lowers or raises resonance by `0.05` in its `0..1` range.
The transition uses the sample-accurate `resonance_ramp_ms` duration (80 ms by
default), and rapid keypresses smoothly retarget an active ramp.

Press `l`, `b`, or `h` to select low-pass, band-pass, or high-pass while audio
continues. The DSP crossfades the state-variable filter taps over
`filter_type_ramp_ms` (80 ms by default), retaining filter history and avoiding
an abrupt output discontinuity. These keys are included in recorded and
replayed control sessions.

Each `n` or `m` press lowers or raises dry/wet by `0.05`. The transition uses
the sample-accurate `mix_ramp_ms` duration (80 ms by default), and repeated
keypresses smoothly retarget a mix ramp already in progress. Dry/wet changes
are also stored in control-session recordings.

Each `j` or `k` press lowers or raises jitter depth by `0.25` ms, clamped to
`0..50` ms. Each `u` or `i` press lowers or raises delay time by `10` ms,
clamped to `1..5000` ms. Both controls use sample-accurate, retargetable DSP
ramps (`jitter_ramp_ms` and `delay_ramp_ms`, 120 ms by default), with fractional
delay interpolation to avoid clicks. The existing `b` and `v` assignments stay
available for band-pass selection and resonance-up. Jitter and delay-time keys
are included in recorded and replayed sessions.

## Ping-pong delay

```sh
cd examples/package_manager_delay_audio
../../build/mlang pkg run demo \
  --option audio_path=/path/to/input.aiff \
  --option mode=pingpong \
  --option bpm=120 \
  --option delay_beats=0.75 \
  --option delay_ramp_ms=120 \
  --option jitter_ms=1.2 \
  --option jitter_ramp_ms=120 \
  --option feedback=0.58 \
  --option reset_feedback=0.68 \
  --option dry_wet=0.60 \
  --option mix_ramp_ms=80 \
  --option filter=lowpass \
  --option filter_scope=feedback \
  --option filter_type_ramp_ms=80 \
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
mix the direct file and repeats. The default is `0.60`, so filter and feedback
changes remain clearly audible during live monitoring. Use `0.75` or `1.0` when
you want to focus almost entirely on the processed repeats. The executable also
accepts the older `--mix` name as a compatibility alias.

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

Filters are `none`, `lowpass`, `bandpass`, or `highpass`. `filter_resonance` ranges from
`0` for a broadly damped response to `1` for a strong cutoff peak. Damping `0` leaves feedback
unfiltered; `1` applies the selected filter fully. `tail_ms` controls how long
silence is processed after the source ends so repeats can decay naturally.

`filter_scope=feedback` gives classic continuous feedback damping: the first
repeat is full-range, and every later trip through the loop applies the filter
again so repeats become progressively darker with a low-pass or thinner with a
high-pass. Live cutoff and resonance changes color the regenerated repeats while
leaving the first repeat intact. `filter_scope=delay` is the default and also
filters the audible wet tap, making the selected filter audible on the first
repeat. The executable spells this setting `--filter-scope feedback` or
`--filter-scope delay`.

## Save the processed audio

Set `output_file` to stream the same processed stereo signal sent to the audio
device into a 16-bit PCM WAV file. The recording includes live or replayed
control changes, pauses, and the delay tail. Pressing `q` stops recording and
finalizes the WAV so it can be opened immediately.

```sh
../../build/mlang pkg run demo \
  --option audio_path=~/Desktop/IllusionShort.aif \
  --option mode=pingpong \
  --option filter=lowpass \
  --option filter_cutoff=500 \
  --option filter_resonance=0.95 \
  --option damping=0.85 \
  --option feedback=0.68 \
  --option bpm=128 \
  --option delay_beats=0.75 \
  --option jitter_ms=1.2 \
  --option dry_wet=1.0 \
  --option output_file=~/Desktop/delay-take.wav
```

The output sample rate is the active audio-device rate. `output_file` is empty
by default, so ordinary playback does not write anything. Existing files at the
selected path are replaced when playback starts. The built executable uses the
hyphenated form `--output-file path.wav`.

## Record and replay a control session

Record the interactive Space, `z/x`, `c/v`, `a/s`, `d`, and `q` actions to a
text file while listening on the current interface:

```sh
../../build/mlang pkg run demo \
  --option audio_path=~/Desktop/IllusionShort.aif \
  --option mode=pingpong \
  --option filter=lowpass \
  --option filter_cutoff=500 \
  --option cutoff_ramp_ms=120 \
  --option filter_resonance=0.95 \
  --option resonance_ramp_ms=120 \
  --option damping=0.85 \
  --option feedback=0.68 \
  --option reset_feedback=0.68 \
  --option bpm=128 \
  --option delay_beats=0.75 \
  --option jitter_ms=1.2 \
  --option dry_wet=1.0 \
  --option record_session=delay-performance.txt
```

Replay the same automation while selecting the interface used by an external
recorder:

```sh
../../build/mlang pkg run demo \
  --option audio_path=~/Desktop/IllusionShort.aif \
  --option mode=pingpong \
  --option filter=lowpass \
  --option filter_cutoff=500 \
  --option cutoff_ramp_ms=120 \
  --option filter_resonance=0.95 \
  --option resonance_ramp_ms=120 \
  --option damping=0.85 \
  --option feedback=0.68 \
  --option reset_feedback=0.68 \
  --option bpm=128 \
  --option delay_beats=0.75 \
  --option jitter_ms=1.2 \
  --option dry_wet=1.0 \
  --option replay_session=delay-performance.txt \
  --option audio_output=0 \
  --option output_file=~/Desktop/delay-performance.wav
```

Use `pkg run list-devices` to find the new `audio_output` id. Session timestamps
are stored as elapsed microseconds, so replay timing remains stable if the new
interface uses a different sample rate. The text file records control actions;
use the same sound options on record and replay runs. Recording and replaying
cannot be enabled simultaneously. `output_file` records the processed audio
directly as an alternative to an external recorder. Manual `q` remains
available during replay.

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
