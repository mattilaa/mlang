# Package Manager + VST3 Instrument + CoreAudio Preview

This example shows a fuller integration path than the SDK metadata bridge:

- fetch the official Steinberg VST3 SDK with `mlang pkg`
- initialize the SDK submodules required by the VST3 headers and CMake helpers automatically
- compile a small oscillator DSP object from MLang
- wrap that DSP object with a thin C++ bridge
- build a minimal VST3 instrument plug-in on macOS
- build a standalone CoreAudio preview app that reuses the exact same MLang DSP

The intent is to keep the DSP in MLang while leaving VST3 and CoreAudio
boundary code in C++, where those APIs naturally live.

## Why There Are Two Outputs

A VST3 plug-in does not normally talk to CoreAudio directly. The host DAW is
responsible for audio I/O. To still satisfy a "VST3 + CoreAudio" integration
example in a practical way, this example builds:

- `MLangMiniSynth.vst3`: a VST3 instrument that responds to note-on/note-off
  events and emits either a sine or square wave
- `mlang_coreaudio_preview`: a tiny standalone Audio Queue Services app that
  instantiates the VST3 processor class directly, drives its realtime
  `process()` loop, and previews it without needing a DAW

Both targets call into the same compiled MLang oscillator object through
`src/mlang_osc_bridge.cpp`.

## Official References Used

Steinberg:

- VST3 SDK Git repo: <https://github.com/steinbergmedia/vst3sdk>
- VST3 CMake tutorial:
  <https://steinbergmedia.github.io/vst3_dev_portal/pages/Tutorials/Creating%2Ba%2Bplug-in%2Bfrom%2Bscratch.html>
- VST3 module factory and required macOS exports:
  <https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/VST%2BModule%2BArchitecture/Index.html>
- VST3 macOS bundle layout:
  <https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BFormat.html>
- `AudioEffect`, `SingleComponentEffect`, and parameter helper classes:
  <https://steinbergmedia.github.io/vst3_doc/vstsdk/classSteinberg_1_1Vst_1_1AudioEffect.html>
  <https://steinbergmedia.github.io/vst3_doc/vstsdk/vstsinglecomponenteffect_8h.html>
  <https://steinbergmedia.github.io/vst3_doc/vstsdk/classSteinberg_1_1Vst_1_1StringListParameter.html>

Apple:

- Audio Queue Services overview:
  <https://developer.apple.com/documentation/audiotoolbox/audio-queue-services>
- `AudioQueueNewOutput`:
  <https://developer.apple.com/documentation/audiotoolbox/audioqueuenewoutput%28_%3A_%3A_%3A_%3A_%3A_%3A_%3A%29>

## Build

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
```

The manifest will:

1. clone `vst3sdk`
2. initialize its submodules recursively because the dependency declares `submodules = true`
3. compile `src/mlang_oscillator.mla` to an object file
4. configure a local CMake project that uses Steinberg's SDK helpers
5. build the VST3 bundle target and the CoreAudio preview app

## Preview The Oscillator

Play the VST3 processor as a sine wave:

```sh
../../build/mlang pkg run preview-sine
```

Play the VST3 processor as a square wave:

```sh
../../build/mlang pkg run preview-square
```

## Finding The Built Plug-In

The VST3 SDK CMake helpers create the final bundle layout. After `pkg build`,
the `.vst3` bundle will be somewhere under `build/cmake/`. To locate it:

```sh
find build/cmake -name '*.vst3'
```

On macOS, the bundle contains the standard VST3 structure documented by
Steinberg, including `Contents/MacOS/` and `Contents/Resources/`.

## What The Plug-In Does

- one event input bus for MIDI-style note events
- one stereo audio output bus
- a two-state waveform parameter:
  - `Sine`
  - `Square`
- monophonic note handling for clarity
- one shared oscillator implementation in MLang
- the standalone preview app triggers a note-on, runs the plug-in's realtime
  render loop for about 3 seconds through CoreAudio, then exits

## Notes

- This example is intentionally macOS-only because the standalone preview path
  uses CoreAudio's Audio Queue Services.
- The VST3 target is built via the SDK's official CMake helpers instead of a
  hand-written bundle command. That keeps the example close to Steinberg's own
  recommended build flow.
- The MLang DSP exports plain functions, and the C++ bridge calls their
  generated symbols directly. The bridge isolates the rest of the example from
  those ABI details.
