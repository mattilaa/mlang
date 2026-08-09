#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "filter_bridge.h"

namespace {

enum class FilterMode : std::int32_t
{
    Lowpass = 1,
    Highpass = 2,
    Bandpass = 3,
};

const char* filterModeName(FilterMode mode)
{
    switch(mode)
    {
    case FilterMode::Lowpass: return "low-pass 24 dB/octave";
    case FilterMode::Highpass: return "high-pass 24 dB/octave";
    case FilterMode::Bandpass: return "band-pass 24 dB/octave";
    }
    return "unknown";
}

bool parseFilterMode(const std::string& value, FilterMode& mode)
{
    if(value == "lowpass24" || value == "lowpass")
        mode = FilterMode::Lowpass;
    else if(value == "highpass24" || value == "highpass")
        mode = FilterMode::Highpass;
    else if(value == "bandpass24" || value == "bandpass")
        mode = FilterMode::Bandpass;
    else
        return false;
    return true;
}

void printUsage(const char* program)
{
    std::printf("CoreAudio real-time 24 dB filter sweep demo\n\n");
    std::printf("Usage:\n");
    std::printf("  %s [--filter MODE] [WAV]\n", program);
    std::printf("  %s --validate [WAV]\n\n", program);
    std::printf("Options:\n");
    std::printf("  --filter MODE  Select a 24 dB/octave filter (default: lowpass24)\n");
    std::printf("  --validate     Validate WAV decoding without opening an audio device\n");
    std::printf("  -h, --help     Show this help\n\n");
    std::printf("Filter modes:\n");
    std::printf("  lowpass24      Fourth-order low-pass cutoff sweeps\n");
    std::printf("  highpass24     Fourth-order high-pass cutoff sweeps\n");
    std::printf("  bandpass24     Cascaded fourth-order band-pass center sweeps\n");
    std::printf("  Short aliases: lowpass, highpass, bandpass\n\n");
    std::printf("Input:\n");
    std::printf("  WAV must be mono or stereo 16-bit PCM. The default is illusion.wav.\n");
    std::printf("  Sweep timing automatically spans the complete WAV length.\n\n");
    std::printf("Display note:\n");
    std::printf("  24 dB/octave is the filter slope. Resonance targets such as +18 dB\n");
    std::printf("  are independent resonance gain settings, not filter slopes.\n\n");
    std::printf("Examples:\n");
    std::printf("  %s --filter lowpass24\n", program);
    std::printf("  %s --filter highpass24 /path/to/sample.wav\n", program);
    std::printf("  %s --filter bandpass24 /path/to/sample.wav\n", program);
    std::printf("  %s --validate /path/to/sample.wav\n", program);
}

struct WavData
{
    std::uint32_t sampleRate {0};
    std::uint16_t channels {0};
    std::vector<float> samples;
};

std::uint16_t readU16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t readU32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool loadPcm16Wav(const std::string& path, WavData& out, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if(!stream)
    {
        error = "cannot open " + path;
        return false;
    }
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    if(size < 44)
    {
        error = "WAV file is too short";
        return false;
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    if(!stream || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
       std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    std::uint16_t format = 0;
    std::uint16_t bits = 0;
    const unsigned char* pcm = nullptr;
    std::size_t pcmBytes = 0;
    std::size_t offset = 12;
    while(offset + 8 <= bytes.size())
    {
        const unsigned char* chunk = bytes.data() + offset;
        const std::uint32_t chunkSize = readU32(chunk + 4);
        const std::size_t dataOffset = offset + 8;
        if(dataOffset + chunkSize > bytes.size())
            break;
        if(std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            format = readU16(bytes.data() + dataOffset);
            out.channels = readU16(bytes.data() + dataOffset + 2);
            out.sampleRate = readU32(bytes.data() + dataOffset + 4);
            bits = readU16(bytes.data() + dataOffset + 14);
        }
        else if(std::memcmp(chunk, "data", 4) == 0)
        {
            pcm = bytes.data() + dataOffset;
            pcmBytes = chunkSize;
        }
        offset = dataOffset + chunkSize + (chunkSize & 1U);
    }

    if(format != 1 || bits != 16 || out.channels == 0 || out.channels > 2 ||
       out.sampleRate == 0 || pcm == nullptr)
    {
        error = "example requires mono or stereo 16-bit PCM WAV input";
        return false;
    }

    const std::size_t sampleCount = pcmBytes / sizeof(std::int16_t);
    out.samples.resize(sampleCount);
    for(std::size_t i = 0; i < sampleCount; ++i)
    {
        const auto raw = static_cast<std::int16_t>(readU16(pcm + i * 2));
        out.samples[i] = static_cast<float>(raw) / 32768.0f;
    }
    return true;
}

struct Sweep
{
    const char* name;
    bool filtered;
    float cutoffStart;
    float cutoffEnd;
    float resonanceDb;
    std::uint32_t durationWeight;
};

constexpr Sweep kSweeps[] = {
    {"dry reference", false, 12000.0f, 12000.0f, 0.0f, 3},
    {"falling cutoff, no resonance", true, 12000.0f, 180.0f, 0.0f, 5},
    {"rising cutoff, no resonance", true, 180.0f, 12000.0f, 0.0f, 5},
    {"falling cutoff, increasing resonance", true, 12000.0f, 180.0f, 18.0f, 5},
    {"rising cutoff, resonant", true, 180.0f, 12000.0f, 18.0f, 5},
};

constexpr std::size_t kSweepCount = sizeof(kSweeps) / sizeof(kSweeps[0]);

constexpr std::uint32_t sweepWeightTotal()
{
    std::uint32_t total = 0;
    for(const Sweep& sweep : kSweeps)
        total += sweep.durationWeight;
    return total;
}

constexpr std::uint32_t kSweepWeightTotal = sweepWeightTotal();

struct PlaybackState
{
    AudioQueueRef queue {nullptr};
    const WavData* wav {nullptr};
    std::uint64_t framesRendered {0};
    std::uint64_t totalFrames {0};
    std::array<std::uint64_t, kSweepCount + 1> sweepBoundaries {};
    std::uint32_t framesPerBuffer {512};
    int activeSweep {-1};
    std::atomic<int> visibleSweep {-1};
    std::atomic<bool> finished {false};
};

void buildSweepSchedule(PlaybackState& state)
{
    std::uint32_t cumulativeWeight = 0;
    state.sweepBoundaries[0] = 0;
    for(std::size_t i = 0; i < kSweepCount; ++i)
    {
        cumulativeWeight += kSweeps[i].durationWeight;
        state.sweepBoundaries[i + 1] =
            state.totalFrames * cumulativeWeight / kSweepWeightTotal;
    }
    state.sweepBoundaries[kSweepCount] = state.totalFrames;
}

int sweepForFrame(const PlaybackState& state, std::uint64_t frame)
{
    for(std::size_t i = 0; i < kSweepCount; ++i)
    {
        if(frame < state.sweepBoundaries[i + 1])
            return static_cast<int>(i);
    }
    return static_cast<int>(kSweepCount);
}

void enterSweep(PlaybackState& state, int index)
{
    state.activeSweep = index;
    state.visibleSweep.store(index, std::memory_order_release);
    if(index < 0 || index >= static_cast<int>(kSweepCount))
        return;
    const Sweep& sweep = kSweeps[index];
    const auto rampSamples = static_cast<std::int64_t>(
        state.sweepBoundaries[index + 1] - state.sweepBoundaries[index]);
    mlang::coreaudio_filter::setTarget(
        sweep.cutoffEnd, sweep.resonanceDb, rampSamples);
    const auto crossfadeSamples = std::min<std::int64_t>(
        rampSamples, static_cast<std::int64_t>(state.wav->sampleRate / 50));
    mlang::coreaudio_filter::setWetTarget(
        sweep.filtered ? 1.0f : 0.0f, crossfadeSamples);
}

void fillBuffer(AudioQueueRef queue, AudioQueueBufferRef buffer,
                PlaybackState& state)
{
    float* output = static_cast<float*>(buffer->mAudioData);
    const WavData& wav = *state.wav;
    const std::uint64_t sourceFrames = wav.samples.size() / wav.channels;
    const std::uint32_t channels = 2;

    for(std::uint32_t i = 0; i < state.framesPerBuffer; ++i)
    {
        if(state.framesRendered >= state.totalFrames ||
           state.framesRendered >= sourceFrames)
        {
            output[i * channels] = 0.0f;
            output[i * channels + 1] = 0.0f;
            continue;
        }

        const int sweepIndex = sweepForFrame(state, state.framesRendered);
        if(sweepIndex != state.activeSweep)
            enterSweep(state, sweepIndex);

        const std::uint64_t sourceIndex = state.framesRendered * wav.channels;
        float left = wav.samples[sourceIndex];
        float right = wav.channels == 2 ? wav.samples[sourceIndex + 1] : left;
        mlang::coreaudio_filter::beginFrame();
        left = mlang::coreaudio_filter::processLeft(left);
        right = mlang::coreaudio_filter::processRight(right);

        output[i * channels] = std::clamp(left * 0.55f, -0.98f, 0.98f);
        output[i * channels + 1] = std::clamp(right * 0.55f, -0.98f, 0.98f);
        ++state.framesRendered;
    }

    buffer->mAudioDataByteSize =
        state.framesPerBuffer * channels * sizeof(float);
    if(state.framesRendered >= state.totalFrames ||
       state.framesRendered >= sourceFrames)
    {
        state.finished.store(true, std::memory_order_release);
        AudioQueueStop(queue, false);
        return;
    }
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

void outputCallback(void* userData, AudioQueueRef queue,
                    AudioQueueBufferRef buffer)
{
    fillBuffer(queue, buffer, *static_cast<PlaybackState*>(userData));
}

} // namespace

int main(int argc, char** argv)
{
    bool validateOnly = false;
    FilterMode filterMode = FilterMode::Lowpass;
    std::string wavPath = "../../examples/fft_example/illusion.wav";
    bool hasWavPath = false;
    for(int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if(argument == "--help" || argument == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
        if(argument == "--validate")
        {
            validateOnly = true;
            continue;
        }
        if(argument == "--filter")
        {
            if(i + 1 >= argc || !parseFilterMode(argv[++i], filterMode))
            {
                std::fprintf(stderr,
                    "--filter requires lowpass24, highpass24, or bandpass24\n");
                return 2;
            }
            continue;
        }
        if(!argument.empty() && argument[0] == '-')
        {
            std::fprintf(stderr, "unknown option: %s\n", argument.c_str());
            printUsage(argv[0]);
            return 2;
        }
        if(hasWavPath)
        {
            std::fprintf(stderr, "only one WAV path may be provided\n");
            return 2;
        }
        wavPath = argument;
        hasWavPath = true;
    }
    WavData wav;
    std::string error;
    if(!loadPcm16Wav(wavPath, wav, error))
    {
        std::fprintf(stderr, "WAV load failed: %s\n", error.c_str());
        return 1;
    }
    if(validateOnly)
    {
        const std::size_t frameCount = wav.samples.size() / wav.channels;
        std::printf("validated %s: %u Hz, %u channel%s, %zu frames, %.2f seconds\n",
                    wavPath.c_str(), wav.sampleRate, wav.channels,
                    wav.channels == 1 ? "" : "s",
                    frameCount, static_cast<double>(frameCount) / wav.sampleRate);
        return 0;
    }

    PlaybackState state;
    state.wav = &wav;
    state.totalFrames = wav.samples.size() / wav.channels;
    buildSweepSchedule(state);
    mlang::coreaudio_filter::reset(
        static_cast<std::int32_t>(filterMode), static_cast<float>(wav.sampleRate),
        kSweeps[0].cutoffStart, kSweeps[0].resonanceDb);

    AudioStreamBasicDescription format {};
    format.mSampleRate = wav.sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = 2 * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 2 * sizeof(float);
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;

    OSStatus rc = AudioQueueNewOutput(
        &format, outputCallback, &state, nullptr, nullptr, 0, &state.queue);
    if(rc != noErr)
    {
        std::fprintf(stderr, "AudioQueueNewOutput failed: %d\n", static_cast<int>(rc));
        return 1;
    }

    constexpr std::uint32_t kBufferCount = 3;
    const std::uint32_t bufferBytes = state.framesPerBuffer * 2 * sizeof(float);
    for(std::uint32_t i = 0; i < kBufferCount; ++i)
    {
        AudioQueueBufferRef buffer = nullptr;
        rc = AudioQueueAllocateBuffer(state.queue, bufferBytes, &buffer);
        if(rc != noErr)
        {
            std::fprintf(stderr, "AudioQueueAllocateBuffer failed: %d\n", static_cast<int>(rc));
            AudioQueueDispose(state.queue, true);
            return 1;
        }
        fillBuffer(state.queue, buffer, state);
    }

    std::printf("CoreAudio realtime dsp::filter demo: %s\n",
                filterModeName(filterMode));
    std::printf("source: %s (%u Hz, %u channel%s)\n", wavPath.c_str(),
                wav.sampleRate, wav.channels, wav.channels == 1 ? "" : "s");
    std::printf("demo duration: %.2f seconds (full source)\n",
                static_cast<double>(state.totalFrames) / wav.sampleRate);
    for(std::size_t i = 0; i < kSweepCount; ++i)
    {
        const double startSeconds =
            static_cast<double>(state.sweepBoundaries[i]) / wav.sampleRate;
        const double endSeconds =
            static_cast<double>(state.sweepBoundaries[i + 1]) / wav.sampleRate;
        if(kSweeps[i].filtered)
        {
            std::printf("  %zu. %6.2f-%6.2f s  %s\n",
                        i + 1, startSeconds, endSeconds, kSweeps[i].name);
            std::printf("      filter: %s; resonance target: %+.0f dB\n",
                        filterModeName(filterMode), kSweeps[i].resonanceDb);
        }
        else
        {
            std::printf("  %zu. %6.2f-%6.2f s  %s [unfiltered]\n",
                        i + 1, startSeconds, endSeconds, kSweeps[i].name);
        }
    }

    rc = AudioQueueStart(state.queue, nullptr);
    if(rc != noErr)
    {
        std::fprintf(stderr, "AudioQueueStart failed: %d\n", static_cast<int>(rc));
        AudioQueueDispose(state.queue, true);
        return 1;
    }

    int announced = -1;
    while(!state.finished.load(std::memory_order_acquire))
    {
        const int current = state.visibleSweep.load(std::memory_order_acquire);
        if(current != announced && current >= 0 && current < static_cast<int>(kSweepCount))
        {
            announced = current;
            const Sweep& sweep = kSweeps[current];
            if(sweep.filtered)
            {
                std::printf("now: %s [%s; resonance target %+.0f dB]\n",
                            sweep.name, filterModeName(filterMode),
                            sweep.resonanceDb);
            }
            else
            {
                std::printf("now: %s [unfiltered]\n", sweep.name);
            }
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    AudioQueueDispose(state.queue, true);
    return 0;
}
