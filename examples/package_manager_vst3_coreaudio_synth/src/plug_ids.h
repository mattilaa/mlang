#pragma once

#include "pluginterfaces/base/funknown.h"

namespace mlang_vst3_example {

static const Steinberg::FUID ProcessorUID(0x7A9C0E11, 0xE8224A61, 0xA5A34B51,
                                          0x5B7282A1);

enum ParamIds : Steinberg::Vst::ParamID
{
    kWaveformParam = 1000,
};

} // namespace mlang_vst3_example
