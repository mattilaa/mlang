# Mla Verb

A VST3 stereo reverb **effect** built from the shared MLang DSP module
[`modules/dsp/reverb2.mla`](../../modules/dsp/reverb2.mla) — an allocation-free
eight-line feedback-delay-network reverb. This is a standalone project: it
compiles on its own and produces a loadable `.vst3` bundle for use in
[`mlacker`](../../mlacker) (and other VST3 hosts).

All reverberation math stays in MLang. The C++ layer (`src/plugin.cpp`) is a
thin `SingleComponentEffect` that negotiates a stereo-in / stereo-out effect
bus, exposes VST3 parameters, and pumps frames through a per-instance DSP handle
returned by the compiled MLang bridge (`src/mla_verb_dsp.mla`).

## Parameters

| Parameter | Range (physical)        | Notes |
|-----------|-------------------------|-------|
| Type      | 9 characters            | Hall, Room, Plate, Gated, Reverse, Nonlinear Short, Nonlinear Long, Custom, Infinite Hall. Selecting a type loads its character and clears the tail. |
| Size      | 0..1                    | Tank/room size. |
| Decay     | 0.1 s .. 20 s           | RT60, exponential mapping. |
| Damp      | 0..1                    | High-frequency damping. |
| Mix       | 0..1                    | Dry/wet. Set to 1.0 for aux/send use. |
| Predelay  | 0 .. 200 ms             | Pre-delay before the reverb. |
| Width     | 0..1                    | Stereo width of the wet signal. |
| Diffusion | 0..1                    | Input allpass diffusion. |
| Early     | 0..1                    | Early-reflection blend. |
| Freeze    | off / on                | Infinite feedback hold. |

Type selects the reverb *character* (modulation, gating, reverse/nonlinear
envelope shaping, freeze); the continuous sliders remain authoritative over the
shared size/decay/damp/etc. so tweaking a knob never snaps back to a preset.

Four MIDI CCs are mapped for pattern/live automation in mlacker: CC 1 → Mix,
CC 70 → Size, CC 74 → Damp, CC 91 → Decay.

## Build

Prerequisites: the repository's compiler/runtime built in `build/` (so
`build/libmlang_std.a` exists), CMake, a C++17 compiler, and Git. From this
directory:

```sh
../../build/mlang pkg --config mlang.toml build
```

That fetches the pinned Steinberg VST3 SDK (with submodules), compiles
`src/mla_verb_dsp.mla` to an object, configures CMake, and builds the bundle:

```
build/cmake/VST3/MlaVerb.vst3
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
  -DMLANG_DSP_OBJECT=build/obj/mla_verb_dsp.o \
  -DMLANG_STD_LIBRARY=/path/to/libmlang_std.a
cmake --build build/cmake --target MlaVerb
```

## Use in mlacker

Mla Verb is a mono/stereo audio effect (subcategory `Fx|Reverb`), which is
exactly what mlacker's aux/master effect slots accept:

- **Effect → Add effect channel**, then Enter on the empty strip and choose the
  built `MlaVerb.vst3`. Set **Mix** to 1.0 for a classic 100% wet aux return and
  dial the send per track.
- Or load it in the master slot to reverberate the whole mix (leave Mix lower
  for a dry/wet blend).

Open **Effect → Edit** (or the parameter editor) to adjust Type, Size, Decay,
Damp, etc. Values are saved with the `.mlack` session by parameter ID.

## Design notes

- **Per-instance state.** `mlaverb_create` heap-allocates one reverb and returns
  an opaque handle stored by the C++ plug-in object; every call passes it back.
  This keeps multiple loaded instances independent — MLang module-level globals
  would be shared and are also restricted to literal-constant primitives.
- **No MVerb / SDK DSP.** The reverb is the repository's own FDN implementation;
  only the Steinberg SDK plumbing (bus negotiation, parameters) is C++.
