# Mla Filter

Stereo multimode VST3 filter with the eight filter types of the
[`package_manager_coreaudio_filter`](../../examples/package_manager_coreaudio_filter/README.md)
example: Lowpass, Highpass, and Bandpass at 12 and 24 dB/octave, plus the
Moog-style ladder at 12 and 24 dB. The DSP is
[`dsp::multimode`](../../docs/dsp/multimode.md), called through
[`mla_filter_dsp.mla`](src/mla_filter_dsp.mla); `plugin.cpp` is a thin VST3
wrapper following `mla_delay`. The same models are selectable in
[Mla Delay](../mla_delay/README.md)'s Filter control.

## Build

From the repository root, with the compiler/runtime already built in `build/`:

```sh
build/mlang pkg --config plugins/mla_filter/mlang.toml build
```

This fetches the same pinned VST3 SDK as the other plugins and builds:

```text
plugins/mla_filter/build/cmake/VST3/Release/MlaFilter.vst3
```

To reuse an existing SDK checkout without fetching another copy:

```sh
mkdir -p plugins/mla_filter/build/obj
build/mlang -c plugins/mla_filter/src/mla_filter_dsp.mla -O2 \
  -o plugins/mla_filter/build/obj/mla_filter_dsp.o
cmake -S plugins/mla_filter -B plugins/mla_filter/build/cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/mlacker/build/deps/vst3sdk" \
  -DMLANG_DSP_OBJECT=build/obj/mla_filter_dsp.o
cmake --build plugins/mla_filter/build/cmake --target MlaFilter -j4
```

As with the sibling plugins, only macOS has been validated.

## Use in mlacker

Load `MlaFilter.vst3` into an insert, master, or effect-channel slot and press
Enter (or **Effect → Edit effect plugin**) to open the parameter editor. **Type**
takes an index, listed below. Continuous controls are normalized `0..1`. Cutoff
uses a logarithmic mapping: `0.5` is about 632 Hz. Parameters persist through
`.mlack` sessions and plugin state.

## Controls

| Control | Physical range / choices | Default |
|---|---|---|
| Type | 0 Lowpass 12, 1 Lowpass 24, 2 Highpass 12, 3 Highpass 24, 4 Bandpass 12, 5 Bandpass 24, 6 Moog 12, 7 Moog 24 | Moog 24 |
| Cutoff | 20–20000 Hz (log) | 2000 Hz |
| Resonance | 0–36 dB | 0 dB |
| Mix | 0 dry–1 filtered | 1 |
| Output | −24–+24 dB | 0 dB |
| Cutoff Ramp | 0–2000 ms | 120 ms |
| Bypass | Off / On | Off |
| Resonance Ramp | 0–2000 ms | 120 ms |
| Mix Ramp | 0–2000 ms | 120 ms |
| Output Ramp | 0–2000 ms | 120 ms |
| Type Ramp | 0–2000 ms type crossfade | 80 ms |

Resonance `0 dB` is a flat Butterworth response (the ladder has no peak). High
settings get loud, and Moog near 36 dB approaches self-oscillation, so lower
Output as needed.
The 24 dB biquad types split resonance across both stages, like the example.

Cutoff, Resonance, Mix, and Output changes ramp linearly over their Ramp
controls, and cutoff moves evenly per octave. The 120 ms defaults outlast a
1/16 row at 120 BPM, so row-timed mlacker automation and stepped MIDI CC glide
instead of zippering. Type changes crossfade over **Type Ramp**, so switching
while audio plays does not click (0 switches instantly). Initial setup and
state restoration apply values immediately. Automation uses the final value in
each processing block, like the sibling plugins. Cutoff Ramp keeps the old
Glide parameter's ID, so older sessions recall their saved Glide time there;
states saved before the other ramps load them at their defaults.

MIDI mappings: CC 1 → Mix, CC 7 → Output, CC 70 → Type, CC 71 → Resonance,
CC 74 → Cutoff.

## Tests

```sh
build/mlang pkg --config plugins/mla_filter/mlang.toml run test
```

Or, after the local SDK build above:

```sh
cmake --build plugins/mla_filter/build/cmake --target mla_filter_tests -j4
ctest --test-dir plugins/mla_filter/build/cmake --output-on-failure
python3 plugins/mla_filter/tests/mlacker_editor_smoke.py \
  mlacker/build/cmake/bin/mlacker \
  plugins/mla_filter/build/cmake/VST3/Release/MlaFilter.vst3
```

Processor coverage: metadata and log-mapping round trips, pass and stop bands of
all eight types, 24 dB types steeper than 12 dB, resonance peaks, ramp timing and defaults,
click-free type crossfades, Mix/Output/Bypass, silent buffers, finite output at
36 dB resonance at both frequency extremes, and state round trip (including
seven-value legacy states) with invalid-state rejection. `dsp::multimode` itself is tested in
`tests/dsp_tests.mla`.
