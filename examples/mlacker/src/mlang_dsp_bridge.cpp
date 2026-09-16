#include "mlang_dsp_bridge.h"

extern "C" void mlang_drum_reset__void(void);
extern "C" void mlang_drum_set_master_gain__f32(float gain);
extern "C" float mlang_drum_process_sample__f32(float x);

namespace mlacker_drum {

void DspBridge::reset()
{
    mlang_drum_reset__void();
}

void DspBridge::setMasterGain(float gain)
{
    mlang_drum_set_master_gain__f32(gain);
}

float DspBridge::processSample(float x)
{
    return mlang_drum_process_sample__f32(x);
}

} // namespace mlacker_drum
