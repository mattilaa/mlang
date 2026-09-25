// Exercise the actual VST3 processor and compiled MLang bridge together.
#define MLA_FILTER_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace mla_filter;
static void check(bool ok, const char* message) {
    if(!ok) { std::cerr << message << '\n'; std::exit(1); }
}
constexpr double kRate = 48000;
struct Fixture {
    Processor processor;
    Fixture() {
        check(processor.initialize(nullptr) == kResultOk, "initialize");
        ProcessSetup setup{kRealtime, kSample32, 8192, kRate};
        check(processor.setupProcessing(setup) == kResultOk, "setup");
        check(processor.setActive(true) == kResultOk, "activate");
    }
    ~Fixture() { processor.setActive(false); processor.terminate(); }
    void set(int i, double plainValue) {
        ParameterChanges changes;
        int32 qi = 0, pi = 0;
        auto* q = changes.addParameterData(kFirstParam + i, qi);
        q->addPoint(0, normalized(i, plainValue), pi);
        ProcessData data{}; data.inputParameterChanges = &changes;
        check(processor.process(data) == kResultOk, "parameter flush");
    }
    std::array<std::vector<float>, 2> render(const std::vector<float>& input, uint64 silence = 0) {
        std::vector<float> left = input, right = input;
        const int count = static_cast<int>(input.size());
        std::array<std::vector<float>, 2> out{std::vector<float>(count), std::vector<float>(count)};
        float* inputs[]{left.data(), right.data()}; float* outputs[]{out[0].data(), out[1].data()};
        AudioBusBuffers in{}, output{}; in.numChannels = output.numChannels = 2;
        in.channelBuffers32 = inputs; in.silenceFlags = silence; output.channelBuffers32 = outputs;
        ProcessData data{}; data.numSamples = count; data.symbolicSampleSize = kSample32;
        data.numInputs = data.numOutputs = 1; data.inputs = &in; data.outputs = &output;
        check(processor.process(data) == kResultOk, "render");
        for(const auto& channel : out) for(float v : channel) check(std::isfinite(v), "non-finite output");
        return out;
    }
    static std::vector<float> sine(double hz, int count = 24000) {
        std::vector<float> values(count);
        for(int i = 0; i < count; ++i) values[i] = static_cast<float>(0.25 * std::sin(2 * M_PI * hz * i / kRate));
        return values;
    }
    // Steady-state gain in dB, measured after ramps and crossfades settle.
    double gainDb(double hz) {
        auto input = sine(hz);
        auto out = render(input);
        double inSum = 0, outSum = 0;
        for(size_t i = input.size() / 2; i < input.size(); ++i) { inSum += input[i] * input[i]; outSum += out[0][i] * out[0][i]; }
        check(std::abs(out[0].back() - out[1].back()) < 1e-6, "stereo channels match");
        return 10 * std::log10(outSum / inSum);
    }
};
int main() {
    Fixture a;
    check(a.processor.getParameterCount() == kCount, "parameter count");
    for(int i = 0; i < kCount; ++i) {
        ParameterInfo info{};
        check(a.processor.getParameterInfo(i, info) == kResultOk, "parameter metadata");
        check(info.id == kFirstParam + i && (info.flags & ParameterInfo::kCanAutomate), "stable automatable ID");
        check(std::abs(a.processor.getParamNormalized(info.id) - normalized(i, specs[i].initial)) < 1e-9, "controller default");
        check(std::abs(physical(i, normalized(i, specs[i].initial)) - specs[i].initial) < 1e-6, "mapping round trip");
    }
    ParameterInfo typeInfo{};
    a.processor.getParameterInfo(kType, typeInfo);
    check(typeInfo.stepCount == kModels - 1, "eight filter types");

    // Default is Moog 24 at 2 kHz: passes 100 Hz, strongly stops 16 kHz.
    check(std::abs(a.gainDb(100)) < 0.5, "moog 24 pass band");
    check(a.gainDb(16000) < -40, "moog 24 stop band");

    // Every model responds on the correct side of a 1 kHz cutoff, and 24 dB
    // models are steeper than 12 dB ones.
    a.set(kCutoff, 1000);
    struct Expect { int model; double pass, stop; };
    const Expect expects[] = {
        {kLowpass12, 100, 4000}, {kLowpass24, 100, 4000}, {kMoog12, 100, 4000}, {kMoog24, 100, 4000},
        {kHighpass12, 8000, 250}, {kHighpass24, 8000, 250}, {kBandpass12, 1000, 8000}, {kBandpass24, 1000, 8000},
    };
    double stop12[3] = {};
    for(const auto& e : expects) {
        a.set(kType, e.model);
        const double pass = a.gainDb(e.pass), stop = a.gainDb(e.stop);
        check(pass > -1.5 && pass < 0.5, "model pass band");
        check(stop < pass - 10, "model stop band");
        const int family = e.model == kLowpass12 || e.model == kLowpass24 ? 0 : e.model == kHighpass12 || e.model == kHighpass24 ? 1 : 2;
        const bool twelve = e.model == kLowpass12 || e.model == kHighpass12 || e.model == kBandpass12;
        if(e.model <= kBandpass24) {
            if(twelve) stop12[family] = stop;
            else check(stop < stop12[family] - 6, "24 dB model is steeper");
        }
    }

    // Resonance peaks the cutoff.
    a.set(kType, kLowpass24); a.set(kResonance, 0);
    const double flat = a.gainDb(1000);
    a.set(kResonance, 12);
    check(a.gainDb(1000) > flat + 6, "resonance boosts the cutoff");
    a.set(kType, kMoog24); a.set(kResonance, 30);
    check(a.gainDb(1000) > a.gainDb(100), "moog resonance peaks");
    a.set(kResonance, 0);

    // Glide: a cutoff jump is ramped over the Glide time.
    Fixture glide; glide.set(kType, kLowpass12); glide.set(kCutoff, 200); glide.gainDb(100);
    glide.set(kGlide, 500); glide.set(kCutoff, 10000);
    auto input = Fixture::sine(3000, 2400);
    auto early = glide.render(input);
    double early3k = 0, in3k = 0;
    for(size_t i = 1200; i < input.size(); ++i) { early3k += early[0][i] * early[0][i]; in3k += input[i] * input[i]; }
    check(10 * std::log10(early3k / in3k) < -12, "cutoff still gliding after 50 ms");
    glide.gainDb(3000); // let the 500 ms glide finish
    check(glide.gainDb(3000) > -0.5, "glide reaches its target");

    // Type changes crossfade: no step larger than the signal allows.
    Fixture fade; fade.set(kType, kLowpass24); fade.set(kCutoff, 500);
    std::vector<float> dc(4800, 0.5f);
    fade.render(dc);
    fade.set(kType, kHighpass24);
    auto faded = fade.render(dc);
    float largest = 0;
    for(size_t i = 1; i < faded[0].size(); ++i) largest = std::max(largest, std::abs(faded[0][i] - faded[0][i - 1]));
    check(largest < 0.02f, "type change crossfades");
    check(std::abs(faded[0].back()) < 0.01f, "crossfade lands on the new type");

    // Mix, output gain, bypass, silent input.
    Fixture out; out.set(kType, kLowpass12); out.set(kCutoff, 200);
    out.set(kMix, 0);
    check(std::abs(out.gainDb(5000)) < 0.05, "mix 0 is dry");
    out.set(kMix, 1); out.set(kOutputGain, -6);
    check(std::abs(out.gainDb(50) + 6) < 0.3, "output gain");
    out.set(kBypass, 1);
    auto dry = out.render(Fixture::sine(5000, 64));
    check(dry[0][9] == Fixture::sine(5000, 64)[9], "bypass dry output");
    out.set(kBypass, 0);
    auto silenced = out.render(std::vector<float>(64, 1.f), 3);
    for(float v : silenced[0]) check(std::abs(v) < 0.01f, "honor silent input buffers");

    // Extreme settings stay finite for every model.
    for(int model = 0; model < kModels; ++model) {
        Fixture stress; stress.set(kType, model); stress.set(kResonance, 36);
        stress.set(kCutoff, 20000); stress.render(Fixture::sine(19000, 8192));
        stress.set(kCutoff, 20); stress.render(Fixture::sine(20, 8192));
    }

    // State round trip.
    MemoryStream state;
    a.set(kType, kBandpass24); a.set(kCutoff, 3300); a.set(kResonance, 9);
    check(a.processor.getState(&state) == kResultOk, "save state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    Fixture b;
    check(b.processor.setState(&state) == kResultOk, "restore state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kCutoff) - normalized(kCutoff, 3300)) < 1e-9, "restored cutoff");
    check(std::abs(b.gainDb(3300) - a.gainDb(3300)) < 0.1, "restored response");
    MemoryStream truncated;
    check(b.processor.setState(&truncated) != kResultOk, "reject truncated state");
    MemoryStream invalid; IBStreamer bad(&invalid, kLittleEndian);
    bad.writeDouble(std::numeric_limits<double>::quiet_NaN()); invalid.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&invalid) != kResultOk, "reject NaN state");
    std::cout << "Mla Filter processor tests passed\n";
}
