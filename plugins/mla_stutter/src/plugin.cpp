// VST3 plumbing for the MLang tempo-locked stutter; no DSP is duplicated here.
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

struct StereoStutter;
extern "C" StereoStutter* mlastutter_create__f32(float);
extern "C" void mlastutter_destroy__ptr_struct_StereoStutter(StereoStutter*);
extern "C" void mlastutter_reset__ptr_struct_StereoStutter(StereoStutter*);
extern "C" void mlastutter_set__ptr_struct_StereoStutter_i32_f64_f64(StereoStutter*, int, double, double);
extern "C" void mlastutter_sync__ptr_struct_StereoStutter_f64_f64_i32(StereoStutter*, double, double, int);
extern "C" int mlastutter_active__ptr_struct_StereoStutter(StereoStutter*);
extern "C" float mlastutter_process__ptr_struct_StereoStutter_f32_f32_ptr_f32(StereoStutter*, float, float, float*);
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_stutter {
static const FUID kProcessorUID(0x4D6C6153, 0x74757474, 0x9E2B41C7, 0x8A53D604);
// Stable, contiguous IDs are saved by mlacker sessions and presets.
enum Index {
    kMode = 0,
    kWidth = 1,
    kPeriod = 2,
    kHold = 3,
    kGate = 4,
    kFade = 5,
    kMix = 6,
    kTempo = 7,
    kBpm = 8,
    kBypass = 9,
    kCount
};
constexpr ParamID kFirstParam = 100;
enum Mode { kOff, kAuto, kOn };
enum TempoSource { kHost, kManual };
// Width list, straight and triplet note values, as quarter-note beats.
constexpr int kWidthCount = 10;
static const TChar* const kWidthNames[kWidthCount] = {
    STR16("1/1"), STR16("1/2"), STR16("1/4"), STR16("1/8"), STR16("1/8T"),
    STR16("1/16"), STR16("1/16T"), STR16("1/32"), STR16("1/32T"), STR16("1/64")};
static const double kWidthBeats[kWidthCount] = {4, 2, 1, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125, 1.0 / 12, 0.0625};
// Auto hold: how many beats at the end of each period repeat.
constexpr int kHoldCount = 11;
static const TChar* const kHoldNames[kHoldCount] = {
    STR16("Off"), STR16("1/4 beat"), STR16("1/2 beat"), STR16("1 beat"), STR16("2 beats"), STR16("3 beats"),
    STR16("4 beats"), STR16("6 beats"), STR16("8 beats"), STR16("12 beats"), STR16("16 beats")};
static const double kHoldBeats[kHoldCount] = {0, 0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16};
// Stepped values are whole numbers from low to high, as VST3 RangeParameter
// and StringListParameter index them.
struct Spec { const TChar* title; const TChar* unit; double low, high, initial; int steps; };
static const Spec specs[kCount] = {
    {STR16("Stutter"), STR16(""), 0, 2, kAuto, 2},
    {STR16("Width"), STR16(""), 0, kWidthCount - 1, 5, kWidthCount - 1},
    {STR16("Every"), STR16("beats"), 1, 16, 4, 15},
    {STR16("Hold"), STR16(""), 0, kHoldCount - 1, 3, kHoldCount - 1},
    {STR16("Gate"), STR16(""), 0.05, 1, 1, 0},
    {STR16("Fade"), STR16("ms"), 0, 20, 3, 0},
    {STR16("Mix"), STR16(""), 0, 1, 1, 0},
    {STR16("Tempo Source"), STR16(""), 0, 1, kHost, 1},
    {STR16("BPM"), STR16("BPM"), 20, 400, 120, 0},
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
            if(i == kMode || i == kWidth || i == kHold || i == kTempo) {
                auto* parameter = new StringListParameter(specs[i].title, kFirstParam + i);
                if(i == kMode) for(const TChar* name : {STR16("Off"), STR16("Auto"), STR16("On")}) parameter->appendString(name);
                if(i == kWidth) for(const TChar* name : kWidthNames) parameter->appendString(name);
                if(i == kHold) for(const TChar* name : kHoldNames) parameter->appendString(name);
                if(i == kTempo) { parameter->appendString(STR16("Host")); parameter->appendString(STR16("Manual")); }
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
    // Sustain pedal down = On, up = Off; the mod wheel sweeps Width.
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID& id) override {
        if(bus != 0 || channel < 0 || channel > 15) return kResultFalse;
        switch(cc) {
            case 1: id = kFirstParam + kWidth; return kResultOk;
            case 64: id = kFirstParam + kMode; return kResultOk;
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
        dsp_ = mlastutter_create__f32(static_cast<float>(setup.sampleRate));
        if(!dsp_) return kOutOfMemory;
        applyAll(); return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if(!state && dsp_) mlastutter_reset__ptr_struct_StereoStutter(dsp_);
        return SingleComponentEffect::setActive(state);
    }
    // A held slice keeps repeating after the input goes quiet.
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }
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
        syncTransport(data.processContext);
        bool silent = true;
        const bool bypass = plain(kBypass) != 0;
        for(int32 i = 0; i < data.numSamples; ++i) {
            const float left = (in.silenceFlags & 1) ? 0.f : in.channelBuffers32[0][i];
            const float right = (in.silenceFlags & 2) ? 0.f : in.channelBuffers32[1][i];
            float wetRight = right, wetLeft = left;
            if(dsp_) wetLeft = mlastutter_process__ptr_struct_StereoStutter_f32_f32_ptr_f32(dsp_, left, right, &wetRight);
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
    bool stuttering() const { return dsp_ && mlastutter_active__ptr_struct_StereoStutter(dsp_) != 0; }
private:
    StereoStutter* dsp_ = nullptr;
    std::array<double, kCount> norm_{};
    double plain(int i) const { return physical(i, norm_[i]); }
    void destroy() { if(dsp_) { mlastutter_destroy__ptr_struct_StereoStutter(dsp_); dsp_ = nullptr; } }
    void push(int i, double value, double extra = 0) {
        if(dsp_) mlastutter_set__ptr_struct_StereoStutter_i32_f64_f64(dsp_, i, value, extra);
    }
    // Host: follow the host tempo, and its beat position while it plays, so
    // slices line up with the song. Manual (or no host tempo): the BPM knob
    // and a free-running grid.
    void syncTransport(const ProcessContext* context) {
        if(!dsp_) return;
        const bool host = plain(kTempo) == kHost && context;
        const bool tempoValid = host && (context->state & ProcessContext::kTempoValid) && std::isfinite(context->tempo) && context->tempo > 0;
        const bool follow = tempoValid && (context->state & ProcessContext::kProjectTimeMusicValid) &&
            (context->state & ProcessContext::kPlaying) && std::isfinite(context->projectTimeMusic);
        mlastutter_sync__ptr_struct_StereoStutter_f64_f64_i32(dsp_, tempoValid ? context->tempo : plain(kBpm),
            follow ? context->projectTimeMusic : 0, follow ? 1 : 0);
    }
    void pushControl(int i) {
        switch(i) {
            case kMode: push(kMode, plain(kMode)); break;
            case kWidth: push(kWidth, kWidthBeats[static_cast<int>(plain(kWidth))]); break;
            case kPeriod: case kHold: push(kPeriod, plain(kPeriod), kHoldBeats[static_cast<int>(plain(kHold))]); break;
            case kGate: case kFade: case kMix: push(i, plain(i)); break;
        }
    }
    void applyAll() { for(int i = 0; i <= kMix; ++i) pushControl(i); }
    void applyOne(ParamID id, double value) {
        if(id < kFirstParam || id >= kFirstParam + kCount || !std::isfinite(value)) return;
        const int i = static_cast<int>(id - kFirstParam);
        norm_[i] = std::clamp(value, 0.0, 1.0);
        pushControl(i);
    }
};
} // namespace mla_stutter
#ifndef MLA_STUTTER_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_stutter::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla Stutter", 0, PlugType::kFx, "0.1.0", kVstVersionString,
    mla_stutter::Processor::createInstance)
END_FACTORY
#endif
