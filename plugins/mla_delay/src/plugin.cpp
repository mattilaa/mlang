// VST3 plumbing for the shared MLang stereo delay; no DSP is duplicated here.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>

struct StereoDelay;
extern "C" StereoDelay* mladelay_create__f32(float);
extern "C" void mladelay_destroy__ptr_struct_StereoDelay(StereoDelay*);
extern "C" void mladelay_reset__ptr_struct_StereoDelay(StereoDelay*);
extern "C" void mladelay_set__ptr_struct_StereoDelay_i32_f32_i64(StereoDelay*, int, float, int64_t);
extern "C" float mladelay_process__ptr_struct_StereoDelay_f32_f32_ptr_f32(StereoDelay*, float, float, float*);
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_delay {
static const FUID kProcessorUID(0x4D6C6144, 0x656C6179, 0xAC7E4231, 0xB2965F08);
// Stable, contiguous IDs are saved by mlacker sessions and presets.
enum Index {
    kMode = 0,
    kDelay = 1,
    kFeedback = 2,
    kMix = 3,
    kFilter = 4,
    kScope = 5,
    kCutoff = 6,
    kResonance = 7,
    kDamping = 8,
    kJitter = 9,
    kTempo = 10,
    kBpm = 11,
    kBeats = 12,
    kDelayRamp = 13,
    kJitterRamp = 14,
    kMixRamp = 15,
    kFilterRamp = 16,
    kCutoffRamp = 17,
    kResonanceRamp = 18,
    kDampingRamp = 19,
    kReset = 20,
    kBypass = 21,
    kCount
};
constexpr ParamID kFirstParam = 100;
// Filter list index -> dsp::delay::FeedbackFilter value. The list is None
// followed by Mla Filter's eight types, in its order, all running the same
// dsp::multimode models.
enum FilterType { kNone, kLowpass12, kLowpass24, kHighpass12, kHighpass24, kBandpass12, kBandpass24, kMoog12, kMoog24, kFilterCount };
static const int kFilterTypes[kFilterCount] = {0, 9, 4, 10, 5, 11, 6, 7, 8};
struct Spec { const TChar* title; const TChar* unit; double low, high, initial; int steps; };
static const Spec specs[kCount] = {
    {STR16("Mode"), STR16(""), 0, 1, 1, 1},
    {STR16("Delay"), STR16("ms"), 1, 5000, 375, 0},
    {STR16("Feedback"), STR16(""), 0, 1.2, 0.58, 0},
    {STR16("Mix"), STR16(""), 0, 1, 0.6, 0},
    {STR16("Filter"), STR16(""), 0, kFilterCount - 1, kLowpass12, kFilterCount - 1},
    {STR16("Filter Scope"), STR16(""), 0, 1, 1, 1},
    {STR16("Cutoff"), STR16("Hz"), 20, 20000, 4200, 0},
    {STR16("Resonance"), STR16(""), 0, 1, 0.25, 0},
    {STR16("Damping"), STR16(""), 0, 1, 0.75, 0},
    {STR16("Jitter"), STR16("ms"), 0, 50, 0, 0},
    {STR16("Tempo Source"), STR16(""), 0, 2, 0, 2},
    {STR16("BPM"), STR16("BPM"), 20, 400, 120, 0},
    {STR16("Beats"), STR16("beats"), 0.0625, 16, 0.75, 0},
    {STR16("Delay Ramp"), STR16("ms"), 0, 2000, 120, 0},
    {STR16("Jitter Ramp"), STR16("ms"), 0, 2000, 120, 0},
    {STR16("Mix Ramp"), STR16("ms"), 0, 2000, 80, 0},
    {STR16("Filter Ramp"), STR16("ms"), 0, 2000, 80, 0},
    {STR16("Cutoff Ramp"), STR16("ms"), 0, 2000, 80, 0},
    {STR16("Resonance Ramp"), STR16("ms"), 0, 2000, 80, 0},
    {STR16("Damping Ramp"), STR16("ms"), 0, 2000, 80, 0},
    {STR16("Reset Loop"), STR16(""), 0, 1, 0, 1},
    {STR16("Bypass"), STR16(""), 0, 1, 0, 1},
};
static double normalized(int i, double plain) { return (plain - specs[i].low) / (specs[i].high - specs[i].low); }
static double physical(int i, double norm) {
    double value = specs[i].low + norm * (specs[i].high - specs[i].low);
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
            if(i == kMode || i == kFilter || i == kScope || i == kTempo) {
                auto* parameter = new StringListParameter(specs[i].title, kFirstParam + i);
                if(i == kMode) { parameter->appendString(STR16("Forward")); parameter->appendString(STR16("Ping-Pong")); }
                if(i == kFilter) {
                    for(const TChar* name : {STR16("None"), STR16("Lowpass 12"), STR16("Lowpass 24"), STR16("Highpass 12"), STR16("Highpass 24"),
                                             STR16("Bandpass 12"), STR16("Bandpass 24"), STR16("Moog 12"), STR16("Moog 24")})
                        parameter->appendString(name);
                }
                if(i == kScope) { parameter->appendString(STR16("Feedback")); parameter->appendString(STR16("Delay")); }
                if(i == kTempo) { parameter->appendString(STR16("Free")); parameter->appendString(STR16("Manual")); parameter->appendString(STR16("Host")); }
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
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID& id) override {
        if(bus != 0 || channel < 0 || channel > 15) return kResultFalse;
        switch(cc) {
            case 1: id = kFirstParam + kMix; return kResultOk;
            case 12: id = kFirstParam + kDelay; return kResultOk;
            case 71: id = kFirstParam + kResonance; return kResultOk;
            case 74: id = kFirstParam + kCutoff; return kResultOk;
            case 91: id = kFirstParam + kFeedback; return kResultOk;
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
        destroy(); rate_ = setup.sampleRate; hostBpm_ = 0;
        dsp_ = mladelay_create__f32(static_cast<float>(rate_));
        if(!dsp_) return kOutOfMemory;
        applyAll(); return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if(!state && dsp_) mladelay_reset__ptr_struct_StereoDelay(dsp_);
        return SingleComponentEffect::setActive(state);
    }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }
    tresult PLUGIN_API process(ProcessData& data) override {
        // Follow valid host tempo; mlacker currently falls back to the BPM knob.
        const auto* context = data.processContext;
        const double tempo = context && (context->state & ProcessContext::kTempoValid) && std::isfinite(context->tempo) && context->tempo > 0 ? context->tempo : 0;
        if(tempo != hostBpm_) { hostBpm_ = tempo; if(plain(kTempo) == 2) pushDelay(false); }
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
        for(int32 i = 0; i < data.numSamples; ++i) {
            const float left = (in.silenceFlags & 1) ? 0.f : in.channelBuffers32[0][i];
            const float right = (in.silenceFlags & 2) ? 0.f : in.channelBuffers32[1][i];
            float wetRight = right, wetLeft = left;
            if(dsp_) wetLeft = mladelay_process__ptr_struct_StereoDelay_f32_f32_ptr_f32(dsp_, left, right, &wetRight);
            const bool bypass = plain(kBypass) != 0;
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
    StereoDelay* dsp_ = nullptr;
    double rate_ = 44100, hostBpm_ = 0;
    std::array<double, kCount> norm_{};
    double plain(int i) const { return physical(i, norm_[i]); }
    int64_t ramp(int i) const { return static_cast<int64_t>(rate_ * plain(i) / 1000); }
    void destroy() { if(dsp_) { mladelay_destroy__ptr_struct_StereoDelay(dsp_); dsp_ = nullptr; } }
    void push(int i, double value, int64_t samples = 0) {
        if(dsp_) mladelay_set__ptr_struct_StereoDelay_i32_f32_i64(dsp_, i, static_cast<float>(value), samples);
    }
    void pushDelay(bool immediate) {
        double ms = plain(kDelay);
        if(plain(kTempo) != 0) {
            const double bpm = plain(kTempo) == 2 && hostBpm_ > 0 ? hostBpm_ : plain(kBpm);
            ms = std::clamp(60000 * plain(kBeats) / bpm, 1.0, 5000.0);
        }
        push(kDelay, ms, immediate ? 0 : ramp(kDelayRamp));
    }
    void pushControl(int i, bool immediate) {
        int r = -1;
        switch(i) {
            case kMix: r = kMixRamp; break;
            case kFilter: r = kFilterRamp; break;
            case kCutoff: r = kCutoffRamp; break;
            case kResonance: r = kResonanceRamp; break;
            case kDamping: r = kDampingRamp; break;
            case kJitter: r = kJitterRamp; break;
        }
        const double value = i == kFilter ? kFilterTypes[static_cast<int>(plain(kFilter))] : plain(i);
        push(i, value, immediate || r < 0 ? 0 : ramp(r));
    }
    void applyAll() {
        for(int i = 0; i <= kJitter; ++i) if(i != kDelay) pushControl(i, true);
        pushDelay(true);
    }
    void applyOne(ParamID id, double value) {
        if(id < kFirstParam || id >= kFirstParam + kCount || !std::isfinite(value)) return;
        const int i = static_cast<int>(id - kFirstParam);
        value = std::clamp(value, 0.0, 1.0);
        const double before = norm_[i]; norm_[i] = value;
        if(i == kReset && before < .5 && value >= .5) {
            if(dsp_) mladelay_reset__ptr_struct_StereoDelay(dsp_);
        } else if(i == kDelay || i == kTempo || i == kBpm || i == kBeats) pushDelay(false);
        else if(i <= kJitter) pushControl(i, false);
    }
};
} // namespace mla_delay
#ifndef MLA_DELAY_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_delay::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla Delay", 0, PlugType::kFxDelay, "0.1.0", kVstVersionString,
    mla_delay::Processor::createInstance)
END_FACTORY
#endif
