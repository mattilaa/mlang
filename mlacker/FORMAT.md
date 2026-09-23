# MLACK session format 1.0

Extension: `.mlack`. A self-contained binary document for the editor's committed
state. No compression, executable code, or live pointers are stored. Plugin paths
refer to external native binaries and must be treated as executable dependencies.

## Primitives

All integers, including counts, IDs, enums and booleans, are **signed little-endian
64-bit** values. Booleans are exactly 0 or 1. Reals are IEEE-754 little-endian f64.
Strings are an i64 UTF-8 byte count followed by those bytes (no trailing NUL).
Lists are a count followed by that many entries, unless a size is implicit below.
The implementation uses the `std::serde` primitive encoding. No trailing bytes
are accepted. Unsupported major or minor versions fail closed.

## Document order

1. String `MLACK`, integer major `1`, integer minor `0`.
2. View record, in order:
   - BPM, beats per bar, beat unit, transport row, fractional phase (0–14999).
   - Focused pane; sidebar mode (0 Patterns, 1 Song, 2 Audio, 3 Instruments).
   - Pattern/song list selection and scroll; audio selection and scroll;
     instrument selection and scroll.
   - Mixer visible, meter grainy, meter FPS.
   - Sample view visible, grainy, wave style, zoom.
   - Instrument editor visible, slot, selected parameter, horizontal scroll.
3. Sample list. Each sample:
   - Source/display path, channel count, sample rate, frame count, frames per row.
   - `frame_count * channel_count` interleaved PCM values, encoded as f64 and
     restored to f32. PCM is embedded, not reloaded from the source path.
   - Row-peak list: left peak, right peak, left mean, right mean (0–1000).
   - Detail-peak list: left peak, right peak, four entries per row.
4. Plugin list, ascending unique slot order. Each plugin:
   - Slot (0 master, 1–32 instruments), plugin bundle path.
   - Parameter list: stable unsigned-32-bit ID represented as i64, normalized f64
     value (0–1). Read-only values are captured but not forced into the processor.
5. Active pattern ID and next pattern ID.
6. Pattern list. Each pattern:
   - Stable ID and name.
   - Next track ID; selected row/column; horizontal/vertical scroll;
     horizontal/vertical scrollbar flags; alternate-track text flag.
   - Track list. Each track: name, mute, volume, pan, audio flag, instrument flag,
     assigned instrument slot, note-line count, waveform zoom, zoom stage;
     CC1 name/min/max; CC2 name/min/max; audio-instance list.
   - Each audio instance: zero-based sample-list index, starting row, length ticks
     (0 means natural sample length). 15000 ticks represent one sixteenth note.
   - Row list. Each row is a string list containing all cells, including hidden
     columns and ROW. Column definitions are rebuilt from track metadata; cells
     must match the resulting column count and writable-column validation rules.
7. Song order: list of stable pattern IDs, allowing repeated occurrences.
   This is lane 1 of the song matrix, so 0 marks an empty row. Files written
   before the matrix never contain 0.
8. Optional MIDI-learn extension: string `MIDI_LEARN`, followed by a list of
   `(channel * 128 + CC, instrument slot, stable parameter ID)` integer triples.
   At most 2048 entries, strictly increasing keys 0–2047, slots 1–32. Every slot
   and parameter ID must exist in the saved plugin list. On restoration the ID
   is resolved to the current parameter index; read-only targets are rejected.
   Files without mappings or aux effects omit this extension, preserving the original 1.0
   layout. This reader accepts both layouts; older readers reject extended files.
   Armed listening is transient and is never saved. Unknown/trailing data is rejected.
9. Optional aux extension, following `MIDI_LEARN` (which may have zero mappings):
   string `AUX_EFFECTS`, then effect count (1–8). Each effect stores its plugin
   path (empty for an unloaded channel), return volume (0–100), and parameter
   list of `(stable parameter ID i64, normalized f64)` pairs. Empty paths require
   zero parameters; IDs must be unique within each effect.
   For each pattern in document order, a track count matching that pattern is
   followed by exactly `effect_count` send levels (0–100) for each track.
   Finally, effect-bank focus (boolean) and selected effect index (0-based) are
   stored. Effects use runtime parameter slots 33–40, separate from the plugin
   list in step 4; the editor view may reference these slots. Older readers
   reject this extension. Files without effects retain the preceding layouts,
   unless track inserts follow (in which case effect count may be zero).
10. Optional `TRACK_INSERTS` extension follows `AUX_EFFECTS`. An instance count
    (0–256) precedes each instance's nonempty path and parameter list of
    `(stable parameter ID i64, normalized f64)` pairs. IDs must be unique within
    an instance. Instance IDs are one-based; parameter slots are 41–296.
    For every pattern, its matching track count precedes four instance IDs per
    track (zero means empty). Finally: insert view visible boolean and selected
    slot index (-1–3). Pattern copies can reference the same instance. Detached
    instances remain available until session close. This extension also requires
    the preceding `MIDI_LEARN` tag, with zero mappings if necessary. When the
    insert view is hidden the selected slot is stored as -1.
11. Optional `SAMPLER_PADS` extension follows `TRACK_INSERTS`: a count (0–4096)
    of `(instrument slot 1–32, pad 0–127, zero-based sample-list index)` integer
    triples in strictly increasing `slot * 128 + pad` order. Every slot must be an
    instrument in the plugin list, and every index must be in the sample list.
    On restoration, after plugins and MIDI learn, each pad's embedded PCM is sent
    to its instrument (see `stdlib/include/mla_sampler_protocol.h`; used by
    Mla Drum). If an instrument refuses a pad, the open is rejected. This
    extension requires the preceding tags, with empty sections if necessary.

12. Optional `SONG_MATRIX` extension follows `SAMPLER_PADS`: a lane count
    (0–15) and, per lane, a row count matching the song length followed by one
    pattern ID per row, where 0 is an empty cell. These are the parallel lanes
    beside the song list of step 7, which is lane 1. Every non-zero ID must
    exist in the pattern list. This extension also requires the preceding tags,
    with empty sections if necessary.

13. Optional `TRACK_OUTPUTS` extension follows `SONG_MATRIX`: per pattern, a
    track count matching that pattern followed by one output channel per track.
    0 is the master bus; any other value is the destination track index + 1
    within the same pattern, and a track may not name itself. It is written only
    when some track leaves master, and it also requires the preceding tags, with
    empty sections if necessary. A route to a track that is no longer an AUDIO
    track is loaded as written and plays to master.

14. Optional `MATRIX_GRID` extension follows `TRACK_OUTPUTS`: the number of
    pattern rows one matrix row holds (1-16384). It is written only when a song
    changed it from the default 64. A session without the tag takes the length
    most of its patterns have (the shortest of those on a tie), so a song of
    16-row patterns plays them one per matrix row. Like the others, it requires the preceding
    tags, with empty sections if necessary.

The active pattern is serialized from the live editor, not its older library
snapshot. Audio placements reference the embedded sample list; plugin assignments
reference stable slots, including holes left by removed instruments.

## Validation and restoration

Maximum file size is 256 MiB; strings are limited to 4096 bytes; at most 256
samples, 33 plugins (including master) plus 8 aux effects and 256 inserts,
16384 parameters per plugin, 1024 patterns,
64 tracks per pattern, 16384 rows per pattern, 8 million cells per document, and
65536 song entries and 15 parallel song lanes are accepted. Each sample has at most 16777216 frames and one
or two channels. Invalid counts, non-finite/out-of-range numeric data, malformed
cell values, duplicate pattern IDs, and dangling song/instrument/sample references
are rejected. Application editing limits may be stricter than archive limits.

Loading first builds a separate document and stopped audio controller. Plugins are
loaded and parameter layouts checked before the running session is replaced.
Parameters are restored on the owning thread with audio stopped, outside the
real-time callback. Values already present in the new instance are not replayed:
some plugins expose command-like MIDI parameters whose default values must not
be resent as commands. Hardware settings are kept from the current application.
Meters/MIDI activity are transient and start silent; playback never auto-starts.

Version 1.0 does not capture opaque VST3 component/controller state blobs or
plugin-internal sample libraries. It preserves the exposed parameter state edited
by mlacker. Sampler pads filled by mlacker are the exception: they are rebuilt
from the embedded sample list (`SAMPLER_PADS`). Future incompatible additions require a new version.

## Portable MIDI learn files (`.mlalearn`, version 1.0)

Uses the same little-endian integer and length-prefixed UTF-8 string primitives:
string `MLALEARN`, major 1, minor 0, plugin display-name string, mapping count,
then `(channel * 128 + CC, stable parameter ID)` integer pairs. Keys are strictly
increasing, 0–2047; IDs are unsigned 32-bit values stored as i64. Maximum size is
64 KiB, maximum count 2048, maximum string length 4096 bytes. Trailing data is
invalid. No session slot, parameter values, or armed-listening state is stored.

Import requires the same plugin display name and writable parameter IDs, and
replaces mappings belonging to the selected loaded instrument. Keys currently
owned by another instrument reject the import. The entire file is validated
before changing mappings. Empty mapping lists clear that instrument's bindings.
Exports use atomic file replacement. Ordinary `.mlack` persistence is unchanged.

## Parameter preset files (`.mlapre`, version 1.0 and 1.1)

String `MLAPRE`, i64 major 1 and minor 0 or 1, plugin display-name string, i64 parameter
count, then `(i64 stable parameter ID, f64 normalized value)` pairs. Uses the same
little-endian primitives. Maximum 256 MiB, 16384 parameters, and 4096-byte strings.

Minor 1 is a **kit preset**, written for sampler instruments (such as Mla Drum)
that have pads loaded from the session. After the parameters: string
`SAMPLER_PADS`, a count (0–64), then per pad its number (0–127, strictly
increasing) and the sample in the session's sample-list encoding (path, format,
PCM, row and detail peaks). Loading a kit replaces every pad of the instrument:
listed pads receive their samples, and all other pads are cleared. Kit samples
join the Audio list, reusing an identical existing sample. Minor 0 presets leave
pads unchanged. Readers that only know 1.0 reject 1.1 files.
The plugin name and complete parameter-ID set must match; duplicate/missing IDs,
non-finite values, values outside 0–1, and trailing/truncated data are rejected
before applying. Read-only parameters are recorded but not written on restore.
Unchanged values are not resent, preserving the session restoration safeguards.
MIDI bindings and opaque VST3 component/controller state are not part of a preset.
