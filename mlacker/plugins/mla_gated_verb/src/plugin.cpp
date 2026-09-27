// VST3 plumbing for the MLang 80s gated drum reverb; no DSP is duplicated here.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>

struct GatedVerb;
extern "C" GatedVerb* mlagated_create__f32(float);
extern "C" void mlagated_destroy__ptr_struct_GatedVerb(GatedVerb*);
extern "C" void mlagated_reset__ptr_struct_GatedVerb(GatedVerb*);
extern "C" void mlagated_set__ptr_struct_GatedVerb_i32_f32(GatedVerb*, int, float);
extern "C" float mlagated_process__ptr_struct_GatedVerb_f32_f32_ptr_f32(GatedVerb*, float, float, float*);
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_gated_verb {
static const FUID kProcessorUID(0x4D6C6147, 0x61746564, 0x8E3B51C7, 0x2A94F06D);
// Stable, contiguous IDs are saved by mlacker sessions and presets.
enum Index {
    kType = 0,
    kSize = 1,
    kHold = 2,
    kRelease = 3,
    kDamp = 4,
    kTone = 5,
    kThreshold = 6,
    kPredelay = 7,
    kSquash = 8,
    kWidth = 9,
    kLowCut = 10,
    kReverbLevel = 11,
    kDryLevel = 12,
    kOutputLevel = 13,
    kBypass = 14,
    kCount
};
constexpr ParamID kFirstParam = 100;
enum Type { kRoom, kPlate, kReverse };
// Stepped values are whole numbers from low to high, as VST3 RangeParameter
// and StringListParameter index them.
struct Spec { const TChar* title; const TChar* unit; double low, high, initial; int steps; };
static const Spec specs[kCount] = {
    {STR16("Type"), STR16(""), 0, 2, kRoom, 2},
    {STR16("Size"), STR16(""), 0, 1, 0.5, 0},
    {STR16("Hold"), STR16("ms"), 10, 1000, 250, 0},
    {STR16("Release"), STR16("ms"), 5, 1000, 80, 0},
    {STR16("Damp"), STR16(""), 0, 1, 0.6, 0},
    {STR16("Tone"), STR16("Hz"), 1500, 16000, 9000, 0},
    {STR16("Threshold"), STR16("dB"), -60, 0, -24, 0},
    {STR16("Pre-delay"), STR16("ms"), 0, 100, 8, 0},
    {STR16("Squash"), STR16(""), 0, 1, 0.6, 0},
    {STR16("Width"), STR16(""), 0, 1, 1, 0},
    {STR16("Low Cut"), STR16("Hz"), 20, 1000, 120, 0},
    {STR16("Reverb"), STR16("dB"), -24, 12, 0, 0},
    {STR16("Dry"), STR16("dB"), -60, 6, 0, 0},
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
            if(i == kType) {
                auto* parameter = new StringListParameter(specs[i].title, kFirstParam + i);
                for(const TChar* name : {STR16("Room"), STR16("Plate"), STR16("Reverse")}) parameter->appendString(name);
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
    // General MIDI's reverb send (CC 91) drives the reverb level; CC 92 the hold.
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID& id) override {
        if(bus != 0 || channel < 0 || channel > 15) return kResultFalse;
        switch(cc) {
            case 91: id = kFirstParam + kReverbLevel; return kResultOk;
            case 92: id = kFirstParam + kHold; return kResultOk;
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
        dsp_ = mlagated_create__f32(static_cast<float>(setup.sampleRate));
        if(!dsp_) return kOutOfMemory;
        applyAll(); mlagated_reset__ptr_struct_GatedVerb(dsp_); return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if(!state && dsp_) mlagated_reset__ptr_struct_GatedVerb(dsp_);
        return SingleComponentEffect::setActive(state);
    }
    // After the last hit the gate is shut once pre-delay, hold and release pass.
    uint32 PLUGIN_API getTailSamples() override {
        const double ms = plain(kPredelay) + plain(kHold) + plain(kRelease) + 10;
        return static_cast<uint32>(std::ceil(ms * processSetup.sampleRate / 1000));
    }
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
            if(dsp_) wetLeft = mlagated_process__ptr_struct_GatedVerb_f32_f32_ptr_f32(dsp_, left, right, &wetRight);
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
    GatedVerb* dsp_ = nullptr;
    std::array<double, kCount> norm_{};
    double plain(int i) const { return physical(i, norm_[i]); }
    void destroy() { if(dsp_) { mlagated_destroy__ptr_struct_GatedVerb(dsp_); dsp_ = nullptr; } }
    void pushControl(int i) {
        if(dsp_ && i < kBypass) mlagated_set__ptr_struct_GatedVerb_i32_f32(dsp_, i, static_cast<float>(plain(i)));
    }
    void applyAll() { for(int i = 0; i < kBypass; ++i) pushControl(i); }
    void applyOne(ParamID id, double value) {
        if(id < kFirstParam || id >= kFirstParam + kCount || !std::isfinite(value)) return;
        const int i = static_cast<int>(id - kFirstParam);
        norm_[i] = std::clamp(value, 0.0, 1.0);
        pushControl(i);
    }
};
} // namespace mla_gated_verb
#ifndef MLA_GATED_VERB_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_gated_verb::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla GatedVerb", 0, PlugType::kFxReverb, "0.1.0", kVstVersionString,
    mla_gated_verb::Processor::createInstance)
END_FACTORY
#endif
