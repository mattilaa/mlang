# Mla GatedVerb

Stereo VST3 effect dedicated to the 80s gated drum reverb: the huge, loud,
suddenly-cut room put on LinnDrum and Oberheim DMX snares and toms. It runs
[`dsp::gated_reverb`](../../../modules/dsp/gated_reverb.mla) with a thin C++
VST3 wrapper following `mla_juno_chorus`.

Each hit opens the gate onto a dense, compressed reverb. It stays fully open
for the **Hold** time, then closes over a short **Release** decay while a
low-pass sweeps down so the tail dies dark (**Damp**). The reverb level has
its own control, so it can be as loud as the drum or louder. The model is the
gated programs of period units (AMS RMX16 "Nonlin", Yamaha REV7 "Gate
Reverb", the Ensoniq ASR-10's gated reverbs); the parameters mirror what those
expose. It is not derived from any of their algorithms.

## Build

From the repository root, with the compiler/runtime already built in `build/`:

```sh
build/mlang pkg --config mlacker/plugins/mla_gated_verb/mlang.toml build
```

This fetches the same pinned VST3 SDK as the other plugins and builds:

```text
mlacker/plugins/mla_gated_verb/build/cmake/VST3/Release/MlaGatedVerb.vst3
```

To reuse an existing SDK checkout without fetching another copy:

```sh
mkdir -p mlacker/plugins/mla_gated_verb/build/obj
build/mlang -c mlacker/plugins/mla_gated_verb/src/mla_gated_verb_dsp.mla -O2 \
  -o mlacker/plugins/mla_gated_verb/build/obj/mla_gated_verb_dsp.o
cmake -S mlacker/plugins/mla_gated_verb -B mlacker/plugins/mla_gated_verb/build/cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/mlacker/build/deps/vst3sdk" \
  -DMLANG_DSP_OBJECT=build/obj/mla_gated_verb_dsp.o
cmake --build mlacker/plugins/mla_gated_verb/build/cmake --target MlaGatedVerb -j4
```

CMake accepts `MLANG_STD_LIBRARY` to select another runtime archive. Only macOS
has been validated here.

## Use in mlacker

As an insert on the snare or tom track: in Pattern view press `f` to show the
insert slots, pick the track with `h/l` and a slot with `j/k`, press Enter and
select `MlaGatedVerb.vst3`. Enter on the slot again opens its parameters.

As an aux effect channel (the classic console setup, one reverb shared by the
snare and toms), choose **Effect → Add effect channel**, load the bundle, set
**Dry** to −60 dB there so the return is wet only, and set each drum track's
send. The gate is keyed from what arrives at the plugin, so each send opens it.

Parameters persist through `.mlack` sessions and plugin state/presets.
Continuous values in mlacker's generic editor use normalized `0..1`; list
controls use indices.

## Controls

| Control | Physical range / choices | Default |
|---|---|---|
| Type | Room, Plate, Reverse | Room |
| Size | 0–1 | 0.5 |
| Hold | 10–1000 ms | 250 ms |
| Release | 5–1000 ms | 80 ms |
| Damp | 0–1 | 0.6 |
| Tone | 1.5–16 kHz | 9 kHz |
| Threshold | −60 to 0 dBFS | −24 dB |
| Pre-delay | 0–100 ms | 8 ms |
| Squash | 0–1 | 0.6 |
| Width | 0 mono – 1 full | 1 |
| Low Cut | 20–1000 Hz | 120 Hz |
| Reverb | −24 to +12 dB | 0 dB |
| Dry | −60 (off) to +6 dB | 0 dB |
| Output | −24 to +12 dB | 0 dB |
| Bypass | Off / On | Off |

**Type**: *Room* is the bright live room with early reflections, the classic
gated snare. *Plate* is shorter and denser with no discrete reflections,
smoother on toms. *Reverse* swells up over the hold and is then cut, the
reverse-gate effect.

**Hold** is the time the gate stays fully open after the reverb starts
(counted from the hit, after the pre-delay); **Release** is the decay that
closes it. The release follows an exponential curve forced to reach zero, so
the reverb stops exactly at the end. 150–350 ms hold with a 30–120 ms release
covers the typical 80s settings; try a hold near an eighth or sixteenth note
of the song tempo.

**Damp** sets how dark the tail gets by the end of the release: the reverb's
low-pass sweeps from **Tone** down by up to five octaves as the gate closes.
0 just fades; 1 swallows the tail into a dull thud.

**Threshold** sets the input level that opens the gate. Each hit above it
(re-armed once the input falls 6 dB below it) restarts the gate, even in the
middle of a release; a quieter signal leaves the reverb shut. Lower it for
ghost notes, raise it so only accents open the reverb.

**Squash** compresses the reverb before the gate, like the heavily compressed
room mics of the original records: at 0 the tail decays naturally through the
hold, at 1 it is an almost flat wall. **Reverb** is the return level; with
Squash on it is loud to begin with.

**Size** sets the delay lengths and the tank's natural decay (1.2–3.6 s, far
longer than the gate, so the reverb is dense for the whole hold). **Low Cut**
keeps kicks and low toms from muddying the tank. **Pre-delay** separates the
hit from the reverb. **Width** narrows the reverb toward mono.

The plugin reports a tail of pre-delay + hold + release, after which its
output is the dry signal only. Stereo inputs are summed to mono into the
reverb; the dry path keeps the original stereo. Level changes glide over
about 10 ms.

Automation uses the final value in each processing block, like the sibling
plugins. Bypass returns the dry input.

MIDI mappings: CC 91 (General MIDI reverb send) → Reverb, CC 92 → Hold.

## Tests

```sh
build/mlang pkg --config mlacker/plugins/mla_gated_verb/mlang.toml run test
build/mlang --tests tests/dsp_gated_reverb_tests.mla
```

Or, after the local SDK build above:

```sh
cmake --build mlacker/plugins/mla_gated_verb/build/cmake --target mla_gated_verb_tests -j4
ctest --test-dir mlacker/plugins/mla_gated_verb/build/cmake --output-on-failure
python3 mlacker/plugins/mla_gated_verb/tests/mlacker_editor_smoke.py \
  mlacker/build/cmake/bin/mlacker \
  mlacker/plugins/mla_gated_verb/build/cmake/VST3/Release/MlaGatedVerb.vst3
```

The DSP tests check silence without input, that one hit holds the reverb and
closes it exactly after pre-delay + hold + release, that Hold and Release move
the close, the threshold, the reverse swell, the reverb and output levels,
Width 0 mono and finite output at extremes. Processor coverage adds all three
types, the loudness of the held reverb, stereo spread, the Damp darkening,
retriggering on a second hit, the reported tail, bypass, silent buffers,
parameter metadata, state round-trip and invalid-state rejection, at 8 kHz and
384 kHz. The PTY smoke test loads the bundle in mlacker, edits Size, scrolls
through the controls, and reopens a saved session to check the edited value.
