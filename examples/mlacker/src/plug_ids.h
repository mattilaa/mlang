#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace mlacker_drum {

// Unique processor class id for the headless mlacker drum machine.
static const Steinberg::FUID ProcessorUID(0x4D4C4B44, 0x52554D01, 0xA1B2C3D4,
                                          0xE5F60718);

enum ParamIds : Steinberg::Vst::ParamID
{
    kMasterGainParam = 1000,
};

} // namespace mlacker_drum
