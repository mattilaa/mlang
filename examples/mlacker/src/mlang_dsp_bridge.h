#pragma once

namespace mlacker_drum {

// Thin C++ wrapper around the compiled MLang master-bus DSP object.
class DspBridge
{
  public:
    void reset();
    void setMasterGain(float gain);
    float processSample(float x);
};

} // namespace mlacker_drum
