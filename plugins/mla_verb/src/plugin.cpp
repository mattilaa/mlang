// Mla Verb - VST3 stereo reverb effect.
//
// A thin SingleComponentEffect wrapper around the shared MLang DSP module
// `modules/dsp/reverb2.mla`. All reverberation math lives in MLang; this file
// only negotiates buses, exposes VST3 parameters, maps them onto the DSP's
// physical ranges, and pumps stereo frames through the per-instance handle
// returned by the compiled MLang bridge.

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"

#include <cmath>

// --- MLang DSP bridge (compiled from src/mla_verb_dsp.mla) -------------------
// Opaque per-instance handle owned here on the C++ side.
struct MlaVerb;
extern "C" MlaVerb *mlaverb_create__f32(float sampleRate);
extern "C" void mlaverb_destroy__ptr_struct_MlaVerb(MlaVerb *handle);
extern "C" void mlaverb_reset__ptr_struct_MlaVerb(MlaVerb *handle);
extern "C" void mlaverb_set_type__ptr_struct_MlaVerb_i32(MlaVerb *handle, int index);
extern "C" void mlaverb_set_size__ptr_struct_MlaVerb_f32(MlaVerb *handle, float value);
extern "C" void mlaverb_set_decay__ptr_struct_MlaVerb_f32(MlaVerb *handle, float seconds);
extern "C" void mlaverb_set_damp__ptr_struct_MlaVerb_f32(MlaVerb *handle, float value);
extern "C" void mlaverb_set_diffusion__ptr_struct_MlaVerb_f32(MlaVerb *handle, float value);
extern "C" void mlaverb_set_predelay__ptr_struct_MlaVerb_f32(MlaVerb *handle, float milliseconds);
extern "C" void mlaverb_set_early__ptr_struct_MlaVerb_f32(MlaVerb *handle, float value);
extern "C" void mlaverb_set_width__ptr_struct_MlaVerb_f32(MlaVerb *handle, float value);
extern "C" void mlaverb_set_mix__ptr_struct_MlaVerb_f32(MlaVerb *handle, float value);
extern "C" void mlaverb_set_freeze__ptr_struct_MlaVerb_i32(MlaVerb *handle, int enabled);
extern "C" float mlaverb_process__ptr_struct_MlaVerb_f32_f32(MlaVerb *handle, float left, float right);
extern "C" float mlaverb_right__ptr_struct_MlaVerb(MlaVerb *handle);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_verb {

// Stable class id. Distinct from the SDK examples and mlacker test bundles.
static const FUID kProcessorUID(0x4D6C6156, 0x65726231, 0x9A3C7F12, 0x55B8E0D4);

enum ParamId : ParamID {
    kTypeParam = 100,
    kSizeParam = 101,
    kDecayParam = 102,
    kDampParam = 103,
    kMixParam = 104,
    kPredelayParam = 105,
    kWidthParam = 106,
    kDiffusionParam = 107,
    kEarlyParam = 108,
    kFreezeParam = 109,
    kNumParams = 10,
};

// Nine reverb characters, matching dsp::reverb2::Reverb2Type indices 0..8.
static const int kTypeCount = 9;

// Physical range mappings shared by the DSP push and any host display.
static float decaySecondsFromNorm(double norm)
{
    // 0.1 s .. 20 s, exponential so the knob feels musical.
    return 0.1f * std::pow(200.0f, static_cast<float>(norm));
}

static float predelayMsFromNorm(double norm)
{
    return static_cast<float>(norm) * 200.0f;
}

static int typeIndexFromNorm(double norm)
{
    int index = static_cast<int>(norm * (kTypeCount - 1) + 0.5);
    if(index < 0)
        index = 0;
    if(index > kTypeCount - 1)
        index = kTypeCount - 1;
    return index;
}

class Processor final : public SingleComponentEffect, public IMidiMapping {
  public:
    Processor() = default;

    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

    static FUnknown *createInstance(void *) { return static_cast<IComponent *>(new Processor()); }

    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::initialize(context);
        if(result != kResultOk)
            return result;

        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

        auto *type = new StringListParameter(STR16("Type"), kTypeParam);
        type->appendString(STR16("Hall"));
        type->appendString(STR16("Room"));
        type->appendString(STR16("Plate"));
        type->appendString(STR16("Gated"));
        type->appendString(STR16("Reverse"));
        type->appendString(STR16("Nonlinear Short"));
        type->appendString(STR16("Nonlinear Long"));
        type->appendString(STR16("Custom"));
        type->appendString(STR16("Infinite Hall"));
        parameters.addParameter(type);

        parameters.addParameter(STR16("Size"), nullptr, 0, 0.72, ParameterInfo::kCanAutomate, kSizeParam);
        parameters.addParameter(STR16("Decay"), STR16("s"), 0, 0.6867, ParameterInfo::kCanAutomate, kDecayParam);
        parameters.addParameter(STR16("Damp"), nullptr, 0, 0.38, ParameterInfo::kCanAutomate, kDampParam);
        parameters.addParameter(STR16("Mix"), nullptr, 0, 0.35, ParameterInfo::kCanAutomate, kMixParam);
        parameters.addParameter(STR16("Predelay"), STR16("ms"), 0, 0.12, ParameterInfo::kCanAutomate, kPredelayParam);
        parameters.addParameter(STR16("Width"), nullptr, 0, 0.92, ParameterInfo::kCanAutomate, kWidthParam);
        parameters.addParameter(STR16("Diffusion"), nullptr, 0, 0.78, ParameterInfo::kCanAutomate, kDiffusionParam);
        parameters.addParameter(STR16("Early"), nullptr, 0, 0.30, ParameterInfo::kCanAutomate, kEarlyParam);
        parameters.addParameter(STR16("Freeze"), nullptr, 1, 0.0, ParameterInfo::kCanAutomate, kFreezeParam);

        norm_[0] = 0.0;    // Type: Hall
        norm_[1] = 0.72;   // Size
        norm_[2] = 0.6867; // Decay ~3.8 s
        norm_[3] = 0.38;   // Damp
        norm_[4] = 0.35;   // Mix
        norm_[5] = 0.12;   // Predelay 24 ms
        norm_[6] = 0.92;   // Width
        norm_[7] = 0.78;   // Diffusion
        norm_[8] = 0.30;   // Early
        norm_[9] = 0.0;    // Freeze off
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        destroyHandle();
        return SingleComponentEffect::terminate();
    }

    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc,
                                                   ParamID &id) SMTG_OVERRIDE
    {
        if(bus != 0 || channel < 0 || channel > 15)
            return kResultFalse;
        // Raw MIDI CC numbers, matching mlacker's pattern/live CC routing.
        switch(cc) {
            case 1: id = kMixParam; return kResultOk;   // Mod wheel
            case 70: id = kSizeParam; return kResultOk;  // Sound Variation
            case 74: id = kDampParam; return kResultOk;  // Brightness
            case 91: id = kDecayParam; return kResultOk; // Effect 1 depth
        }
        return kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                          SpeakerArrangement *outputs, int32 numOuts) SMTG_OVERRIDE
    {
        if(numIns != 1 || numOuts != 1 || inputs == nullptr || outputs == nullptr)
            return kResultFalse;
        if(inputs[0] != SpeakerArr::kStereo || outputs[0] != SpeakerArr::kStereo)
            return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup &setup) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::setupProcessing(setup);
        if(result != kResultOk)
            return result;
        destroyHandle();
        dsp_ = mlaverb_create__f32(static_cast<float>(setup.sampleRate));
        applyAllParameters();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(!state && dsp_)
            mlaverb_reset__ptr_struct_MlaVerb(dsp_);
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        handleParameterChanges(data.inputParameterChanges);

        if(data.numOutputs < 1 || data.numInputs < 1)
            return kResultOk;
        if(data.symbolicSampleSize != kSample32)
            return kResultFalse;

        AudioBusBuffers &in = data.inputs[0];
        AudioBusBuffers &out = data.outputs[0];
        if(out.numChannels < 2 || in.numChannels < 2 || out.channelBuffers32 == nullptr ||
           in.channelBuffers32 == nullptr)
            return kResultOk;

        float *inL = in.channelBuffers32[0];
        float *inR = in.channelBuffers32[1];
        float *outL = out.channelBuffers32[0];
        float *outR = out.channelBuffers32[1];

        if(!dsp_) {
            for(int32 i = 0; i < data.numSamples; ++i) {
                outL[i] = inL[i];
                outR[i] = inR[i];
            }
            out.silenceFlags = 0;
            return kResultOk;
        }

        for(int32 i = 0; i < data.numSamples; ++i) {
            const float left = mlaverb_process__ptr_struct_MlaVerb_f32_f32(dsp_, inL[i], inR[i]);
            const float right = mlaverb_right__ptr_struct_MlaVerb(dsp_);
            outL[i] = left;
            outR[i] = right;
        }
        out.silenceFlags = 0;
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream *state) SMTG_OVERRIDE
    {
        if(state == nullptr)
            return kResultFalse;
        IBStreamer streamer(state, kLittleEndian);
        for(int i = 0; i < kNumParams; ++i) {
            double value = norm_[i];
            if(!streamer.readDouble(value))
                break;
            norm_[i] = value;
            setParamNormalized(paramIdAt(i), value);
        }
        applyAllParameters();
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream *state) SMTG_OVERRIDE
    {
        if(state == nullptr)
            return kResultFalse;
        IBStreamer streamer(state, kLittleEndian);
        for(int i = 0; i < kNumParams; ++i)
            streamer.writeDouble(norm_[i]);
        return kResultOk;
    }

  private:
    MlaVerb *dsp_ = nullptr;
    double norm_[kNumParams] = {};

    static ParamID paramIdAt(int index) { return static_cast<ParamID>(kTypeParam + index); }
    static int indexOf(ParamID id)
    {
        const int index = static_cast<int>(id) - static_cast<int>(kTypeParam);
        return (index >= 0 && index < kNumParams) ? index : -1;
    }

    void destroyHandle()
    {
        if(dsp_) {
            mlaverb_destroy__ptr_struct_MlaVerb(dsp_);
            dsp_ = nullptr;
        }
    }

    // Push every continuous control to the DSP without clearing the tail.
    void pushContinuous()
    {
        if(!dsp_)
            return;
        mlaverb_set_size__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(norm_[1]));
        mlaverb_set_decay__ptr_struct_MlaVerb_f32(dsp_, decaySecondsFromNorm(norm_[2]));
        mlaverb_set_damp__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(norm_[3]));
        mlaverb_set_mix__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(norm_[4]));
        mlaverb_set_predelay__ptr_struct_MlaVerb_f32(dsp_, predelayMsFromNorm(norm_[5]));
        mlaverb_set_width__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(norm_[6]));
        mlaverb_set_diffusion__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(norm_[7]));
        mlaverb_set_early__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(norm_[8]));
        mlaverb_set_freeze__ptr_struct_MlaVerb_i32(dsp_, norm_[9] >= 0.5 ? 1 : 0);
    }

    // Selecting a type loads its character preset (and clears the tail); the
    // continuous sliders then stay authoritative over the shared parameters.
    void applyType()
    {
        if(!dsp_)
            return;
        mlaverb_set_type__ptr_struct_MlaVerb_i32(dsp_, typeIndexFromNorm(norm_[0]));
        pushContinuous();
    }

    void applyAllParameters()
    {
        applyType();
    }

    void applyOne(ParamID id, double value)
    {
        const int index = indexOf(id);
        if(index < 0)
            return;
        norm_[index] = value;
        if(!dsp_)
            return;
        switch(id) {
            case kTypeParam: applyType(); break;
            case kSizeParam: mlaverb_set_size__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(value)); break;
            case kDecayParam: mlaverb_set_decay__ptr_struct_MlaVerb_f32(dsp_, decaySecondsFromNorm(value)); break;
            case kDampParam: mlaverb_set_damp__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(value)); break;
            case kMixParam: mlaverb_set_mix__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(value)); break;
            case kPredelayParam: mlaverb_set_predelay__ptr_struct_MlaVerb_f32(dsp_, predelayMsFromNorm(value)); break;
            case kWidthParam: mlaverb_set_width__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(value)); break;
            case kDiffusionParam: mlaverb_set_diffusion__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(value)); break;
            case kEarlyParam: mlaverb_set_early__ptr_struct_MlaVerb_f32(dsp_, static_cast<float>(value)); break;
            case kFreezeParam: mlaverb_set_freeze__ptr_struct_MlaVerb_i32(dsp_, value >= 0.5 ? 1 : 0); break;
        }
    }

    void handleParameterChanges(IParameterChanges *changes)
    {
        if(changes == nullptr)
            return;
        const int32 count = changes->getParameterCount();
        for(int32 q = 0; q < count; ++q) {
            IParamValueQueue *queue = changes->getParameterData(q);
            if(queue == nullptr)
                continue;
            const int32 points = queue->getPointCount();
            if(points <= 0)
                continue;
            int32 offset = 0;
            ParamValue value = 0;
            if(queue->getPoint(points - 1, offset, value) != kResultOk)
                continue;
            const ParamID id = queue->getParameterId();
            applyOne(id, value);
        }
    }
};

} // namespace mla_verb

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_verb::kProcessorUID), PClassInfo::kManyInstances,
           kVstAudioEffectClass, "Mla Verb", 0, PlugType::kFxReverb, "0.1.0", kVstVersionString,
           mla_verb::Processor::createInstance)
END_FACTORY
