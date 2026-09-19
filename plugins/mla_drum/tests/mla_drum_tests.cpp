// Offline functional tests for the Mla Drum bundle.
//
// Loads the built .vst3 through the SDK hosting classes (as mlacker does),
// fills pads through the mla_sampler_protocol messages and checks rendered
// audio: pad/key mapping, level and pan, the amp ADSR, Mono choke vs Poly,
// one-shot vs gated note-off, WAV loading, and state round trips.
//
// Usage: mla_drum_tests <path/to/MlaDrum.vst3> <scratch dir>

#include "mla_sampler_protocol.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

int failures = 0;
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if(!(condition)) {                                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
            ++failures;                                                                        \
        }                                                                                      \
    } while(0)

constexpr double kRate = 48000.0;
constexpr int32 kBlock = 256;
constexpr int kRootKey = 36;

// Parameter IDs (see plugin.cpp).
constexpr ParamID kLevel = 100, kTune = 101, kVelocity = 102, kMode = 103, kTrigger = 104, kRootKeyId = 105;
constexpr ParamID kAttack = 106, kDecay = 107, kSustain = 108, kRelease = 109;
constexpr ParamID padParam(int pad, int k) { return 200 + pad * 3 + k; }

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_drum_tests";
        std::copy(std::begin(text), std::end(text), name);
        return kResultOk;
    }
};
Application application;

struct Instance {
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    HostProcessData data;
    EventList events{512};
    ParameterChanges changes{64};
    ProcessContext context{};
    std::vector<std::string> subCategories;

    bool open(const std::string &path)
    {
        std::string error;
        module = VST3::Hosting::Module::create(path, error);
        if(!module) {
            std::fprintf(stderr, "load failed: %s\n", error.c_str());
            return false;
        }
        const auto &factory = module->getFactory();
        factory.setHostContext(&application);
        for(const auto &info : factory.classInfos()) {
            if(info.category() != kVstAudioEffectClass)
                continue;
            subCategories = info.subCategories();
            provider = owned(new PlugProvider(factory, info, true));
            if(!provider->initialize())
                return false;
            component = provider->getComponentPtr();
            processor = U::cast<IAudioProcessor>(component);
            break;
        }
        if(!component || !processor)
            return false;
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        if(processor->setBusArrangements(nullptr, 0, &stereo, 1) != kResultOk)
            return false;
        component->activateBus(kAudio, kOutput, 0, true);
        component->activateBus(kEvent, kInput, 0, true);
        ProcessSetup setup{kOffline, kSample32, kBlock, kRate};
        if(processor->setupProcessing(setup) != kResultOk || !data.prepare(*component, kBlock, kSample32))
            return false;
        context.sampleRate = kRate;
        data.processContext = &context;
        data.inputEvents = &events;
        data.inputParameterChanges = &changes;
        component->setActive(true);
        processor->setProcessing(true);
        return true;
    }

    ~Instance()
    {
        if(processor)
            processor->setProcessing(false);
        if(component)
            component->setActive(false);
        data.unprepare();
        processor.reset();
        component.reset();
        provider.reset();
    }

    void param(ParamID id, double value)
    {
        int32 index = 0;
        if(auto *queue = changes.addParameterData(id, index))
            queue->addPoint(0, value, index);
    }

    void noteOn(int pitch, float velocity = 1.0f, int32 offset = 0)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = offset;
        e.noteOn.pitch = static_cast<int16>(pitch);
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        events.addEvent(e);
    }

    void noteOff(int pitch, int32 offset = 0)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = offset;
        e.noteOff.pitch = static_cast<int16>(pitch);
        e.noteOff.noteId = -1;
        events.addEvent(e);
    }

    // Render `frames` (multiple of the block), appending interleaved stereo.
    std::vector<float> render(int frames)
    {
        std::vector<float> out;
        for(int done = 0; done < frames; done += kBlock) {
            data.numSamples = kBlock;
            processor->process(data);
            events.clear();
            changes.clearQueue();
            const auto &bus = data.outputs[0];
            for(int32 i = 0; i < kBlock; ++i) {
                out.push_back(bus.channelBuffers32[0][i]);
                out.push_back(bus.channelBuffers32[1][i]);
            }
        }
        return out;
    }

    tresult send(IPtr<IMessage> message)
    {
        auto connection = U::cast<IConnectionPoint>(component);
        return connection ? connection->notify(message) : kNoInterface;
    }
};

IPtr<IMessage> message(const char *id)
{
    auto msg = owned(new HostMessage);
    msg->setMessageID(id);
    return msg;
}

tresult loadPcm(Instance &plugin, int pad, const std::vector<float> &pcm, int channels, double rate = kRate,
                std::string *error = nullptr)
{
    auto msg = message(mla_sampler::kLoadPcmMessage);
    auto *a = msg->getAttributes();
    a->setInt("pad", pad);
    a->setInt("channels", channels);
    a->setInt("frames", static_cast<int64>(pcm.size() / channels));
    a->setFloat("rate", rate);
    a->setBinary("data", pcm.data(), static_cast<uint32>(pcm.size() * sizeof(float)));
    const tresult result = plugin.send(msg);
    const void *data = nullptr;
    uint32 size = 0;
    if(error && a->getBinary("error", data, size) == kResultOk)
        *error = std::string(static_cast<const char *>(data), size);
    return result;
}

std::vector<float> constant(int frames, float value) { return std::vector<float>(frames, value); }

float peak(const std::vector<float> &stereo, int from, int to, int channel)
{
    float result = 0.0f;
    for(int f = from; f < to; ++f)
        result = std::max(result, std::fabs(stereo[f * 2 + channel]));
    return result;
}

bool near(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }

void writeWav(const std::string &path, const std::vector<int16_t> &mono, int rate)
{
    std::ofstream file(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char *>(&v), 2); };
    const uint32_t bytes = static_cast<uint32_t>(mono.size() * 2);
    file.write("RIFF", 4); u32(36 + bytes); file.write("WAVE", 4);
    file.write("fmt ", 4); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    file.write("data", 4); u32(bytes);
    file.write(reinterpret_cast<const char *>(mono.data()), bytes);
}

void testLayoutAndMapping(const std::string &bundle)
{
    Instance plugin;
    CHECK(plugin.open(bundle));
    CHECK(std::find(plugin.subCategories.begin(), plugin.subCategories.end(), "Instrument") !=
          plugin.subCategories.end());
    CHECK(plugin.component->getBusCount(kAudio, kInput) == 0);
    CHECK(plugin.component->getBusCount(kEvent, kInput) == 1);

    // Empty pad: silence.
    plugin.noteOn(kRootKey);
    CHECK(peak(plugin.render(kBlock), 0, kBlock, 0) == 0.0f);

    // Constant 0.5 mono on pad 2: centred pan and unity level pass it through.
    plugin.param(kAttack, 0.0);
    plugin.render(kBlock);
    CHECK(loadPcm(plugin, 2, constant(4800, 0.5f), 1) == kResultOk);
    plugin.noteOn(kRootKey + 2, 1.0f, 100);
    auto out = plugin.render(kBlock * 4);
    CHECK(peak(out, 0, 100, 0) == 0.0f); // Sample-accurate start.
    CHECK(near(out[200 * 2], 0.5f) && near(out[200 * 2 + 1], 0.5f));

    // Keys outside the 16 pads, and pads without samples, stay silent.
    plugin.render(kBlock * 20); // Let the first hit finish (4800 frames).
    plugin.noteOn(kRootKey - 1);
    plugin.noteOn(kRootKey + 16);
    plugin.noteOn(kRootKey + 3);
    CHECK(peak(plugin.render(kBlock), 0, kBlock, 0) == 0.0f);

    // Root Key moves the pad range.
    plugin.param(kRootKeyId, 60.0 / 127.0);
    plugin.noteOn(62);
    CHECK(peak(plugin.render(kBlock), 0, kBlock, 0) > 0.4f);
}

void testLevelPanTuneVelocity(const std::string &bundle)
{
    Instance plugin;
    CHECK(plugin.open(bundle));
    CHECK(loadPcm(plugin, 0, constant(48000, 0.5f), 1) == kResultOk);

    plugin.param(padParam(0, 1), 1.0); // Hard right.
    plugin.noteOn(kRootKey);
    auto out = plugin.render(kBlock);
    CHECK(near(out[100 * 2], 0.0f) && near(out[100 * 2 + 1], 0.5f * std::sqrt(2.0f), 2e-3f));

    Instance quiet;
    CHECK(quiet.open(bundle));
    CHECK(loadPcm(quiet, 0, constant(48000, 0.5f), 1) == kResultOk);
    quiet.param(kVelocity, 1.0);
    quiet.noteOn(kRootKey, 0.5f);
    out = quiet.render(kBlock);
    CHECK(near(out[100 * 2], 0.25f));

    Instance muted;
    CHECK(muted.open(bundle));
    CHECK(loadPcm(muted, 0, constant(48000, 0.5f), 1) == kResultOk);
    muted.param(kLevel, 0.0); // Bottom of the level range is silence.
    muted.noteOn(kRootKey);
    CHECK(peak(muted.render(kBlock), 0, kBlock, 0) == 0.0f);

    // +12 semitones plays a 480-frame sample in 240 frames.
    Instance tuned;
    CHECK(tuned.open(bundle));
    CHECK(loadPcm(tuned, 0, constant(480, 0.5f), 1) == kResultOk);
    tuned.param(padParam(0, 2), 36.0 / 48.0);
    tuned.noteOn(kRootKey);
    out = tuned.render(kBlock * 2);
    CHECK(peak(out, 200, 235, 0) > 0.4f);
    CHECK(peak(out, 245, 400, 0) == 0.0f);

    // A 24 kHz sample plays at half speed on a 48 kHz host.
    Instance resampled;
    CHECK(resampled.open(bundle));
    CHECK(loadPcm(resampled, 0, constant(240, 0.5f), 1, 24000.0) == kResultOk);
    resampled.noteOn(kRootKey);
    out = resampled.render(kBlock * 2);
    CHECK(peak(out, 470, 478, 0) > 0.4f);
    CHECK(peak(out, 485, 512, 0) == 0.0f);
}

void testEnvelope(const std::string &bundle)
{
    Instance plugin;
    CHECK(plugin.open(bundle));
    CHECK(loadPcm(plugin, 0, constant(96000, 1.0f), 1) == kResultOk);
    // 2 s * 0.5^3 = 250 ms linear attack.
    plugin.param(kAttack, 0.5);
    plugin.param(kSustain, 1.0);
    plugin.noteOn(kRootKey);
    auto out = plugin.render(12000 + kBlock * 2);
    const float quarter = out[3000 * 2];
    CHECK(quarter > 0.2f && quarter < 0.3f);
    CHECK(near(out[12200 * 2], 1.0f));

    // Decay to a zero sustain ends the hit; release is not needed.
    Instance decaying;
    CHECK(decaying.open(bundle));
    CHECK(loadPcm(decaying, 0, constant(96000, 1.0f), 1) == kResultOk);
    decaying.param(kAttack, 0.0);
    decaying.param(kDecay, 0.25); // 10 ms
    decaying.param(kSustain, 0.0);
    decaying.noteOn(kRootKey);
    out = decaying.render(kBlock * 8);
    CHECK(peak(out, 1024, 2048, 0) == 0.0f);
}

void testTriggerModes(const std::string &bundle)
{
    // One-shot ignores note-off.
    Instance oneShot;
    CHECK(oneShot.open(bundle));
    CHECK(loadPcm(oneShot, 0, constant(48000, 1.0f), 1) == kResultOk);
    oneShot.param(kRelease, 0.0);
    oneShot.noteOn(kRootKey);
    oneShot.noteOff(kRootKey, 10);
    auto out = oneShot.render(kBlock * 4);
    CHECK(near(out[1000 * 2], 1.0f));

    // Gated releases on note-off (1 ms release -> silent soon after).
    Instance gated;
    CHECK(gated.open(bundle));
    CHECK(loadPcm(gated, 0, constant(48000, 1.0f), 1) == kResultOk);
    gated.param(kTrigger, 1.0);
    gated.param(kRelease, 0.0);
    gated.noteOn(kRootKey);
    gated.noteOff(kRootKey, 10);
    out = gated.render(kBlock * 4);
    CHECK(peak(out, 200, 1024, 0) == 0.0f);
}

void testMonoChokeAndPoly(const std::string &bundle)
{
    for(const bool mono : {true, false}) {
        Instance plugin;
        CHECK(plugin.open(bundle));
        // Pad 0 is hard left, pad 1 hard right: the channels separate the hits.
        CHECK(loadPcm(plugin, 0, constant(48000, 0.5f), 1) == kResultOk);
        CHECK(loadPcm(plugin, 1, constant(48000, 0.5f), 1) == kResultOk);
        plugin.param(padParam(0, 1), 0.0);
        plugin.param(padParam(1, 1), 1.0);
        plugin.param(kMode, mono ? 1.0 : 0.0);
        plugin.noteOn(kRootKey);
        plugin.noteOn(kRootKey + 1, 1.0f, 128);
        auto out = plugin.render(kBlock * 4);
        CHECK(peak(out, 0, 128, 0) > 0.6f); // Open hat rings...
        const float after = peak(out, 128 + 200, 1024, 0);
        if(mono)
            // ...until the closed hat chokes it within 3 ms. (Not exactly 0:
            // hard-right pan leaves cosf(pi/2) ~ 4e-8 of pad 1 on the left.)
            CHECK(after < 1e-6f);
        else
            CHECK(after > 0.6f);
        CHECK(peak(out, 128 + 200, 1024, 1) > 0.6f);
    }
}

void testLoadFileAndErrors(const std::string &bundle, const std::string &scratch)
{
    Instance plugin;
    CHECK(plugin.open(bundle));
    const std::string wav = scratch + "/mla_drum_test_kick.wav";
    writeWav(wav, std::vector<int16_t>(4800, 16384), 48000);

    auto msg = message(mla_sampler::kLoadFileMessage);
    msg->getAttributes()->setInt("pad", 5);
    msg->getAttributes()->setBinary("path", wav.data(), static_cast<uint32>(wav.size()));
    CHECK(plugin.send(msg) == kResultOk);
    plugin.noteOn(kRootKey + 5);
    CHECK(near(plugin.render(kBlock)[100 * 2], 0.5f));

    auto missing = message(mla_sampler::kLoadFileMessage);
    const std::string nowhere = scratch + "/does-not-exist.wav";
    missing->getAttributes()->setInt("pad", 5);
    missing->getAttributes()->setBinary("path", nowhere.data(), static_cast<uint32>(nowhere.size()));
    CHECK(plugin.send(missing) == kResultFalse);
    const void *why = nullptr;
    uint32 size = 0;
    CHECK(missing->getAttributes()->getBinary("error", why, size) == kResultOk && size > 0);

    std::string error;
    CHECK(loadPcm(plugin, 16, constant(10, 0.1f), 1, kRate, &error) == kResultFalse && !error.empty());
    CHECK(loadPcm(plugin, 0, constant(10, 0.1f), 3) == kResultFalse);
    CHECK(loadPcm(plugin, 0, constant(10, 0.1f), 1, 10.0) == kResultFalse);

    // Clearing silences the pad; a pad loaded while a voice plays swaps cleanly.
    plugin.noteOn(kRootKey + 5);
    plugin.render(kBlock);
    auto clear = message(mla_sampler::kClearMessage);
    clear->getAttributes()->setInt("pad", 5);
    CHECK(plugin.send(clear) == kResultOk);
    CHECK(peak(plugin.render(kBlock), 0, kBlock, 0) == 0.0f);
    plugin.noteOn(kRootKey + 5);
    CHECK(peak(plugin.render(kBlock), 0, kBlock, 0) == 0.0f);
}

void testStateRoundTrip(const std::string &bundle)
{
    MemoryStream state;
    {
        Instance source;
        CHECK(source.open(bundle));
        std::vector<float> stereo;
        for(int f = 0; f < 2000; ++f) {
            stereo.push_back(0.25f);
            stereo.push_back(-0.5f);
        }
        CHECK(loadPcm(source, 7, stereo, 2) == kResultOk);
        source.param(kTune, 0.5);
        source.param(kMode, 1.0);
        source.param(padParam(7, 0), 0.5);
        source.render(kBlock);
        CHECK(source.component->getState(&state) == kResultOk);
    }
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    Instance restored;
    CHECK(restored.open(bundle));
    CHECK(restored.component->setState(&state) == kResultOk);
    auto controller = restored.provider->getControllerPtr();
    CHECK(controller && near(static_cast<float>(controller->getParamNormalized(kMode)), 1.0f));
    restored.noteOn(kRootKey + 7);
    const auto out = restored.render(kBlock);
    // Pad level norm 0.5 = -27 dB.
    const float gain = std::pow(10.0f, -27.0f / 20.0f);
    CHECK(near(out[100 * 2], 0.25f * gain, 1e-4f) && near(out[100 * 2 + 1], -0.5f * gain, 1e-4f));

    MemoryStream garbage;
    int32 written = 0;
    garbage.write(const_cast<char *>("nope"), 4, &written);
    garbage.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(restored.component->setState(&garbage) == kResultFalse);
}

} // namespace

int main(int argc, char **argv)
{
    if(argc != 3) {
        std::fprintf(stderr, "usage: %s <MlaDrum.vst3> <scratch dir>\n", argv[0]);
        return 2;
    }
    PluginContextFactory::instance().setPluginContext(&application);
    PlugProvider::setErrorStream(nullptr);
    const std::string bundle = argv[1];
    testLayoutAndMapping(bundle);
    testLevelPanTuneVelocity(bundle);
    testEnvelope(bundle);
    testTriggerModes(bundle);
    testMonoChokeAndPoly(bundle);
    testLoadFileAndErrors(bundle, argv[2]);
    testStateRoundTrip(bundle);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mla_drum_tests: all checks passed\n");
    return 0;
}
