#include "mlang_osc_bridge.h"

extern "C" void mlang_vst3_reset__void(void);
extern "C" void mlang_vst3_set_sample_rate__f32(float sampleRate);
extern "C" void mlang_vst3_set_waveform__i32(int waveform);
extern "C" void mlang_vst3_note_on__i32_f32(int midiNote, float velocity);
extern "C" void mlang_vst3_note_off__void(void);
extern "C" float mlang_vst3_next_sample__void(void);

namespace mlang_vst3_example {

void OscillatorBridge::reset()
{
    mlang_vst3_reset__void();
}

void OscillatorBridge::setSampleRate(float sampleRate)
{
    mlang_vst3_set_sample_rate__f32(sampleRate);
}

void OscillatorBridge::setWaveform(Waveform waveform)
{
    mlang_vst3_set_waveform__i32(static_cast<int>(waveform));
}

void OscillatorBridge::noteOn(int midiNote, float velocity)
{
    mlang_vst3_note_on__i32_f32(midiNote, velocity);
}

void OscillatorBridge::noteOff()
{
    mlang_vst3_note_off__void();
}

float OscillatorBridge::nextSample()
{
    return mlang_vst3_next_sample__void();
}

} // namespace mlang_vst3_example
