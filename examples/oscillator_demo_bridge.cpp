#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "../include/mlang_c_types.h"

namespace {

struct mlang_list_t
{
    std::int64_t size;
    void* data;
};

struct StereoWav
{
    std::vector<float> left;
    std::vector<float> right;
    std::uint32_t sampleRate {0};
};

struct PlaybackState
{
    AudioQueueRef queue {nullptr};
    StereoWav wav;
    std::atomic<std::uint64_t> frameCursor {0};
    std::atomic<std::uint64_t> totalFrames {0};
    std::atomic<std::uint32_t> sampleRate {0};
    std::atomic<int> running {0};
    std::atomic<int> finished {0};
    std::mutex ringMutex;
    std::vector<float> ring;
    std::uint32_t ringWrite {0};
    std::string lastError;
};

PlaybackState g_state;

constexpr std::uint32_t kFramesPerBuffer = 512;
constexpr std::uint32_t kBufferCount = 3;
constexpr std::uint32_t kRingCapacity = 32768;

void setLastError(const char* text)
{
    g_state.lastError = text ? text : "unknown error";
}

void clearLastError()
{
    g_state.lastError.clear();
}

void setAudioError(const char* stage, OSStatus status)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s failed: %d", stage, static_cast<int>(status));
    setLastError(buf);
}

std::uint32_t readU32LE(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t readU16LE(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

std::int16_t readI16LE(const std::uint8_t* p)
{
    return static_cast<std::int16_t>(readU16LE(p));
}

std::int32_t readI24LE(const std::uint8_t* p)
{
    std::int32_t v = static_cast<std::int32_t>(p[0]) |
                     (static_cast<std::int32_t>(p[1]) << 8) |
                     (static_cast<std::int32_t>(p[2]) << 16);
    if((v & 0x00800000) != 0)
        v |= static_cast<std::int32_t>(0xFF000000);
    return v;
}

std::int32_t readI32LE(const std::uint8_t* p)
{
    return static_cast<std::int32_t>(readU32LE(p));
}

float clampUnit(float x)
{
    if(x > 1.0f)
        return 1.0f;
    if(x < -1.0f)
        return -1.0f;
    return x;
}

bool loadWav(const char* path, StereoWav& out)
{
    std::FILE* f = std::fopen(path, "rb");
    if(!f)
    {
        setLastError(std::strerror(errno));
        return false;
    }

    std::uint8_t header[12];
    if(std::fread(header, 1, sizeof(header), f) != sizeof(header))
    {
        std::fclose(f);
        setLastError("failed to read wav header");
        return false;
    }
    if(std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
    {
        std::fclose(f);
        setLastError("input is not a RIFF/WAVE file");
        return false;
    }

    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint32_t dataSize = 0;
    long dataOffset = 0;
    bool hasFmt = false;
    bool hasData = false;

    while(!hasData)
    {
        std::uint8_t chunkHeader[8];
        if(std::fread(chunkHeader, 1, sizeof(chunkHeader), f) != sizeof(chunkHeader))
            break;

        const std::uint32_t chunkSize = readU32LE(chunkHeader + 4);
        if(std::memcmp(chunkHeader, "fmt ", 4) == 0)
        {
            std::vector<std::uint8_t> fmt(chunkSize);
            if(chunkSize < 16 || std::fread(fmt.data(), 1, chunkSize, f) != chunkSize)
            {
                std::fclose(f);
                setLastError("failed to read wav fmt chunk");
                return false;
            }
            audioFormat = readU16LE(fmt.data() + 0);
            channels = readU16LE(fmt.data() + 2);
            sampleRate = readU32LE(fmt.data() + 4);
            blockAlign = readU16LE(fmt.data() + 12);
            bitsPerSample = readU16LE(fmt.data() + 14);
            if(audioFormat == 0xFFFE && chunkSize >= 26)
                audioFormat = readU16LE(fmt.data() + 24);
            hasFmt = true;
        }
        else if(std::memcmp(chunkHeader, "data", 4) == 0)
        {
            dataOffset = std::ftell(f);
            dataSize = chunkSize;
            if(std::fseek(f, static_cast<long>(chunkSize), SEEK_CUR) != 0)
            {
                std::fclose(f);
                setLastError("failed to seek wav data");
                return false;
            }
            hasData = true;
        }
        else if(std::fseek(f, static_cast<long>(chunkSize), SEEK_CUR) != 0)
        {
            std::fclose(f);
            setLastError("failed to skip wav chunk");
            return false;
        }

        if((chunkSize & 1u) != 0)
            (void)std::fseek(f, 1, SEEK_CUR);
    }

    const bool isPcm = (audioFormat == 1);
    const bool isFloat = (audioFormat == 3);
    const bool supportedBits = (bitsPerSample == 16 || bitsPerSample == 24 ||
                                bitsPerSample == 32 || bitsPerSample == 64);
    if(!hasFmt || !hasData || channels == 0 || channels > 2 || sampleRate == 0 ||
       (!isPcm && !isFloat) || !supportedBits || blockAlign == 0)
    {
        std::fclose(f);
        setLastError("unsupported wav format");
        return false;
    }
    if(isFloat && !(bitsPerSample == 32 || bitsPerSample == 64))
    {
        std::fclose(f);
        setLastError("unsupported floating-point wav format");
        return false;
    }

    if(std::fseek(f, dataOffset, SEEK_SET) != 0)
    {
        std::fclose(f);
        setLastError("failed to seek to wav sample data");
        return false;
    }

    const int bytesPerFrame = static_cast<int>(blockAlign);
    const int frameCount = bytesPerFrame <= 0 ? 0 : static_cast<int>(dataSize / blockAlign);
    if(frameCount <= 0)
    {
        std::fclose(f);
        setLastError("wav data chunk is empty");
        return false;
    }

    std::vector<std::uint8_t> raw(dataSize);
    if(std::fread(raw.data(), 1, dataSize, f) != dataSize)
    {
        std::fclose(f);
        setLastError("failed to read wav sample data");
        return false;
    }
    std::fclose(f);

    out.left.assign(static_cast<std::size_t>(frameCount), 0.0f);
    out.right.assign(static_cast<std::size_t>(frameCount), 0.0f);
    out.sampleRate = sampleRate;

    for(int i = 0; i < frameCount; ++i)
    {
        const std::uint8_t* frame = raw.data() + static_cast<std::size_t>(i) * bytesPerFrame;
        for(std::uint16_t channel = 0; channel < channels; ++channel)
        {
            const std::uint8_t* sample = frame + channel * (bitsPerSample / 8);
            float v = 0.0f;
            if(isPcm)
            {
                if(bitsPerSample == 16)
                    v = static_cast<float>(readI16LE(sample) / 32768.0);
                else if(bitsPerSample == 24)
                    v = static_cast<float>(readI24LE(sample) / 8388608.0);
                else if(bitsPerSample == 32)
                    v = static_cast<float>(readI32LE(sample) / 2147483648.0);
                else if(bitsPerSample == 64)
                {
                    std::int64_t x = 0;
                    std::memcpy(&x, sample, sizeof(x));
                    v = static_cast<float>(static_cast<double>(x) / 9223372036854775808.0);
                }
            }
            else
            {
                if(bitsPerSample == 32)
                {
                    float x = 0.0f;
                    std::memcpy(&x, sample, sizeof(x));
                    v = x;
                }
                else if(bitsPerSample == 64)
                {
                    double x = 0.0;
                    std::memcpy(&x, sample, sizeof(x));
                    v = static_cast<float>(x);
                }
            }

            v = clampUnit(v);
            if(channel == 0)
                out.left[static_cast<std::size_t>(i)] = v;
            if(channel == 1)
                out.right[static_cast<std::size_t>(i)] = v;
        }
        if(channels == 1)
            out.right[static_cast<std::size_t>(i)] = out.left[static_cast<std::size_t>(i)];
    }

    clearLastError();
    return true;
}

void stopPlayback()
{
    if(g_state.queue != nullptr)
    {
        AudioQueueStop(g_state.queue, true);
        AudioQueueDispose(g_state.queue, true);
        g_state.queue = nullptr;
    }
    g_state.running.store(0);
    g_state.finished.store(1);
}

void writeRingSample(float v)
{
    std::lock_guard<std::mutex> lock(g_state.ringMutex);
    if(g_state.ring.empty())
        return;
    g_state.ring[static_cast<std::size_t>(g_state.ringWrite)] = clampUnit(v);
    g_state.ringWrite = (g_state.ringWrite + 1u) % static_cast<std::uint32_t>(g_state.ring.size());
}

void fillBuffer(AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    float* samples = static_cast<float*>(buffer->mAudioData);
    const std::uint64_t frameCursor = g_state.frameCursor.load();
    const std::uint64_t totalFrames = g_state.totalFrames.load();

    if(frameCursor >= totalFrames)
    {
        std::memset(samples, 0, kFramesPerBuffer * 2 * sizeof(float));
        buffer->mAudioDataByteSize = kFramesPerBuffer * 2 * sizeof(float);
        g_state.running.store(0);
        g_state.finished.store(1);
        AudioQueueStop(queue, false);
        return;
    }

    const std::uint32_t renderFrames = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(kFramesPerBuffer, totalFrames - frameCursor));
    for(std::uint32_t i = 0; i < renderFrames; ++i)
    {
        const std::size_t idx = static_cast<std::size_t>(frameCursor + i);
        const float left = g_state.wav.left[idx];
        const float right = g_state.wav.right[idx];
        samples[i * 2] = left;
        samples[i * 2 + 1] = right;
        writeRingSample((left + right) * 0.5f);
    }

    if(renderFrames < kFramesPerBuffer)
    {
        const std::size_t tail = static_cast<std::size_t>(kFramesPerBuffer - renderFrames) * 2;
        std::memset(samples + renderFrames * 2, 0, tail * sizeof(float));
    }

    buffer->mAudioDataByteSize = kFramesPerBuffer * 2 * sizeof(float);
    g_state.frameCursor.store(frameCursor + renderFrames);
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

void outputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    (void)userData;
    fillBuffer(queue, buffer);
}

} // namespace

extern "C" int32_t oscillator_demo_start(mlang_string wavPath)
{
    if(!wavPath || !wavPath[0])
    {
        setLastError("wav path is empty");
        return -1;
    }

    stopPlayback();

    StereoWav wav;
    if(!loadWav(wavPath, wav))
        return -1;

    g_state.wav = std::move(wav);
    g_state.frameCursor.store(0);
    g_state.totalFrames.store(static_cast<std::uint64_t>(g_state.wav.left.size()));
    g_state.sampleRate.store(g_state.wav.sampleRate);
    g_state.finished.store(0);
    g_state.running.store(1);
    g_state.ring.assign(kRingCapacity, 0.0f);
    g_state.ringWrite = 0;

    AudioStreamBasicDescription format {};
    format.mSampleRate = static_cast<Float64>(g_state.wav.sampleRate);
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = 2 * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 2 * sizeof(float);
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;

    const OSStatus createRc = AudioQueueNewOutput(
        &format, outputCallback, nullptr, nullptr, nullptr, 0, &g_state.queue);
    if(createRc != noErr)
    {
        setAudioError("AudioQueueNewOutput", createRc);
        stopPlayback();
        return -1;
    }

    const std::uint32_t bufferBytes = kFramesPerBuffer * 2 * sizeof(float);
    for(std::uint32_t i = 0; i < kBufferCount; ++i)
    {
        AudioQueueBufferRef buffer = nullptr;
        const OSStatus allocRc = AudioQueueAllocateBuffer(g_state.queue, bufferBytes, &buffer);
        if(allocRc != noErr)
        {
            setAudioError("AudioQueueAllocateBuffer", allocRc);
            stopPlayback();
            return -1;
        }
        fillBuffer(g_state.queue, buffer);
    }

    const OSStatus startRc = AudioQueueStart(g_state.queue, nullptr);
    if(startRc != noErr)
    {
        setAudioError("AudioQueueStart", startRc);
        stopPlayback();
        return -1;
    }

    clearLastError();
    return 0;
}

extern "C" int32_t oscillator_demo_stop(void)
{
    stopPlayback();
    clearLastError();
    return 0;
}

extern "C" int32_t oscillator_demo_running(void)
{
    return g_state.running.load() != 0 ? 1 : 0;
}

extern "C" int64_t oscillator_demo_sample_rate(void)
{
    return static_cast<std::int64_t>(g_state.sampleRate.load());
}

extern "C" int64_t oscillator_demo_total_frames(void)
{
    return static_cast<std::int64_t>(g_state.totalFrames.load());
}

extern "C" int64_t oscillator_demo_current_frame(void)
{
    return static_cast<std::int64_t>(g_state.frameCursor.load());
}

extern "C" mlang_string oscillator_demo_last_error(void)
{
    return g_state.lastError.empty() ? "" : g_state.lastError.c_str();
}

extern "C" mlang_list_t oscillator_demo_snapshot_i64(int64_t window)
{
    if(window <= 0)
        return {0, nullptr};

    std::int64_t* out = static_cast<std::int64_t*>(
        std::malloc(static_cast<std::size_t>(window) * sizeof(std::int64_t)));
    if(!out)
        return {0, nullptr};

    std::lock_guard<std::mutex> lock(g_state.ringMutex);
    const std::uint32_t ringSize = static_cast<std::uint32_t>(g_state.ring.size());
    if(ringSize == 0)
    {
        std::memset(out, 0, static_cast<std::size_t>(window) * sizeof(std::int64_t));
        return {window, out};
    }

    for(std::int64_t i = 0; i < window; ++i)
    {
        const std::uint32_t offset = static_cast<std::uint32_t>(window - i);
        const std::uint32_t idx =
            (g_state.ringWrite + ringSize - (offset % ringSize)) % ringSize;
        const float sample = g_state.ring[idx];
        out[static_cast<std::size_t>(i)] =
            static_cast<std::int64_t>(std::lrint(sample * 1000.0f));
    }

    return {window, out};
}
