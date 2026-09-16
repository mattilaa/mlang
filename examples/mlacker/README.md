# mlacker — Headless Drum-Machine VST3 (no GUI)

A subproject for the **mlacker** tracker DAW: a GUI-less VST3 drum machine that
receives its samples *from the host* and plays them from tracker note rows. The
DSP master bus is written in MLang; the VST3 boundary and sample engine are in
C++.

The `mlang.toml` in this directory drives the whole build. It compiles the
MLang DSP object, configures CMake against the fetched Steinberg VST3 SDK, and
builds **two separate binaries**:

| Target | Output | Purpose |
| --- | --- | --- |
| `MlackerDrum` | `MlackerDrum.vst3` (bundle) | the headless plug-in mlacker loads |
| `mlacker_offline_host` | `mlacker_offline_host` (exe) | a standalone mlacker simulator that uploads a sample and renders a pattern to WAV — no audio device required |

## No GUI

There is no editor. `Plugin` never implements `IPlugView`, so the host uses its
own generic parameter panel. The only parameter exposed is **Master Gain**.
Everything else — which sample lives on which pad, and when pads fire — is
driven by mlacker through note events and the upload message below.

## Uploading samples from mlacker

VST3 lets a host push arbitrary data into a plug-in through the connection-point
message system. mlacker builds an `IMessage`, fills its `IAttributeList`, and
sends it; the plug-in decodes it in `Plugin::notify()`. The contract lives in
[`src/messages.h`](src/messages.h):

- message id: `mlacker.upload.sample`
- `pad` (int) — target pad, 0..15
- **either** `pcm` (binary, interleaved float32) **or** `path` (string, a WAV file)
- `channels`, `sampleRate` (int) — describe the `pcm` payload

`pcm` is the "upload the bytes" path — good for one-shot drum hits. `path` is
the lighter "upload by reference" path — the plug-in decodes the WAV itself
(16-bit PCM) and, because the path is stored, it can reload on project open.

### mlacker-side sketch

```cpp
IPtr<IMessage> msg = owned(hostApp->allocateMessage()); // or new HostMessage
msg->setMessageID("mlacker.upload.sample");
auto* a = msg->getAttributes();
a->setInt("pad", padIndex);
a->setInt("channels", 1);
a->setInt("sampleRate", 48000);
a->setBinary("pcm", floatSamples, byteCount);   // or a->setString("path", wavPath)
connectionToPlugin->notify(msg);                // -> Plugin::notify()
```

`src/offline_host.cpp` does exactly this (see `uploadPcm` / `uploadPath`).

## Playing pads

The plug-in has one event input bus and one stereo output bus. Note-on pitch
maps to a pad: `pad = pitch - 36` (C1 → pad 0), clamped to 0..15. Pads are
one-shots, so note-off is ignored. In a tracker, each instrument row simply
emits a note-on for its pad.

## Persistence

`getState`/`setState` store the master gain and each pad's WAV path, so a
mlacker song reloads its kit automatically. Pads uploaded as raw `pcm` bytes
(no path) are not re-embedded in the state — send them by `path` if you need
them to survive a reload.

## MLang DSP seam

`src/drum_dsp.mla` implements the master-bus shaping (gain + cubic soft-clip).
It compiles to `build/obj/drum_dsp.o` and is called per output sample by the C++
engine through `src/mlang_dsp_bridge.cpp`. This mirrors the other VST3 examples:
the audio API stays in C++, the signal math stays in MLang.

## Build

From this directory (a POSIX shell and the compiler built from the repo root
are required, as with the other package-manager examples):

```sh
mlang pkg fetch
mlang pkg build
```

Then run the standalone mlacker simulator (fetches + builds first if needed):

```sh
mlang pkg run render
```

That uploads a synthetic kick into pad 0 via an `IMessage`, plays a 16-step
pattern, and writes `build/mlacker_offline_render.wav`. To also load pad 1 from
a real WAV by path:

```sh
mlang pkg run render -- /path/to/snare.wav
```

## Finding the plug-in

After `pkg build`, the bundle is under `build/cmake/`:

```sh
find build/cmake -name '*.vst3'
```

## Files

| File | Role |
| --- | --- |
| `src/drum_dsp.mla` | MLang master-bus DSP |
| `src/mlang_dsp_bridge.{h,cpp}` | C++ ⇄ MLang DSP bridge |
| `src/drum_engine.{h,cpp}` | pads, WAV decode, voice mixing (SDK-independent) |
| `src/messages.h` | host ⇄ plug-in sample-upload contract |
| `src/plugin.{h,cpp}` | headless VST3 processor, `notify()` upload handler, state |
| `src/plugin_entry.cpp` | VST3 factory (`Instrument|Drum`) |
| `src/plug_ids.h`, `src/version.h` | class id, parameter ids, version |
| `src/offline_host.cpp` | standalone mlacker simulator → WAV |

## Notes

- The offline host needs no CoreAudio/JACK, so it is cross-platform.
- On multi-config generators the offline-host binary may be under
  `build/cmake/bin/<Config>/`; adjust the `render` task path if needed.
- The WAV decoder handles 16-bit PCM `.wav` files (mono or multi-channel,
  down-mixed to mono per pad).
