// Exercise the actual VST3 processor and compiled MLang bridge together.
#define MLA_EQ_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace mla_eq;
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
    std::array<std::vector<float>, 2> render(const std::vector<float>& input) {
        std::vector<float> left = input, right = input;
        const int count = static_cast<int>(input.size());
        std::array<std::vector<float>, 2> out{std::vector<float>(count), std::vector<float>(count)};
        float* inputs[]{left.data(), right.data()}; float* outputs[]{out[0].data(), out[1].data()};
        AudioBusBuffers in{}, output{}; in.numChannels = output.numChannels = 2;
        in.channelBuffers32 = inputs; output.channelBuffers32 = outputs;
        ProcessData data{}; data.numSamples = count; data.symbolicSampleSize = kSample32;
        data.numInputs = data.numOutputs = 1; data.inputs = &in; data.outputs = &output;
        check(processor.process(data) == kResultOk, "render");
        for(const auto& channel : out) for(float v : channel) check(std::isfinite(v), "non-finite output");
        return out;
    }
    // Steady-state gain in dB of a sine at `hz`, measured after glides settle.
    double gainDb(double hz) {
        std::vector<float> sine(24000);
        for(size_t i = 0; i < sine.size(); ++i) sine[i] = static_cast<float>(0.25 * std::sin(2 * M_PI * hz * i / kRate));
        auto out = render(sine);
        double inSum = 0, outSum = 0;
        for(size_t i = sine.size() / 2; i < sine.size(); ++i) { inSum += sine[i] * sine[i]; outSum += out[0][i] * out[0][i]; }
        check(std::abs(out[0].back() - out[1].back()) < 1e-6, "stereo channels match");
        return 10 * std::log10(outSum / inSum);
    }
};
int main() {
    Fixture a;
    check(a.processor.getParameterCount() == kCount, "parameter count");
    check(kCount == 41, "41 controls");
    for(int i = 0; i < kCount; ++i) {
        ParameterInfo info{};
        check(a.processor.getParameterInfo(i, info) == kResultOk, "parameter metadata");
        check(info.id == kFirstParam + i && (info.flags & ParameterInfo::kCanAutomate), "stable automatable ID");
        check(std::abs(a.processor.getParamNormalized(info.id) - normalized(i, specs[i].initial)) < 1e-9, "controller default");
        check(std::abs(physical(i, normalized(i, specs[i].initial)) - specs[i].initial) < 1e-6, "mapping round trip");
    }
    // Defaults: every band at 0 dB, HP/LP off -> bit-exact passthrough.
    std::vector<float> noise(512);
    for(size_t i = 0; i < noise.size(); ++i) noise[i] = static_cast<float>(std::sin(i * 0.37) * 0.5);
    auto flat = a.render(noise);
    for(size_t i = 0; i < noise.size(); ++i) check(flat[0][i] == noise[i], "default is transparent");

    // Bell +12 dB at 1 kHz; Q sets how far the boost spreads.
    a.set(bandParam(1, kFreq), 1000); a.set(bandParam(1, kGain), 12); a.set(bandParam(1, kQ), 1);
    check(std::abs(a.gainDb(1000) - 12) < 0.3, "bell centre gain");
    const double wide = a.gainDb(2000);
    a.set(bandParam(1, kQ), 8);
    check(std::abs(a.gainDb(1000) - 12) < 0.3, "narrow bell centre gain");
    const double narrow = a.gainDb(2000);
    check(wide > 3 && narrow < 1, "higher Q narrows the bell");
    a.set(bandParam(1, kGain), 0);
    check(std::abs(a.gainDb(1000)) < 0.05, "bell back to flat");

    // Shelves.
    a.set(bandParam(0, kGain), -9);
    check(std::abs(a.gainDb(30) + 9) < 0.5 && std::abs(a.gainDb(4000)) < 0.3, "low shelf");
    a.set(bandParam(0, kGain), 0); a.set(bandParam(3, kGain), 6);
    check(std::abs(a.gainDb(18000) - 6) < 0.6 && std::abs(a.gainDb(500)) < 0.3, "high shelf");
    a.set(bandParam(3, kGain), 0);

    // Band 5 only runs in 8-band mode.
    a.set(bandParam(4, kFreq), 3000); a.set(bandParam(4, kGain), 10);
    check(std::abs(a.gainDb(3000)) < 0.05, "4-band mode ignores band 5");
    a.set(kEqType, 1);
    check(std::abs(a.gainDb(3000) - 10) < 0.3, "8-band mode applies band 5");
    a.set(bandParam(4, kType), kNotch); a.set(bandParam(4, kQ), 4);
    check(a.gainDb(3000) < -30, "notch band");
    a.set(kEqType, 0);

    // High-pass slopes: two octaves below the corner lose 24/48/72/96 dB.
    Fixture hp; hp.set(kHpFreq, 1000);
    double previous = 0;
    for(int slope = 1; slope <= 4; ++slope) {
        hp.set(kHpSlope, slope);
        const double below = hp.gainDb(250);   // two octaves under the corner
        check(std::abs(below + 24 * slope) < 1.5, "high-pass 12/24/36/48 dB per octave");
        check(std::abs(hp.gainDb(1000) + 3) < 0.4, "Butterworth -3 dB corner");
        check(std::abs(hp.gainDb(12000)) < 0.2, "high-pass passband");
        check(below < previous, "steeper slope attenuates more");
        previous = below;
    }
    hp.set(kHpQ, 4);
    check(hp.gainDb(1000) > 3, "high-pass Q resonates at the corner");

    Fixture lp; lp.set(kLpFreq, 500); lp.set(kLpSlope, 2);
    check(std::abs(lp.gainDb(2000) + 48) < 1.5 && std::abs(lp.gainDb(50)) < 0.2, "24 dB low-pass");

    // Output gain and bypass.
    Fixture out; out.set(kOutputGain, -6);
    check(std::abs(out.gainDb(1000) + 6) < 0.05, "output gain");
    out.set(kBypass, 1);
    auto dry = out.render(noise);
    check(dry[0][7] == noise[7], "bypass dry output");

    // Extreme settings stay finite at every frequency.
    Fixture stress; stress.set(kEqType, 1); stress.set(kHpSlope, 4); stress.set(kLpSlope, 4);
    stress.set(kHpQ, 18); stress.set(kLpQ, 18); stress.set(kHpFreq, 20); stress.set(kLpFreq, 20000);
    for(int b = 0; b < kBands; ++b) { stress.set(bandParam(b, kGain), 24); stress.set(bandParam(b, kQ), 18); }
    for(double hz : {20.0, 100.0, 5000.0, 19000.0}) stress.gainDb(hz);

    // State round trip.
    MemoryStream state;
    a.set(bandParam(2, kGain), 7.5); a.set(kLpSlope, 3);
    check(a.processor.getState(&state) == kResultOk, "save state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    Fixture b;
    check(b.processor.setState(&state) == kResultOk, "restore state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + bandParam(2, kGain)) - normalized(bandParam(2, kGain), 7.5)) < 1e-9, "restored gain");
    MemoryStream truncated;
    check(b.processor.setState(&truncated) != kResultOk, "reject truncated state");
    MemoryStream invalid; IBStreamer bad(&invalid, kLittleEndian);
    bad.writeDouble(std::numeric_limits<double>::quiet_NaN()); invalid.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&invalid) != kResultOk, "reject NaN state");
    std::cout << "Mla EQ processor tests passed\n";
}
