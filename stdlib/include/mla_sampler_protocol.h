// Host -> plug-in sample loading protocol (VST3 IConnectionPoint messages).
//
// VST3 has no standard way for a host to hand audio to an instrument, and
// Mla Drum has no editor, so pads are filled through IMessage notifications
// sent to the *component's* IConnectionPoint. Any host may send these; mlacker
// uses them to load drum pads from disk or from its own sample pool.
// Implemented by plugins/mla_drum; sent by mlacker/src/vst3_host.cpp.
//
// All messages carry an integer "pad" attribute (0-based). Strings are UTF-8
// sent with setBinary (no terminator). On failure the plug-in returns
// kResultFalse from notify() and, when possible, writes a UTF-8 reason back
// into the message as a binary "error" attribute.
#pragma once

namespace mla_sampler {

// Decode a mono/stereo 16-bit PCM WAV, AIFF or AIFF-C file into a pad.
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

inline constexpr int kMaxFrames = 16777216;

} // namespace mla_sampler
