# Mla Distortion

A VST3 stereo saturation/distortion **effect** built from the shared MLang DSP
module [`modules/dsp/distortion.mla`](../../modules/dsp/distortion.mla) — an
allocation-free analog-style soft clipper with biased `tanh` saturation, a
one-pole tone filter, and coupling-capacitor DC blocking. This is a standalone
project: it compiles on its own and produces a loadable `.vst3` bundle for use
in [`mlacker`](../../mlacker) (and other VST3 hosts).

All saturation math stays in MLang. The C++ layer (`src/plugin.cpp`) is a thin
`SingleComponentEffect` that negotiates a stereo-in / stereo-out effect bus,
exposes VST3 parameters, and pumps frames through a per-instance DSP handle
returned by the compiled MLang bridge (`src/mla_distortion_dsp.mla`). The mono
`AnalogDistortion` is instantiated twice (independent left/right state) inside
the bridge's per-instance struct.

## Parameters

| Parameter | Range (physical)   | Notes |
|-----------|--------------------|-------|
| Drive     | 0 .. 36 dB         | Pre-saturation gain into the `tanh` curve. |
| Tone      | 20 Hz .. 20 kHz    | One-pole low-pass after saturation, exponential mapping. |
| Bias      | -0.5 .. +0.5       | Transfer-curve asymmetry (even-harmonic character); centred at norm 0.5. |
| Output    | -24 .. +12 dB      | Post-effect output trim (unity at norm 2/3). |
| Mix       | 0 .. 1             | Dry/wet blend. Defaults to 1.0 for insert use. |

Four MIDI CCs are mapped for pattern/live automation in mlacker: CC 1 → Drive,
CC 74 → Tone, CC 71 → Bias, CC 91 → Mix.

## Build

Prerequisites: the repository's compiler/runtime built in `build/` (so
`build/libmlang_std.a` exists), CMake, a C++17 compiler, and Git. From this
directory:

```sh
../../build/mlang pkg --config mlang.toml build
```

That fetches the pinned Steinberg VST3 SDK (with submodules), compiles
`src/mla_distortion_dsp.mla` to an object, configures CMake, and builds the
bundle:

```
build/cmake/VST3/MlaDistortion.vst3
```

`mlang.toml` pins SDK 3.8.1 at commit
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`. Generated SDK sources and binaries
stay under the ignored `build/` directory. Preserve the SDK license notices in
`build/deps/vst3sdk/LICENSE.txt` when distributing.

### Building against a runtime built elsewhere

The CMake project accepts overrides, so you can point it at an already-fetched
SDK, a pre-compiled DSP object, and a runtime archive in a non-default location:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT=/path/to/vst3sdk \
  -DMLANG_DSP_OBJECT=build/obj/mla_distortion_dsp.o \
  -DMLANG_STD_LIBRARY=/path/to/libmlang_std.a
cmake --build build/cmake --target MlaDistortion
```

### Linux: position-independent runtime required

A `.vst3` is a shared object, so on Linux every input must be
position-independent. macOS (and therefore mlacker) builds PIC by default, so
this only applies to Linux hosts:

- Compile the DSP as PIC. The plain `mlang -c` object is not guaranteed PIC, so
  emit LLVM IR and let clang produce a PIC object:

  ```sh
  mlang -emit-llvm src/mla_distortion_dsp.mla -O2 -o mla_distortion_dsp.ll
  clang -fPIC -O2 -c mla_distortion_dsp.ll -o mla_distortion_dsp.o
  ```

- Point `-DMLANG_STD_LIBRARY` at a **PIC** build of `libmlang_std.a` (build the
  `mlang_std` target in a tree configured with `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`).
  A non-PIC archive fails to link into the shared object.

## Use in mlacker

Mla Distortion is a stereo audio effect (subcategory `Fx|Distortion`), which is
exactly what mlacker's aux/master effect slots accept:

- **Effect → Add effect channel**, then Enter on the empty strip and choose the
  built `MlaDistortion.vst3`. As an insert, leave **Mix** at 1.0 and drive the
  send per track; lower Mix for a parallel/blended grit.
- Or load it in the master slot to saturate the whole mix.

Open the parameter editor to adjust Drive, Tone, Bias, Output, and Mix. Values
are saved with the `.mlack` session by parameter ID.

## Design notes

- **Per-instance state.** `mladist_create` heap-allocates one `MlaDistortion`
  (independent left/right saturators plus the sample rate) and returns an opaque
  handle stored by the C++ plug-in object; every call passes it back. This keeps
  multiple loaded instances independent — MLang module-level globals would be
  shared and are also restricted to literal-constant primitives.
- **Value-type DSP.** `AnalogDistortion` owns no heap buffers (only scalar
  state), so the bridge can wrap two channels in a struct and call methods on
  the nested fields directly — the `list`-drop hazard that forces the reverb
  bridge to point straight at its struct does not apply here.
- **Own DSP, SDK plumbing only.** The saturation is the repository's own
  implementation; only the Steinberg SDK glue (bus negotiation, parameters) is
  C++.
