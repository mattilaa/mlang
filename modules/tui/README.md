# tui

A standalone MLang terminal widget library, built on `std::esc`, `std::term`,
`std::bytes`, and `std::strbuf`. Import `mod tui;` or individual `tui::*` modules.
It lives outside `std` and uses the existing module search/install mechanism.

This first version provides:

- Horizontal and vertical layouts with fixed sizes, weighted flexible sizes,
  gaps, and `Alignment::Start`, `Center`, or `End` on either axis. Nest layouts
  by splitting a child rectangle again. Alignment positions fixed-size groups
  in unused space; flexible children consume that space instead.
- A viewport whose optional menu row is reserved above its content. Opening a
  menu never changes the layout's available height.
- Box-drawn panels and labels, using a customizable dark blue/gray RGB theme.
- Spatial pane focus with accent-colored borders and Ctrl+Shift+H/J/K/L
  navigation. Menus temporarily suspend focus and restore the selected pane.
- A menu bar with keyboard navigation, disabled items, command IDs, scrolling
  selection in short popups, cascading submenus, and popup bounds constrained
  to the viewport.
- Cell compositing: draw content first, then the menu bar. Popup shadows retain
  existing glyphs and darken both foreground and background colors.
- A modal Open session browser with an editable path, lazy directory tree,
  file list, and its own pane focus; a reusable base dialog, push buttons,
  and a non-file question dialog.
- Explicit alternate-screen/raw-input setup and restoration, terminal size
  queries, and plain-text snapshots when output is redirected.

```mlang
mod tui;
use tui::geometry::*;
use tui::layout::*;
use tui::menu::*;
use tui::surface::*;
use tui::theme::*;
use tui::widgets::*;

fn main() -> i32 {
    let theme: Theme = Theme::dark_blue();
    let surface: Surface = Surface::new(80, 24);
    if !surface.is_valid() { return 1; }
    var menus: MenuBar = MenuBar::new([
        Menu { title: "File", items: [
            MenuItem { label: "Open", action: 1, enabled: true },
            MenuItem { label: "Quit", action: 2, enabled: true }
        ] }
    ]);
    let viewport: Viewport = menus.viewport(surface.bounds());
    let layout: Layout = Layout { axis: Axis::Horizontal, gap: 1,
        sizes: [Size::fixed(24), Size::flex(1)] };
    let panes: list<Rect> = layout.split(viewport.content());
    surface.fill(surface.bounds(), Cell { glyph: 32,
        foreground: theme.foreground, background: theme.background });
    let panel: Panel = Panel { title: " Browser " };
    panel.paint(surface, panes[0], theme);
    menus.open(0);
    menus.paint(surface, surface.bounds(), theme); // top layer, painted last
    surface.release();
    return 0;
}
```

`MenuBar::new([])` creates a viewport with no menu row. `on_key(Key::...)` returns
zero for navigation and a positive application-defined command ID on Enter.
Handle the command in the application; the library does not invoke callbacks.
The demo maps arrows, Tab, Escape, Enter, and `hjkl` to these semantic keys.

### Cascading submenus

Add a submenu as a menu item with `MenuItem::submenu(label, children)`:

```mlang
MenuItem::submenu("Recent sessions", [
    MenuItem { label: "Blue hour", action: 8, enabled: true },
    MenuItem::submenu("Templates", [
        MenuItem { label: "Ambient", action: 9, enabled: true },
        MenuItem { label: "Dance", action: 10, enabled: true }
    ])
])
```

Rows with children display `>` at the right. `l`/Right or Enter opens the
selected item's submenu. `h`/Left closes one submenu and restores its parent's
selected row. `j`/Down and `k`/Up navigate only the deepest open menu, skipping
disabled items. On a leaf inside a submenu, `l` does nothing. At the root,
Left selects the previous menu-bar entry and Right selects the next entry when
the selected row has no submenu.

Children open beside their parent with their first row aligned to the invoking
row where space permits. They flip left at the right edge, then clamp to the
viewport when neither side fits. Parent menus stay visible underneath; each
popup has its own glyph-preserving shadow. Short popups scroll the selected row
into view at every depth.

Escape closes **all** menus at once, as do Tab and activating a leaf command.
The last selected pane becomes active again. `MenuBar::depth()` reports the
number of open child levels; `popup_rect_at(bounds, depth)` exposes each level's
geometry. `active` continues to identify the root menu-bar entry, while
`selected` identifies the deepest menu's row. Existing leaf item literals work
unchanged because `children` defaults to an empty list. An empty children list
is a leaf, not an openable submenu. To disable a branch, set `enabled: false`
on a `MenuItem` with nonempty `children`; a branch does not invoke its `action`.

### Pane focus and modified keys

`tui::focus::PaneFocus` remembers a stable, nonnegative application pane ID.
Describe the current layout as `list<Pane>` (`id` and `bounds`) and call
`reconcile(panes)` after layout changes. A selected visible pane keeps its ID
across resize/reordering; when it disappears or becomes too small for a border,
focus falls back to the first visible pane (or -1 when none remain).

Call `set_menu_active(menus.active >= 0)` before routing input and painting.
While the menu owns input, `is_active(id)` is false for every pane and
`navigate()` does nothing; the remembered selection is retained. Synchronizing
again after any menu dismissal restores that pane, including dismissal through
Escape, Tab, or command activation.

Paint focusable panels with `panel.paint_focused(surface, bounds, theme,
focus.is_active(id))`. The active border uses `theme.accent`; inactive borders
use `theme.border` and inactive titles use `theme.muted`. Route pane commands to
`focus.navigate(Key::Left/Down/Up/Right, panes)`. Navigation does not wrap; it
prefers neighbors overlapping on the perpendicular axis, then the nearest edge
and center. Ties use pane declaration order.

`tui::input::InputDecoder` handles input a byte at a time and emits `InputEvent`
values with kinds `None`, `Menu`, `Pane`, `Quit`, `Text`, or `Edit`. In the demo, Ctrl+Shift+H/J/K/L
maps to left/down/up/right pane navigation; plain `hjkl` remains menu navigation.
Pass bytes to `feed()`. If `pending()` stays true with no new input for about
50ms, call `expire()` to resolve a bare Escape and discard incomplete packets.

The shortcut needs a terminal that reports the modifiers distinctly.
`Terminal::begin()` pushes the disambiguation flag from the
[kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/), and
`finish()` pops it. The decoder accepts CSI-u/kitty sequences such as
`ESC [ 104 ; 6 u` and already-configured xterm modifyOtherKeys sequences such as
`ESC [ 27 ; 6 ; 104 ~`. Releases are ignored, while presses and repeats work.
Terminal-owned shortcuts may need rebinding to pass these combinations through.
Legacy terminals can collapse Ctrl+Shift+H/J to Backspace/Enter; those ambiguous
bytes are deliberately not treated as pane navigation. The menu still works
with legacy arrow keys and unmodified `hjkl`.

### Audio import (demo)

Choose **Add → Audio** to load a sample. The file chooser
lists `.wav`, `.aif`, and `.aiff` files case-insensitively and enforces the same
filter on manually entered paths. Directories remain browsable. The current
stdlib decoder supports mono/stereo **16-bit PCM** WAV/AIFF (not every encoding
these containers can hold); decode errors preserve the existing clip.

Loaded samples belong to a session-wide **View → Audio** list in the left pane.
The list shows sample numbers and filenames; j/k, gg, and G navigate without
changing the active pattern or its cursor. Enter inserts the selected sample
into the selected AUDIO track at the Pattern cursor row. Without an audio track
selected, importing only adds the sample to the Audio list and opens that view;
it does not create a track or put audio on a MIDI track.

Importing with an AUDIO track selected also inserts an instance at the selected
row. A track can contain multiple independent, non-overlapping instances.
Overlapping insertions are rejected, while the loaded sample remains available.
With the Pattern pane focused, Backspace over audio removes just that instance,
not its loaded sample, other instances, automation, or pattern rows. Samples can
be inserted again even if the original file is no longer available. Clear track
removes all its instances; deleting a pattern/track does not unload samples.

Each instance references decoded samples and carries its own start row.
A read-only waveform column
beside CC1/CC2 draws time downward, with green left-channel bars extending left
from the center line and red right-channel bars extending right. Mono is shown
on both sides. Waveforms scroll with the table, while ROW stays frozen.

Waveforms use eighth-cell edges, a solid mean-absolute-amplitude body, and a
shaded peak envelope. With the Pattern pane focused, press `z` on an audio track
to toggle its waveform between 13 and 25 cells wide (6 or 12 cells per channel).
Zoom is stored per track and preserved by pattern/track cloning. This expands
amplitude detail horizontally, not time: rows and notes remain aligned. Narrow
viewports clip the waveform safely; menus and text editors capture `z` normally.

**View → Sample view** selects **Normal** or **Grainy**, and its **Type** submenu
selects **Filled blocks** or **Wave (osc)**. These settings affect both the Pattern
waveform and the horizontal sample viewer. Grainy Pattern waveforms pack four
successive audio slices into each terminal row using Braille's 2×4 dot grid,
rather than rendering a single row peak with a dotted texture.
Grainy is the default for both sample waveforms and Mixer meters.

Press `s` while an audio clip is under the Pattern cursor to toggle the horizontal
**Sample** view in the lower pane. It replaces (and remembers) the Mixer/Inspector;
moving off the clip closes it. With either the Pattern or Sample pane focused,
**Ctrl+H/L** zooms time out/in by factors of two, anchored around the cursor row.
Zoom ranges from the full clip to one original sample per horizontal pixel (two
pixels per cell in Grainy mode). The display shows its time range and frames per
pixel, with a highlighted cursor column. Stereo uses green L and red R lanes;
mono uses one lane. Filled mode extends to the zero line; Wave draws the signed
sample trace/envelope, revealing individual oscillations at sufficient zoom.
These controls require modifier-aware terminal input; raw Backspace is not zoom.
Menus and editors retain exclusive keyboard ownership, including `s` and Ctrl+H/L.

Decoded samples are shared read-only by pattern/track copies, so zooming neither
reopens the source file nor changes clip timing, notes, or automation.

One row currently represents a sixteenth note at the displayed BPM (120 by
default). Longer clips extend the pattern with empty MIDI/automation cells;
Deleting instances does not discard existing rows. Instance ends are limited to
16384 rows. Pattern/track cloning preserves all instance offsets and sample data.
Audio playback, clip trimming, and tempo-change resampling
are not implemented yet; row editing does not trim the source audio.

### Patterns and Song lists

The demo's bottom status bar shows BPM (initially 120), sequence elapsed time
(`MM:SS.mmm`), time signature (4/4), PLAY/STOP, and a MIDI-input light.
**Ctrl+B** opens the tempo dialog (20–400 integer BPM; Enter saves, Escape cancels,
Tab switches between the field and OK/Cancel). CSI-u Ctrl+B and legacy Ctrl+B
are supported. The input light stays idle until a MIDI-input processor is added.

**Space** starts/stops the current pattern from the selected row and loops at
its end. A monotonic clock with fractional-row phase drives sixteenth-note rows,
the selected-row highlight, automatic scrolling, and sequence time. An open
Sample view follows within the row too, including at single-sample zoom. BPM
changes preserve row phase; menus and the BPM dialog do not pause transport.
Switching patterns or opening a content editor/file/action dialog stops it so
edits cannot accidentally target a moving row. Modal controls capture Space and
Ctrl+B before the transport handles them. Animated repainting uses the configured
meter update rate (60 FPS by default), independently of the sequencer clock.

This is a pattern transport and meter/event preview, not audio-device playback
or MIDI output. Song-order playback is not connected. Imported audio envelopes
remain on their import-time row grid; BPM changes do not yet resample/re-grid them.

The left pane defaults to **Patterns**, listing `<pattern_nr> <pattern_name>`.
The demo starts with `001 Intro`, `002 Verse`, and `003 Chorus`. Selection
immediately loads that pattern into the **Pattern** view and Mixer. Each pattern
has independent rows, tracks, automation settings, mixer state, and cursor/scroll
position; edits are retained when switching away and back.

The **Pattern** menu implements Add pattern, Rename pattern, Remove pattern,
and Clone pattern. Add creates a blank 64-row pattern with three MIDI tracks;
Clone makes an independent copy of the current pattern. Both select the new
pattern and append it to Song order. Rename uses a text dialog (Enter saves,
Escape cancels). Pattern IDs remain stable after deletion. Remove deletes the
pattern and all its Song occurrences. The demo retains at least one pattern and
limits the library to 128 patterns. Changes are in memory; there is no undo or
session persistence yet.

View → Show patterns / Show song switches the left pane. Song lists
`<order_position> <pattern_nr> <pattern_name>` in playback order, initially
Intro → Verse → Verse → Chorus. Repeated occurrences reference the same pattern;
renaming or editing it updates all occurrences. In Song view, `dd` removes only
the selected occurrence, not the pattern. An empty Song is allowed and keeps
the current Pattern view available. Song is an order-list preview, not playback.

With Song focused, Ctrl+J/K moves the selected occurrence down/up and selection
follows it. `o` inserts a duplicate above, `O` below, selecting the inserted row.
Shift+J/K changes only that occurrence to the previous/next available pattern,
stopping at either end of the pattern list. Duplicates reference the same pattern;
these shortcuts do nothing in an empty Song. Ctrl+J/K requires modifier-aware
terminal reporting (CSI-u or modifyOtherKeys); legacy LF remains Enter.

Both views use `tui::listview::ListView`, with selected-row highlighting and the
table's darker alternating rows. With the left pane active, j/k or arrows select,
`G` jumps to the last item, `gg` jumps to the first, and `dd` requests deletion.
Enter activates the selection. Menus, dialogs, editors, and other panes capture
their own input; leaving the list cancels pending multi-key commands.

The reusable list borrows `ListItem { id, label }` values. `on_event(event, area)`
returns 0 for unhandled input, 1 for navigation, 2 for activation, or 3 for a
delete request; it does not delete application data itself. `reconcile(area)`
clamps selection and scrolling after data changes, and `paint` also reconciles.
Call `cancel_pending()` when suspending list input. Empty lists select `-1`.
The demo model is `modules/tui_demo/patterns.mla` and owns its labels/snapshots;
call its `release()` once and do not shallow-copy the owning library.

### Table widget

MIDI tracks start collapsed to NOTE/VEL for every note line. In the Pattern view,
The demo opens the default MIDI input on a dedicated worker thread at startup.
`tui_demo::midi_controller` decodes note-on/off (including velocity-zero note-on)
and running status, then posts fixed-size `MidiInputEvent` values through a
1024-entry `std::sync::SpscQueue`. The main loop drains at most 256 events per
iteration, independently of modal keyboard focus. `MidiController.dispatch`
uses `match` to call main-thread note handlers; the current handlers retain the
last note/channel/velocity and flash MIDI IN for 150 ms. This does not record
notes into the pattern or send MIDI output. No input device is a nonfatal state;
connect the device before starting the demo (no hot-plug reopening yet).

The worker-to-window queue never waits when full: dropped events are counted
atomically, and the consumer clears its last-note display state on loss. Native
input queue drops are counted too. MIDI polling/packet allocation happens on
the worker, not on a real-time audio callback. Shutdown signals and joins the
worker before releasing shared storage. Hardware-independent decoder, threaded
handoff, overflow, and indicator tests live in `tests/tui_midi_tests.mla`.

`z` cycles the selected MIDI track through three column stages:

1. NOTE / VEL (default)
2. NOTE / VEL / LEN / OFF
3. All columns, including CC1 / CC2

The next press returns to stage 1. Shift+Z advances every MIDI track's stage
independently. Track → Collapse all selects stage 1; Expand all selects stage 3.
Hidden columns consume no width and are skipped by h/l navigation, but their
values and playback remain unchanged. Collapsing keeps selection in the same
track, returning a hidden LEN/OFF selection to its note line's NOTE column.
Track/pattern copies preserve their stages. Audio retains its existing `z`
waveform-width toggle; MIDI column stages do not change audio tracks.

The reusable `TableColumn.hidden` property controls visibility independently
of `read_only`: hidden cells can still be validated and updated by the model.
Pattern tables also enable `Table.alternate_group_text`: tracks 2, 4, etc. use
slightly dimmer cell text and column headers. The selected column always uses
the normal text/header color, independently of track parity and row shading.

LEN uses decimal sixteenth-note units: `1.00` = one row, `0.50` = half a row,
`3.75` = three and three-quarter rows. Enter edits LEN; Shift+J/K adjusts it by
0.01. MIDI accepts 0.01–16384.00, with new notes defaulting to 1.00. A muted-color
duration rail beside NOTE follows sustained notes using four vertical Braille dots
per terminal cell for partial endings. Its background matches NOTE, including
selection and alternating-row shading. Empty NOTE cells do not cut off a sustain.

Each MIDI note line is NOTE / VEL / LEN / OFF. OFF uses the same scale as LEN,
but is signed: `-0.50` starts half a sixteenth before its row, `+0.50` starts
half a sixteenth after it. New notes default to OFF `0.00`; empty OFF also means
zero. Enter edits it, Shift+J/K adjusts by 0.01, and the accepted range is
−16384.00 through +16384.00. Note-off is always **row time + OFF + LEN**.
Duration rails and MIDI meters follow the shifted start, including notes from
later rows starting early. Partial starts, ends and disjoint fragments use thin
Braille glyphs without a separate background strip. Note-line and pattern duplication preserve OFF.

The scheduler looks ahead across all rows and maintains chronological event
order even when offsets reorder notes. Pattern loops repeat shifted onsets;
starting/seeking skips onsets already in the past (no automatic preroll or
retroactive note-ons). Thus a negative OFF on row 1 first plays just before the
next loop. A live edit refreshes future onsets without altering already-issued
voices' end times. OFF is MIDI-only; audio retains its LEN column.

Audio LEN defaults to the sample duration. Edit it at the instance's starting
row to shorten its gate; it cannot exceed the sample, pattern end or next
instance. Its fractional tail is visible in the waveform and its mixer gate
ends at LEN, leaving the loaded sample unchanged. Clear audio LEN to restore
the available sample duration.

`tui_demo::note_playback::NotePlayback` produces timestamped note-on/off events
using the transport's continuous musical ticks (15000 per sixteenth). Note-offs
are scheduled at LEN, independent of row/frame boundaries and visual meter
decay. Equal-pitch overlaps on a track hold until the last voice ends; stopping
flushes active notes. This is demo-side scheduling, not a real-time/lock-free
MIDI backend: the demo still has no connected MIDI-device output. Consumers must
drain `events` after each update; timestamps preserve timing across late frames.

Pattern → Set length opens a row-count dialog prefilled with the current length
(1–16,384 rows). Growth adds empty rows. Shrinking past populated cells or audio
asks “Are you sure, data will be truncated”; Cancel/Escape leaves data intact.
Confirmed truncation trims or removes audio instances without changing loaded
samples. Extending again does not restore discarded data. Playback stops when
opening the length dialog.

`tui::table::Table` renders the demo's 64-row sequence with a frozen ROW column
and three independent tracks, each containing compact NOTE, VEL, LEN, OFF, CC1, and CC2
columns. Focus the sequence pane with Ctrl+Shift+L; `l`/`h`
select the next/previous column and `j`/`k` (or Down/Up) select rows. Navigation
stops at the edges and scrolls the selection into view, with a fixed header.
`G` jumps to the bottom row; consecutive `gg` jumps to the first row. These
bindings operate only in the active sequence pane, not in menus or cell editors.
Any unrelated key or focus change cancels a pending first `g`.
Menus and dialogs retain exclusive keyboard ownership while open.

Construct with `Table::new(columns, rows)`, using `TableColumn { title, width }`
and `TableRow { cells }`. Widths are terminal cells (clamped to 1–4096), and
strings are borrowed. Missing cells are blank; extra cells are ignored.
Paint inside a panel's returned content rectangle. Route input to
`on_event(event, content_rect)` only when its pane is active; it returns whether
it handled navigation. `selected_row` and `selected_column` are zero-based
(`-1` for empty data). Call `reconcile` after changing data or viewport size;
painting also reconciles the selection and scroll offsets.

Both scrollbars are enabled by default and reserve one cell each, even when
all data fits. Set `horizontal_scrollbar` or `vertical_scrollbar` to `false`
independently to hide them and reclaim that space. Scrolling still works:

```mlang
table.vertical_scrollbar = false; // e.g. while the application is playing
table.selected_row = playing_row;
table.reconcile(content_rect); // keep the playback row visible
```

Playback itself remains the application's responsibility. Scrollbars display
the visible range and position; navigation is keyboard-driven, without mouse
dragging. The selected row uses the theme selection color, the selected column
is slightly lighter (including their intersection), and alternating data rows
are darker. Striping follows absolute row numbers while scrolling.

#### Track groups and actions

The base table accepts `groups: list<TableGroup>`, where each group specifies
`title`, `first` column index, and `count`. This adds an upper header row; only
the group containing the selected child column gets the selection highlight.
Set `frozen_columns: 1` to pin ROW during horizontal navigation. The scrollbar
represents only the unfrozen columns. Selecting another track scrolls it into
view; if the whole group fits, it is kept together. Wider groups scroll by child
column. Column widths are NOTE=6, VEL=5, LEN=6, OFF=6, CC1=6, CC2=6, including spacing.
VEL is left-aligned with two trailing spaces after a three-digit value.

The demo's **Track** menu always targets the selected column's track:

- Rename opens a text dialog (Enter saves, Escape cancels).
- Create track → MIDI track / AUDIO track appends an empty track of the chosen
  type and selects it. MIDI has NOTE, VEL, LEN, OFF, CC1, CC2; AUDIO has CC1, CC2, LEN.
  Mixed groups have different widths; navigation, frozen ROW, deletion, and
  duplication follow their actual column ranges. Duplicate preserves track type.
- Duplicate appends an independent copy of the pattern, mute flag, and automation
  settings, then selects the copy.
- Delete removes the track after confirmation; at least one track is retained.
- Clear pattern clears only that track's cells after confirmation.
- Mute / unmute toggles the track flag and its `[M]` header indicator.
- Configure CC1 / CC2 assigns the corresponding per-track automation slot.

Each automation slot accepts `cc:N` for MIDI CC 0–127 (values 0–127),
`pitchbend` (signed values -8192–8191), or `name:min:max` for a custom integer
parameter, e.g. `cutoff:0:1000`. Custom names use letters, digits, and underscores
and start with a letter. Limits must fit i32. Existing slot values must fit new
limits or configuration is rejected without changing data. The Inspector shows
the selected track's mute state and both parameter assignments.

New tracks start with CC1=`cc:1` and CC2=`cc:74`. There is a 64-track demo limit.
These actions change in-memory demo data, not audio/MIDI output; mute is state
for a future playback engine, and custom parameters need an application mapping.

#### Mixer preview

Press `m` outside menus, dialogs, or cell editing to toggle Inspector/Mixer.
`modules/tui_demo/mixer.mla` provides six-cell-wide track strips (five content
cells plus a separator). Headers use `M1`, `A2`, etc. for MIDI/audio and their
current track order; full names remain in the sequence and Inspector. Compact
readouts show `V100` for volume (0–100) and `P0` for pan (-100 left to +100 right).
MIDI uses one pre-pan meter; AUDIO uses two meters, left then right.
The selected track shares the sequence selection and has a lighter background.
When tracks exceed the pane width, the visible strip range follows selection.
New tracks, duplicates, names, and mute state are read from the sequence model.
Volume and pan are model readouts in this first preview, not editable controls.

The meter takes all remaining height below its three readout rows, shrinking or
growing on terminal resize. Tiny panes clip readouts and omit meters when no
height remains. `tui::meter::VuMeter` is reusable independently: pass a level
from 0 to 1000 and a target rectangle. Its quarter-cell blocks and
green/yellow/orange/red thresholds match the oscilloscope demo: green below
520, yellow from 520, orange from 700, and red from 850 on the 0–1000 scale.
View → Meter selects Solid bars or Grainy (osc, default). Both retain
the same fill calculation and colors. Grainy uses `std::esc::acs_meter` directly,
including its braille glyphs. Custom `VuMeter` instances set `grainy: true`.

**View → Meter → Set update rate** opens an FPS dialog. Enter a whole number
from 1 to 240; `60` requests 60 FPS. Enter/OK applies it immediately, and
Escape/Cancel leaves it unchanged. Fractional deadlines avoid rounding 60 FPS
to a 20 ms / 50 FPS polling cadence. Missed frames are skipped rather than
replayed; actual throughput depends on terminal/rendering speed. Input remains
responsive at low rates, and transport timing, smoothing time constants, and
button animations do not change with FPS. The setting is session-local.

The **Mixer** has no synthetic levels. A MIDI note supplies its velocity / 127
as a meter impulse whose target falls to zero before the next row; empty notes and zero
velocity are silent. Audio tracks use the current instance-relative row's
left/right peak amplitudes. Levels use a fast attack (8 ms time constant) and
release (25 ms), bounded toward the next target without overshoot. They continue
updating when the Mixer is hidden, and decay to silence on stop. Track volume,
pan, and mute still apply; V100 is the editable track gain, not a fabricated VU
source. These are sequencer-derived levels, not measurements from an audio device.

#### Typed columns and editing

MIDI tracks support 1–16 NOTE/VEL/LEN/OFF groups followed by their shared CC1/CC2 columns.
**Track → Add note line** inserts a blank group after the selected group (or appends
it before CC1 when an automation column is selected). **Duplicate note line**
copies the selected NOTE/VEL/LEN/OFF group for every row and selects the copy. **Remove
note line** removes that group, keeping at least one. Selecting NOTE, VEL, LEN or
OFF identifies the group; Remove/Duplicate require such a selection. These actions
do not apply to audio tracks, and no new shortcuts are assigned yet.

All note lines retain the usual note editing, typed velocity limits, empty values,
column navigation, and horizontal scrolling. Pattern/track clones preserve the
note-line count and contents. Each MIDI track still has one Mixer strip: the highest
velocity among its non-empty notes feeds that strip's smoothed meter; velocities
are not summed, and a velocity without a note produces no meter activity.

`TableColumn` owns the reusable validation rules:

```mlang
TableColumn { title: "ROW", width: 7, value_type: "u32", read_only: true }
TableColumn { title: "VEL", width: 7, value_type: "i32",
              bounded: true, minimum: 0, maximum: 127 }
TableColumn { title: "NAME", width: 16, value_type: "str8", pattern: "[A-Za-z ]+" }
```

`value_type` defaults to `str8`. Supported types are `i8`, `u8`, `i16`, `u16`,
`i32`, `u32`, `i64`, `bool`, and `str8`; unknown/unsupported types fail validation.
Integer syntax is checked with `std::regex`, then converted with overflow checks
and checked against both the type's range and optional inclusive column limits.
Unsigned types reject negative signs; booleans require `true` or `false`.
An optional POSIX `pattern` must match the entire value in addition to its type
rules; invalid regexes reject input. `bounded` applies only to integer columns.
Cells remain textual display values, not a dynamically typed language object.

`read_only` columns are skipped by `l/h` and selection reconciliation; an
all-read-only table has `selected_column == -1`. `can_set(row, column, value)`
and `set_cell(row, column, value)` reject invalid values, missing cells, and
read-only writes. The base still borrows strings: callers keep accepted values
alive and manage replaced strings. Directly replacing the public `rows` data
bypasses write validation; use `set_cell` for interactive edits.

The demo uses `SequenceTable : Table` in `modules/tui_demo/sequence.mla`, outside
the reusable widget library. It owns its cell strings and a compiler-provided
`std::array` (`array<str8, 128>`) containing every MIDI note. Notes use tracker
spelling: MIDI 0 is `C--1`, MIDI 60 is `C-4`, and MIDI 127 is `G-9`; sharps use
`#`, e.g. `C#4`. Manual notes must occur in this array as well as match the
column regex. ROW is read-only, NOTE starts selected, VEL is bounded to 0–127,
and each track has two nullable i32 automation columns with independent limits.

- Shift+J decreases and Shift+K increases the selected value. Notes step by a
  semitone; integer columns step by one. Endpoints stop without wrapping.
- Enter starts an inline `TextField`; Enter validates and commits, while Escape
  discards the draft. Invalid input stays editable with an Inspector error.
- While editing, text including q/h/j/k/l and spaces is literal input. Ctrl+U
  clears the draft. Pane/menu navigation is suspended until commit or cancel.
- Columns can opt into `nullable: true`: an empty string represents no value
  and bypasses the value regex/range checks, but unsupported types still fail.
  NOTE, VEL, CC1, and CC2 enable this in the demo. An empty NOTE means a rest.
  Enter, Ctrl+U, Enter clears a single cell.
- Shift+Backspace clears all editable cells in the selected row, preserving its
  read-only ROW number. `dd` deletes the row, shifts following rows upward, and
  renumbers ROW. Selection and scrolling are clamped, including an empty table.
  The two d keys must be consecutive; another key, menu, or pane switch cancels
  the pending command. Inside the cell editor, `dd` is ordinary text.
- Adjusting an empty note with Shift+J/K initializes it to C-4; an empty integer
  starts at its column minimum (or zero without limits).

Shift+Backspace requires a terminal that reports the modifier (CSI-u or
modifyOtherKeys). If it sends the same byte as plain Backspace, the shortcut
cannot be distinguished; plain Backspace never clears a whole row.

The demo edits display data only; it does not send changes to an audio engine.

### Open session dialog

File → Open session in the demo opens `tui::dialog::OpenSessionDialog`. Its
title is drawn into the upper box border. The first inside row is the editable
path; beneath it a horizontal layout holds the directory tree and the selected
directory's files. The dialog paints last, with a shadow above the main view.

- Ctrl+Shift+H/J/K/L moves between the path, directory tree, file list, and button
  row using the same spatial rules as the main view. Tab cycles these controls.
  Each control has its own highlight; the main view is inactive while modal.
- In the tree, arrows or j/k select directories and immediately refresh the
  files on the right. Right/l or Enter expands a directory lazily; Left/h
  collapses it or selects its parent. The `..` entry navigates above the current
  tree root. Expansion is limited to 32 levels.
- In the file list, arrows or j/k select a file. Enter accepts its path and
  closes the dialog.
- In the path field, type an absolute path or a path relative to the currently
  selected directory. Enter visits a directory or accepts an existing file.
  Backspace, Delete, Left/Right, Home/End, Ctrl+A/E, and Ctrl+U (clear) edit the
  text. Spaces and UTF-8 input are preserved. Letters such as q/h/j/k/l are text
  here, even though they have shortcut meanings elsewhere.
- Escape cancels the dialog and restores the main view's selected pane. Ctrl+C
  also cancels while the dialog is open. Invalid paths leave the dialog open
  with an error, and resizing keeps it inside the viewport.

Construct with `OpenSessionDialog::new()`, call `open(initial_directory)` (empty
means process CWD), route events to `on_event(event, viewport)` while `active`,
and call `paint(surface, viewport, theme)` after all other UI. Suspend main focus
while either a menu or the dialog is active. `accepted` and `accepted_path`
provide the result after completion; the path belongs to the dialog until its
next `open()` or `release()`. Call `release()` exactly once when finished and
avoid copying this owning widget. No process-directory changes or filesystem
writes occur. The chooser does not deserialize sessions; the demo reports the
selected path for an application handler to load.

### Base dialogs and push buttons

`OpenSessionDialog` and `QuestionDialog` both derive from `Dialog`. Construct a
base with `Dialog::new(title, width, height)`: its default buttons are OK and
Cancel, centered in the bottom four-cell-high row (including button shadow).
`content_rect(viewport)` excludes this row, so nested content cannot overlap it.
`paint_frame` paints the border, shadow, and buttons and returns that content area.

Buttons are independent `tui::button::PushButton` widgets. Replace the inherited
`buttons` list to choose any number of labels, action IDs, and enabled states:

```mlang
var question: QuestionDialog = QuestionDialog::new("Confirm", "Start a new session?");
question.buttons = [PushButton { label: "Proceed", action: 1 },
                    PushButton { label: "Not now", action: 2 }];
question.button_alignment = Alignment::Center;
question.open();
```

Import `tui::dialog::*`, `tui::button::*`, and `tui::layout::*` for this example.
Labels and question text are borrowed and must remain valid while displayed.
There is no fixed button-count limit; an overflowing row scrolls to keep the
selected button visible. Disabled buttons are skipped. Left/h and Right/l
choose buttons, Enter or Space presses one; Tab also cycles question buttons.
The face shifts down and right, changing its color, before the action completes.

Route events only to the active modal (`QuestionDialog::on_event(event)` or
`OpenSessionDialog::on_event(event, viewport)`). Call its `tick()` every 10 ms
and repaint after input and while a press is pending; activation completes after
10 ticks. `result` is the selected action: 0 means no result, 1 is OK, and 2 is
Cancel/Escape. File-dialog OK validates the path/selected file and keeps the
dialog open on failure. Custom nonzero actions close it without accepting a file.
Change button lists between activations, not during a pending press.

File → New session demonstrates the non-file question, reporting OK or Cancel
without changing session data. Both modals restore the main pane on dismissal.
Custom derived dialogs can reuse `reset_buttons`, `on_button_event`, and
`tick_buttons`; pane ID 3 identifies the shared button row for highlighting.

The reusable `tui::textfield::TextField` can also be used separately.
`InputEvent` preserves `code`, `modifiers`, and `raw_byte`, and
adds `Text` and `Edit` kinds. Route text-field input before global shortcuts and
retain control-code events even when their kind is `None`. Text input storage
is bounded to roughly 4096 bytes. Pane minimum dimensions default to 2×2;
single-line fields opt in with `min_height: 1`.

Directory names/files are sorted by `std::fs::list_dir`, including hidden
entries. That API reports an empty list for both an empty and an unreadable
directory; the dialog explicitly labels this ambiguous state. There is no
extension filtering, shell expansion (`~`/environment variables), or filesystem
watcher in this version.

Repaint the underlying content before compositing menus each frame. This removes
old shadows when a menu closes or moves; repeatedly shading an existing frame
would otherwise darken it again. `Surface::cell` lets tests and custom widgets
inspect the final glyph and RGB colors independently of terminal escape output.

Rectangles use zero-based cell coordinates and exclusive right/bottom bounds.
Fixed layout sizes take priority over flexible sizes; if space is insufficient,
fixed sizes are truncated in declaration order. Gaps and all children stay within
the input extent. Use `Size::fixed` or `Size::flex` to construct constraints.

Surface copies share storage. Call `release()` exactly once after painting and
presentation, and do not use aliases afterward. `encode()` returns an owned
string; release it using `std::strbuf::free`. `Terminal::present` handles this for
you. Pair `Terminal::begin` with `finish` on every normal exit path. Applications
need their own abnormal-exit/signal policy. The demo handles `q`, Ctrl-C, and EOF,
redraws after resize, and exits after one plain snapshot outside a suitable TTY.

The initial text repertoire is single-cell printable ASCII, Latin characters,
and Unicode box drawing. Unsupported wide/combining characters and controls are
replaced with `?`; this is not a full Unicode grapheme/width engine. Rendering uses
truecolor escape sequences, so RGB-capable terminals give the intended theme and
shadows. There is no mouse support, focus traversal inside a pane, or
differential repainting yet.

From the repository root:

```sh
build/mlang examples/tui_demo.mla -L build -lmlang_std -o /tmp/mlang_tui_demo
/tmp/mlang_tui_demo
build/mlang --tests tests/tui_tests.mla -L build -lmlang_std
python3 tests/tui_terminal_smoke.py /tmp/mlang_tui_demo
```

The demo is an interaction/layout showcase. Open session browses real files and
returns a path; other commands report selection rather than editing sessions.
