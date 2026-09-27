// Exercise the actual VST3 processor and compiled MLang bridge together.
#define MLA_GATED_VERB_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace mla_gated_verb;
static void check(bool ok, const char* message) {
    if(!ok) { std::cerr << message << '\n'; std::exit(1); }
}
struct Fixture {
    Processor processor;
    double rate;
    explicit Fixture(double sampleRate = 48000) : rate(sampleRate) {
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
    std::array<std::vector<float>, 2> render(const std::vector<float>& mono) { return render(mono, mono); }
};
// A drum-machine style snare: a noise burst over a short body tone.
static std::vector<float> snare(int count, double rate, float level = .8f) {
    std::vector<float> values(count);
    uint32 seed = 1;
    for(int i = 0; i < count; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(seed >> 8) / 8388608.f - 1.f;
        const float t = static_cast<float>(i / rate);
        values[i] = level * (0.6f * noise * std::exp(-t / .05f) + 0.5f * std::sin(6.2831853f * 190 * t) * std::exp(-t / .02f));
    }
    return values;
}
static double energy(const std::vector<float>& values, int from, int to) {
    double sum = 0; for(int i = from; i < to; ++i) sum += std::abs(values[i]); return sum;
}
static int ms(double value, double rate = 48000) { return static_cast<int>(value * rate / 1000); }
int main() {
    Fixture a;
    check(a.processor.getParameterCount() == kCount, "parameter count");
    for(int i = 0; i < kCount; ++i) {
        ParameterInfo info{};
        check(a.processor.getParameterInfo(i, info) == kResultOk, "parameter metadata");
        check(info.id == kFirstParam + i && (info.flags & ParameterInfo::kCanAutomate), "stable automatable ID");
        check(std::abs(a.processor.getParamNormalized(info.id) - normalized(i, specs[i].initial)) < 1e-9, "controller default");
    }
    ParameterInfo typeInfo{};
    a.processor.getParameterInfo(kType, typeInfo);
    check(typeInfo.stepCount == 2, "Room, Plate, Reverse");

    // Wet only: one hit opens the gate for pre-delay + hold, the release ends
    // it, and afterwards the output is exactly silent.
    const int length = ms(1000);
    for(int type : {kRoom, kPlate, kReverse}) {
        Fixture verb; verb.set(kType, type); verb.set(kDryLevel, -60);
        auto wet = verb.render(snare(length, 48000));
        check(energy(wet[0], ms(150), ms(250)) > 50, "reverb holds while the gate is open");
        check(energy(wet[0], ms(345), length) == 0 && energy(wet[1], ms(345), length) == 0, "gate closes after hold + release");
    }
    // Room reverb is loud: its held level is in the dry snare's ballpark.
    {
        Fixture verb; verb.set(kDryLevel, -60);
        auto input = snare(length, 48000);
        auto wet = verb.render(input);
        check(energy(wet[0], ms(10), ms(250)) > energy(input, 0, ms(240)) * 0.8, "reverb is loud");
        double spread = 0; for(int i = ms(20); i < ms(250); ++i) spread += std::abs(wet[0][i] - wet[1][i]);
        check(spread > 50, "stereo reverb");
    }
    // Hold sets when the release starts.
    {
        Fixture verb; verb.set(kDryLevel, -60); verb.set(kHold, 500);
        auto wet = verb.render(snare(length, 48000));
        check(energy(wet[0], ms(450), ms(500)) > 20, "longer hold keeps the reverb");
        check(energy(wet[0], ms(600), length) == 0, "and still closes");
    }
    // Reverse swells: the end of the hold is louder than its start.
    {
        Fixture verb; verb.set(kType, kReverse); verb.set(kDryLevel, -60); verb.set(kSquash, 0);
        auto wet = verb.render(snare(length, 48000));
        check(energy(wet[0], ms(200), ms(250)) > 3 * energy(wet[0], ms(20), ms(70)), "reverse swells up");
    }
    // Nothing above the threshold, no reverb (once the dry level has glided out).
    {
        Fixture verb; verb.set(kDryLevel, -60); verb.set(kThreshold, -6);
        verb.render(std::vector<float>(ms(300)));
        auto wet = verb.render(snare(length, 48000, .2f));
        check(energy(wet[0], 0, length) == 0, "quiet input keeps the gate shut");
    }
    // The reverb level scales the wet signal; the dry path is untouched.
    {
        Fixture loud, quiet; loud.set(kDryLevel, -60); quiet.set(kDryLevel, -60); quiet.set(kReverbLevel, -12);
        auto input = snare(length, 48000);
        const double l = energy(loud.render(input)[0], ms(50), ms(250));
        const double q = energy(quiet.render(input)[0], ms(50), ms(250));
        check(q > l * 0.2 && q < l * 0.3, "reverb level -12 dB");
        Fixture dry; dry.set(kReverbLevel, -24);
        auto mixed = dry.render(input);
        check(std::abs(mixed[0][ms(1)] - input[ms(1)]) < 1e-6f, "dry path at 0 dB before the reverb arrives");
    }
    // Damp darkens the tail during the release.
    {
        auto brightness = [](double damp) {
            Fixture verb; verb.set(kDryLevel, -60); verb.set(kDamp, damp); verb.set(kRelease, 200); verb.set(kSquash, 1);
            auto wet = verb.render(snare(ms(1000), 48000));
            double edge = 0, level = 0;
            for(int i = ms(400); i < ms(450); ++i) { edge += std::abs(wet[0][i] - wet[0][i - 1]); level += std::abs(wet[0][i]); }
            return edge / level;
        };
        check(brightness(1) < 0.5 * brightness(0), "damp darkens the closing tail");
    }
    // Every hit retriggers the gate.
    {
        Fixture verb; verb.set(kDryLevel, -60);
        auto hit = snare(ms(600), 48000);
        std::vector<float> twice(hit); twice.insert(twice.end(), hit.begin(), hit.end());
        auto wet = verb.render(twice);
        check(energy(wet[0], ms(500), ms(600)) == 0, "closed between hits");
        check(energy(wet[0], ms(700), ms(850)) > 50, "second hit reopens");
    }
    check(a.processor.getTailSamples() == static_cast<uint32>(std::ceil((8 + 250 + 80 + 10) * 48.0)), "tail covers pre-delay, hold and release");

    auto input = snare(ms(200), 48000);
    a.set(kBypass, 1);
    auto bypassed = a.render(input);
    check(bypassed[0][500] == input[500], "bypass dry output");
    auto silenced = a.render(input, input, 3);
    check(energy(silenced[0], 0, ms(200)) == 0, "honor silent input buffers");

    MemoryStream state;
    Fixture b;
    a.set(kType, kPlate); a.set(kHold, 420); a.set(kReverbLevel, 6);
    check(a.processor.getState(&state) == kResultOk, "save state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&state) == kResultOk, "restore state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kHold) - normalized(kHold, 420)) < 1e-9, "restored hold");
    check(b.processor.getParamNormalized(kFirstParam + kType) == normalized(kType, kPlate), "restored type");
    MemoryStream truncated;
    check(b.processor.setState(&truncated) != kResultOk, "reject truncated state");
    MemoryStream invalid; IBStreamer bad(&invalid, kLittleEndian);
    bad.writeDouble(std::numeric_limits<double>::quiet_NaN()); invalid.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&invalid) != kResultOk, "reject NaN state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kHold) - normalized(kHold, 420)) < 1e-9, "failed restore is atomic");

    // Extremes at the lowest and highest supported rates stay finite.
    for(double rate : {8000.0, 384000.0}) {
        Fixture stress(rate); stress.set(kSize, 1); stress.set(kSquash, 1); stress.set(kReverbLevel, 12);
        stress.set(kTone, 16000); stress.set(kDamp, 1); stress.set(kPredelay, 100); stress.set(kHold, 1000);
        stress.set(kRelease, 5); stress.set(kThreshold, -60); stress.set(kOutputLevel, 12); stress.set(kLowCut, 1000);
        std::vector<float> loud(8192); for(int i = 0; i < 8192; ++i) loud[i] = (i % 97 < 48) ? 4.f : -4.f;
        for(int type : {kRoom, kPlate, kReverse}) { stress.set(kType, type); stress.render(loud, loud); stress.set(kSize, 0); stress.render(loud, loud); }
    }
    std::cout << "Mla GatedVerb processor tests passed\n";
}
