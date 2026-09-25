# Mla Stutter

Tempo-synced stereo stutter (beat repeat) VST3 effect. It runs
[`dsp::stutter`](../../modules/dsp/stutter.mla) with a per-instance capture
buffer and a thin C++ VST3 wrapper following `mla_delay`.

The beat grid is cut into slices of the selected **Width**. When the stutter
engages, it waits for the next grid line, plays and records one slice, then
repeats that slice on every following grid line until it releases. In mlacker
the grid follows the sequencer: its BPM, and its song position while playing,
so repeats land on the pattern's beats.

## Build

From the repository root, with the compiler/runtime already built in `build/`:

```sh
build/mlang pkg --config plugins/mla_stutter/mlang.toml build
```

This fetches the same pinned VST3 SDK as the other plugins and builds:

```text
plugins/mla_stutter/build/cmake/VST3/Release/MlaStutter.vst3
```

To reuse an existing SDK checkout without fetching another copy:

```sh
mkdir -p plugins/mla_stutter/build/obj
build/mlang -c plugins/mla_stutter/src/mla_stutter_dsp.mla -O2 \
  -o plugins/mla_stutter/build/obj/mla_stutter_dsp.o
cmake -S plugins/mla_stutter -B plugins/mla_stutter/build/cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/mlacker/build/deps/vst3sdk" \
  -DMLANG_DSP_OBJECT=build/obj/mla_stutter_dsp.o
cmake --build plugins/mla_stutter/build/cmake --target MlaStutter -j4
```

CMake accepts `MLANG_STD_LIBRARY` to select another runtime archive. On Linux,
use a PIC runtime and DSP object, as described in the sibling `mla_verb`
project. Only macOS has been validated here.

## Use in mlacker

As an insert on one track (the usual stutter):

1. With Pattern view focused, press `f` to show the insert slots.
2. Select the track with `h/l` and a slot with `j/k`, then press Enter and
   select `MlaStutter.vst3`.
3. Enter on the slot again opens its parameter editor.

Or as an aux effect channel: **Effect → Add effect channel**, load the bundle,
set the track's send, and keep **Mix** at `1.0`.

With the default settings (**Stutter** Auto, **Width** 1/16, **Every** 4,
**Hold** 1 beat), the last beat of every bar repeats its first 1/16 note.
Change the sequencer BPM and the slices follow. Parameters persist through
`.mlack` sessions and plugin state/presets. Continuous values in mlacker's
generic editor use normalized `0..1`; list controls use indices.

## Controls

| Control | Physical range / choices | Default |
|---|---|---|
| Stutter | Off, Auto, On | Auto |
| Width | 1/1, 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64 | 1/16 |
| Every | 1–16 beats | 4 |
| Hold | Off, 1/4, 1/2, 1, 2, 3, 4, 6, 8, 12, 16 beats | 1 beat |
| Gate | 0.05–1 of each slice | 1 |
| Fade | 0–20 ms | 3 ms |
| Mix | 0 dry–1 stutter | 1 |
| Tempo Source | Host, Manual | Host |
| BPM | 20–400 | 120 |
| Bypass | Off / On | Off |
| Style | Repeat, Cut | Repeat |
| Origin | Grid, Beat, Bar | Grid |
| Variation | Straight, Roll, Reverse, Pitch Down, Pitch Up, Skip 3-3-2, Random | Straight |

Style, Origin and Variation come after Bypass in the editor. Their IDs were
added after the first ten, so sessions saved before they existed still load,
with these three at their defaults.

**Stutter**: **On** starts at the next Width grid line and repeats until it is
switched off, then fades back to the input over Fade. **Auto** repeats during
the last **Hold** beats of every **Every** beats, for hands-free fills.

**Width** is the slice length as a note value, in quarter-note beats: 1/4 is
one beat, 1/16 a quarter beat, T marks triplets. Narrowing Width while a
slice repeats replays its start faster, the classic stutter roll.

**Gate** keeps the first part of each slice and silences the rest. **Fade**
declicks the slice edges and engage/release; `0` gives sample-exact repeats.
**Mix** is the stutter's share while it is engaged. Outside a stutter the
input always passes unchanged.

**Style**: **Repeat** records a slice and replays it on every grid line.
**Cut** repeats nothing: the live sound keeps playing and is cut on the grid,
passing the first **Gate** share of each slice. Gate 1 would cut nothing, so
Cut treats it as 1/2 (a straight 1/16 chop by default).

**Origin** picks where a repeated slice starts. **Grid** takes it at the grid
line where the stutter engages. **Beat** takes it from the start of the beat
that line falls in, so engaging on the third 1/16 of a beat replays the beat's
first 1/16. **Bar** does the same from the start of the 4-beat bar. Origin only
matters for Repeat, and reaches back at most as far as the 12.5-second buffer
holds, less the slice.

**Variation** changes each repeat after the first slice (in Cut style it
changes each cut):

| Variation | Repeats |
|---|---|
| Straight | every slice alike: `e e e e` |
| Roll | halves the slice every two repeats (2, 4, 8, then 16 splits): a build-up |
| Reverse | every other repeat plays backwards |
| Pitch Down | each repeat a semitone lower, down to two octaves |
| Pitch Up | each repeat a semitone higher, up to two octaves |
| Skip 3-3-2 | only cells 1, 4 and 7 of every 8 sound: `e--e--e-` |
| Random | each repeat is normal, split in 2 or 4, reversed, an octave down, or a rest |

In Cut style, Reverse and the pitch variations cut straight, and Random picks
splits and rests.

**Tempo Source Host** uses the host tempo and, while the host plays, its
musical position (`projectTimeMusic`), so the grid matches the song. With no
host tempo, or with **Manual**, the BPM knob drives a free-running grid. The
longest slice, 1/1 at 20 BPM, is 12 seconds.

Automation uses the final value in each processing block, like the sibling
plugins. Bypass returns the dry input while the stutter keeps running.

MIDI mappings: CC 64 (sustain pedal) → Stutter (down On, up Off),
CC 1 → Width.

## Tests

```sh
build/mlang pkg --config plugins/mla_stutter/mlang.toml run test
build/mlang --tests tests/dsp_stutter_tests.mla
```

Or, after the local SDK build above:

```sh
cmake --build plugins/mla_stutter/build/cmake --target mla_stutter_tests -j4
ctest --test-dir plugins/mla_stutter/build/cmake --output-on-failure
```

Processor coverage includes pass-through when off, sample-exact repeats on the
grid, Cut chopping, Beat origin, Roll, loading states saved before the newer
controls, Width changes while holding, release, Auto periods, host tempo and
position sync (waiting for the grid line, and relocation), manual tempo, gate,
bypass, silent buffers, parameter metadata, state round-trip and
invalid-state rejection, and finite output for every width at extreme tempi.
