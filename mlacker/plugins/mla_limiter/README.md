# Mla Limiter

Stereo look-ahead maximizer VST3 effect for mix buses and masters, with
controls for **Punch** and **Fat**. It runs
[`dsp::punch_limiter`](../../../modules/dsp/punch_limiter.mla) with a thin C++
VST3 wrapper following `mla_juno_chorus`.

It takes the Waves L2 Ultramaximizer as the reference for how it works:
Threshold drives the signal into a transparent 1.5 ms look-ahead brickwall
limiter, the output never exceeds the Ceiling, and Auto Release adapts the
release to the material, like L2's ARC. **True Peak** (on by default) goes
further than L2's sample-peak ceiling: it also limits the peaks the waveform
reaches *between* samples, so the Ceiling is in dBTP as streaming services
measure it. **Smooth** mode takes Waves
Renaissance Axx as the reference: a musical soft-knee compressor with its own
Attack in front of the limiter, for single sources and buses. **Punch** and
**Fat** add character on top. It is an original implementation and contains
no code from either product. The names are there only to describe the sound.

## Build

From the repository root, with the compiler/runtime already built in `build/`:

```sh
build/mlang pkg --config mlacker/plugins/mla_limiter/mlang.toml build
```

This fetches the same pinned VST3 SDK as the other plugins and builds:

```text
mlacker/plugins/mla_limiter/build/cmake/VST3/Release/MlaLimiter.vst3
```

To reuse an existing SDK checkout without fetching another copy:

```sh
mkdir -p mlacker/plugins/mla_limiter/build/obj
build/mlang -c mlacker/plugins/mla_limiter/src/mla_limiter_dsp.mla -O2 \
  -o mlacker/plugins/mla_limiter/build/obj/mla_limiter_dsp.o
cmake -S mlacker/plugins/mla_limiter -B mlacker/plugins/mla_limiter/build/cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/mlacker/build/deps/vst3sdk" \
  -DMLANG_DSP_OBJECT=build/obj/mla_limiter_dsp.o
cmake --build mlacker/plugins/mla_limiter/build/cmake --target MlaLimiter -j4
```

CMake accepts `MLANG_STD_LIBRARY` to select another runtime archive. Only macOS
has been validated here.

## Use in mlacker

On the master or a bus: in Pattern view press `f` to show the insert slots,
pick the track with `h/l` and a slot with `j/k`, press Enter and select
`MlaLimiter.vst3`. Enter on the slot again opens its parameters.

Parameters persist through `.mlack` sessions and plugin state/presets.
Continuous values in mlacker's generic editor use normalized `0..1`; list
controls use indices.

## Controls

| Control | Physical range / choices | Default |
|---|---|---|
| Mode | Maximizer, Smooth | Maximizer |
| Threshold | −30 to 0 dB | −6 dB |
| Ceiling | −12 to 0 dBFS | −0.3 dB |
| Release | 1–1000 ms | 60 ms |
| Auto Release | Off / On | On |
| Punch | 0–1 | 0.3 |
| Fat | 0–1 | 0.3 |
| Attack | 0.1–50 ms (Smooth mode) | 5 ms |
| Bypass | Off / On | Off |
| True Peak | Off / On | On |

The plugin also has a read-only **Reduction** output parameter (0–24 dB) that
reports the deepest gain reduction in each block, for host meters.

**Threshold** works like a maximizer's: lowering it drives the input by that
many dB into the limiter, so loudness and gain reduction rise together.
**Ceiling** is the output peak level; no sample exceeds it.

**Release** is how fast the gain recovers after a peak. With **Auto
Release** on, it is the centre of the range: short peaks recover at about
0.3× the release, and sustained limiting recovers at about 3×. Drum hits
don't leave holes, and dense material doesn't pump or distort. Turn it off
for one fixed release time.

**Punch** decides who deals with the loudest transients. At 0 the limiter
alone keeps every peak under the ceiling, and each drum hit ducks the whole
mix. Turning Punch up lets peaks up to 6 dB over the limit pass the limiter
into an anti-aliased soft clipper, which rounds them off instead. The limiter
ducks less, so the body of the mix stays up between hits, and the transients
keep their snap with a little clipper grit. It also adds loudness at the same
Threshold.

**Fat** saturates the band below about 110 Hz and lifts it (up to about
+4.6 dB for quiet bass, less as it saturates). This thickens kick and bass
with harmonics that also carry on small speakers. Since this is before the
limiter, more Fat also means more limiting on bass-heavy material.

**Mode**: *Maximizer* is the transparent brickwall limiter. *Smooth* adds a
soft-knee 3:1 compressor 6 dB under the limit, with **Attack** setting how
much of each transient gets through before it acts. It is denser and a
little quieter at the same Threshold (there is no make-up gain), and suits
single instruments and buses.

**True Peak**: a DAC, or a lossy encoder (MP3, AAC, Ogg) on a streaming
service, reconstructs the continuous waveform, which can peak above the
samples. The overshoot is up to 3 dB on bright material, and more with Punch
clipping. With True Peak on, the limiter detects peaks at seven points between
every pair of samples (8× oversampling), and a short guard stage after the
clipper catches what the clipper adds between samples. The Ceiling then
holds for the reconstructed waveform. For streaming, set the Ceiling to
−1 dB (dBTP). Measured against an independent 32× reference meter:

| Material | True Peak off | True Peak on |
|---|---|---|
| Quarter-rate sine sampled at 45°, ceiling −1 dB | +2.0 dBTP | −1.00 dBTP |
| Drum beat band-limited to 20 kHz, Punch 1 | +1.2 dBTP | −0.97 dBTP |
| Beat with white noise up to Nyquist, Punch 1 | +2.3 dBTP | −0.67 dBTP |

The detection is exact below about 20 kHz (at 48 kHz). Energy between
there and Nyquist can read a few tenths of a dB higher on an ideal
full-band meter, but DAC and codec filters remove that band. True Peak
roughly doubles the CPU cost, to about 6% of one core at 48 kHz here.
Off, the Ceiling applies to sample values only, as in L2.

The plugin reports 145 samples of latency at 48 kHz (1.5 ms look-ahead,
two 24-sample true-peak detectors, the clipper and the 0.5 ms guard) for
host delay compensation. The latency is the same with True Peak on or off.
Bypass outputs the dry input delayed by the same amount, so switching either
doesn't shift the timing.

Plugin states saved before True Peak existed still load, with True Peak on.
mlacker sessions made with the earlier nine-control version will report
that the parameter layout changed and need the limiter re-inserted.

Automation uses the final value in each processing block, like the sibling
plugins. Threshold and Ceiling glide over about 10 ms.

## Tests

```sh
build/mlang pkg --config mlacker/plugins/mla_limiter/mlang.toml run test
build/mlang --tests tests/dsp_punch_limiter_tests.mla
```

Or, after the local SDK build above:

```sh
cmake --build mlacker/plugins/mla_limiter/build/cmake --target mla_limiter_tests -j4
ctest --test-dir mlacker/plugins/mla_limiter/build/cmake --output-on-failure
python3 mlacker/plugins/mla_limiter/tests/mlacker_editor_smoke.py \
  mlacker/build/cmake/bin/mlacker \
  mlacker/plugins/mla_limiter/build/cmake/VST3/Release/MlaLimiter.vst3
```

The DSP tests check that the ceiling holds in both modes and at any Punch
and Fat, and that True Peak keeps a 45° quarter-rate sine under the ceiling
between samples (and that it doesn't when off). They also check that Threshold drives loudness, that Punch ducks
less, that the output is bit-exact and latency-aligned below the limit, the
reduction reporting, the latency-aligned bypass, and finite output at
extremes. Processor coverage adds the ceiling on a drum beat across every
mode × Punch × Fat, the Ceiling control, the Fat bass lift, Auto Release,
the reduction meter output, True Peak checked against an independent 32×
reference interpolator, silent buffers, parameter metadata, the reported
latency, state round-trip, loading pre-True Peak states and invalid-state
rejection, at 8 kHz and 384 kHz.
The PTY smoke test loads the bundle in mlacker, edits Threshold, scrolls
through the controls, and reopens a saved session to check the edited value.
