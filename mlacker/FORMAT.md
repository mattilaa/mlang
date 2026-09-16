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
   reject this extension. Files without effects retain the preceding layouts.

The active pattern is serialized from the live editor, not its older library
snapshot. Audio placements reference the embedded sample list; plugin assignments
reference stable slots, including holes left by removed instruments.

## Validation and restoration

Maximum file size is 256 MiB; strings are limited to 4096 bytes; at most 256
samples, 33 plugins (including master) plus 8 aux effects, 16384 parameters per plugin, 1024 patterns,
64 tracks per pattern, 16384 rows per pattern, 8 million cells per document, and
65536 song entries are accepted. Each sample has at most 16777216 frames and one
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
by mlacker. Future incompatible additions require a new version.

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

## Parameter preset files (`.mlapre`, version 1.0)

String `MLAPRE`, i64 major 1 and minor 0, plugin display-name string, i64 parameter
count, then `(i64 stable parameter ID, f64 normalized value)` pairs. Uses the same
little-endian primitives. Maximum 512 KiB, 16384 parameters, and 4096-byte strings.
The plugin name and complete parameter-ID set must match; duplicate/missing IDs,
non-finite values, values outside 0–1, and trailing/truncated data are rejected
before applying. Read-only parameters are recorded but not written on restore.
Unchanged values are not resent, preserving the session restoration safeguards.
MIDI bindings and opaque VST3 component/controller state are not part of a preset.
