// VST3 plumbing for Mla Filter; the filter models, ramps and crossfades live
// in dsp::multimode (see mla_filter_dsp.mla).
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>

struct StereoMultimodeFilter;
extern "C" StereoMultimodeFilter* mlafilter_create__f32(float);
extern "C" void mlafilter_destroy__ptr_struct_StereoMultimodeFilter(StereoMultimodeFilter*);
extern "C" void mlafilter_reset__ptr_struct_StereoMultimodeFilter(StereoMultimodeFilter*);
extern "C" void mlafilter_set_model__ptr_struct_StereoMultimodeFilter_i32_i64(StereoMultimodeFilter*, int, int64_t);
extern "C" void mlafilter_set_cutoff__ptr_struct_StereoMultimodeFilter_f32_i64(StereoMultimodeFilter*, float, int64_t);
extern "C" void mlafilter_set_resonance__ptr_struct_StereoMultimodeFilter_f32_i64(StereoMultimodeFilter*, float, int64_t);
extern "C" float mlafilter_process__ptr_struct_StereoMultimodeFilter_f32_f32_ptr_f32(StereoMultimodeFilter*, float, float, float*);
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_filter {
static const FUID kProcessorUID(0x4D6C6146, 0x696C7472, 0xA3C54E19, 0x86F20B7D);
// Stable, contiguous IDs are saved by mlacker sessions and presets.
enum Index {
    kType = 0,
    kCutoff = 1,
    kResonance = 2,
    kMix = 3,
    kOutputGain = 4,
    kGlide = 5,
    kBypass = 6,
    kCount
};
// Index order matches dsp::multimode::FilterModel.
enum Model { kLowpass12, kLowpass24, kHighpass12, kHighpass24, kBandpass12, kBandpass24, kMoog12, kMoog24, kModels };
constexpr ParamID kFirstParam = 100;
constexpr double kModelFadeMs = 20;
struct Spec { const TChar* title; const TChar* unit; double low, high, initial; int steps; bool log; };
static const Spec specs[kCount] = {
    {STR16("Type"), STR16(""), 0, kModels - 1, kMoog24, kModels - 1, false},
    {STR16("Cutoff"), STR16("Hz"), 20, 20000, 2000, 0, true},
    {STR16("Resonance"), STR16("dB"), 0, 36, 0, 0, false},
    {STR16("Mix"), STR16(""), 0, 1, 1, 0, false},
    {STR16("Output"), STR16("dB"), -24, 24, 0, 0, false},
    {STR16("Glide"), STR16("ms"), 0, 2000, 20, 0, false},
    {STR16("Bypass"), STR16(""), 0, 1, 0, 1, false},
};
// Cutoff uses a logarithmic normalized mapping: equal travel per octave.
static double normalized(int i, double plain) {
    const Spec& s = specs[i];
    if(s.log) return std::log(plain / s.low) / std::log(s.high / s.low);
    return (plain - s.low) / (s.high - s.low);
}
static double physical(int i, double norm) {
    const Spec& s = specs[i];
    if(s.log) return s.low * std::pow(s.high / s.low, norm);
    double value = s.low + norm * (s.high - s.low);
    return s.steps ? std::round(value) : value;
}
class LogParameter final : public RangeParameter {
public:
    LogParameter(int index, int32 flags)
    : RangeParameter(specs[index].title, kFirstParam + index, specs[index].unit,
          specs[index].low, specs[index].high, specs[index].initial, 0, flags), index_(index) {
        // The base constructor ran its own linear toNormalized.
        info.defaultNormalizedValue = valueNormalized = normalized(index_, specs[index_].initial);
    }
    ParamValue toPlain(ParamValue norm) const override { return physical(index_, norm); }
    ParamValue toNormalized(ParamValue plain) const override {
        return std::clamp(normalized(index_, std::clamp(plain, getMin(), getMax())), 0.0, 1.0);
    }
private:
    int index_;
};
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
                for(const TChar* name : {STR16("Lowpass 12"), STR16("Lowpass 24"), STR16("Highpass 12"), STR16("Highpass 24"),
                                         STR16("Bandpass 12"), STR16("Bandpass 24"), STR16("Moog 12"), STR16("Moog 24")})
                    parameter->appendString(name);
                parameter->getInfo().defaultNormalizedValue = norm_[i];
                parameter->setNormalized(norm_[i]); parameters.addParameter(parameter);
            } else {
                int32 flags = ParameterInfo::kCanAutomate;
                if(i == kBypass) flags |= ParameterInfo::kIsBypass;
                if(specs[i].log) parameters.addParameter(new LogParameter(i, flags));
                else parameters.addParameter(new RangeParameter(specs[i].title, kFirstParam + i, specs[i].unit,
                    specs[i].low, specs[i].high, specs[i].initial, specs[i].steps, flags));
            }
        }
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override { destroy(); return SingleComponentEffect::terminate(); }
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID& id) override {
        if(bus != 0 || channel < 0 || channel > 15) return kResultFalse;
        switch(cc) {
            case 1: id = kFirstParam + kMix; return kResultOk;
            case 7: id = kFirstParam + kOutputGain; return kResultOk;
            case 70: id = kFirstParam + kType; return kResultOk;
            case 71: id = kFirstParam + kResonance; return kResultOk;
            case 74: id = kFirstParam + kCutoff; return kResultOk;
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
        destroy(); rate_ = setup.sampleRate;
        dsp_ = mlafilter_create__f32(static_cast<float>(rate_));
        if(!dsp_) return kOutOfMemory;
        // About 20 ms mix/output glide.
        smooth_ = static_cast<float>(1 - std::exp(-1 / (0.02 * rate_)));
        applyAll(true); mix_ = mixTarget_; gain_ = gainTarget_;
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if(!state && dsp_) mlafilter_reset__ptr_struct_StereoMultimodeFilter(dsp_);
        return SingleComponentEffect::setActive(state);
    }
    uint32 PLUGIN_API getTailSamples() override { return static_cast<uint32>(rate_ * 0.2); }
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
        const bool bypass = plain(kBypass) != 0;
        bool silent = true;
        for(int32 i = 0; i < data.numSamples; ++i) {
            const float left = (in.silenceFlags & 1) ? 0.f : in.channelBuffers32[0][i];
            const float right = (in.silenceFlags & 2) ? 0.f : in.channelBuffers32[1][i];
            float wetLeft = left, wetRight = right;
            if(dsp_) wetLeft = mlafilter_process__ptr_struct_StereoMultimodeFilter_f32_f32_ptr_f32(dsp_, left, right, &wetRight);
            mix_ += smooth_ * (mixTarget_ - mix_);
            gain_ += smooth_ * (gainTarget_ - gain_);
            out.channelBuffers32[0][i] = bypass ? left : (left + (wetLeft - left) * mix_) * gain_;
            out.channelBuffers32[1][i] = bypass ? right : (right + (wetRight - right) * mix_) * gain_;
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
        applyAll(true); return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override {
        if(!state) return kResultFalse;
        IBStreamer stream(state, kLittleEndian);
        for(double value : norm_) if(!stream.writeDouble(value)) return kResultFalse;
        return kResultOk;
    }
private:
    StereoMultimodeFilter* dsp_ = nullptr;
    double rate_ = 44100;
    float mix_ = 1, mixTarget_ = 1, gain_ = 1, gainTarget_ = 1, smooth_ = 1;
    std::array<double, kCount> norm_{};
    double plain(int i) const { return physical(i, norm_[i]); }
    int64_t samples(double ms) const { return static_cast<int64_t>(rate_ * ms / 1000); }
    void destroy() { if(dsp_) { mlafilter_destroy__ptr_struct_StereoMultimodeFilter(dsp_); dsp_ = nullptr; } }
    void push(int i, bool immediate) {
        if(!dsp_) return;
        const int64_t glide = immediate ? 0 : samples(plain(kGlide));
        if(i == kType) mlafilter_set_model__ptr_struct_StereoMultimodeFilter_i32_i64(dsp_, static_cast<int>(plain(kType)), immediate ? 0 : samples(kModelFadeMs));
        else if(i == kCutoff) mlafilter_set_cutoff__ptr_struct_StereoMultimodeFilter_f32_i64(dsp_, static_cast<float>(plain(kCutoff)), glide);
        else if(i == kResonance) mlafilter_set_resonance__ptr_struct_StereoMultimodeFilter_f32_i64(dsp_, static_cast<float>(plain(kResonance)), glide);
        else if(i == kMix) mixTarget_ = static_cast<float>(plain(kMix));
        else if(i == kOutputGain) gainTarget_ = static_cast<float>(std::pow(10.0, plain(kOutputGain) / 20));
    }
    void applyAll(bool immediate) { for(int i = 0; i < kCount; ++i) push(i, immediate); }
    void applyOne(ParamID id, double value) {
        if(id < kFirstParam || id >= kFirstParam + kCount || !std::isfinite(value)) return;
        const int i = static_cast<int>(id - kFirstParam);
        norm_[i] = std::clamp(value, 0.0, 1.0);
        push(i, false);
    }
};
} // namespace mla_filter
#ifndef MLA_FILTER_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_filter::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla Filter", 0, PlugType::kFxFilter, "0.1.0", kVstVersionString,
    mla_filter::Processor::createInstance)
END_FACTORY
#endif
