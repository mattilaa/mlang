// VST3 plumbing for the MLang Juno-style ensemble chorus; no DSP is duplicated here.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>

struct JunoChorus;
extern "C" JunoChorus* mlajuno_create__f32(float);
extern "C" void mlajuno_destroy__ptr_struct_JunoChorus(JunoChorus*);
extern "C" void mlajuno_reset__ptr_struct_JunoChorus(JunoChorus*);
extern "C" void mlajuno_set__ptr_struct_JunoChorus_i32_f32(JunoChorus*, int, float);
extern "C" float mlajuno_process__ptr_struct_JunoChorus_f32_f32_ptr_f32(JunoChorus*, float, float, float*);
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_juno_chorus {
static const FUID kProcessorUID(0x4D6C614A, 0x756E6F43, 0xB61E4A29, 0x9D07C3E5);
// Stable, contiguous IDs are saved by mlacker sessions and presets.
enum Index {
    kMode = 0,
    kMix = 1,
    kDepth = 2,
    kWidth = 3,
    kTone = 4,
    kWarmth = 5,
    kNoise = 6,
    kNoiseLevel = 7,
    kOutputLevel = 8,
    kBypass = 9,
    kCount
};
constexpr ParamID kFirstParam = 100;
enum Mode { kOff, kOne, kTwo, kBoth };
// Stepped values are whole numbers from low to high, as VST3 RangeParameter
// and StringListParameter index them.
struct Spec { const TChar* title; const TChar* unit; double low, high, initial; int steps; };
static const Spec specs[kCount] = {
    {STR16("Chorus"), STR16(""), 0, 3, kOne, 3},
    {STR16("Mix"), STR16(""), 0, 1, 0.5, 0},
    {STR16("Depth"), STR16(""), 0, 1.5, 1, 0},
    {STR16("Width"), STR16(""), 0, 1.5, 1, 0},
    {STR16("Tone"), STR16("Hz"), 2000, 18000, 9000, 0},
    {STR16("Warmth"), STR16(""), 0, 1, 0.3, 0},
    {STR16("Noise"), STR16(""), 0, 1, 0, 1},
    {STR16("Noise Level"), STR16("dB"), -96, -30, -62, 0},
    {STR16("Output"), STR16("dB"), -24, 12, 0, 0},
    {STR16("Bypass"), STR16(""), 0, 1, 0, 1},
};
static double normalized(int i, double plain) { return (plain - specs[i].low) / (specs[i].high - specs[i].low); }
static double physical(int i, double norm) {
    const double value = specs[i].low + norm * (specs[i].high - specs[i].low);
    return specs[i].steps ? std::round(value) : value;
}
class Processor final : public SingleComponentEffect, public IMidiMapping {
public:
    Processor() { for(int i = 0; i < kCount; ++i) norm_[i] = normalized(i, specs[i].initial); }
    ~Processor() override { destroy(); }
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)
    static FUnknown* createInstance(void*) { return static_cast<IComponent*>(new Processor()); }
    tresult PLUGIN_API initialize(FUnknown* context) override {
        const auto result = SingleComponentEffect::initialize(context);
        if(result != kResultOk) return result;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        for(int i = 0; i < kCount; ++i) {
            if(i == kMode || i == kNoise) {
                auto* parameter = new StringListParameter(specs[i].title, kFirstParam + i);
                if(i == kMode) for(const TChar* name : {STR16("Off"), STR16("I"), STR16("II"), STR16("I+II")}) parameter->appendString(name);
                if(i == kNoise) { parameter->appendString(STR16("Off")); parameter->appendString(STR16("On")); }
                parameter->getInfo().defaultNormalizedValue = norm_[i];
                parameter->setNormalized(norm_[i]); parameters.addParameter(parameter);
            } else {
                int32 flags = ParameterInfo::kCanAutomate;
                if(i == kBypass) flags |= ParameterInfo::kIsBypass;
                parameters.addParameter(new RangeParameter(specs[i].title, kFirstParam + i, specs[i].unit,
                    specs[i].low, specs[i].high, specs[i].initial, specs[i].steps, flags));
            }
        }
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override { destroy(); return SingleComponentEffect::terminate(); }
    // General MIDI's chorus send (CC 93) drives Mix; CC 94 drives Depth.
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID& id) override {
        if(bus != 0 || channel < 0 || channel > 15) return kResultFalse;
        switch(cc) {
            case 93: id = kFirstParam + kMix; return kResultOk;
            case 94: id = kFirstParam + kDepth; return kResultOk;
        }
        return kResultFalse;
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 ni, SpeakerArrangement* outputs, int32 no) override {
        if(ni != 1 || no != 1 || !inputs || !outputs || inputs[0] != SpeakerArr::kStereo || outputs[0] != SpeakerArr::kStereo) return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, ni, outputs, no);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if(!std::isfinite(setup.sampleRate) || setup.sampleRate < 8000 || setup.sampleRate > 384000 || setup.symbolicSampleSize != kSample32) return kResultFalse;
        const auto result = SingleComponentEffect::setupProcessing(setup);
        if(result != kResultOk) return result;
        destroy();
        dsp_ = mlajuno_create__f32(static_cast<float>(setup.sampleRate));
        if(!dsp_) return kOutOfMemory;
        applyAll(); mlajuno_reset__ptr_struct_JunoChorus(dsp_); return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if(!state && dsp_) mlajuno_reset__ptr_struct_JunoChorus(dsp_);
        return SingleComponentEffect::setActive(state);
    }
    // A few ms of line, plus hiss that never ends while Noise is on.
    uint32 PLUGIN_API getTailSamples() override { return plain(kNoise) != 0 ? kInfiniteTail : 1024; }
    tresult PLUGIN_API process(ProcessData& data) override {
        // Match sibling plugins: consume the final value of each block queue.
        if(data.inputParameterChanges) {
            for(int32 q = 0; q < data.inputParameterChanges->getParameterCount(); ++q) {
                auto* queue = data.inputParameterChanges->getParameterData(q);
                if(!queue || queue->getPointCount() <= 0) continue;
                int32 offset = 0; ParamValue value = 0;
                if(queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk)
                    applyOne(queue->getParameterId(), value);
            }
        }
        if(data.numSamples <= 0) return kResultOk; // Parameter-only flush.
        if(data.symbolicSampleSize != kSample32) return kResultFalse;
        if(data.numInputs < 1 || data.numOutputs < 1 || !data.inputs || !data.outputs) return kResultOk;
        auto& in = data.inputs[0]; auto& out = data.outputs[0];
        if(in.numChannels != 2 || out.numChannels != 2 || !in.channelBuffers32 || !out.channelBuffers32) return kResultFalse;
        for(int c = 0; c < 2; ++c) if(!in.channelBuffers32[c] || !out.channelBuffers32[c]) return kResultFalse;
        bool silent = true;
        const bool bypass = plain(kBypass) != 0;
        for(int32 i = 0; i < data.numSamples; ++i) {
            const float left = (in.silenceFlags & 1) ? 0.f : in.channelBuffers32[0][i];
            const float right = (in.silenceFlags & 2) ? 0.f : in.channelBuffers32[1][i];
            float wetRight = right, wetLeft = left;
            if(dsp_) wetLeft = mlajuno_process__ptr_struct_JunoChorus_f32_f32_ptr_f32(dsp_, left, right, &wetRight);
            out.channelBuffers32[0][i] = bypass ? left : wetLeft;
            out.channelBuffers32[1][i] = bypass ? right : wetRight;
            if(out.channelBuffers32[0][i] != 0 || out.channelBuffers32[1][i] != 0) silent = false;
        }
        out.silenceFlags = silent ? 3 : 0;
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* state) override {
        if(!state) return kResultFalse;
        IBStreamer stream(state, kLittleEndian);
        std::array<double, kCount> values{};
        for(auto& value : values) if(!stream.readDouble(value) || !std::isfinite(value) || value < 0 || value > 1) return kResultFalse;
        norm_ = values;
        for(int i = 0; i < kCount; ++i) setParamNormalized(kFirstParam + i, norm_[i]);
        applyAll(); return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override {
        if(!state) return kResultFalse;
        IBStreamer stream(state, kLittleEndian);
        for(double value : norm_) if(!stream.writeDouble(value)) return kResultFalse;
        return kResultOk;
    }
private:
    JunoChorus* dsp_ = nullptr;
    std::array<double, kCount> norm_{};
    double plain(int i) const { return physical(i, norm_[i]); }
    void destroy() { if(dsp_) { mlajuno_destroy__ptr_struct_JunoChorus(dsp_); dsp_ = nullptr; } }
    void pushControl(int i) {
        if(dsp_ && i < kBypass) mlajuno_set__ptr_struct_JunoChorus_i32_f32(dsp_, i, static_cast<float>(plain(i)));
    }
    void applyAll() { for(int i = 0; i < kBypass; ++i) pushControl(i); }
    void applyOne(ParamID id, double value) {
        if(id < kFirstParam || id >= kFirstParam + kCount || !std::isfinite(value)) return;
        const int i = static_cast<int>(id - kFirstParam);
        norm_[i] = std::clamp(value, 0.0, 1.0);
        pushControl(i);
    }
};
} // namespace mla_juno_chorus
#ifndef MLA_JUNO_CHORUS_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_juno_chorus::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla JunoChorus", 0, PlugType::kFxModulation, "0.1.0", kVstVersionString,
    mla_juno_chorus::Processor::createInstance)
END_FACTORY
#endif
