# mlacker

Terminal tracker built with MLang, the shared `tui` widget library, macOS AUHAL,
and a native VST3 host. This is the full application project; the old
`examples/tui_demo.mla` remains an SDK-free entrypoint for widget development.
Both entrypoints share `modules/tui_demo/app.mla` and the existing tracker model.

## Build and run

Prerequisites: the repository's compiler/runtime built in `../build`, macOS,
Xcode command-line tools, CMake, Git, Python 3, and the OpenSSL development
installation used by the MLang runtime. From the repository root:

```sh
build/mlang pkg --config mlacker/mlang.toml run run
```

This fetches the official VST3 SDK and submodules, builds the runtime and tracker,
and starts `mlacker/build/cmake/bin/mlacker`. To build without launching:

```sh
build/mlang pkg --config mlacker/mlang.toml build
```

`mlang.toml` pins SDK 3.8.1 at commit
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`; `mlang.lock` records the resolved
revision. Generated SDK sources and binaries stay under ignored `build/`.
SDK license and usage notices remain in `build/deps/vst3sdk/LICENSE.txt` and
`VST3_Usage_Guidelines.pdf`; preserve applicable notices when distributing.

## Instrument tracks

- **Add → Instrument** browses `.vst3` bundles on disk. On selection, the host
  validates the first class marked `Instrument`, its MIDI input and supported
  audio buses. Effects and incompatible/broken bundles report an error without
  adding an entry. Only open trusted plugins.
- **View → Instruments** shows session-wide loaded instances, numbered by ID.
  `j/k`, `gg`, and `G` navigate; Enter assigns the selected instance to the
  current Instrument track. Loading while an Instrument track is selected also
  assigns the new instance automatically.
- **Track → Create track → Instrument track** creates a note/velocity/LEN/OFF
  track using the selected library instance. If none is loaded it remains silent
  until assigned through **Add → Instrument** or the Instruments list.
- Up to 32 instances may be loaded, independently routed and summed before the
  master processor. Loading the same bundle again creates another instance.
  Assigning the same entry to multiple tracks shares plugin state and its 16 MIDI
  channels (track index modulo 16); use separate instances for isolated voices.
  Track/pattern duplication preserves the instrument ID rather than loading a
  new instance. Removing a track does not unload the library entry.
- Audio-device changes reload all instances at the new sample rate. Disabling
  output keeps them loaded in an offline controller.
- Live MIDI follows the selected Pattern-view track, even while another pane or
  menu has keyboard focus. Instrument tracks address their VST3 instance; MIDI
  tracks use the preview/master path. Audio, muted, and unassigned instrument
  tracks reject new notes. Input MIDI channels are preserved. Held keys retain
  their original destination for note-off when selection or assignment changes.
- Every instrument's stereo PCM is summed before the master processor, gain,
  and output clipping. The rightmost **Master** mixer strip stays pinned while
  track strips scroll. Its L/R meters show measured output peaks with smooth
  decay, using the selected meter style and update rate—not MIDI velocity.

## Use a master plugin

1. Select MIDI input and master output in **File → Settings**.
2. Choose **Add → VST3 master plugin**.
3. Select a `.vst3` bundle, or type its full path into the dialog and press Enter.
   The chooser starts in `/Library/Audio/Plug-Ins/VST3`; user plugins are commonly
   under `~/Library/Audio/Plug-Ins/VST3`. Matching bundles are selectable items,
   not directories to navigate into.
4. Play MIDI input or press Space to play pattern MIDI through the plugin.
5. **Add → Unload master VST3** restores the reference sine preview instrument.

The first audio-processor class in a bundle is loaded into one master slot.
Zero-audio-input plugins act as instruments and replace the reference sine
voices. Mono/stereo effects process the master mix instead. Notes retain MIDI
channel, pitch, velocity, and sample offset. Sequencer tracks use channels modulo
16; live MIDI retains its input channel. Audio can be disabled while loading a
plugin for inspection; its name appears in the Inspector, with no hardware open.
Failed replacement keeps the previous plugin. Audio-device changes reload the
selected plugin for the new sample rate, resetting its internal state.

Only load plugins you trust. Plugins execute in-process; they are not sandboxed.
A plugin can display its own authorization UI, block, or crash the tracker.

## Audio path and lifecycle

The main/sequencer and MIDI-worker producers keep separate preallocated SPSC
queues. The AUHAL consumer drains a bounded batch and converts MIDI events to
VST3 events with offsets inside the current block. The host preallocates audio
and event buffers and does not load modules, allocate, log, or take locks in its
render path. Third-party plugins must independently satisfy realtime constraints.
Panic/queue overflow sends bounded note-offs covering every channel and pitch.
Failed processing silences the block, increments an atomic error counter, and
reports the failure to the UI without logging from the callback.

Module loading, component/controller initialization, bus negotiation, activation,
and teardown run on the main thread with audio stopped. The module remains loaded
until its component, controller, processor and connections have been released.
The VST3 SDK is linked only into mlacker, not into the TUI library or stdlib.
`stdlib/include/mlang_audio_processor.h` is the SDK-independent native bridge.

Current scope:

- macOS, native-architecture VST3 bundles; float32 mono/stereo, at most one audio
  input bus and one output bus, and the first MIDI input bus.
- One master slot plus 32 instrument slots; no per-track effect chains, plugin editor windows, preset or
  state persistence, parameter automation, MIDI CC/pitch-bend mapping, sidechains,
  latency compensation, or transport/tempo synchronization yet.
- Existing UI-loop sequencer timing is retained. The audio API supports absolute
  frame scheduling, but a look-ahead sequencer is still future work.
- Pattern audio-instance scheduling is still not connected to master playback;
  the controller's PCM sample event API remains available.
- AUHAL's 128-frame request is not a measured end-to-end latency guarantee.

## Tests

```sh
build/mlang pkg --config mlacker/mlang.toml run test
```

This builds two local test bundles (never installed in system plugin folders),
loads them through the real module loader, and checks instrument/effect output,
frame-accurate event offsets, live MIDI lane independence, panic, failed
replacement, repeated unload/reload, instrument-only validation and independent
instrument-slot mixing. Four PTY tests cover Settings, plugin selection/load/unload,
Instrument track creation/assignment, Instruments view, existing widgets and
playback. They open no audio devices.

For a hardware-free manual run:

```sh
MLANG_TUI_NO_HARDWARE=1 mlacker/build/cmake/bin/mlacker
```

Run that command from the repository root. To test only the widgets without
fetching/building the SDK, continue compiling `examples/tui_demo.mla` as before.
