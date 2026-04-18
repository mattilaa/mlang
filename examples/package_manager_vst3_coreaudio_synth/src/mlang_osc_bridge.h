#pragma once

namespace mlang_vst3_example {

enum class Waveform
{
    Sine = 0,
    Square = 1,
};

class OscillatorBridge
{
  public:
    void reset();
    void setSampleRate(float sampleRate);
    void setWaveform(Waveform waveform);
    void noteOn(int midiNote, float velocity = 1.0f);
    void noteOff();
    float nextSample();
};

} // namespace mlang_vst3_example
