// Standalone "mlacker" simulator for the headless drum-machine VST3.
//
// This tiny host stands in for the mlacker DAW. It:
//   1. instantiates the plugin directly and initializes it with a VST3 host
//      context (Steinberg::Vst::HostApplication),
//   2. uploads a sample into a pad exactly the way mlacker would -- by sending
//      an IMessage through the plugin's connection point (Plugin::notify),
//   3. plays a short drum pattern by triggering pads, and
//   4. writes the rendered stereo audio to a .wav file.
//
// It needs no audio device, so it builds and runs on every platform.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "messages.h"
#include "plugin.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fstring.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using mlacker_drum::Plugin;

namespace {

constexpr int kSampleRate = 48000;
constexpr int kBlockSize = 512;
constexpr double kPi = 3.14159265358979323846;

// A short synthetic kick so the demo works with zero external files.
std::vector<float> makeKick()
{
    const int frames = kSampleRate / 4; // 250 ms
    std::vector<float> out(static_cast<std::size_t>(frames));
    for(int i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        const double env = std::exp(-t * 22.0);
        const double freq = 120.0 * std::exp(-t * 30.0) + 45.0;
        out[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * kPi * freq * t) * env * 0.9);
    }
    return out;
}

// Upload interleaved float PCM into a pad using a VST3 message (host -> plugin).
void uploadPcm(Plugin& plugin, int pad, const std::vector<float>& pcm,
               int channels, int sampleRate)
{
    IPtr<IMessage> message = owned(new HostMessage());
    message->setMessageID(mlacker_drum::kUploadMessageId);

    IAttributeList* attr = message->getAttributes();
    attr->setInt(mlacker_drum::kAttrPad, pad);
    attr->setInt(mlacker_drum::kAttrChannels, channels);
    attr->setInt(mlacker_drum::kAttrSampleRate, sampleRate);
    attr->setBinary(mlacker_drum::kAttrPcm, pcm.data(),
                    static_cast<uint32>(pcm.size() * sizeof(float)));

    plugin.notify(message);
}

// Upload a pad by WAV path using a VST3 message (host -> plugin).
void uploadPath(Plugin& plugin, int pad, const std::string& path)
{
    IPtr<IMessage> message = owned(new HostMessage());
    message->setMessageID(mlacker_drum::kUploadMessageId);

    IAttributeList* attr = message->getAttributes();
    attr->setInt(mlacker_drum::kAttrPad, pad);

    String wide(path.c_str());
    wide.toWideString(kCP_Utf8);
    attr->setString(mlacker_drum::kAttrPath, wide.text16());

    plugin.notify(message);
}

bool writeWav(const std::string& path, const std::vector<float>& interleaved,
              int channels, int sampleRate)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if(!f)
        return false;

    const std::uint32_t frames =
        static_cast<std::uint32_t>(interleaved.size() / channels);
    const std::uint32_t dataBytes = frames * channels * 2u;
    const std::uint32_t byteRate =
        static_cast<std::uint32_t>(sampleRate) * channels * 2u;

    auto putU32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto putU16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);
    putU32(36u + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    putU32(16u);
    putU16(1u); // PCM
    putU16(static_cast<std::uint16_t>(channels));
    putU32(static_cast<std::uint32_t>(sampleRate));
    putU32(byteRate);
    putU16(static_cast<std::uint16_t>(channels * 2));
    putU16(16u);
    std::fwrite("data", 1, 4, f);
    putU32(dataBytes);

    for(float sample : interleaved)
    {
        float clamped = sample;
        if(clamped > 1.0f)
            clamped = 1.0f;
        if(clamped < -1.0f)
            clamped = -1.0f;
        const std::int16_t s =
            static_cast<std::int16_t>(clamped * 32767.0f);
        std::fwrite(&s, 2, 1, f);
    }

    std::fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    HostApplication hostContext;
    Plugin plugin;

    if(plugin.initialize(static_cast<FUnknown*>(
           static_cast<IHostApplication*>(&hostContext))) != kResultOk)
    {
        std::fprintf(stderr, "plugin.initialize failed\n");
        return 1;
    }

    SpeakerArrangement outputs[1] = {SpeakerArr::kStereo};
    if(plugin.setBusArrangements(nullptr, 0, outputs, 1) != kResultOk)
    {
        std::fprintf(stderr, "plugin.setBusArrangements failed\n");
        return 1;
    }

    ProcessSetup setup {};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kBlockSize;
    setup.sampleRate = static_cast<SampleRate>(kSampleRate);
    if(plugin.setupProcessing(setup) != kResultOk)
    {
        std::fprintf(stderr, "plugin.setupProcessing failed\n");
        return 1;
    }

    plugin.setActive(true);
    plugin.setProcessing(true);

    // Pad 0: synthetic kick uploaded as raw PCM bytes.
    uploadPcm(plugin, 0, makeKick(), 1, kSampleRate);

    // Pad 1 (optional): a WAV file the plugin decodes itself.
    bool hasPad1 = false;
    if(argc > 1)
    {
        uploadPath(plugin, 1, argv[1]);
        hasPad1 = true;
        std::printf("[mlacker] uploaded '%s' into pad 1 by path\n", argv[1]);
    }

    // A simple 16-step pattern at 120 BPM (16th-note steps).
    const int stepFrames = kSampleRate / 8; // 125 ms
    const int kSteps = 16;
    const bool pad0[kSteps] = {true, false, false, false, true, false, false, true,
                               true, false, false, false, true, false, false, false};
    const bool pad1[kSteps] = {false, false, false, false, true, false, false, false,
                               false, false, false, false, true, false, true, false};

    std::vector<float> render;
    render.reserve(static_cast<std::size_t>(stepFrames) * kSteps * 2);

    float left[kBlockSize];
    float right[kBlockSize];
    float* channels[2] = {left, right};

    for(int step = 0; step < kSteps; ++step)
    {
        if(pad0[step])
            plugin.previewTriggerPad(0, 1.0f);
        if(hasPad1 && pad1[step])
            plugin.previewTriggerPad(1, 0.85f);

        int remaining = stepFrames;
        while(remaining > 0)
        {
            const int n = remaining < kBlockSize ? remaining : kBlockSize;
            plugin.previewRender(channels, 2, n);
            for(int i = 0; i < n; ++i)
            {
                render.push_back(left[i]);
                render.push_back(right[i]);
            }
            remaining -= n;
        }
    }

    plugin.setProcessing(false);
    plugin.setActive(false);

    const std::string outPath = "build/mlacker_offline_render.wav";
    if(!writeWav(outPath, render, 2, kSampleRate))
    {
        std::fprintf(stderr, "failed to write %s\n", outPath.c_str());
        return 1;
    }

    std::printf("[mlacker] rendered %zu frames to %s\n",
                render.size() / 2, outPath.c_str());
    return 0;
}
