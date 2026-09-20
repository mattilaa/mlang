# Mla Drum

A VST3 drum **sampler instrument** (subcategory `Instrument|Drum`). Each instance
holds up to 16 samples ("pads") on consecutive keys, with one amp ADSR. The
intended setup is one instance per drum family, each on its own
[mlacker](../../mlacker) instrument track with its own insert effects. For
example, a kick through [Mla Distortion](../mla_distortion) and claps through
[Mla Verb](../mla_verb), all summed into the master.

The per-sample voice math (playhead, linear interpolation, ADSR, constant-power
pan, choke fade) is MLang: `src/mla_drum_dsp.mla` on top of the shared
[`dsp::envelope`](../../modules/dsp/envelope.mla) module. The C++ layer
(`src/plugin.cpp`) owns the sample buffers and the 32-voice pool, schedules MIDI
sample-accurately, maps parameters and persists state.

## Pads and keys

Pad 1 plays at **Root Key** (default MIDI note 36), pad 2 at 37, and so on up to
pad 16. Other keys, and pads without a sample, are silent. Put several hi-hats
on neighbouring pads, or load a single kick into pad 1 only.

Mono/stereo samples at any rate from 1 kHz to 384 kHz are resampled to the host
rate. Pads can be filled in three ways:

- **From mlacker**, see [Loading pads in mlacker](#loading-pads-in-mlacker).
- **From any host**, through the `IConnectionPoint` messages in
  [`stdlib/include/mla_sampler_protocol.h`](../../stdlib/include/mla_sampler_protocol.h):
  `mlang.sampler.loadFile` (a 16-bit PCM WAV/AIFF path), `mlang.sampler.loadPcm`
  (raw float PCM), `mlang.sampler.clear`, and `mlang.sampler.info` (reports the
  root key, pad count and loaded pads). The plug-in has no editor.
- **From saved state.** The VST3 component state embeds every pad's audio, so
  DAW projects and presets restore the full kit.

## Parameters

| Parameter      | Range                         | Notes |
|----------------|-------------------------------|-------|
| Level          | -60 .. +6 dB (bottom = off)   | Instance output level. MIDI CC 7. |
| Tune           | ±24 semitones                 | Added to each pad's tune. |
| Velocity       | 0..1                          | 0 = every hit at full level, 1 = fully velocity-scaled. |
| Mode           | Poly / Mono                   | **Mono**: a new hit chokes whatever is sounding (3 ms fade), e.g. closed hi-hat cutting the open one. |
| Trigger        | One-shot / Gated              | One-shot ignores note-off. Gated releases the envelope on note-off. |
| Root Key       | MIDI 0..127                   | Key of pad 1. |
| Attack         | 0 .. 2 s                      | Linear. MIDI CC 73. |
| Decay          | 1 ms .. 10 s                  | Exponential, measured to a 60 dB fall. MIDI CC 75. |
| Sustain        | 0..1                          | 0 ends the hit after the decay, even in one-shot mode. |
| Release        | 1 ms .. 10 s                  | Exponential, used on note-off in Gated mode. MIDI CC 72. |
| Pad N Level    | -60 .. +6 dB                  | Per pad. |
| Pad N Pan      | L .. R                        | Constant-power; centre is unity in both channels. |
| Pad N Tune     | ±24 semitones                 | Per pad. |

A voice ends when its sample ends, when its envelope finishes, or when it is
choked. With all 32 voices busy, the oldest is stolen.

## Build

Prerequisites: the repository's compiler/runtime built in `build/` (so
`build/libmlang_std.a` exists), CMake, a C++17 compiler, and Git. From this
directory:

```sh
../../build/mlang pkg --config mlang.toml build   # -> build/cmake/VST3/Release/MlaDrum.vst3
../../build/mlang pkg --config mlang.toml run test
```

`build` fetches the Steinberg VST3 SDK 3.8.1 (commit
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`) into the ignored `build/`, compiles the
MLang voice to an object and builds the bundle. `test` builds and runs
`tests/mla_drum_tests.cpp`, an offline host that checks rendered audio: key/pad
mapping, sample-accurate starts, level/pan/tune/velocity, resampling, the ADSR,
Mono choke vs Poly, One-shot vs Gated, WAV loading, error replies, and state
round trips. The SDK's `validator` target also passes (47/47).

The WAV/AIFF decoder is the runtime's `std::audio` loader, so on macOS the bundle
links CoreAudio, AudioToolbox, AudioUnit, AVFoundation and CoreMIDI. On Linux,
build PIC objects as described in the [Mla Verb README](../mla_verb/README.md).

## Loading pads in mlacker

1. **Track → Create Instrument track**, then **Instrument → Add instrument** and
   choose `MlaDrum.vst3`. Repeat for each drum instance you want (kick, snare,
   hats, ...). Each instance gets its own track.
2. Select the instance in **View → Instruments**, then use either:
   - **Instrument → Drum pads → Load pad sample from file**: pick a key on the piano
     keyboard, then a WAV/AIFF. The file is also added to the Audio list, so the
     session embeds it.
   - **Instrument → Drum pads → Send audio sample to pad**: sends the sample selected in
     **View → Audio** to the key you pick.

   The piano marks loaded pads. Enter on one replaces its sample, and Backspace
   clears it. Samples can be trimmed, faded or normalized with
   **Audio → Edit sample (destructive)**. Saving an edit re-sends it to every pad
   that uses the sample.
3. Put effects on each drum's track (`f` in Pattern view, see mlacker's
   **Track inserts**), for example Mla Distortion on the kick and Mla Verb on the
   claps. The tracks are summed into the master.

`.mlack` sessions store which session sample each pad uses and re-send them on
open and after audio-device changes.

### Presets

- **In mlacker**, **Instrument → Presets → Save plugin preset** writes a kit preset
  (`.mlapre` 1.1) with all parameters and the pad samples. **Presets → Load plugin preset**
  restores the whole kit, clearing pads the kit does not use.
- **In other hosts**, the plugin's VST3 state embeds every pad, so the host's own
  preset and project saving keep the full kit.

## Design notes

- **MLang limits shape the split.** MLang cannot index a raw `ptr<f32>`, and a
  fixed `array<>` field in a `calloc`'d struct reads as empty. So each voice is a
  scalar-only value struct behind an opaque handle. C++ passes the two stereo
  frames around the playhead (`mladrum_voice_frame`) to `mladrum_voice_render`.
- **Realtime-safe sample swaps.** Pads are immutable `Sample` objects owned by the
  control thread and published through atomics. The audio thread picks them up
  at block start and stops voices on a replaced pad. The old sample is freed only
  after two more completed blocks.
- **`IConnectionPoint` is exposed deliberately.** `SingleComponentEffect` hides it
  from hosts, and Mla Drum re-exposes it for the sampler messages. A host that
  connects the component to itself is accepted without keeping a
  self-reference.
