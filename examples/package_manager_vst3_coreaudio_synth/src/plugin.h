#pragma once

#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"

#include "mlang_osc_bridge.h"

namespace mlang_vst3_example {

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

    // Preview helpers for the standalone CoreAudio host in this example.
    void previewSetWaveform(Waveform waveform);
    void previewNoteOn(Steinberg::int32 midiNote, float velocity = 1.0f);
    void previewNoteOff();
    void previewRender(float** channels,
                       Steinberg::int32 numChannels,
                       Steinberg::int32 numSamples);

  private:
    void applyWaveformParam(Steinberg::Vst::ParamValue normalizedValue);
    void handleParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void handleEvents(Steinberg::Vst::IEventList* events);

    OscillatorBridge bridge_;
    Steinberg::Vst::ParamValue waveformValue_ {0.0};
};

} // namespace mlang_vst3_example
