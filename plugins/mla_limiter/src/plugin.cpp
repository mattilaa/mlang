// VST3 plumbing for the MLang punch maximizer; no DSP is duplicated here.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>

struct PunchLimiter;
extern "C" PunchLimiter* mlalimiter_create__f32(float);
extern "C" void mlalimiter_destroy__ptr_struct_PunchLimiter(PunchLimiter*);
extern "C" void mlalimiter_reset__ptr_struct_PunchLimiter(PunchLimiter*);
extern "C" long long mlalimiter_latency__ptr_struct_PunchLimiter(PunchLimiter*);
extern "C" float mlalimiter_reduction__ptr_struct_PunchLimiter(PunchLimiter*);
extern "C" void mlalimiter_set__ptr_struct_PunchLimiter_i32_f32(PunchLimiter*, int, float);
extern "C" float mlalimiter_process__ptr_struct_PunchLimiter_f32_f32_ptr_f32(PunchLimiter*, float, float, float*);
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_limiter {
static const FUID kProcessorUID(0x4D6C614C, 0x696D6974, 0x3F8D62A1, 0xC47E19B5);
// Stable, contiguous IDs are saved by mlacker sessions and presets.
enum Index {
    kMode = 0,
    kThreshold = 1,
    kCeiling = 2,
    kRelease = 3,
    kAutoRelease = 4,
    kPunch = 5,
    kFat = 6,
    kAttack = 7,
    kBypass = 8,
    kCount
};
constexpr ParamID kFirstParam = 100;
// Read-only meter, outside the saved range.
constexpr ParamID kReductionMeter = kFirstParam + kCount;
enum Mode { kMaximizer, kSmooth };
// Stepped values are whole numbers from low to high, as VST3 RangeParameter
// and StringListParameter index them.
struct Spec { const TChar* title; const TChar* unit; double low, high, initial; int steps; };
static const Spec specs[kCount] = {
    {STR16("Mode"), STR16(""), 0, 1, kMaximizer, 1},
    {STR16("Threshold"), STR16("dB"), -30, 0, -6, 0},
    {STR16("Ceiling"), STR16("dB"), -12, 0, -0.3, 0},
    {STR16("Release"), STR16("ms"), 1, 1000, 60, 0},
    {STR16("Auto Release"), STR16(""), 0, 1, 1, 1},
    {STR16("Punch"), STR16(""), 0, 1, 0.3, 0},
    {STR16("Fat"), STR16(""), 0, 1, 0.3, 0},
    {STR16("Attack"), STR16("ms"), 0.1, 50, 5, 0},
    {STR16("Bypass"), STR16(""), 0, 1, 0, 1},
};
constexpr double kMeterRangeDb = 24;
static double normalized(int i, double plain) { return (plain - specs[i].low) / (specs[i].high - specs[i].low); }
static double physical(int i, double norm) {
    const double value = specs[i].low + norm * (specs[i].high - specs[i].low);
    return specs[i].steps ? std::round(value) : value;
}
class Processor final : public SingleComponentEffect {
public:
    Processor() { for(int i = 0; i < kCount; ++i) norm_[i] = normalized(i, specs[i].initial); }
    ~Processor() override { destroy(); }
    static FUnknown* createInstance(void*) { return static_cast<IComponent*>(new Processor()); }
    tresult PLUGIN_API initialize(FUnknown* context) override {
        const auto result = SingleComponentEffect::initialize(context);
        if(result != kResultOk) return result;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        for(int i = 0; i < kCount; ++i) {
            if(i == kMode || i == kAutoRelease) {
                auto* parameter = new StringListParameter(specs[i].title, kFirstParam + i);
                if(i == kMode) { parameter->appendString(STR16("Maximizer")); parameter->appendString(STR16("Smooth")); }
                else { parameter->appendString(STR16("Off")); parameter->appendString(STR16("On")); }
                parameter->getInfo().defaultNormalizedValue = norm_[i];
                parameter->setNormalized(norm_[i]); parameters.addParameter(parameter);
            } else {
                int32 flags = ParameterInfo::kCanAutomate;
                if(i == kBypass) flags |= ParameterInfo::kIsBypass;
                parameters.addParameter(new RangeParameter(specs[i].title, kFirstParam + i, specs[i].unit,
                    specs[i].low, specs[i].high, specs[i].initial, specs[i].steps, flags));
            }
        }
        parameters.addParameter(new RangeParameter(STR16("Reduction"), kReductionMeter, STR16("dB"),
            0, kMeterRangeDb, 0, 0, ParameterInfo::kIsReadOnly));
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override { destroy(); return SingleComponentEffect::terminate(); }
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
        dsp_ = mlalimiter_create__f32(static_cast<float>(setup.sampleRate));
        if(!dsp_) return kOutOfMemory;
        applyAll(); mlalimiter_reset__ptr_struct_PunchLimiter(dsp_); return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if(!state && dsp_) mlalimiter_reset__ptr_struct_PunchLimiter(dsp_);
        return SingleComponentEffect::setActive(state);
    }
    // The 1.5 ms look-ahead; bypass is delayed by the same amount.
    uint32 PLUGIN_API getLatencySamples() override {
        return dsp_ ? static_cast<uint32>(mlalimiter_latency__ptr_struct_PunchLimiter(dsp_)) : 0;
    }
    uint32 PLUGIN_API getTailSamples() override { return getLatencySamples() + 1; }
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
        float deepest = 0;
        for(int32 i = 0; i < data.numSamples; ++i) {
            const float left = (in.silenceFlags & 1) ? 0.f : in.channelBuffers32[0][i];
            const float right = (in.silenceFlags & 2) ? 0.f : in.channelBuffers32[1][i];
            float outRight = right, outLeft = left;
            if(dsp_) {
                outLeft = mlalimiter_process__ptr_struct_PunchLimiter_f32_f32_ptr_f32(dsp_, left, right, &outRight);
                deepest = std::max(deepest, mlalimiter_reduction__ptr_struct_PunchLimiter(dsp_));
            }
            out.channelBuffers32[0][i] = outLeft;
            out.channelBuffers32[1][i] = outRight;
            if(outLeft != 0 || outRight != 0) silent = false;
        }
        out.silenceFlags = silent ? 3 : 0;
        reduction_ = deepest;
        if(data.outputParameterChanges) {
            int32 index = 0;
            if(auto* queue = data.outputParameterChanges->addParameterData(kReductionMeter, index))
                queue->addPoint(0, std::clamp(deepest / kMeterRangeDb, 0.0, 1.0), index);
        }
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
    // Deepest gain reduction in the last block, dB (for tests and meters).
    float reduction() const { return reduction_; }
private:
    PunchLimiter* dsp_ = nullptr;
    std::array<double, kCount> norm_{};
    float reduction_ = 0;
    double plain(int i) const { return physical(i, norm_[i]); }
    void destroy() { if(dsp_) { mlalimiter_destroy__ptr_struct_PunchLimiter(dsp_); dsp_ = nullptr; } }
    void pushControl(int i) {
        if(dsp_) mlalimiter_set__ptr_struct_PunchLimiter_i32_f32(dsp_, i, static_cast<float>(plain(i)));
    }
    void applyAll() { for(int i = 0; i < kCount; ++i) pushControl(i); }
    void applyOne(ParamID id, double value) {
        if(id < kFirstParam || id >= kFirstParam + kCount || !std::isfinite(value)) return;
        const int i = static_cast<int>(id - kFirstParam);
        norm_[i] = std::clamp(value, 0.0, 1.0);
        pushControl(i);
    }
};
} // namespace mla_limiter
#ifndef MLA_LIMITER_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_limiter::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla Limiter", 0, PlugType::kFxDynamics, "0.1.0", kVstVersionString,
    mla_limiter::Processor::createInstance)
END_FACTORY
#endif
