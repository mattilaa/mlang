# Mla Delay

Stereo VST3 delay effect ported from
[`package_manager_delay_audio`](../../examples/package_manager_delay_audio/src/main.mla).
It runs the same [`dsp::delay`](../../modules/dsp/delay.mla) implementation, with
per-instance buffers and a thin C++ VST3 wrapper following `mla_verb`.

## Build

From the repository root, with the compiler/runtime already built in `build/`:

```sh
build/mlang pkg --config plugins/mla_delay/mlang.toml build
```

This fetches the same pinned VST3 SDK as the other plugins and builds:

```text
plugins/mla_delay/build/cmake/VST3/Release/MlaDelay.vst3
```

To reuse an existing SDK checkout without fetching another copy:

```sh
mkdir -p plugins/mla_delay/build/obj
build/mlang -c plugins/mla_delay/src/mla_delay_dsp.mla -O2 \
  -o plugins/mla_delay/build/obj/mla_delay_dsp.o
cmake -S plugins/mla_delay -B plugins/mla_delay/build/cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/mlacker/build/deps/vst3sdk" \
  -DMLANG_DSP_OBJECT=build/obj/mla_delay_dsp.o
cmake --build plugins/mla_delay/build/cmake --target MlaDelay -j4
```

CMake accepts `MLANG_STD_LIBRARY` to select another runtime archive. On Linux,
use a PIC runtime and DSP object (emit LLVM IR with `mlang -emit-llvm`, then
compile it with `clang -fPIC -c`), as described in the sibling `mla_verb` project.
Only macOS has been validated here. SDK license notices remain with its checkout.

## Use in mlacker

1. Choose **Effect → Add effect channel**, then Enter on its empty strip.
2. Select `MlaDelay.vst3` in the file browser.
3. Press Enter again, or choose **Effect → Edit effect plugin**, to edit parameters.
4. Set a track send to that effect. For an aux return, set **Mix** to `1.0`.

The plugin also works in master and insert effect slots. All 22 controls are
exposed to mlacker's generic VST3 parameter editor; there is no separate native
GUI. Continuous values in mlacker use normalized `0..1`, while enumerated
controls use indices. VST3 hosts that display physical values get the units and
ranges below. For example, Feedback `0.25` in mlacker means a gain of `0.30`.
Parameters persist through `.mlack` sessions and plugin state/presets.

## Controls

| Control | Physical range / choices | Default |
|---|---|---|
| Mode | Forward, Ping-Pong | Ping-Pong |
| Delay | 1–5000 ms | 375 ms |
| Feedback | 0–1.20 | 0.58 |
| Mix | 0 dry–1 wet | 0.60 |
| Filter | None, Moog 12, Moog 24, Lowpass 12, Lowpass 24, Highpass 12, Highpass 24, Bandpass 24, Bandpass 12 | Lowpass 12 |
| Filter Scope | Feedback, Delay | Delay |
| Cutoff | 20–20000 Hz; DSP clamps below Nyquist | 4200 Hz |
| Resonance | 0–1 | 0.25 |
| Damping | 0–1 filtered-feedback blend | 0.75 |
| Jitter | 0–50 ms | 0 ms |
| Tempo Source | Free, Manual, Host | Free |
| BPM | 20–400 | 120 |
| Beats | 0.0625–16 quarter-note beats | 0.75 |
| Delay Ramp | 0–2000 ms | 120 ms |
| Jitter Ramp | 0–2000 ms | 120 ms |
| Mix Ramp | 0–2000 ms | 80 ms |
| Filter Ramp | 0–2000 ms | 80 ms |
| Cutoff Ramp | 0–2000 ms | 80 ms |
| Resonance Ramp | 0–2000 ms | 80 ms |
| Damping Ramp | 0–2000 ms | 80 ms |
| Reset Loop | Off / On | Off |
| Bypass | Off / On | Off |

**Free** uses Delay directly. **Manual** uses `60000 × Beats / BPM` milliseconds.
**Host** follows valid VST3 host tempo, falling back to BPM when the host does
not provide it. Mlacker currently needs that fallback. Synced delay is clamped
to 1–5000 ms. A dotted eighth note is `0.75` beats.

**Filter** types: Lowpass/Highpass/Bandpass 12 are the original state-variable
filters. The Moog and 24 dB types are the `dsp::multimode` models shared with
[Mla Filter](../mla_filter/README.md) and the `package_manager_coreaudio_filter`
example. Resonance `0..1` maps to `0..24 dB` for them. Type changes crossfade over
Filter Ramp. In mlacker the list indices are: 0 None, 1 Moog 12, 2 Moog 24,
3 Lowpass 12, 4 Lowpass 24, 5 Highpass 12, 6 Highpass 24, 7 Bandpass 24,
8 Bandpass 12. This order keeps sessions and presets saved with the earlier
four-entry list (None/Lowpass/Highpass/Bandpass) on the same filters.

**Delay** filter scope colors the wet tap; **Feedback** filters repeats
progressively. Feedback above 1 permits the demo's self-oscillation behavior.
Reset Loop clears delay/filter history on the Off → On transition; switch it
off before triggering again. It retains the current settings, including
Feedback. The demo's keyboard transport, file playback/recording, and reset-gain
shortcut are not part of the audio effect.

Ramp controls apply to subsequent parameter changes. Initial setup and state
restoration apply settings immediately. Like the sibling plugins, automation
uses the final value in each processing block; the DSP then performs its ramps.
Bypass returns dry input while continuing to process the delay history.

MIDI mappings: CC 1 → Mix, CC 12 → Delay, CC 71 → Resonance,
CC 74 → Cutoff, CC 91 → Feedback.

## Tests

```sh
build/mlang pkg --config plugins/mla_delay/mlang.toml run test
```

Or, after the local SDK build above:

```sh
cmake --build plugins/mla_delay/build/cmake --target mla_delay_tests -j4
ctest --test-dir plugins/mla_delay/build/cmake --output-on-failure
python3 plugins/mla_delay/tests/mlacker_editor_smoke.py \
  mlacker/build/cmake/bin/mlacker \
  plugins/mla_delay/build/cmake/VST3/Release/MlaDelay.vst3
```

Processor coverage includes impulse timing, ping-pong routing, instance isolation,
legacy Filter-value compatibility, Moog/24 dB repeat filtering,
reset, bypass, silent buffers, manual/host tempo, parameter metadata, state
round-trip and invalid-state rejection, and finite output at high feedback.
The PTY smoke test loads the actual bundle in mlacker, edits Feedback, scrolls
through the controls, and reopens a saved session to check the edited value.
