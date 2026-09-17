// Mla Distortion - VST3 stereo saturation/distortion effect.
//
// A thin SingleComponentEffect wrapper around the shared MLang DSP module
// `modules/dsp/distortion.mla`. All saturation math lives in MLang; this file
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

// --- MLang DSP bridge (compiled from src/mla_distortion_dsp.mla) -------------
// Opaque per-instance handle owned here on the C++ side.
struct MlaDistortion;
extern "C" MlaDistortion *mladist_create__f32(float sampleRate);
extern "C" void mladist_destroy__ptr_struct_MlaDistortion(MlaDistortion *handle);
extern "C" void mladist_reset__ptr_struct_MlaDistortion(MlaDistortion *handle);
extern "C" void mladist_set_drive__ptr_struct_MlaDistortion_f32(MlaDistortion *handle, float driveDb);
extern "C" void mladist_set_tone__ptr_struct_MlaDistortion_f32(MlaDistortion *handle, float cutoffHz);
extern "C" void mladist_set_bias__ptr_struct_MlaDistortion_f32(MlaDistortion *handle, float bias);
extern "C" void mladist_set_output__ptr_struct_MlaDistortion_f32(MlaDistortion *handle, float outputDb);
extern "C" void mladist_set_mix__ptr_struct_MlaDistortion_f32(MlaDistortion *handle, float mix);
extern "C" float mladist_process__ptr_struct_MlaDistortion_f32_f32_ptr_f32(MlaDistortion *handle, float left, float right, float *outRight);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_distortion {

// Stable class id. Distinct from Mla Verb, the SDK examples and mlacker bundles.
static const FUID kProcessorUID(0x4D6C6144, 0x69737431, 0x7B2E9C05, 0x1144AF63);

enum ParamId : ParamID {
    kDriveParam = 100,
    kToneParam = 101,
    kBiasParam = 102,
    kOutputParam = 103,
    kMixParam = 104,
    kNumParams = 5,
};

// Physical range mappings shared by the DSP push and any host display.
static float driveDbFromNorm(double norm)
{
    // 0 dB .. 36 dB of pre-saturation drive.
    return static_cast<float>(norm) * 36.0f;
}

static float toneHzFromNorm(double norm)
{
    // 20 Hz .. 20 kHz, exponential so the tone knob feels musical.
    return 20.0f * std::pow(1000.0f, static_cast<float>(norm));
}

static float biasFromNorm(double norm)
{
    // -0.5 .. +0.5 asymmetry, centred at norm 0.5.
    return static_cast<float>(norm) - 0.5f;
}

static float outputDbFromNorm(double norm)
{
    // -24 dB .. +12 dB output trim (unity at norm 2/3).
    return -24.0f + static_cast<float>(norm) * 36.0f;
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

        parameters.addParameter(STR16("Drive"), STR16("dB"), 0, 0.33, ParameterInfo::kCanAutomate, kDriveParam);
        parameters.addParameter(STR16("Tone"), STR16("Hz"), 0, 0.85, ParameterInfo::kCanAutomate, kToneParam);
        parameters.addParameter(STR16("Bias"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kBiasParam);
        parameters.addParameter(STR16("Output"), STR16("dB"), 0, 0.6667, ParameterInfo::kCanAutomate, kOutputParam);
        parameters.addParameter(STR16("Mix"), nullptr, 0, 1.0, ParameterInfo::kCanAutomate, kMixParam);

        norm_[0] = 0.33;   // Drive ~12 dB
        norm_[1] = 0.85;   // Tone ~7 kHz
        norm_[2] = 0.5;    // Bias 0 (symmetric)
        norm_[3] = 0.6667; // Output 0 dB
        norm_[4] = 1.0;    // Mix 100% wet (insert default)
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
            case 1: id = kDriveParam; return kResultOk;   // Mod wheel
            case 74: id = kToneParam; return kResultOk;   // Brightness
            case 71: id = kBiasParam; return kResultOk;   // Harmonic content
            case 91: id = kMixParam; return kResultOk;    // Effect 1 depth
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
        dsp_ = mladist_create__f32(static_cast<float>(setup.sampleRate));
        applyAllParameters();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(!state && dsp_)
            mladist_reset__ptr_struct_MlaDistortion(dsp_);
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
            float right = 0.0f;
            const float left = mladist_process__ptr_struct_MlaDistortion_f32_f32_ptr_f32(dsp_, inL[i], inR[i], &right);
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
    MlaDistortion *dsp_ = nullptr;
    double norm_[kNumParams] = {};

    static ParamID paramIdAt(int index) { return static_cast<ParamID>(kDriveParam + index); }
    static int indexOf(ParamID id)
    {
        const int index = static_cast<int>(id) - static_cast<int>(kDriveParam);
        return (index >= 0 && index < kNumParams) ? index : -1;
    }

    void destroyHandle()
    {
        if(dsp_) {
            mladist_destroy__ptr_struct_MlaDistortion(dsp_);
            dsp_ = nullptr;
        }
    }

    // Push every control to the DSP without clearing the filter history.
    void applyAllParameters()
    {
        if(!dsp_)
            return;
        mladist_set_drive__ptr_struct_MlaDistortion_f32(dsp_, driveDbFromNorm(norm_[0]));
        mladist_set_tone__ptr_struct_MlaDistortion_f32(dsp_, toneHzFromNorm(norm_[1]));
        mladist_set_bias__ptr_struct_MlaDistortion_f32(dsp_, biasFromNorm(norm_[2]));
        mladist_set_output__ptr_struct_MlaDistortion_f32(dsp_, outputDbFromNorm(norm_[3]));
        mladist_set_mix__ptr_struct_MlaDistortion_f32(dsp_, static_cast<float>(norm_[4]));
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
            case kDriveParam: mladist_set_drive__ptr_struct_MlaDistortion_f32(dsp_, driveDbFromNorm(value)); break;
            case kToneParam: mladist_set_tone__ptr_struct_MlaDistortion_f32(dsp_, toneHzFromNorm(value)); break;
            case kBiasParam: mladist_set_bias__ptr_struct_MlaDistortion_f32(dsp_, biasFromNorm(value)); break;
            case kOutputParam: mladist_set_output__ptr_struct_MlaDistortion_f32(dsp_, outputDbFromNorm(value)); break;
            case kMixParam: mladist_set_mix__ptr_struct_MlaDistortion_f32(dsp_, static_cast<float>(value)); break;
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

} // namespace mla_distortion

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_distortion::kProcessorUID), PClassInfo::kManyInstances,
           kVstAudioEffectClass, "Mla Distortion", 0, PlugType::kFxDistortion, "0.1.0", kVstVersionString,
           mla_distortion::Processor::createInstance)
END_FACTORY
