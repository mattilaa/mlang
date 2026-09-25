// Exercise the actual VST3 processor and compiled MLang bridge together.
#define MLA_STUTTER_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace mla_stutter;
static void check(bool ok, const char* message) {
    if(!ok) { std::cerr << message << '\n'; std::exit(1); }
}
// 8192 Hz at 120 BPM is 4096 samples per beat: a 1/16 slice is 1024 samples.
struct Fixture {
    Processor processor;
    explicit Fixture(double rate = 8192) {
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
    // Exact comparisons: manual 120 BPM, no declick fade, full mix.
    void neutral() { set(kTempo, kManual); set(kBpm, 120); set(kFade, 0); set(kMix, 1); set(kGate, 1); set(kWidth, 5); }
    std::array<std::vector<float>, 2> render(const std::vector<float>& input, ProcessContext* context = nullptr, uint64 silence = 0) {
        const int count = static_cast<int>(input.size());
        std::vector<float> left(input), right(count);
        for(int i = 0; i < count; ++i) right[i] = -input[i];
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
static std::vector<float> ramp(int count, int from = 0) {
    std::vector<float> values(count);
    for(int i = 0; i < count; ++i) values[i] = static_cast<float>(from + i) / 8192.f;
    return values;
}
int main() {
    Fixture a;
    check(a.processor.getParameterCount() == kCount, "parameter count");
    for(int i = 0; i < kCount; ++i) {
        ParameterInfo info{};
        check(a.processor.getParameterInfo(i, info) == kResultOk, "parameter metadata");
        check(info.id == kFirstParam + i && (info.flags & ParameterInfo::kCanAutomate), "stable automatable ID");
        check(std::abs(a.processor.getParamNormalized(info.id) - normalized(i, specs[i].initial)) < 1e-9, "controller default");
    }
    ParameterInfo widthInfo{};
    a.processor.getParameterInfo(kWidth, widthInfo);
    check(widthInfo.stepCount == kWidthCount - 1, "ten width choices");

    // Off is transparent.
    a.neutral(); a.set(kMode, kOff);
    auto input = ramp(4096);
    auto dry = a.render(input);
    for(int i = 0; i < 4096; ++i) check(dry[0][i] == input[i] && dry[1][i] == -input[i], "off passes input");
    check(!a.processor.stuttering(), "off is idle");

    // On: a fresh grid starts on a line; the first 1/16 plays and is recorded,
    // then it repeats every 1024 samples.
    Fixture on; on.neutral(); on.set(kMode, kOn);
    auto repeated = on.render(ramp(4096));
    for(int i = 0; i < 1024; ++i) check(repeated[0][i] == input[i], "first slice is live");
    for(int i = 1024; i < 4096; ++i) check(repeated[0][i] == input[i % 1024] && repeated[1][i] == -input[i % 1024], "slice repeats on the grid");
    check(on.processor.stuttering(), "on is stuttering");
    on.set(kWidth, 7); // 1/32: the held slice restarts every 512 samples
    auto faster = on.render(ramp(2048, 4096));
    for(int i = 0; i < 2048; ++i) check(faster[0][i] == input[i % 512], "narrower width repeats faster");
    on.set(kMode, kOff);
    auto released = on.render(ramp(16, 6144));
    check(released[0][0] == 6144 / 8192.f, "release returns to input");

    // Auto: every beat, hold the last 1/2 beat (cells 2 and 3 of each beat).
    Fixture automatic; automatic.neutral(); automatic.set(kMode, kAuto); automatic.set(kPeriod, 1); automatic.set(kHold, 2);
    auto autoInput = ramp(8192);
    auto autoOut = automatic.render(autoInput);
    for(int i = 0; i < 3072; ++i) check(autoOut[0][i] == autoInput[i], "auto is dry before the hold");
    for(int i = 3072; i < 4096; ++i) check(autoOut[0][i] == autoInput[i - 1024], "auto repeats the first held slice");
    for(int i = 4096; i < 7168; ++i) check(autoOut[0][i] == autoInput[i], "auto releases at the next beat");
    for(int i = 7168; i < 8192; ++i) check(autoOut[0][i] == autoInput[i - 1024], "auto holds again next period");

    // Host: follow the sequencer tempo (240 BPM: 512-sample 1/16 cells) and
    // its playing beat. 10.125 beats is half a cell before the next line.
    Fixture host; host.neutral(); host.set(kTempo, kHost); host.set(kMode, kOn);
    ProcessContext context{};
    context.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid | ProcessContext::kPlaying;
    context.tempo = 240; context.projectTimeMusic = 10.125;
    auto synced = host.render(ramp(2048), &context);
    for(int i = 0; i < 256; ++i) check(synced[0][i] == input[i], "waits for the host grid line");
    for(int i = 768; i < 2048; ++i) check(synced[0][i] == input[256 + (i - 768) % 512], "host-synced repeats");
    // A host jump (loop or relocate) restarts the slice on the new grid line.
    context.projectTimeMusic = 20.0;
    auto jumped = host.render(ramp(1024, 2048), &context);
    check(jumped[0][0] == input[256] && jumped[0][511] == input[767], "jump restarts the held slice on the new line");
    // Stopped host: tempo still applies, the grid free-runs.
    context.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
    host.render(ramp(1024), &context);
    // Manual ignores the host tempo.
    Fixture manual; manual.neutral(); manual.set(kMode, kOn);
    auto own = manual.render(ramp(2048), &context);
    check(own[0][1024] == input[0] && own[0][1535] == input[511], "manual uses the BPM knob");

    // Gate silences the tail of each repeat.
    Fixture gated; gated.neutral(); gated.set(kMode, kOn); gated.set(kGate, 0.5);
    auto chopped = gated.render(ramp(2048));
    check(chopped[0][1024 + 100] == input[100] && chopped[0][1024 + 600] == 0, "gate chops the repeat");

    a.set(kBypass, 1); a.set(kMode, kOn);
    auto bypassed = a.render(ramp(4096));
    check(bypassed[0][3000] == input[3000], "bypass dry output");
    auto silenced = a.render(ramp(32), nullptr, 3);
    check(silenced[0][5] == 0, "honor silent input buffers");

    MemoryStream state;
    Fixture b;
    a.set(kWidth, 3); a.set(kPeriod, 8); a.set(kGate, .4);
    check(a.processor.getState(&state) == kResultOk, "save state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&state) == kResultOk, "restore state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kGate) - normalized(kGate, .4)) < 1e-9, "restored gate");
    MemoryStream truncated;
    check(b.processor.setState(&truncated) != kResultOk, "reject truncated state");
    MemoryStream invalid; IBStreamer bad(&invalid, kLittleEndian);
    bad.writeDouble(std::numeric_limits<double>::quiet_NaN()); invalid.seek(0, IBStream::kIBSeekSet, nullptr);
    check(b.processor.setState(&invalid) != kResultOk, "reject NaN state");
    check(std::abs(b.processor.getParamNormalized(kFirstParam + kWidth) - normalized(kWidth, 3)) < 1e-9, "failed restore is atomic");

    // Every width, extreme tempo and fades stay finite at 48 kHz.
    for(int width = 0; width < kWidthCount; ++width) {
        Fixture stress(48000); stress.set(kMode, kOn); stress.set(kWidth, width); stress.set(kFade, 20);
        stress.set(kTempo, kManual); stress.set(kBpm, width % 2 ? 20 : 400);
        std::vector<float> noise(8192); uint32_t seed = 1u + width;
        for(auto& v : noise) { seed = seed * 1664525u + 1013904223u; v = static_cast<float>(seed >> 8) / 8388608.f - 1.f; }
        stress.render(noise); stress.set(kBpm, 137); stress.render(noise);
    }
    std::cout << "Mla Stutter processor tests passed\n";
}
