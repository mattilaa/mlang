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
F1 opens the menu bar, and Tab / Shift+Tab cycle the panes. To open an existing
session at startup:

```sh
mlacker/build/cmake/bin/mlacker "my song.mlack"
```

**File → Save session** (or Ctrl+S) saves to the current filename; the first save
asks for a `.mlack` path. **Save session as** chooses another path. **Open session**
loads a `.mlack` file. **New session** confirms before resetting to an empty editor.
Saving stops the sequencer; loading always restores a stopped transport.

## Menus

The menu bar opens with **F1**. Each menu starts with what it creates, then what
it changes, then what it removes; related entries live in submenus.

| Menu | Contents |
|------|----------|
| File | New / Open / Recent sessions ▸ / Save / Save as / Settings / Quit |
| Edit | Undo, Redo, Copy/Cut/Paste clip |
| View | Patterns, Song matrix, Audio, Instruments, Sample view ▸, Meter ▸, Reset layout, Show details |
| Track | Create MIDI/AUDIO/Instrument track, Rename, Duplicate, Mute, Note lines ▸, Automation ▸, Clear pattern, Delete |
| Pattern | Add, Clone, Rename, Set length, Follow matrix patterns, Set matrix row length, Remove, Save pattern, Load pattern |
| Audio | Add audio, Edit sample (destructive), Clip ▸, Remove audio |
| Instrument | Add instrument, Open VST3 editor, Drum pads ▸, Presets ▸, MIDI learn ▸, Remove instrument |
| Effect | Add effect channel, Load/Edit effect plugin, Set track send, Master ▸, Remove effect plugin |
| Record | Play metronome, Extend pattern when playing, Metronome ▸ |

## Song matrix

**Shift+M**, or **View → Song matrix**, shows the song matrix in the pattern
pane. It replaces the old Song sidebar: rows are song steps, columns are
parallel lanes, and every pattern on a row plays together. The sidebar keeps the
pattern list.

| Key | Action |
|-----|--------|
| `h/j/k/l` or arrows | Move the cursor |
| `Enter` | Choose the cell's pattern from a list of `<no>:<name>`, or "(empty)" |
| `Backspace` | Empty the cell |
| `y` / `p` | Copy the cell's pattern / paste it into another cell |
| `o` / `O` | Insert an empty row below / above, across every lane |
| `Ctrl+O` / `Ctrl+Shift+O` | Insert a cell below / above in this lane only, leaving the other lanes where they are |
| `dd` | Remove the whole row |
| `Ctrl+P` | Play the matrix from the cursor row |
| `Shift+M` | Close the matrix |

**Ctrl+P** plays the song from the cursor row: every pattern in a row plays
together, then the transport moves to the next row holding patterns, wrapping at
the end. The playing row is drawn brighter. Plain **Space** always means "the
selected pattern", so pressing it during matrix playback leaves the song and
plays that pattern alone; Ctrl+P again stops.

### Row length and long patterns

One matrix row holds a fixed stretch of pattern time. A new song uses 64 rows;
a song opened from a file that predates the setting takes the length most of its
patterns have, so a song written from 16-row patterns plays them one per matrix
row instead of repeating each of them four times. **Pattern → Set matrix row
length** changes it, and from then on the song keeps what you chose. A pattern longer than
that occupies as many matrix rows as it needs and plays a different stretch of
itself in each, so the lanes beside it move on to their own patterns instead of
waiting for it. A 128-row pattern beside two 64-row ones looks like this:

```
ROW  L1              L2
001  Verse       ┬   DrumsA
002          ┴       DrumsB
003  Chorus          DrumsA
```

With a 16-row grid, four 16-row patterns in a lane fill exactly the time one
64-row pattern takes beside them:

```
ROW  L1              L2
002  Beat A          Untitled ┬
003  Beat B                 │
004  Beat A                 │
005  Snarefill              ┴
```

The rows a pattern covers belong to it: placing another pattern there is refused
with the row it started on, and Backspace on such a row clears the pattern that
covers it. A pattern shorter than the row length repeats to fill its row, as
before, and a song that never changes the length keeps playing exactly as it did.

Within a row, every lane plays its own stretch, and a lane holding nothing is
silent. Every lane brings its own tracks with their clips,
faders, sends, inserts and output channels, and the mixer the engine follows is
the row that is playing, not whatever pattern the sidebar has selected. A row
that starts the song with a pattern carrying no effects therefore no longer
takes the next row's routing with it.

The matrix replaces the pattern editor in that pane while it is open, and menus
open above it. A session with no song yet starts the matrix on one empty row.

**Pattern → Follow matrix patterns** (on by default, shown as `[x]` in the menu)
keeps the rest of the app on the pattern the matrix points at:

- *Browsing*: moving the cursor over a cell selects that cell's pattern, so the
  editor behind the matrix, the sidebar and the mixer show its channels, faders,
  sends, inserts and output routing. The status line names it. Empty cells keep
  the pattern already shown.
- *Playing*: each matrix row moves the editor and sidebar to the pattern the row
  starts from, and the mixer shows the whole row: every lane's channels, with a
  channel that several parallel patterns share drawn once (same kind, name and
  instrument). Those strips are the song's, not one pattern's, so they are a view
  only; stop the matrix or turn the option off to mix.

With the option off, the editor and mixer stay on the selected pattern. The
setting lasts for the session and is not saved in `.mlack`.

Cells may be empty, including in lane 1: an empty row is a silent step. An empty
lane is always available past the last used one, up to 16 lanes. Pasting a
pattern warns about clashes just like choosing one does.

### Parallel patterns that clash

Two patterns on the same row can drive one instrument with the same note at the
same step, e.g. two patterns hitting the same drum pad. Placing such a pattern
warns first, counting the clashing notes:

- **OK** places it anyway, leaving the notes as they are.
- **Cancel** leaves the cell alone.
- **Split** places it and gives each clashing track its own note line, so the
  parallel patterns stop sharing one. A split track is renamed with the
  `Track 1 - 2`, `Track 1 - 3` convention, counting the patterns that share the
  instrument in that row. Splitting again replaces the suffix rather than
  stacking it.

Only real collisions warn: the same instrument playing different notes, or the
same note at different steps, is left alone. The matrix is arrangement state —
the transport still plays the selected pattern.

## Removing library items

**Shift+Backspace** in the library pane removes the selected item: a sample when
the Audio list is shown, an instrument instance when the Instruments list is.
**Audio → Remove audio** and **Instrument → Remove instrument** do the same from
the menu.

An item still in use asks first:

- A sample used by clips: **Remove clips** deletes the sample and every clip of
  it, or **Cancel** keeps both. Clips cannot outlive their sample, because a
  session stores each clip as an index into the sample list.
- An instrument played by tracks: **Remove tracks** deletes those tracks, or
  **Keep tracks** keeps their notes and only clears the assignment, ready for
  another plugin.

Removing a sample also empties any drum pad loaded from it and renumbers the
list.

## Keys and panes

**F1** opens and closes the menu bar. **Tab** and **Shift+Tab** cycle forwards and
backwards through every pane of the main view: the sidebar, the pattern editor,
the inspector/mixer, the FX bus (when the mixer shows effect channels) and the
VST3 editor (while it is open). The pattern pane is skipped while the VST3 editor
covers it, and the editor keeps its own keys only while it holds focus, so Tab
moves out of it without closing it. `Ctrl+Shift+H/J/K/L` still moves between
panes by direction.

**Space** starts and stops the sequencer from every pane, including the mixer,
the FX bus and the VST3 editor. Text entry, modal dialogs and an open menu keep
Space for themselves.

Inside a dialog, Tab keeps cycling that dialog's own panes.

## File browser

Every chooser (sessions, samples, VST3 bundles, MIDI-learn and preset files)
remembers the directory it last browsed for the rest of the run, so reopening it
returns there. The kinds are remembered separately: plugins, samples, presets and
sessions each keep their own directory, and plugin browsing starts at
`/Library/Audio/Plug-Ins/VST3` until you browse elsewhere. The memory is not
saved in `.mlack`, so it resets when mlacker restarts.

In the **Files** pane, Space marks the file under the cursor and moves to the
next one; Space again unmarks it. Marked rows are drawn a step brighter than the
rest, and Enter (or OK) opens every marked file instead of the one under the
cursor. Marks survive moving between directories and are cleared when the
chooser reopens. Multi-select is only offered where opening several files makes
sense: **Audio → Add audio**, **Instrument → Add instrument** (each bundle becomes its own
instance) and pad samples (which fill consecutive pads from the chosen key).
Session, preset, MIDI-learn, effect/insert and every save chooser stay
single-file, and Space does nothing there.

Browsing starts focused on the **Directories** pane on the left, where `j/k`
moves, Enter or `l` expands, and `h` collapses. Tab cycles Path → Directories →
Files → buttons, and Ctrl+Shift+H/J/K/L moves between them. Typing `/` or `~`
jumps to the path field and starts a fresh absolute path; Ctrl+U clears the field
and focuses it. Save choosers open in the path field instead, since they start
from a suggested filename. Relative paths typed into the field resolve against
the directory being browsed.

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

**Pattern → Save pattern / Load pattern** uses `.mlapatt` files. Saving includes
the active pattern’s notes, track settings and embedded audio clips. Loading adds
and selects a new pattern without changing the song matrix. Instrument assignments
and effect routing are cleared so you can assign plugins in the current session.
The save dialog appends `.mlapatt` when needed.

In the Pattern pane, **Ctrl-Z** cycles vertical detail through **1/16 → 1/32 →
1/64 → 1/16**. Only original sixteenth-note rows have row numbers; intermediate
lines show `.` and use alternating darker backgrounds. Up/Down (or `j`/`k`)
visits each visible subdivision: the 1/64 view has three editable positions
between numbered rows. Enter edits a note, velocity, length or offset; Shift-J/K
adjusts values and Backspace clears the selected field. MIDI step entry on an
armed track also uses the selected subdivision. Notes entered between
rows are stored with timing offsets and play at every zoom level, including
1/16. They survive session and pattern save/load without increasing the pattern's
main row count. Chords at each visible position share horizontal note lanes.
The 1/32 notes are slightly darker and 1/64 notes darker again; note-length lines
expand with the view.

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

- **Instrument → Add instrument** browses `.vst3` bundles on disk. On selection, the host
  validates the first class marked `Instrument`, its MIDI input and supported
  audio buses. Effects and incompatible/broken bundles report an error without
  adding an entry. Only open trusted plugins.
- Each Instrument track shows its instance under the track name in the pattern
  view, numbered as in the Instruments list (e.g. `001 Mla Drum`), or
  `- unassigned -` until one is assigned.
- **View → Instruments** shows session-wide loaded instances, numbered by ID.
  `j/k`, `gg`, and `G` navigate; Enter assigns the selected instance to the
  current Instrument track. If another track already plays that instance,
  mlacker asks first. **New instance** loads another copy of the same plugin for
  this track, with its own pads, fader, meters and empty insert slots. **Share**
  links the track to the existing instance. Loading while an Instrument track is selected also
  assigns the new instance automatically.
- **Track → Create Instrument track** creates a note/velocity/LEN/OFF
  track using the selected library instance, unless another track in the pattern
  already uses that instance. The new track then starts unassigned, so it never
  silently shares another track's pads, fader and meters. An unassigned track
  stays silent until assigned through **Instrument → Add instrument** (a new instance) or
  Enter in the Instruments list, which asks whether to share or load a new
  instance.
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

### Drum sampler pads

[Mla Drum](../plugins/mla_drum) (and any instrument implementing
`stdlib/include/mla_sampler_protocol.h`) takes samples into numbered pads. Select
the loaded instance in **View → Instruments**, then:

- **Instrument → Drum pads → Send audio sample to pad** sends the sample selected in
  **View → Audio** to a key you pick. Pads go to the selected Instrument track's
  own instance (the picker title shows e.g. `Mla Drum #2`). For other tracks they
  go to the instance selected in the Instruments list.
- **Instrument → Drum pads → Load pad sample from file** picks the key first, then a WAV/AIFF.
  The file is added to the Audio list too.

Both open a piano keyboard. It spans the instrument's pads: pad 1 is the plugin's
Root Key (default MIDI 36, `C-2`), pad 2 one key higher, and so on. Keys outside
the pads are dimmed. The selected key is dark gray, and a red dot marks pads that
already hold a sample. The line under the keyboard names the key, pad and current
sample. `h/l` (or Left/Right) moves one key and `j/k` one octave. The picker opens
on the first empty pad. Enter inserts the sample, replacing any sample already on
that pad. Backspace/Delete clears a loaded pad. Esc cancels.

Pads can be loaded while audio is playing. For per-drum effects, load
one instance per drum family, give each its own Instrument track, and add
inserts to those tracks. `.mlack` saves which Audio sample each pad uses and
reloads the pads on open and after audio-device changes. Removing an instrument
forgets its pads. **Save plugin preset** on a sampler writes a kit preset that
embeds its pad samples. Loading it restores all pads and clears pads the kit
does not use.

### Destructive sample editing

**Audio → Edit sample (destructive)**, or `e` in the Audio list, opens the selected
sample in an editor over the Pattern view. Edits work on a copy with 16 undo
steps. **Enter** saves the result into the session, and **Esc** discards it.
Saving replaces the sample in the Audio list and in every pattern placement.
Placements whose length changed return to the natural length. Every drum pad
that uses the sample is re-sent at once.

Select a range with `Ctrl+N`/`Ctrl+M` (start) and `Shift+N`/`Shift+M` (end).
`Ctrl+H`/`Ctrl+L` zoom, and `a` selects everything. Without a narrower selection,
edits apply to the whole sample.

| Key | Edit |
|-----|------|
| `t` | Crop to the selection |
| `x` | Delete the selection |
| `r` | Reverse |
| `i` / `o` | Linear fade in / fade out |
| `n` | Normalize the selection peak to 0 dBFS |
| `-` / `+` | Gain −1 dB / +1 dB |
| `s` | Silence |
| `u` | Undo |

Results are hard-clipped to −1..1, the range sessions store. Edits change the
session copy only. The source WAV/AIFF on disk is never rewritten.

### Mixer faders and MIDI recording

Press `m` to show Mixer, then focus it with Tab (or `Ctrl+Shift+J`) from Pattern
view.
`h/l` or Left/Right selects tracks. Each strip has a vertical volume fader beside
its VU meter, with the 0–100 value underneath. `Shift+J/K` lowers/raises volume;
these keys still edit cells when Pattern view has focus. Gain affects preview,
PCM voices and VST instrument output. Tracks sharing a loaded VST instance share
its output gain; changing one linked fader updates the others in the pattern.

Instrument strips also show `M LR`: MIDI velocity beside measured left/right
plugin PCM peaks, after the instrument fader and before the master chain. PCM
meters share the meter style, update rate, and smooth decay. Tracks using the
same loaded instance display the same cached stereo output readings.

`Shift+R` in Mixer arms the selected MIDI/instrument track (red `R`). Press
Space to record from the selected row; press Space again to stop and enter the
track name (Enter accepts, Esc keeps the old name). Each take targets one synth
track, with up to 64 automatically added note lines. Notes on the same row are
inserted in ascending pitch order with their velocities and timing intact.
LEN captures key-hold duration. OFF captures early/late timing relative to the
nearest sixteenth-note row, to 0.01 row precision. All recorded cells remain editable.

The **Record** menu has two options, enabled by default:

- **Play metronome**: clicks at the current tempo and time signature, with a higher
  first beat and lower remaining beats. An armed recording starts after one full
  bar of count-in; pattern playback and note capture wait until it finishes.
  Turning this off starts recording immediately. **Metronome → On recording**
  (default) limits clicks to recording and its count-in; **Always** also clicks
  during normal pattern and song playback. These modes are mutually exclusive.
- **Extend pattern when playing**: grows the pattern while recording (up to the
  pattern limit of 16,384 rows). With it off, recording loops at the existing
  length, replacing the armed track's notes as each row is reached. Other tracks
  remain intact. The choice takes effect at the start of a take.

Press Space to stop, including during the count-in.
The live MIDI path monitors the synth during recording.

While stopped, incoming notes still provide single-cell step entry on the armed
track. Audio tracks do not record MIDI. Opening a modal editor stops a timed take.
Volume is saved in `.mlack`; record-arm is transient and starts off on load.

### Output channels

The bottom row of every mixer strip names where the channel goes, under the
`0–100` fader value:

| Row | Meaning |
|-----|---------|
| `OUT:MST` | Straight to Master (the default on every channel) |
| `OUT:A3` | Into audio track 3, which then applies its own inserts, fader and sends |
| `OUT:I4` | An instrument track: its notes play instrument instance 4, whose audio goes to Master |
| `I4>A3` | Instrument 4, routed on into audio track 3 |

**Track → Set output channel** lists the destinations for the selected track and
routes it with Enter (`j/k` selects, Esc cancels). What it offers depends on the
kind of track:

- A **MIDI track** picks the instrument its notes play: `MST` keeps the built-in
  preview tone, `I1`, `I2`, … assign a loaded instance, exactly like Enter in the
  Instruments view. Picking an instance another track already plays shares it
  (same pads, fader and inserts), which the status line reports.
- An **instrument or audio track** picks the audio channel it feeds: `MST` or any
  other audio track. Master is always the default.

A track cannot feed itself, only audio tracks take other channels, and a route
that would loop back is refused. Tracks sharing one instrument instance share its
output channel, as they already share its inserts and sends. Deleting a track
clears the routes into it and shifts the ones past it. Routed audio arrives before
the destination's inserts, so the destination's chain, fader and sends all apply
on top of the source's own. Sends stay post-fader on the source itself.

A channel that something is routed into meters what it actually puts out: its
`L`/`R` bars show the measured post-fader output of the whole channel, its own
clips and every source routed into it, instead of the levels derived from its
pattern. The same measured meter appears on channels with inserts. Channels that
mix straight into Master with neither keep their pattern-derived meters, and an
instrument strip's `M LR` pair still shows that instance alone, before its
destination's chain.

Output channels are saved in `.mlack` per pattern track.

### Track inserts

With Pattern view focused, `f` toggles four insert slots between each track name
and its NOTE/VEL column headers. While shown, `h/l` selects a track and `j/k`
selects one of its four slots. Enter browses for a VST3 audio effect in an empty
slot, or opens the existing effect's parameter editor. **Effect → Edit effect
plugin** also edits the selected insert. Backspace removes the slot's assignment;
`f` returns to normal pattern navigation. Loaded plugin names appear in the slots.

Inserts process serially from top to bottom, before channel volume and aux sends.
They work on sample tracks, MIDI preview audio, and VST instrument outputs.
Tracks assigned to the same VST instrument share its output and insert chain.
Pattern copies retain their insert assignments; a duplicated non-instrument track
starts with empty inserts so it does not reuse another audio stream's processor.
`.mlack` preserves assignments, exposed parameters, and the active insert view;
audio-device changes restore parameters as well. Up to 256 insert instances are
retained per session. Detached instances are reused on the next load only when no
pattern references them, so removing an insert does not alter another pattern.

### Aux effect channels

Choose **Effect → Add effect channel** to create an aux return (up to eight).
Effect strips occupy a separate bank before the pinned Master strip; ordinary
tracks and effects scroll independently when they do not fit. In Mixer, `l`
from the last ordinary track enters the effect bank; `h` from the first effect
returns to the tracks. `Shift+J/K` adjusts the selected return's volume.

Press Enter on an empty effect strip to browse for a VST3 effect, or on a loaded
strip to edit its parameters. The same actions are available in the Effect menu.
**Remove effect plugin** empties the selected channel without removing its strip.

Select a source track in Pattern view, then select the desired effect strip and
choose **Effect → Set track send** (0–100%). In Mixer, `z` expands/collapses the
selected track's FX sliders beside its volume and meters. `h/l` moves between
volume and the `FX1`, `FX2`, … sliders; moving past the last slider selects the
next channel. `Shift+J/K` adjusts the focused slider. Wide strips scroll their
FX controls to keep the selected slider visible. Expansion is per track and
transient; switching patterns resets it, while send values remain saved.

Sends are post-fader: 0% is dry, 100% fully wet through the loaded effect.
With multiple FX, each receives its own send amount, while the dry gain is
`1 - max(loaded FX sends)`. All effect returns are summed into Master.
Empty FX slots do not attenuate the dry path.
For reverb/delay aux use, set the plugin's wet mix to 100%; mlacker does not force
plugin-specific wet/dry parameters. Effects continue processing silence for tails.
Effect-to-effect sends are not supported.

Send levels belong to each pattern. Tracks sharing a VST instrument instance
share its PCM send levels, just as they share output gain. `.mlack` saves effect
paths, exposed parameters, return levels, sends, and effect-bank selection.

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
- **Instrument → MIDI learn → Save / Load MIDI learn** exports/imports mappings
  for the selected entry in Instruments as a `.mlalearn` file. Saving suggests
  `<plugin name> - `: type a suffix, or Ctrl+U to replace the whole filename/path.
  The extension is appended automatically when omitted. Loading replaces that
  instrument's mappings, matching stable parameter IDs rather than session slots.
  Different plugin names, missing/read-only parameters, malformed files, and CCs
  owned by another instrument are rejected without changing existing mappings.
  Plugin parameter values are not included; `.mlack` still saves session mappings.
- **Instrument → Presets → Save / Load plugin preset** saves/restores exposed
  parameter values in `.mlapre` files for the selected instrument. The suggested
  `<plugin name> - ` prefix is editable (Ctrl+U replaces it); omitted extensions
  are appended. Loading validates the complete file, matches stable parameter
  IDs, and pauses audio/MIDI input while applying values. MIDI-learn mappings
  remain unchanged. These are parameter presets, not opaque VST3 state: internal
  sample libraries and other non-parameter plugin state are not included. The
  exception is sampler pads loaded from mlacker (Mla Drum): the preset becomes a
  kit that embeds those samples, and loading it restores every pad (see
  **Drum sampler pads**).
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

1. Select MIDI input, master output, buffer size, and sample rate in **File → Settings**.
   Buffer choices are 32–4096 frames (samples per channel), default 128.
   Smaller buffers reduce latency; larger buffers allow more processing time.
   Sample rates are Device default, 44.1, 48, and 96 kHz. The explicit rate sets
   audio-engine and plugin processing; CoreAudio converts to the device rate if
   needed. Device default keeps the native device rate.
   Both selections survive device changes and session opens during this run.
   After applying, the status shows the actual device buffer size and sample rate.
2. Choose **Effect → Master → Load master VST3**.
3. Select a `.vst3` bundle, or type its full path into the dialog and press Enter.
   The chooser starts in `/Library/Audio/Plug-Ins/VST3`; user plugins are commonly
   under `~/Library/Audio/Plug-Ins/VST3`. Matching bundles are selectable items,
   not directories to navigate into.
4. Play MIDI input or press Space to play pattern MIDI through the plugin.
5. **Effect → Master → Unload master VST3** restores the reference sine preview instrument.

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
- One master slot plus 32 instrument slots, eight aux returns, and four inserts
  per track; no native plugin editor windows, opaque preset
  persistence, arbitrary named-parameter automation, sidechains,
  latency compensation, or transport/tempo synchronization yet.
- Existing UI-loop sequencer timing is retained. The audio API supports absolute
  frame scheduling, but a look-ahead sequencer is still future work. Because the
  UI loop drives it, a note can start up to one frame late, never a loop late:
  each pattern is scheduled from the row it starts on, so the row that starts
  playback, and row 1 of a pattern the matrix moves to, play their own notes even
  though the update that schedules them arrives a frame after the transport did.
  A pattern keeps its own grid, so matrix rows of different lengths stay in step.
  Audio clips follow the same rule: a matrix row change silences the previous
  row's clips and re-arms the new pattern's from the row it starts on. Arming a
  pattern never stops the audio device: clip PCM is registered while it renders
  and cached per clip for the session, so effect tails and instrument voices
  survive starting playback and every matrix row change. Only a full sample
  table (256 clips) reloads the hard way.
- Pattern audio samples feed Master directly, respecting track mute/volume, start
  row and LEN. Starting inside a sample seeks into its embedded PCM; stopping
  playback releases its voices. FX sliders blend the dry path with parallel
  effect returns. PCM is copied while audio is stopped before playback
  (up to 256 placements per pattern); the callback receives lock-free events only.
- AUHAL's selected buffer-size request is not a measured end-to-end latency guarantee.

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

### Effect plugin presets

**Effect → Presets → Load plugin preset / Save plugin preset** uses `.mlafxpre`
files. Select an effect channel in Mixer, a loaded insert in the track's insert
rows, or open the desired effect's parameter editor and move focus out with Tab.
An open effect editor takes precedence. Saving adds `.mlafxpre` when
needed; loading filters for that extension, case-insensitively.

FX presets save exposed plugin parameters by stable ID and check the plugin name
and parameter layout before loading. Invalid or incompatible files leave the
parameters unchanged. Instrument presets continue to use `.mlapre` and may
include sampler pads; the two formats are deliberately distinct.
