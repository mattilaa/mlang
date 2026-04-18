# Package Manager + Steinberg VST3 SDK Bridge

This example shows how to fetch the official Steinberg VST3 SDK with
`mlang pkg`, compile a small C++ bridge against the SDK headers, and call that
bridge from MLang.

The goal is not to build a full `.vst3` plug-in bundle yet. The goal is to
show the integration seam:

- `mlang pkg fetch` downloads the VST3 SDK source tree into `build/deps/vst3sdk`
- a `language = 'c++'` task compiles `src/vst3_bridge.cpp` against the SDK
- `src/main.mla` calls plain `extern fn` bridge functions
- the MLang side reshapes the returned SDK metadata into a builder object and
  prints it as JSON

## Upstream SDK

This manifest fetches the official Steinberg GitHub repository tarball:

- Repository: `https://github.com/steinbergmedia/vst3sdk`
- Archive URL used in the manifest:
  `https://github.com/steinbergmedia/vst3sdk/archive/refs/heads/master.tar.gz`

As of April 18, 2026, Steinberg documents the VST3 SDK from that repository and
the online interface reference describes:

- `kVstVersionString` in `pluginterfaces/vst/vsttypes.h`
- `kVstAudioEffectClass` and `PlugType::kFx` in
  `pluginterfaces/vst/ivstaudioprocessor.h`

Those are the symbols this example exposes through the bridge.

## Build

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
```

That produces:

```text
build/obj/main_mlang.o
build/obj/vst3_bridge.o
build/lib/libvst3_bridge.a
build/vst3_sdk_demo
```

## Run

```sh
../../build/mlang pkg run demo
```

Or run the linked binary directly:

```sh
./build/vst3_sdk_demo
```

## What The Demo Shows

- the SDK version string reported by Steinberg headers
- the VST3 component class string for an audio effect
- the standard `Fx` sub-category
- the stereo speaker arrangement bitset
- the same data shaped into an MLang builder object and printed as pretty JSON

## Notes

- This example intentionally uses the SDK as a fetched source dependency with
  `build = 'none'`. The bridge only needs headers, so there is no reason to
  build the full SDK for this first integration step.
- To evolve this into an actual VST3 plug-in target, the next step would be a
  dedicated C++ target that uses Steinberg's SDK CMake helpers or a matching
  hand-written bundle build per platform.
- The VST3 SDK repository uses submodules for some optional parts. This example
  does not rely on them.
