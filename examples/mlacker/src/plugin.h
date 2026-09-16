#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "drum_engine.h"
#include "mlang_dsp_bridge.h"

namespace mlacker_drum {

// Headless VST3 drum machine. There is no editor: createView is left at the
// SingleComponentEffect default (returns none), so the host draws its own
// generic parameter panel. mlacker drives everything through note events and
// the sample-upload message contract in messages.h.
class Plugin : public Steinberg::Vst::SingleComponentEffect
{
  public:
    Plugin();

    static Steinberg::FUnknown* createInstance(void*);

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                     Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

    // IConnectionPoint: receives the mlacker sample-upload messages.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    // Helpers for the standalone offline host in this example.
    void previewTriggerPad(int pad, float velocity = 1.0f);
    void previewSetMasterGain(float gain);
    void previewRender(float** channels,
                       Steinberg::int32 numChannels,
                       Steinberg::int32 numSamples);

    static int noteToPad(Steinberg::int16 pitch);

  private:
    void applyMasterGainParam(Steinberg::Vst::ParamValue normalizedValue);
    void handleParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void handleEvents(Steinberg::Vst::IEventList* events);
    void handleUpload(Steinberg::Vst::IMessage* message);

    DrumEngine engine_;
    DspBridge dsp_;
    Steinberg::Vst::ParamValue masterGainValue_ {0.8};
};

} // namespace mlacker_drum
