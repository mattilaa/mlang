#include <cstdio>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vsttypes.h"

extern "C" const char* ml_vst3_sdk_version()
{
    return kVstVersionString;
}

extern "C" const char* ml_vst3_audio_effect_class()
{
    return kVstAudioEffectClass;
}

extern "C" const char* ml_vst3_fx_subcategory()
{
    return Steinberg::Vst::PlugType::kFx;
}

extern "C" long long ml_vst3_stereo_speaker_arrangement()
{
    return static_cast<long long>(Steinberg::Vst::SpeakerArr::kStereo);
}

extern "C" int ml_vst3_print_bridge_summary()
{
    std::printf("[c++] Steinberg VST3 SDK bridge is active.\n");
    std::printf("[c++] kVstVersionString: %s\n", ml_vst3_sdk_version());
    std::printf("[c++] kVstAudioEffectClass: %s\n",
                ml_vst3_audio_effect_class());
    std::printf("[c++] PlugType::kFx: %s\n", ml_vst3_fx_subcategory());
    std::printf("[c++] SpeakerArr::kStereo: %lld\n",
                ml_vst3_stereo_speaker_arrangement());
    return 0;
}
