// Exercise the actual VST3 processor and compiled MLang bridge together.
#define MLA_LIMITER_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace mla_limiter;
static void check(bool ok, const char* message) {
    if(!ok) { std::cerr << message << '\n'; std::exit(1); }
}
struct Fixture {
    Processor processor;
    explicit Fixture(double rate = 48000) {
        check(processor.initialize(nullptr) == kResultOk, "initialize");
        ProcessSetup setup{kRealtime, kSample32, 8192, rate};
        check(processor.setupProcessing(setup) == kResultOk, "setup");
        check(processor.setActive(true) == kResultOk, "activate");
    }
    ~Fixture() { processor.setActive(false); processor.terminate(); }
    void set(int i, double plain) {
        ParameterChanges changes;
        int32 qi = 0, pi = 0;
        auto* q = changes.addParameterData(kFirstParam + i, qi);
        q->addPoint(0, normalized(i, plain), pi);
        ProcessData data{}; data.inputParameterChanges = &changes;
        check(processor.process(data) == kResultOk, "parameter flush");
    }
    std::array<std::vector<float>, 2> render(const std::vector<float>& left, const std::vector<float>& right, uint64 silence = 0,
                                             ParameterChanges* meter = nullptr) {
        const int count = static_cast<int>(left.size());
        std::vector<float> l(left), r(right);
        std::array<std::vector<float>, 2> out{std::vector<float>(count), std::vector<float>(count)};
        float* inputs[]{l.data(), r.data()}; float* outputs[]{out[0].data(), out[1].data()};
        AudioBusBuffers in{}, output{}; in.numChannels = output.numChannels = 2;
        in.channelBuffers32 = inputs; in.silenceFlags = silence; output.channelBuffers32 = outputs;
        ProcessData data{}; data.numSamples = count; data.symbolicSampleSize = kSample32;
        data.numInputs = data.numOutputs = 1; data.inputs = &in; data.outputs = &output;
        data.outputParameterChanges = meter;
        check(processor.process(data) == kResultOk, "render");
        for(const auto& channel : out) for(float v : channel) check(std::isfinite(v), "non-finite output");
        return out;
    }
    std::array<std::vector<float>, 2> render(const std::vector<float>& mono) { return render(mono, mono); }
};
// Two seconds of a 120 bpm beat: kick, snare on 2 and 4, bass and a pad.
static std::vector<float> beat(int count, double rate, float level = .6f) {
    std::vector<float> values(count);
    uint32 seed = 7;
    const int half = static_cast<int>(rate / 2);
    for(int i = 0; i < count; ++i) {
        const float t = static_cast<float>((i % half) / rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(seed >> 8) / 8388608.f - 1.f;
        const float kick = .9f * std::sin(6.2831853f * (50 + 120 * std::exp(-t / .03f)) * t) * std::exp(-t / .15f);
        const float snare = (i / half) % 2 ? .7f * noise * std::exp(-t / .06f) : 0.f;
        const float bed = .25f * std::sin(6.2831853f * 55 * i / static_cast<float>(rate)) + .1f * std::sin(6.2831853f * 440 * i / static_cast<float>(rate));
        values[i] = level * (kick + snare + bed);
    }
    return values;
}
static double peak(const std::vector<float>& values, int from = 0) {
    double p = 0; for(size_t i = from; i < values.size(); ++i) p = std::max(p, static_cast<double>(std::abs(values[i]))); return p;
}
static double power(const std::vector<float>& values, int from = 0) {
    double sum = 0; for(size_t i = from; i < values.size(); ++i) sum += values[i] * values[i]; return sum / (values.size() - from);
}
static double db(double ratio) { return 10 * std::log10(ratio); }
static double bessel(double x) { double s = 1, t = 1; for(int m = 1; m < 40; ++m) { t *= (x / 2 / m) * (x / 2 / m); s += t; } return s; }
// Reference true peak, independent of the plugin's detector: 32 points per
// sample from a 256-tap Kaiser-windowed sinc.
static double truePeak(const std::vector<float>& v, int from) {
    const int half = 128; double best = 0;
    for(int n = from; n + half < static_cast<int>(v.size()); ++n)
        for(int k = 0; k < 32; ++k) {
            const double t = n + k / 32.0; double sum = 0;
            for(int i = n - half + 1; i <= n + half; ++i) {
                const double d = t - i, x = d / half;
                if(i < 0 || std::abs(x) >= 1) continue;
                sum += v[i] * (d == 0 ? 1 : std::sin(M_PI * d) / (M_PI * d)) * bessel(9 * std::sqrt(1 - x * x)) / bessel(9);
            }
            best = std::max(best, std::abs(sum));
        }
    return best;
}
int main() {
    Fixture a;
    check(a.processor.getParameterCount() == kCount + 1, "parameter count (controls + meter)");
    for(int i = 0; i < kCount; ++i) {
        ParameterInfo info{};
        check(a.processor.getParameterInfo(i, info) == kResultOk, "parameter metadata");
        check(info.id == kFirstParam + i && (info.flags & ParameterInfo::kCanAutomate), "stable automatable ID");
        check(std::abs(a.processor.getParamNormalized(info.id) - normalized(i, specs[i].initial)) < 1e-9, "controller default");
    }
    ParameterInfo meterInfo{};
    a.processor.getParameterInfo(kCount, meterInfo);
    check(meterInfo.id == kReductionMeter && (meterInfo.flags & ParameterInfo::kIsReadOnly), "read-only reduction meter");
    const int latency = static_cast<int>(a.processor.getLatencySamples());
    check(latency == 72 + 2 * 24 + 1 + 24, "look-ahead, detectors, clipper and guard latency at 48 kHz");

    const int rate = 48000, length = 2 * rate;
    auto input = beat(length, rate);
    const double ceiling = std::pow(10.0, -0.3 / 20);
    // The ceiling holds for every mode, punch and fat, and threshold drive
    // makes it louder.
    for(int mode : {kMaximizer, kSmooth})
        for(double punch : {0.0, 0.5, 1.0})
            for(double fat : {0.0, 1.0}) {
                Fixture limiter; limiter.set(kMode, mode); limiter.set(kPunch, punch); limiter.set(kFat, fat); limiter.set(kThreshold, -12);
                auto out = limiter.render(input);
                check(peak(out[0]) <= ceiling + 1e-6 && peak(out[1]) <= ceiling + 1e-6, "output never exceeds the ceiling");
                check(db(power(out[0], rate / 2) / power(input, rate / 2)) > 4, "threshold drive makes it louder");
            }
    // True Peak: a quarter-rate sine sampled at 45 degrees peaks 3 dB between
    // its samples. Off, the samples obey the ceiling but the waveform does
    // not; on, the reconstructed waveform does too.
    {
        std::vector<float> sine(rate / 4);
        for(int i = 0; i < rate / 4; ++i) sine[i] = .9f * static_cast<float>(std::sin(M_PI / 2 * i + M_PI / 4));
        const double limit = std::pow(10.0, -1.0 / 20);
        for(int on : {0, 1}) {
            Fixture limiter; limiter.set(kTruePeak, on); limiter.set(kCeiling, -1); limiter.set(kThreshold, -9);
            auto out = limiter.render(sine);
            const double tp = truePeak(out[0], rate / 8);
            if(on) check(tp <= limit * 1.001, "true peak holds the ceiling between samples");
            else check(tp > limit * 1.25 && peak(out[0], rate / 8) <= limit + 1e-6, "sample-peak mode lets inter-sample peaks through");
        }
    }
    // Ceiling moves the output peak.
    {
        Fixture limiter; limiter.set(kCeiling, -6); limiter.set(kThreshold, -12);
        auto out = limiter.render(input);
        const double p = peak(out[0], rate / 2);
        check(p <= std::pow(10.0, -6.0 / 20) + 1e-6 && p > std::pow(10.0, -6.5 / 20), "ceiling -6 dB");
    }
    // Punch hands peaks to the clipper, so the limiter ducks the mix less.
    auto reduction = [&](double punch) {
        Fixture limiter; limiter.set(kPunch, punch); limiter.set(kThreshold, -12);
        auto out = limiter.render(input);
        return power(out[0], rate / 2);
    };
    check(db(reduction(1) / reduction(0)) > 0.5, "punch keeps the body louder");
    // Fat lifts the low end.
    {
        Fixture thin, fat; thin.set(kFat, 0); fat.set(kFat, 1);
        thin.set(kThreshold, 0); fat.set(kThreshold, 0); thin.set(kPunch, 1); fat.set(kPunch, 1);
        std::vector<float> bass(length);
        for(int i = 0; i < length; ++i) bass[i] = .2f * std::sin(6.2831853f * 50 * i / rate);
        check(db(power(fat.render(bass)[0], rate / 2) / power(thin.render(bass)[0], rate / 2)) > 2.5, "fat lifts the bass");
    }
    // Quiet material under the threshold is only delayed by the look-ahead.
    {
        Fixture clean; clean.set(kThreshold, 0); clean.set(kFat, 0); clean.set(kPunch, 0);
        std::vector<float> tone(rate);
        for(int i = 0; i < rate; ++i) tone[i] = .3f * std::sin(6.2831853f * 1000 * i / rate);
        auto out = clean.render(tone);
        // After the threshold's 10 ms glide from the default -6 dB.
        for(int i = rate / 4; i < rate; ++i) check(std::abs(out[0][i] - tone[i - latency] * static_cast<float>(ceiling)) < 1e-4f, "clean below the limit, scaled to the ceiling");
    }
    // The meter reports gain reduction, and none for silence.
    {
        Fixture limiter; limiter.set(kThreshold, -18);
        ParameterChanges meter;
        limiter.render(input, input, 0, &meter);
        check(limiter.processor.reduction() > 3, "reduction on a hot beat");
        check(meter.getParameterCount() == 1 && meter.getParameterData(0)->getParameterId() == kReductionMeter, "meter output");
        Fixture quiet; quiet.render(std::vector<float>(4800));
        check(quiet.processor.reduction() == 0, "no reduction in silence");
    }
    // Auto release recovers differently than a fixed release.
    {
        Fixture on, off; on.set(kThreshold, -12); off.set(kThreshold, -12); off.set(kAutoRelease, 0);
        check(on.render(input)[0] != off.render(input)[0], "auto release changes the envelope");
    }
    // Bypass is the input delayed by the latency.
    a.set(kBypass, 1);
    auto bypassed = a.render(input);
    for(int i = latency; i < 2000; ++i) check(bypassed[0][i] == input[i - latency], "bypass is the latency-aligned input");
    auto silenced = a.render(input, input, 3);
    check(peak(silenced[0], latency) == 0, "honor silent input buffers");

    MemoryStream state;
    Fixture b;
    a.set(kMode, kSmooth); a.set(kPunch, 0.8); a.set(kRelease, 250);
    check(a.processor.getState(&state) == kResultOk, "save state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&state) == kResultOk, "restore state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kPunch) - normalized(kPunch, 0.8)) < 1e-9, "restored punch");
    check(b.processor.getParamNormalized(kFirstParam + kMode) == 1, "restored mode");
    // A state saved before True Peak existed (nine values) still loads.
    {
        MemoryStream legacy; IBStreamer writer(&legacy, kLittleEndian);
        for(int i = 0; i < kTruePeak; ++i) writer.writeDouble(i == kPunch ? 0.9 : normalized(i, specs[i].initial));
        legacy.seek(0, IBStream::kIBSeekSet, nullptr);
        Fixture old;
        check(old.processor.setState(&legacy) == kResultOk, "load pre-True Peak state");
        check(std::abs(old.processor.getParamNormalized(kFirstParam + kPunch) - 0.9) < 1e-9, "legacy value restored");
        check(old.processor.getParamNormalized(kFirstParam + kTruePeak) == 1, "legacy state defaults True Peak on");
    }
    MemoryStream truncated;
    check(b.processor.setState(&truncated) != kResultOk, "reject truncated state");
    MemoryStream invalid; IBStreamer bad(&invalid, kLittleEndian);
    bad.writeDouble(std::numeric_limits<double>::quiet_NaN()); invalid.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&invalid) != kResultOk, "reject NaN state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kRelease) - normalized(kRelease, 250)) < 1e-9, "failed restore is atomic");

    // Extremes at the lowest and highest supported rates stay finite and
    // under the ceiling.
    for(double sr : {8000.0, 384000.0}) {
        Fixture stress(sr); stress.set(kThreshold, -30); stress.set(kPunch, 1); stress.set(kFat, 1);
        stress.set(kRelease, 1); stress.set(kAttack, 0.1); stress.set(kCeiling, 0);
        std::vector<float> loud(8192); for(int i = 0; i < 8192; ++i) loud[i] = (i % 97 < 48) ? 4.f : -4.f;
        for(int mode : {kMaximizer, kSmooth}) {
            stress.set(kMode, mode);
            auto out = stress.render(loud, loud);
            check(peak(out[0]) <= 1 && peak(out[1]) <= 1, "ceiling at extremes");
        }
    }
    std::cout << "Mla Limiter processor tests passed\n";
}
