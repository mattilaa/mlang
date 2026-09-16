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

## Sessions (.mlack 1.0)

Launching mlacker without a filename starts one empty, 64-row **Untitled**
pattern, with no tracks, song entries, instruments, or samples. Menus stay closed.
Tab opens the menu bar. To open an existing session at startup:

```sh
mlacker/build/cmake/bin/mlacker "my song.mlack"
```

**File → Save session** (or Ctrl+S) saves to the current filename; the first save
asks for a `.mlack` path. **Save session as** chooses another path. **Open session**
loads a `.mlack` file. **New session** confirms before resetting to an empty editor.
Saving stops the sequencer; loading always restores a stopped transport.

Version 1.0 stores all patterns and song order, track types and assignments,
NOTE/VEL/LEN/OFF/automation data, audio placements, loaded samples, loaded
instrument/master-plugin paths and normalized parameter states, BPM/time signature,
playhead/cursors, pane focus, track zoom, scroll positions, sidebar mode, mixer and
sample styles, meter update rate, and the parameter editor's selection/visibility.
Ctrl+S works with the parameter editor open. Uncommitted text-entry drafts are not
saved.

Decoded audio and waveform data are embedded: the original WAV/AIFF files are not
needed to reopen the session. VST3 binaries are **not** embedded and must remain
installed at the saved paths. Only open trusted sessions: opening one can load
native plugin code. The current audio/MIDI device selection is retained, rather
than reopening machine-specific device IDs from another computer.

Saves use a flushed, same-directory temporary file and atomic replacement. Parse,
version, missing-plugin, and parameter-restore failures retain the current session.
There is no autosave or unsaved-change prompt on Open/Quit yet. Native plugin
opaque presets, internal sample banks, and non-parameter controller state are not
stored in 1.0; exposed parameter values are restored by ID. Device changes still
reload plugins with defaults. See [the binary format](FORMAT.md) for the schema
and limits.

## Pattern visual selection

In normal Pattern mode, `o` inserts a blank row below the cursor and `O` inserts
one above it, selecting the new row while keeping the current column. Later rows
and audio clip starts shift down; clips already spanning that point keep their
duration. The maximum pattern length remains 16384 rows.

In the Pattern pane, `v` starts a rectangular selection at the current cell.
Use `h/j/k/l` to extend it; selected cells have a brighter background. `y` copies
the selection, `d` cuts/clears its cells without removing rows, and `p` pastes at
the current cell. `Esc` or `v` cancels selection. The internal clipboard survives
pattern switches; it is not the operating-system clipboard or session data.

`Shift+V` selects whole rows across every track; `j/k` extends the row range.
`y` copies and `d` clears/cuts all writable cells, including hidden LEN/OFF and
automation fields. Row numbers and pattern length stay unchanged. To paste with
`p`, place the cursor at the first writable column of the destination row.
`Shift+V` again or `Esc` leaves whole-row selection.

Column-mode selection includes only visible, writable columns. Paste must fit the pattern and match
the destination column roles throughout (NOTE to NOTE, VEL to VEL, etc.). Invalid
pastes show an OK-only dialog and leave the pattern unchanged. Pasting does not
insert rows. New notes default to VEL 100, LEN 1.00 and OFF 0.00; explicitly copied
values override these defaults when those columns are included.

While selecting, `Shift+J/K` decreases/increases every nonempty selected value:
notes move by one semitone, VEL/automation by one, and LEN/OFF by 0.01 sixteenth
notes. If any value would exceed its limits, the entire step does nothing. Empty,
hidden and read-only cells stay unchanged; selection remains active for repeats.

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
  tracks reject new notes. Live CC 0–127 and 14-bit pitch bend use the same
  selected destination and VST3 MIDI mapping as pattern controls. Input MIDI
  channels and note velocities (0–127) are preserved; velocity zero is note-off.
  A patch must have velocity sensitivity/modulation enabled to respond audibly.
  Held keys retain
  their original destination for note-off when selection or assignment changes.
- Every instrument's stereo PCM is summed before the master processor, gain,
  and output clipping. The rightmost **Master** mixer strip stays pinned while
  track strips scroll. Its L/R meters show measured output peaks with smooth
  decay, using the selected meter style and update rate—not MIDI velocity.

### Mixer faders and MIDI recording

Press `m` to show Mixer, then focus it with `Ctrl+Shift+J` from Pattern view.
`h/l` or Left/Right selects tracks. Each strip has a vertical volume fader beside
its VU meter, with the 0–100 value underneath. `Shift+J/K` lowers/raises volume;
these keys still edit cells when Pattern view has focus. Gain affects preview,
PCM voices and VST instrument output. Tracks sharing a loaded VST instance share
its output gain; changing one linked fader updates the others in the pattern.

Instrument strips also show `M LR`: MIDI velocity beside measured left/right
plugin PCM peaks, after the instrument fader and before the master chain. PCM
meters share the meter style, update rate, and smooth decay. Tracks using the
same loaded instance display the same cached stereo output readings.

`Shift+R` in Mixer toggles the selected MIDI/instrument track's record arm. Its
upper `R` becomes white on red while armed. Incoming MIDI note-ons overwrite NOTE
and VEL at the selected pattern row/note line without advancing the cursor. A new
note defaults to LEN 1.00 and OFF 0.00; existing timing is retained. The last note
in a chord wins on that note line. Other armed tracks are not written unless
selected. Audio tracks do not record MIDI. Menus, dialogs, plugin editors, text
editing, and visual selection suspend recording. Volume is saved in `.mlack`;
record-arm is transient and starts off when a session is loaded.

## Use a master plugin

### Instrument parameter editor

Select an entry in **View → Instruments**, then choose **Instrument → Open VST3
editor**. This opens a terminal parameter editor over the Pattern pane (not the
plugin's native graphical window). Parameter names, step counts, read-only flags,
and initial values are copied into memory when the plugin loads.

- Arrow keys or `h/j/k/l` browse sliders. Moving between columns scrolls horizontally to keep
  the selected control visible; the bottom bar shows the horizontal position.
- `Shift+L` arms MIDI learn, displaying a white **L** on red. Move a synth CC control
  to bind its channel/CC to this parameter; capture ends listening. Press `Shift+L`
  again, move to another parameter, or close the editor to cancel listening.
  Read-only parameters cannot learn. Learned CCs control the assigned instrument
  independently of track focus and replace ordinary selected-track CC routing
  for that channel/CC. Values span the parameter range, rounded for discrete controls.
  Bindings belong to the session, survive output changes, and save in `.mlack`.
  New sessions start unmapped; removing an instrument removes its bindings.
- **Instrument → Save MIDI learn / Load MIDI learn** exports/imports mappings
  for the selected entry in Instruments as a `.mlalearn` file. Saving suggests
  `<plugin name> - `: type a suffix, or Ctrl+U to replace the whole filename/path.
  The extension is appended automatically when omitted. Loading replaces that
  instrument's mappings, matching stable parameter IDs rather than session slots.
  Different plugin names, missing/read-only parameters, malformed files, and CCs
  owned by another instrument are rejected without changing existing mappings.
  Plugin parameter values are not included; `.mlack` still saves session mappings.
- **Instrument → Save plugin preset / Load plugin preset** saves/restores exposed
  parameter values in `.mlapre` files for the selected instrument. The suggested
  `<plugin name> - ` prefix is editable (Ctrl+U replaces it); omitted extensions
  are appended. Loading validates the complete file, matches stable parameter
  IDs, and pauses audio/MIDI input while applying values. MIDI-learn mappings
  remain unchanged. These are parameter presets, not opaque VST3 state: internal
  sample libraries and other non-parameter plugin state are not included.
- `Shift+J` decreases and `Shift+K` increases the selected value.
- Enter opens manual entry; Ctrl+U clears the field and Enter validates/commits.
  Invalid input stays in the field. Esc cancels entry; Esc outside entry closes
  the editor and restores the previous pane focus.
- Continuous parameters use VST3's normalized `f64` range 0–1, with 0.01 steps;
  discrete controls use `i32` indices 0–stepCount. These are not physical Hz/dB
  units. Read-only parameters cannot be edited. The public `tui::slider::Slider`
  widget checks numeric type and min/max bounds and supports i32/u32/f32/f64.
- Accepted edits use the existing preallocated audio-event/parameter queues.
  The editor captures navigation, so the pattern and library do not move.

**Instrument → Remove instrument** stops playback, joins the MIDI worker, stops
audio, unloads the selected instance, and clears its assignments in every
pattern. Other instrument IDs remain unchanged; vacant slots can be reused.
Saved `.mlack` sessions restore exposed parameter edits across application
restarts and audio-device changes. Dynamic parameter-list changes and plugin-originated
parameter notifications are not handled yet; reopen to refresh cached values.

### Pattern CC1 / CC2

Both automation columns send their non-empty values at the pattern row boundary,
even without a note or when the columns are collapsed. **Track → Configure CC1 /
Configure CC2** selects `cc:N` (0–127; values 0–127) or `pitchbend` (values
-8192–8191). Defaults are CC1 = `cc:1`, CC2 = `cc:74`; the column labels are slot
names, not fixed MIDI controller numbers. Instrument tracks target their assigned
instance; MIDI tracks target the master plugin. Muted/audio tracks do not send
these events. Empty cells leave the current parameter value unchanged.

The host translates controllers using the plugin's `IMidiMapping` assignments
for event bus 0 and the track's MIDI channel. Assignments are cached at load time;
unmapped controllers and custom `name:min:max` slots are not sent. Plugins without
MIDI mappings cannot receive these controls yet. Parameter queues are bounded and
preallocated, with sample offsets preserved by the native audio event queue.

### Master slot

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
selected plugin for the new sample rate and restores its exposed parameter values.
Opaque, non-parameter plugin state is not preserved by device changes yet.

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
- One master slot plus 32 instrument slots; no per-track effect chains, native plugin editor windows, opaque preset
  persistence, arbitrary named-parameter automation, sidechains,
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
The session PTY test separately checks real empty startup, command-line opening,
parameter/editor-state round trips and rejected files. Legacy widget tests opt in
to seeded demo data with `MLANG_TUI_DEMO=1`; normal mlacker startup does not.

For a hardware-free manual run:

```sh
MLANG_TUI_NO_HARDWARE=1 mlacker/build/cmake/bin/mlacker
```

Run that command from the repository root. To test only the widgets without
fetching/building the SDK, continue compiling `examples/tui_demo.mla` as before.
