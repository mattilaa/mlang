#include "drum_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mlacker_drum {

namespace {

std::uint32_t readU32LE(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t readU16LE(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

} // namespace

void DrumEngine::setSampleRate(float sampleRate)
{
    if(sampleRate > 1000.0f)
        engineSampleRate_ = sampleRate;
}

void DrumEngine::reset()
{
    allVoicesOff();
    for(int pad = 0; pad < kNumPads; ++pad)
        clearPad(pad);
}

void DrumEngine::clearPad(int pad)
{
    if(pad < 0 || pad >= kNumPads)
        return;
    pads_[pad] = PadSample {};
}

bool DrumEngine::padLoaded(int pad) const
{
    if(pad < 0 || pad >= kNumPads)
        return false;
    return pads_[pad].loaded();
}

const std::string& DrumEngine::padPath(int pad) const
{
    static const std::string kEmpty;
    if(pad < 0 || pad >= kNumPads)
        return kEmpty;
    return pads_[pad].path;
}

void DrumEngine::loadPadPcm(int pad,
                            const float* interleaved,
                            int frames,
                            int channels,
                            int sourceSampleRate)
{
    if(pad < 0 || pad >= kNumPads || interleaved == nullptr || frames <= 0 ||
       channels <= 0)
        return;

    PadSample sample;
    sample.sourceSampleRate =
        sourceSampleRate > 0 ? sourceSampleRate
                             : static_cast<int>(engineSampleRate_);
    sample.mono.resize(static_cast<std::size_t>(frames));

    const float inv = 1.0f / static_cast<float>(channels);
    for(int frame = 0; frame < frames; ++frame)
    {
        float sum = 0.0f;
        for(int ch = 0; ch < channels; ++ch)
            sum += interleaved[static_cast<std::size_t>(frame) * channels + ch];
        sample.mono[static_cast<std::size_t>(frame)] = sum * inv;
    }

    pads_[pad] = std::move(sample);
}

bool DrumEngine::loadPadWav(int pad, const std::string& path)
{
    if(pad < 0 || pad >= kNumPads)
        return false;

    FILE* f = std::fopen(path.c_str(), "rb");
    if(!f)
        return false;

    std::uint8_t header[12];
    if(std::fread(header, 1, sizeof(header), f) != sizeof(header) ||
       std::memcmp(header, "RIFF", 4) != 0 ||
       std::memcmp(header + 8, "WAVE", 4) != 0)
    {
        std::fclose(f);
        return false;
    }

    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint32_t dataSize = 0;
    long dataOffset = 0;
    bool hasFmt = false;
    bool hasData = false;

    while(!hasData)
    {
        std::uint8_t chunkHdr[8];
        if(std::fread(chunkHdr, 1, sizeof(chunkHdr), f) != sizeof(chunkHdr))
            break;

        const std::uint32_t chunkSize = readU32LE(chunkHdr + 4);
        if(std::memcmp(chunkHdr, "fmt ", 4) == 0)
        {
            std::vector<std::uint8_t> fmt(chunkSize);
            if(chunkSize < 16 ||
               std::fread(fmt.data(), 1, chunkSize, f) != chunkSize)
            {
                std::fclose(f);
                return false;
            }
            const std::uint16_t format = readU16LE(fmt.data() + 0);
            channels = readU16LE(fmt.data() + 2);
            sampleRate = readU32LE(fmt.data() + 4);
            bitsPerSample = readU16LE(fmt.data() + 14);
            if(format != 1)
            {
                std::fclose(f);
                return false;
            }
            hasFmt = true;
        }
        else if(std::memcmp(chunkHdr, "data", 4) == 0)
        {
            dataOffset = std::ftell(f);
            dataSize = chunkSize;
            if(std::fseek(f, static_cast<long>(chunkSize), SEEK_CUR) != 0)
            {
                std::fclose(f);
                return false;
            }
            hasData = true;
        }
        else if(std::fseek(f, static_cast<long>(chunkSize), SEEK_CUR) != 0)
        {
            std::fclose(f);
            return false;
        }

        if((chunkSize & 1u) != 0)
            (void)std::fseek(f, 1, SEEK_CUR);
    }

    if(!hasFmt || !hasData || bitsPerSample != 16 || channels == 0 ||
       sampleRate == 0 || std::fseek(f, dataOffset, SEEK_SET) != 0)
    {
        std::fclose(f);
        return false;
    }

    const int totalSamples = static_cast<int>(dataSize / 2u);
    const int frames = totalSamples / static_cast<int>(channels);
    if(frames <= 0)
    {
        std::fclose(f);
        return false;
    }

    std::vector<std::int16_t> pcm(static_cast<std::size_t>(totalSamples));
    if(std::fread(pcm.data(), 1, dataSize, f) != dataSize)
    {
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    PadSample sample;
    sample.sourceSampleRate = static_cast<int>(sampleRate);
    sample.path = path;
    sample.mono.resize(static_cast<std::size_t>(frames));

    const float inv = 1.0f / (32768.0f * static_cast<float>(channels));
    for(int frame = 0; frame < frames; ++frame)
    {
        int sum = 0;
        for(int ch = 0; ch < static_cast<int>(channels); ++ch)
            sum += pcm[static_cast<std::size_t>(frame) * channels + ch];
        sample.mono[static_cast<std::size_t>(frame)] =
            static_cast<float>(sum) * inv;
    }

    pads_[pad] = std::move(sample);
    return true;
}

void DrumEngine::trigger(int pad, float velocity)
{
    if(!padLoaded(pad))
        return;

    Voice* slot = nullptr;
    for(Voice& voice : voices_)
    {
        if(!voice.active)
        {
            slot = &voice;
            break;
        }
    }
    if(!slot)
        slot = &voices_[0]; // steal voice 0 when the pool is full

    const PadSample& sample = pads_[pad];
    slot->active = true;
    slot->pad = pad;
    slot->pos = 0.0;
    slot->step = static_cast<double>(sample.sourceSampleRate) /
                 static_cast<double>(engineSampleRate_);
    slot->gain = std::clamp(velocity, 0.0f, 1.0f);
}

void DrumEngine::allVoicesOff()
{
    for(Voice& voice : voices_)
        voice = Voice {};
}

void DrumEngine::render(float* left, float* right, int numFrames, DspBridge& dsp)
{
    for(int frame = 0; frame < numFrames; ++frame)
    {
        float mix = 0.0f;

        for(Voice& voice : voices_)
        {
            if(!voice.active)
                continue;

            const PadSample& sample = pads_[voice.pad];
            const auto index = static_cast<std::size_t>(voice.pos);
            if(index + 1 >= sample.mono.size())
            {
                voice.active = false;
                continue;
            }

            const float frac = static_cast<float>(voice.pos - static_cast<double>(index));
            const float a = sample.mono[index];
            const float b = sample.mono[index + 1];
            mix += (a + (b - a) * frac) * voice.gain;

            voice.pos += voice.step;
        }

        const float shaped = dsp.processSample(mix);
        left[frame] = shaped;
        right[frame] = shaped;
    }
}

} // namespace mlacker_drum
