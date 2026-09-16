#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mlang_dsp_bridge.h"

namespace mlacker_drum {

// Fixed pad grid, tracker-friendly (16 instruments per row group).
constexpr int kNumPads = 16;
constexpr int kMaxVoices = 32;

// One decoded, mono, float sample assigned to a pad. Kept at its source
// sample rate; playback resamples on the fly via a phase step.
struct PadSample
{
    std::vector<float> mono;
    int sourceSampleRate {0};
    std::string path; // set when loaded from a file path (used for persistence)

    bool loaded() const { return !mono.empty(); }
};

// A running one-shot voice reading from a pad sample.
struct Voice
{
    bool active {false};
    int pad {-1};
    double pos {0.0};   // fractional read position in source frames
    double step {1.0};  // source frames advanced per output frame
    float gain {1.0f};
};

// SDK-independent drum engine: pad storage, WAV decode, voice mixing.
// All audio math lives here; the master-bus shaping is delegated to the
// compiled MLang DSP through DspBridge.
class DrumEngine
{
  public:
    void setSampleRate(float sampleRate);
    void reset();

    // Load already-decoded interleaved float PCM into a pad (host upload path).
    void loadPadPcm(int pad,
                    const float* interleaved,
                    int frames,
                    int channels,
                    int sourceSampleRate);

    // Load a pad from a 16-bit PCM WAV file on disk (path upload path).
    bool loadPadWav(int pad, const std::string& path);

    void clearPad(int pad);
    bool padLoaded(int pad) const;
    const std::string& padPath(int pad) const;

    // Start a one-shot voice for a pad.
    void trigger(int pad, float velocity);
    void allVoicesOff();

    // Mix active voices into the stereo buffers for numFrames, running the
    // summed mono bus through the MLang master DSP. Buffers are overwritten.
    void render(float* left, float* right, int numFrames, DspBridge& dsp);

  private:
    float engineSampleRate_ {48000.0f};
    PadSample pads_[kNumPads];
    Voice voices_[kMaxVoices];
};

} // namespace mlacker_drum
