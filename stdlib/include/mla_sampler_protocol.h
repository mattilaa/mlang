// Host -> plug-in sample loading protocol (VST3 IConnectionPoint messages).
//
// VST3 has no standard way for a host to hand audio to an instrument, and
// Mla Drum has no editor, so pads are filled through IMessage notifications
// sent to the *component's* IConnectionPoint. Any host may send these; mlacker
// uses them to load drum pads from disk or from its own sample pool.
// Implemented by plugins/mla_drum and sent by src/vst3_host.cpp in mlacker
// (https://github.com/mattilaa/mlacker).
//
// All messages carry an integer "pad" attribute (0-based). Strings are UTF-8
// sent with setBinary (no terminator). On failure the plug-in returns
// kResultFalse from notify() and, when possible, writes a UTF-8 reason back
// into the message as a binary "error" attribute.
#pragma once

namespace mla_sampler {

// Decode a mono/stereo WAV (16-bit PCM or float32), AIFF (8-32-bit) or
// AIFF-C (integer, float32 or float64) file into a pad.
//   "pad"  int    target pad
//   "path" binary UTF-8 file path
inline constexpr const char *kLoadFileMessage = "mlang.sampler.loadFile";

// Copy raw PCM into a pad.
//   "pad"      int    target pad
//   "rate"     float  source sample rate in Hz (1000..384000)
//   "channels" int    1 or 2
//   "frames"   int    frame count (1..16777216)
//   "data"     binary interleaved float32, frames * channels values
//   "name"     binary optional UTF-8 display name
inline constexpr const char *kLoadPcmMessage = "mlang.sampler.loadPcm";

// Empty a pad.
//   "pad"  int    target pad
inline constexpr const char *kClearMessage = "mlang.sampler.clear";

// Query the pad layout. No input attributes; the plug-in writes back:
//   "root"     int    MIDI key of pad 0 (pad n plays at root + n)
//   "pads"     int    number of pads (at most 64)
//   "occupied" int    bit n set when pad n holds a sample
inline constexpr const char *kInfoMessage = "mlang.sampler.info";

// Read or replace a pad's slice markers: the frames where its hits start,
// ascending, the first 0. Mla Sampler detects them when a sample loads.
//   "pad"    int     target pad
//   "set"    int     optional: 1 replaces them with "frames", 2 detects them again
//   "frames" binary  float64 frames: the new markers with set=1; the plug-in
//                    writes back the pad's markers the same way
inline constexpr const char *kMarkersMessage = "mlang.sampler.markers";

inline constexpr int kMaxFrames = 16777216;

} // namespace mla_sampler
