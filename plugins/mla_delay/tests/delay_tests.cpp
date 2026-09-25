// Exercise the actual VST3 processor and compiled MLang bridge together.
#define MLA_DELAY_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace mla_delay;
static void check(bool ok, const char* message) {
    if(!ok) { std::cerr << message << '\n'; std::exit(1); }
}
struct Fixture {
    Processor processor;
    explicit Fixture(double rate = 8000) {
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
    void neutral() {
        set(kDelayRamp, 0); set(kMixRamp, 0); set(kFilterRamp, 0);
        set(kMode, 0); set(kFilter, 0); set(kFeedback, 0); set(kMix, 1); set(kDelay, 10);
    }
    std::array<std::vector<float>, 2> render(int count, float impulse = 0, uint64 silence = 0, ProcessContext* context = nullptr) {
        std::vector<float> left(count), right(count);
        left[0] = impulse;
        std::array<std::vector<float>, 2> out{std::vector<float>(count), std::vector<float>(count)};
        float* inputs[]{left.data(), right.data()}; float* outputs[]{out[0].data(), out[1].data()};
        AudioBusBuffers in{}, output{}; in.numChannels = output.numChannels = 2;
        in.channelBuffers32 = inputs; in.silenceFlags = silence; output.channelBuffers32 = outputs;
        ProcessData data{}; data.numSamples = count; data.symbolicSampleSize = kSample32;
        data.numInputs = data.numOutputs = 1; data.inputs = &in; data.outputs = &output; data.processContext = context;
        check(processor.process(data) == kResultOk, "render");
        for(const auto& channel : out) for(float v : channel) check(std::isfinite(v), "non-finite output");
        return out;
    }
};
static double energy(const std::vector<float>& values) { double sum = 0; for(float v : values) sum += std::abs(v); return sum; }
int main() {
    Fixture a, b;
    check(a.processor.getParameterCount() == kCount, "parameter count");
    for(int i = 0; i < kCount; ++i) {
        ParameterInfo info{};
        check(a.processor.getParameterInfo(i, info) == kResultOk, "parameter metadata");
        check(info.id == kFirstParam + i && (info.flags & ParameterInfo::kCanAutomate), "stable automatable ID");
        check(std::abs(a.processor.getParamNormalized(info.id) - normalized(i, specs[i].initial)) < 1e-9, "controller default");
    }
    a.neutral(); b.neutral();
    auto first = a.render(81, 1);
    check(std::abs(first[0][80] - 1) < 1e-5, "10 ms impulse delay at 8 kHz");
    check(energy(first[1]) == 0, "forward routing");
    auto independent = b.render(200);
    check(energy(independent[0]) == 0 && energy(independent[1]) == 0, "instance isolation");
    a.set(kReset, 1); a.set(kMode, 1); a.set(kFeedback, .5);
    auto ping = a.render(241, 1);
    check(energy(ping[1]) > .1, "ping-pong crosses stereo channels");
    a.set(kReset, 0); a.set(kReset, 1);
    auto cleared = a.render(241);
    check(energy(cleared[0]) == 0 && energy(cleared[1]) == 0, "reset clears history");
    a.set(kBypass, 1);
    auto dry = a.render(32, .75);
    check(dry[0][0] == .75f, "bypass dry output");
    auto silenced = a.render(32, 1, 3);
    check(energy(silenced[0]) == 0, "honor silent input buffers");

    Fixture tempo;
    tempo.neutral(); tempo.set(kTempo, 1); tempo.set(kBpm, 120); tempo.set(kBeats, .25);
    auto beat = tempo.render(1001, 1);
    check(std::abs(beat[0][1000] - 1) < 1e-5, "manual beat sync");
    tempo.set(kReset, 1); tempo.set(kTempo, 2);
    ProcessContext context{}; context.state = ProcessContext::kTempoValid; context.tempo = 240;
    auto host = tempo.render(501, 1, 0, &context);
    check(std::abs(host[0][500] - 1) < 1e-5, "host tempo sync");

    MemoryStream state;
    a.set(kCutoff, 1700); a.set(kJitter, 12); a.set(kFeedback, .81);
    check(a.processor.getState(&state) == kResultOk, "save state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&state) == kResultOk, "restore state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kFeedback) - normalized(kFeedback, .81)) < 1e-9, "restored feedback");
    MemoryStream truncated;
    check(b.processor.setState(&truncated) != kResultOk, "reject truncated state");
    MemoryStream invalid; IBStreamer bad(&invalid, kLittleEndian);
    bad.writeDouble(std::numeric_limits<double>::quiet_NaN()); invalid.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&invalid) != kResultOk, "reject NaN state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kFeedback) - normalized(kFeedback, .81)) < 1e-9, "failed restore is atomic");
    // The filter list matches Mla Filter: None, then LP/HP/BP 12/24, Moog 12/24.
    ParameterInfo filterInfo{};
    a.processor.getParameterInfo(kFilter, filterInfo);
    check(filterInfo.stepCount == kFilterCount - 1, "nine filter choices");
    // Every multimode type filters the repeat: an impulse through a 200 Hz
    // low-/band-pass comes back smeared well below its unfiltered peak.
    for(int index : {kLowpass12, kLowpass24, kBandpass12, kBandpass24, kMoog12, kMoog24}) {
        Fixture model; model.neutral(); model.set(kCutoffRamp, 0); model.set(kFilter, index); model.set(kCutoff, 200); model.set(kDamping, 1);
        auto echo = model.render(400, 1);
        double peak = 0; for(int i = 0; i < 400; ++i) peak = std::max(peak, static_cast<double>(std::abs(echo[0][i])));
        check(peak > 0.001 && peak < 0.5, "multimode filter colors the repeat");
    }
    for(int filter = 0; filter < kFilterCount; ++filter) {
        Fixture stress(48000); stress.neutral(); stress.set(kFilter, filter); stress.set(kResonance, 1);
        stress.set(kFeedback, 1.2); stress.set(kJitter, 50); stress.set(kDamping, 1);
        stress.render(8192, 1);
        stress.set(kCutoff, 20); stress.set(kDelay, 2); stress.render(8192);
    }
    std::cout << "Mla Delay processor tests passed\n";
}
