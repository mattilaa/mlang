// Exercise the actual VST3 processor and compiled MLang bridge together.
#define MLA_JUNO_CHORUS_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace mla_juno_chorus;
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
    std::array<std::vector<float>, 2> render(const std::vector<float>& left, const std::vector<float>& right, uint64 silence = 0) {
        const int count = static_cast<int>(left.size());
        std::vector<float> l(left), r(right);
        std::array<std::vector<float>, 2> out{std::vector<float>(count), std::vector<float>(count)};
        float* inputs[]{l.data(), r.data()}; float* outputs[]{out[0].data(), out[1].data()};
        AudioBusBuffers in{}, output{}; in.numChannels = output.numChannels = 2;
        in.channelBuffers32 = inputs; in.silenceFlags = silence; output.channelBuffers32 = outputs;
        ProcessData data{}; data.numSamples = count; data.symbolicSampleSize = kSample32;
        data.numInputs = data.numOutputs = 1; data.inputs = &in; data.outputs = &output;
        check(processor.process(data) == kResultOk, "render");
        for(const auto& channel : out) for(float v : channel) check(std::isfinite(v), "non-finite output");
        return out;
    }
};
static std::vector<float> saw(int count, float level = .5f) {
    std::vector<float> values(count);
    for(int i = 0; i < count; ++i) values[i] = level * (static_cast<float>(i % 109) / 54.5f - 1.f);
    return values;
}
static double energy(const std::vector<float>& values) { double sum = 0; for(float v : values) sum += std::abs(v); return sum; }
int main() {
    Fixture a;
    check(a.processor.getParameterCount() == kCount, "parameter count");
    for(int i = 0; i < kCount; ++i) {
        ParameterInfo info{};
        check(a.processor.getParameterInfo(i, info) == kResultOk, "parameter metadata");
        check(info.id == kFirstParam + i && (info.flags & ParameterInfo::kCanAutomate), "stable automatable ID");
        check(std::abs(a.processor.getParamNormalized(info.id) - normalized(i, specs[i].initial)) < 1e-9, "controller default");
    }
    ParameterInfo modeInfo{};
    a.processor.getParameterInfo(kMode, modeInfo);
    check(modeInfo.stepCount == 3, "Off, I, II, I+II");

    // Off (after its 20 ms fade) is the dry signal.
    Fixture off; off.set(kMode, kOff);
    auto input = saw(9600), silence = std::vector<float>(9600);
    auto dry = off.render(input, input);
    for(int i = 2000; i < 9600; ++i) check(dry[0][i] == input[i] && dry[1][i] == input[i], "off passes dry input");

    // Each mode turns a mono input into two different, chorused channels.
    for(int mode : {kOne, kTwo, kBoth}) {
        Fixture chorus; chorus.set(kMode, mode);
        auto wide = chorus.render(input, input);
        double spread = 0; for(int i = 4800; i < 9600; ++i) spread += std::abs(wide[0][i] - wide[1][i]);
        check(spread > 10, "chorus widens a mono input");
    }
    // Width 0 folds the taps back to mono.
    Fixture narrow; narrow.set(kWidth, 0);
    auto mono = narrow.render(input, input);
    for(int i = 0; i < 9600; ++i) check(std::abs(mono[0][i] - mono[1][i]) < 1e-6f, "width 0 is mono");

    // Noise: silence in, hiss out only while Noise is on and the chorus runs.
    Fixture hiss;
    auto quiet = hiss.render(silence, silence);
    check(energy(quiet[0]) == 0 && energy(quiet[1]) == 0, "no hiss by default");
    hiss.set(kNoise, 1);
    auto noisy = hiss.render(silence, silence);
    check(energy(noisy[0]) > .05 && energy(noisy[1]) > .05, "noise toggle adds hiss");
    check(hiss.processor.getTailSamples() == kInfiniteTail, "hiss has an endless tail");
    hiss.set(kNoiseLevel, -90);
    auto faint = hiss.render(silence, silence);
    check(energy(faint[0]) < energy(noisy[0]) / 10, "noise level (28 dB lower)");
    hiss.set(kNoiseLevel, -62); hiss.set(kMode, kOff);
    hiss.render(silence, silence);
    auto stopped = hiss.render(silence, silence);
    check(energy(stopped[0]) == 0, "hiss stops with the chorus");

    a.set(kBypass, 1);
    auto bypassed = a.render(input, input);
    check(bypassed[0][500] == input[500], "bypass dry output");
    auto silenced = a.render(input, input, 3);
    check(energy(silenced[0]) == 0, "honor silent input buffers");

    MemoryStream state;
    Fixture b;
    a.set(kMode, kBoth); a.set(kTone, 6500); a.set(kNoise, 1);
    check(a.processor.getState(&state) == kResultOk, "save state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&state) == kResultOk, "restore state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kTone) - normalized(kTone, 6500)) < 1e-9, "restored tone");
    check(b.processor.getParamNormalized(kFirstParam + kNoise) == 1, "restored noise");
    MemoryStream truncated;
    check(b.processor.setState(&truncated) != kResultOk, "reject truncated state");
    MemoryStream invalid; IBStreamer bad(&invalid, kLittleEndian);
    bad.writeDouble(std::numeric_limits<double>::quiet_NaN()); invalid.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&invalid) != kResultOk, "reject NaN state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kTone) - normalized(kTone, 6500)) < 1e-9, "failed restore is atomic");

    // Extremes at the lowest and highest supported rates stay finite.
    for(double rate : {8000.0, 384000.0}) {
        Fixture stress(rate); stress.set(kDepth, 1.5); stress.set(kWarmth, 1); stress.set(kWidth, 1.5);
        stress.set(kTone, 18000); stress.set(kNoise, 1); stress.set(kNoiseLevel, -30); stress.set(kOutputLevel, 12);
        auto loud = saw(8192, 4);
        for(int mode : {kOne, kTwo, kBoth, kOff}) { stress.set(kMode, mode); stress.render(loud, loud); }
    }
    std::cout << "Mla JunoChorus processor tests passed\n";
}
