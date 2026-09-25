# Mla JunoChorus

Stereo ensemble chorus VST3 effect in the style of the Roland Juno-60/106
chorus. It runs [`dsp::chorus`](../../modules/dsp/chorus.mla) with a thin C++
VST3 wrapper following `mla_delay`.

Like the Juno, it feeds a mono mix into one bucket-brigade (BBD) style delay
line and reads it at two taps swept in opposite directions by a single
triangle LFO: left is dry plus tap 1, right is dry plus tap 2. The pitch
wobble on each side runs against the other, which is what makes the sound
wide and thick while staying centred in mono. The wet path is band-limited
before and after the line like the BBD's filters, softly saturated like its
compander, and can carry the BBD's hiss.

## Build

From the repository root, with the compiler/runtime already built in `build/`:

```sh
build/mlang pkg --config plugins/mla_juno_chorus/mlang.toml build
```

This fetches the same pinned VST3 SDK as the other plugins and builds:

```text
plugins/mla_juno_chorus/build/cmake/VST3/Release/MlaJunoChorus.vst3
```

To reuse an existing SDK checkout without fetching another copy:

```sh
mkdir -p plugins/mla_juno_chorus/build/obj
build/mlang -c plugins/mla_juno_chorus/src/mla_juno_chorus_dsp.mla -O2 \
  -o plugins/mla_juno_chorus/build/obj/mla_juno_chorus_dsp.o
cmake -S plugins/mla_juno_chorus -B plugins/mla_juno_chorus/build/cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/mlacker/build/deps/vst3sdk" \
  -DMLANG_DSP_OBJECT=build/obj/mla_juno_chorus_dsp.o
cmake --build plugins/mla_juno_chorus/build/cmake --target MlaJunoChorus -j4
```

CMake accepts `MLANG_STD_LIBRARY` to select another runtime archive. Only macOS
has been validated here.

## Use in mlacker

As an insert on a synth or pad track (the usual place for it): in Pattern view
press `f` to show the insert slots, pick the track with `h/l` and a slot with
`j/k`, press Enter and select `MlaJunoChorus.vst3`. Enter on the slot again
opens its parameters.

As an aux effect channel, choose **Effect → Add effect channel**, load the
bundle and set the track's send; set **Mix** to `1.0` there so the return is
wet only.

Parameters persist through `.mlack` sessions and plugin state/presets.
Continuous values in mlacker's generic editor use normalized `0..1`; list
controls use indices.

## Controls

| Control | Physical range / choices | Default |
|---|---|---|
| Chorus | Off, I, II, I+II | I |
| Mix | 0 dry – 0.5 Juno – 1 wet | 0.5 |
| Depth | 0–1.5 × the Juno's sweep | 1 |
| Width | 0 mono – 1 Juno – 1.5 extra wide | 1 |
| Tone | 2–18 kHz BBD filter cutoff | 9 kHz |
| Warmth | 0–1 compander saturation | 0.3 |
| Noise | Off / On | Off |
| Noise Level | −96 to −30 dBFS | −62 dB |
| Output | −24 to +12 dB | 0 dB |
| Bypass | Off / On | Off |

**Chorus** follows the Juno's buttons. The values are the commonly cited
measurements of the Juno-60/106 chorus, not taken from a unit here:

| Mode | LFO (triangle) | Tap delay sweep | Sound |
|---|---|---|---|
| I | 0.51 Hz | 1.66–5.35 ms | slow, wide, lush |
| II | 0.86 Hz | 1.66–5.35 ms | faster and more obvious |
| I+II | 9.75 Hz | 3.3–3.7 ms | fast, shallow shimmer (both buttons) |
| Off | – | – | dry; the chorus and its hiss fade out over 20 ms |

Switching modes changes the LFO rate at once and glides the sweep range over
about 50 ms, so it can be switched while playing.

**Mix** 0.5 is the Juno: the dry signal and each tap at full level, so a
mono source gets louder by up to 6 dB; use **Output** to trim it. 0 is dry
only and 1 wet only. **Width** scales the difference between the two sides:
0 folds the taps back to mono (the chorus becomes a subtle flanger-like
doubling), above 1 exaggerates the spread.

**Tone** sets the band-limiting filters before and after the delay line
(two poles each side). The Juno's BBD filters sit around 9–10 kHz; lower
values give a darker, more vintage chorus. **Warmth** drives the line's soft
saturation, like the compander around the BBD.

**Noise** switches on the BBD hiss. It is injected into the delay line, so it
is band-limited by Tone, swept by the LFO and spread across the two taps like
the Juno's; it only sounds while the chorus is on. **Noise Level** sets it in
dBFS of white noise into the line (the result after filtering is quieter).
While Noise is on the plugin reports an endless tail, so hosts keep
processing it through silence.

Stereo inputs are summed to mono into the line (as the Juno's voice bus is
mono); the dry path keeps the original stereo.

Automation uses the final value in each processing block, like the sibling
plugins. Bypass returns the dry input.

MIDI mappings: CC 93 (General MIDI chorus send) → Mix, CC 94 → Depth.

## Tests

```sh
build/mlang pkg --config plugins/mla_juno_chorus/mlang.toml run test
build/mlang --tests tests/dsp_chorus_tests.mla
```

Or, after the local SDK build above:

```sh
cmake --build plugins/mla_juno_chorus/build/cmake --target mla_juno_chorus_tests -j4
ctest --test-dir plugins/mla_juno_chorus/build/cmake --output-on-failure
python3 plugins/mla_juno_chorus/tests/mlacker_editor_smoke.py \
  mlacker/build/cmake/bin/mlacker \
  plugins/mla_juno_chorus/build/cmake/VST3/Release/MlaJunoChorus.vst3
```

The DSP tests check each mode's tap delays against the Juno ranges, the
anti-phase stereo sweep, the noise toggle, mix/output gains and finite
output at extremes. Processor coverage adds Off pass-through, stereo
widening for every mode, Width 0 mono, the noise switch, level and tail,
bypass, silent buffers, parameter metadata, state round-trip and invalid-state
rejection, at 8 kHz and 384 kHz. The PTY smoke test loads the bundle in
mlacker, edits Mix, scrolls through the controls, and reopens a saved
session to check the edited value.
