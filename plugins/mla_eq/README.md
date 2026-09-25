# Mla EQ

Stereo parametric VST3 equalizer with 4 or 8 bands and optional high-pass and
low-pass filters at 12, 24, 36, or 48 dB/octave. Filter design, parameter
smoothing, and processing are in MLang
([`mla_eq_dsp.mla`](src/mla_eq_dsp.mla), using the `dsp::filter::Biquad`
equalizer sections); `plugin.cpp` is a thin VST3 wrapper following `mla_delay`.

## Build

From the repository root, with the compiler/runtime already built in `build/`:

```sh
build/mlang pkg --config plugins/mla_eq/mlang.toml build
```

This fetches the same pinned VST3 SDK as the other plugins and builds:

```text
plugins/mla_eq/build/cmake/VST3/Release/MlaEQ.vst3
```

To reuse an existing SDK checkout without fetching another copy:

```sh
mkdir -p plugins/mla_eq/build/obj
build/mlang -c plugins/mla_eq/src/mla_eq_dsp.mla -O2 \
  -o plugins/mla_eq/build/obj/mla_eq_dsp.o
cmake -S plugins/mla_eq -B plugins/mla_eq/build/cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/mlacker/build/deps/vst3sdk" \
  -DMLANG_DSP_OBJECT=build/obj/mla_eq_dsp.o
cmake --build plugins/mla_eq/build/cmake --target MlaEQ -j4
```

As with the sibling plugins, only macOS has been validated; on Linux use a PIC
runtime and DSP object as described in `mla_verb`.

## Use in mlacker

Load `MlaEQ.vst3` into an insert, master, or effect-channel slot and press Enter
(or **Effect → Edit effect plugin**) to open the generic parameter editor.
Enumerated controls use indices, so **EQ Type** is `0` for 4 bands and `1` for
8 bands. Continuous controls are normalized `0..1`; Freq and Q use a logarithmic
mapping (equal travel per octave), so `0.5` Freq is about 632 Hz and `0.5` Q is
about 1.34. Parameters persist through `.mlack` sessions and plugin state.

## Controls

| Control | Physical range / choices | Default |
|---|---|---|
| EQ Type | 4 Band, 8 Band | 4 Band |
| Output | −24–+24 dB | 0 dB |
| Bypass | Off / On | Off |
| HP Slope | Off, 12, 24, 36, 48 dB/oct | Off |
| HP Freq | 20–20000 Hz (log) | 30 Hz |
| HP Q | 0.1–18 (log) | 0.707 |
| LP Slope | Off, 12, 24, 36, 48 dB/oct | Off |
| LP Freq | 20–20000 Hz (log) | 18000 Hz |
| LP Q | 0.1–18 (log) | 0.707 |
| Band *n* Type | Off, Bell, Low Shelf, High Shelf, Notch | see below |
| Band *n* Freq | 20–20000 Hz (log) | see below |
| Band *n* Gain | −24–+24 dB | 0 dB |
| Band *n* Q | 0.1–18 (log) | 0.707 shelves, 1.0 bells |

Band defaults: 1 Low Shelf 80 Hz, 2 Bell 400 Hz, 3 Bell 2.5 kHz,
4 High Shelf 10 kHz, 5 Bell 150 Hz, 6 Bell 800 Hz, 7 Bell 1.5 kHz, 8 Bell 5 kHz.
With all gains at 0 dB and both filters off, the output is bit-identical to the
input.

- **EQ Type** switches bands 5–8 in and out. Their settings are kept while in
  4-band mode.
- **Q** narrows a bell or notch as it rises. On shelves, 0.707 is the steepest
  shelf without overshoot; higher values add a bump at the corner. Notch ignores
  Gain.
- **HP/LP** are Butterworth cascades of 1–4 biquads (12 dB per stage), flat at
  Q 0.707 with −3 dB at the corner. Raising Q scales the highest-Q stage to
  add resonance at the cutoff.
- Signal order: high-pass → bands 1–8 → low-pass → output gain.

Frequency, gain, and Q changes glide over about 20 ms to avoid zipper noise.
Changing a band type, a slope, or EQ Type takes effect immediately, and newly
enabled sections start from cleared history. Automation uses the final value in
each processing block, like the sibling plugins.

MIDI mappings: CC 7 → Output, CC 71 → LP Q, CC 74 → LP Freq, CC 75 → HP Freq,
CC 76 → HP Q, CC 80 → EQ Type (0–63 = 4 bands, 64–127 = 8 bands).

## Tests

```sh
build/mlang pkg --config plugins/mla_eq/mlang.toml run test
```

Or, after the local SDK build above:

```sh
cmake --build plugins/mla_eq/build/cmake --target mla_eq_tests -j4
ctest --test-dir plugins/mla_eq/build/cmake --output-on-failure
python3 plugins/mla_eq/tests/mlacker_editor_smoke.py \
  mlacker/build/cmake/bin/mlacker \
  plugins/mla_eq/build/cmake/VST3/Release/MlaEQ.vst3
```

Processor coverage: parameter metadata and log-mapping round trips, bit-exact
default passthrough, bell gain and Q width, low/high shelves, notch, 4- vs 8-band
switching, all four HP slopes (attenuation two octaves below the corner and
−3 dB at it), HP resonance, 24 dB low-pass, output gain, bypass, finite output at
extreme settings, and state round trip with invalid-state rejection. The
`Biquad` equalizer shapes are also tested in `tests/dsp_tests.mla`.
