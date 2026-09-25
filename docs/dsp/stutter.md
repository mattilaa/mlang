# dsp::stutter

Module file: `modules/dsp/stutter.mla`

`StereoStutter` is a tempo-locked beat repeater. It divides the beat grid into
cells `width` beats long (a 1/16 note is `0.25`). When the stutter engages on
a cell boundary, that first cell plays the live input while recording it, and
every following cell replays the recording from its start. Repeats therefore
always land on the grid, whatever the block size. Storage is allocated only by
`new()`; processing and parameter changes do not allocate or lock.

```mla
mod dsp::stutter;
use dsp::stutter::StereoStutter;
use dsp::stutter::StutterFrame;
use dsp::stutter::StutterMode;
use dsp::stutter::StutterOrigin;
use dsp::stutter::StutterStyle;
use dsp::stutter::StutterVariation;

var stutter: StereoStutter = StereoStutter::new(48000.0f, 12.5f); // 12.5 s capture
stutter.set_tempo_bpm(128.0);
stutter.set_width_beats(0.25);    // 1/16 note slices
stutter.set_mode(StutterMode::Auto);
stutter.set_auto(4.0, 1.0);       // repeat the last beat of every 4 beats
stutter.set_style(StutterStyle::Repeat);
stutter.set_origin(StutterOrigin::Beat);        // slices start on the beat
stutter.set_variation(StutterVariation::Roll);  // speeding-up repeats
stutter.set_gate(0.75);           // silence the last quarter of each slice
stutter.set_fade_ms(3.0);         // declick slice edges, engage and release
stutter.set_mix(1.0f);

// Per block, when a host transport is running:
stutter.sync_beat(host_beat);

let output: StutterFrame = stutter.process_stereo(input_left, input_right);
```

## Modes

- `Off` passes the input through. Switching to `Off` releases a running
  repeat at once, fading back to the input.
- `On` waits for the next grid line, then captures and repeats until switched
  off.
- `Auto` engages on each cell that starts within the last `hold` beats of every
  `period` beats (`set_auto(period, hold)`), and releases at the first cell
  after it. `set_auto(4.0, 1.0)` stutters the last beat of each 4/4 bar.

## Style, origin and variation

`StutterStyle::Repeat` replays the recorded slice. `StutterStyle::Cut`
repeats nothing: the live input keeps playing and the gate, splits and rests
chop it on the grid. A full gate would cut nothing, so `Cut` treats a gate of
1 as 1/2.

`StutterOrigin` picks where a repeated slice starts. `Grid` records it from
the grid line where the stutter engages. `Beat` and `Bar` (4 beats) replay
from the start of the beat or bar holding that line, read from a rolling
input history. The history is as long as the slice buffer (`max_seconds`); an
origin further back than it holds, less the slice, starts as far back as it
can.

`StutterVariation` shapes each cell after the first:

- `Straight`: every slice alike.
- `Roll`: 2, 4, 8, then 16 splits, doubling every two cells.
- `Reverse`: every other repeat backwards.
- `PitchDown` / `PitchUp`: one semitone per repeat, up to two octaves, read
  with linear interpolation.
- `Skip`: only cells 1, 4 and 7 of every 8 sound (3-3-2).
- `Random`: each repeat is normal, split in 2 or 4, reversed, an octave down,
  or a rest.

In `Cut` style, reverse and pitch do not apply; `Random` picks splits and rests.

## Grid and tempo

The stutter keeps its own beat position and advances it by
`tempo / (60 × rate)` per sample. `sync_beat(beat)` follows an external
position, such as a VST3 host's `projectTimeMusic`. Positions within two
samples of the running one are ignored, so reporting each block's start does
not jitter the grid. Entering a cell part way (on the first block, or after a
jump) never starts a capture off the grid: an idle stutter waits for the next
line, and a running repeat continues in phase with the grid.

Changing `set_width_beats` while repeating keeps the recording and replays its
start at the new cell length, so narrowing the width speeds the stutter up.
The recording holds up to `max_seconds`; a longer cell is silent after that.

## Shaping

- `set_gate(0.05..1)`: the audible share of each cell, or of each split; the
  rest is silent.
- `set_fade_ms(0..50)`: fade-in on each repeat or split and fade-out before
  the gate, the end of the recording, and on release. The first slice
  continues the dry signal without a fade-in. `0` gives sample-exact repeats.
- `set_mix(0..1)`: the stutter's share of the output while it is engaged.
  Outside a stutter the input passes unchanged at any mix.

`is_active()` reports a stutter in progress, and `reset()` forgets the
recording, the history and the grid position while keeping the storage.

See [Mla Stutter](../../plugins/mla_stutter/README.md) for the VST3 effect built
on this module.
