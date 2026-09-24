// VST3 plumbing for Mla EQ; filter design, smoothing and processing live in
// mla_eq_dsp.mla. This file owns the section pool and maps parameters to it.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>

struct EqSection;
extern "C" EqSection* mlaeq_section_create__f32(float);
extern "C" void mlaeq_section_destroy__ptr_struct_EqSection(EqSection*);
extern "C" void mlaeq_section_reset__ptr_struct_EqSection(EqSection*);
extern "C" void mlaeq_section_set__ptr_struct_EqSection_i32_f32_f32_f32_i32(EqSection*, int, float, float, float, int);
extern "C" void mlaeq_section_set_pass__ptr_struct_EqSection_i32_i32_i32_f32_f32_i32(EqSection*, int, int, int, float, float, int);
extern "C" int mlaeq_section_is_identity__ptr_struct_EqSection(EqSection*);
extern "C" float mlaeq_section_process__ptr_struct_EqSection_f32_f32_ptr_f32(EqSection*, float, float, float*);
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_eq {
static const FUID kProcessorUID(0x4D6C6145, 0x51A7B3C1, 0x9E2D4F60, 0x8B17C5D3);
constexpr int kBands = 8, kPassStages = 4, kBandParams = 4;
// Stable, contiguous IDs are saved by mlacker sessions and presets.
enum Index {
    kEqType = 0,   // 0 = 4 bands, 1 = 8 bands
    kOutputGain = 1,
    kBypass = 2,
    kHpSlope = 3,  // Off, 12, 24, 36, 48 dB/oct
    kHpFreq = 4,
    kHpQ = 5,
    kLpSlope = 6,
    kLpFreq = 7,
    kLpQ = 8,
    kBand1 = 9,    // per band: Type, Freq, Gain, Q
    kCount = kBand1 + kBands * kBandParams
};
enum BandParam { kType = 0, kFreq = 1, kGain = 2, kQ = 3 };
constexpr int bandParam(int band, int which) { return kBand1 + band * kBandParams + which; }
// Band type index doubles as the DSP section kind (0 off .. 4 notch).
enum BandType { kOff = 0, kBell = 1, kLowShelf = 2, kHighShelf = 3, kNotch = 4 };
constexpr ParamID kFirstParam = 100;
struct Spec { const TChar* title; const TChar* unit; double low, high, initial; int steps; bool log; };
#define MLA_EQ_BAND(n, type, freq, q) \
    {STR16("Band " #n " Type"), STR16(""), 0, 4, type, 4, false}, \
    {STR16("Band " #n " Freq"), STR16("Hz"), 20, 20000, freq, 0, true}, \
    {STR16("Band " #n " Gain"), STR16("dB"), -24, 24, 0, 0, false}, \
    {STR16("Band " #n " Q"), STR16(""), 0.1, 18, q, 0, true}
static const Spec specs[kCount] = {
    {STR16("EQ Type"), STR16(""), 0, 1, 0, 1, false},
    {STR16("Output"), STR16("dB"), -24, 24, 0, 0, false},
    {STR16("Bypass"), STR16(""), 0, 1, 0, 1, false},
    {STR16("HP Slope"), STR16(""), 0, 4, 0, 4, false},
    {STR16("HP Freq"), STR16("Hz"), 20, 20000, 30, 0, true},
    {STR16("HP Q"), STR16(""), 0.1, 18, 0.7071, 0, true},
    {STR16("LP Slope"), STR16(""), 0, 4, 0, 4, false},
    {STR16("LP Freq"), STR16("Hz"), 20, 20000, 18000, 0, true},
    {STR16("LP Q"), STR16(""), 0.1, 18, 0.7071, 0, true},
    MLA_EQ_BAND(1, kLowShelf, 80, 0.7071),
    MLA_EQ_BAND(2, kBell, 400, 1),
    MLA_EQ_BAND(3, kBell, 2500, 1),
    MLA_EQ_BAND(4, kHighShelf, 10000, 0.7071),
    MLA_EQ_BAND(5, kBell, 150, 1),
    MLA_EQ_BAND(6, kBell, 800, 1),
    MLA_EQ_BAND(7, kBell, 1500, 1),
    MLA_EQ_BAND(8, kBell, 5000, 1),
};
#undef MLA_EQ_BAND
// Frequencies and Q use a logarithmic normalized mapping so the 0..1 editor
// range spends equal travel per octave.
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
            const bool bandType = i >= kBand1 && (i - kBand1) % kBandParams == kType;
            if(i == kEqType || i == kHpSlope || i == kLpSlope || bandType) {
                auto* parameter = new StringListParameter(specs[i].title, kFirstParam + i);
                if(i == kEqType) { parameter->appendString(STR16("4 Band")); parameter->appendString(STR16("8 Band")); }
                if(i == kHpSlope || i == kLpSlope) {
                    for(const TChar* name : {STR16("Off"), STR16("12 dB"), STR16("24 dB"), STR16("36 dB"), STR16("48 dB")})
                        parameter->appendString(name);
                }
                if(bandType) {
                    for(const TChar* name : {STR16("Off"), STR16("Bell"), STR16("Low Shelf"), STR16("High Shelf"), STR16("Notch")})
                        parameter->appendString(name);
                }
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
            case 7: id = kFirstParam + kOutputGain; return kResultOk;
            case 71: id = kFirstParam + kLpQ; return kResultOk;
            case 74: id = kFirstParam + kLpFreq; return kResultOk;
            case 75: id = kFirstParam + kHpFreq; return kResultOk;
            case 76: id = kFirstParam + kHpQ; return kResultOk;
            case 80: id = kFirstParam + kEqType; return kResultOk;
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
        for(auto& section : sections_) {
            section = mlaeq_section_create__f32(static_cast<float>(rate_));
            if(!section) { destroy(); return kOutOfMemory; }
        }
        // About 20 ms output-gain glide.
        gainSmooth_ = static_cast<float>(1 - std::exp(-1 / (0.02 * rate_)));
        applyAll(); gain_ = gainTarget_;
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if(!state) reset();
        return SingleComponentEffect::setActive(state);
    }
    uint32 PLUGIN_API getTailSamples() override { return static_cast<uint32>(rate_ * 0.1); }
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
        // Chain order: high-pass stages, bands, low-pass stages. Settled 0 dB
        // bands and unused sections are skipped for this block.
        std::array<EqSection*, kBands + 2 * kPassStages> chain{};
        int count = 0;
        const int bands = plain(kEqType) != 0 ? kBands : kBands / 2;
        for(int s = 0; s < plain(kHpSlope); ++s) chain[count++] = stage(true, s);
        for(int b = 0; b < bands; ++b) if(band(b) && !mlaeq_section_is_identity__ptr_struct_EqSection(band(b))) chain[count++] = band(b);
        for(int s = 0; s < plain(kLpSlope); ++s) chain[count++] = stage(false, s);
        const bool bypass = plain(kBypass) != 0;
        bool silent = true;
        for(int32 i = 0; i < data.numSamples; ++i) {
            const float left = (in.silenceFlags & 1) ? 0.f : in.channelBuffers32[0][i];
            const float right = (in.silenceFlags & 2) ? 0.f : in.channelBuffers32[1][i];
            float wetLeft = left, wetRight = right;
            for(int s = 0; s < count && chain[s]; ++s)
                wetLeft = mlaeq_section_process__ptr_struct_EqSection_f32_f32_ptr_f32(chain[s], wetLeft, wetRight, &wetRight);
            gain_ += gainSmooth_ * (gainTarget_ - gain_);
            out.channelBuffers32[0][i] = bypass ? left : wetLeft * gain_;
            out.channelBuffers32[1][i] = bypass ? right : wetRight * gain_;
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
    // Bands first, then high-pass and low-pass stages.
    std::array<EqSection*, kBands + 2 * kPassStages> sections_{};
    double rate_ = 44100;
    float gain_ = 1, gainTarget_ = 1, gainSmooth_ = 1;
    std::array<double, kCount> norm_{};
    double plain(int i) const { return physical(i, norm_[i]); }
    EqSection* band(int b) const { return sections_[b]; }
    EqSection* stage(bool highpass, int s) const { return sections_[kBands + (highpass ? 0 : kPassStages) + s]; }
    void destroy() {
        for(auto& section : sections_) if(section) { mlaeq_section_destroy__ptr_struct_EqSection(section); section = nullptr; }
    }
    void reset() {
        for(auto* section : sections_) if(section) mlaeq_section_reset__ptr_struct_EqSection(section);
    }
    void pushBand(int band, bool immediate) {
        EqSection* section = this->band(band);
        if(!section) return;
        // Bands 5-8 only run in 8-band mode; their settings are kept meanwhile.
        const int kind = band >= kBands / 2 && plain(kEqType) == 0 ? kOff : static_cast<int>(plain(bandParam(band, kType)));
        mlaeq_section_set__ptr_struct_EqSection_i32_f32_f32_f32_i32(section, kind,
            static_cast<float>(plain(bandParam(band, kFreq))), static_cast<float>(plain(bandParam(band, kGain))),
            static_cast<float>(plain(bandParam(band, kQ))), immediate);
    }
    void pushPass(bool highpass, bool immediate) {
        const int used = static_cast<int>(plain(highpass ? kHpSlope : kLpSlope));
        const float freq = static_cast<float>(plain(highpass ? kHpFreq : kLpFreq));
        const float q = static_cast<float>(plain(highpass ? kHpQ : kLpQ));
        for(int s = 0; s < kPassStages; ++s) {
            EqSection* section = stage(highpass, s);
            if(!section) continue;
            if(s < used) mlaeq_section_set_pass__ptr_struct_EqSection_i32_i32_i32_f32_f32_i32(section, highpass, used, s, freq, q, immediate);
            else mlaeq_section_set__ptr_struct_EqSection_i32_f32_f32_f32_i32(section, kOff, freq, 0, q, 1);
        }
    }
    void pushOutput() { gainTarget_ = static_cast<float>(std::pow(10.0, plain(kOutputGain) / 20)); }
    void applyAll() {
        for(int b = 0; b < kBands; ++b) pushBand(b, true);
        pushPass(true, true); pushPass(false, true); pushOutput();
    }
    void applyOne(ParamID id, double value) {
        if(id < kFirstParam || id >= kFirstParam + kCount || !std::isfinite(value)) return;
        const int i = static_cast<int>(id - kFirstParam);
        norm_[i] = std::clamp(value, 0.0, 1.0);
        if(i == kEqType) for(int b = kBands / 2; b < kBands; ++b) pushBand(b, false);
        else if(i == kOutputGain) pushOutput();
        else if(i >= kHpSlope && i <= kHpQ) pushPass(true, false);
        else if(i >= kLpSlope && i <= kLpQ) pushPass(false, false);
        else if(i >= kBand1) pushBand((i - kBand1) / kBandParams, false);
    }
};
} // namespace mla_eq
#ifndef MLA_EQ_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_eq::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla EQ", 0, PlugType::kFxEQ, "0.1.0", kVstVersionString,
    mla_eq::Processor::createInstance)
END_FACTORY
#endif
