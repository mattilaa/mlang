#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "../../../include/mlang_c_types.h"

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
    std::string outputDeviceName;
    std::atomic<std::uint64_t> frameCursor {0};
    std::atomic<std::uint64_t> totalFrames {0};
    std::atomic<std::uint32_t> sampleRate {0};
    std::atomic<int> running {0};
    std::atomic<int> finished {0};
    std::mutex ringMutex;
    std::vector<float> ringLeft;
    std::vector<float> ringRight;
    std::uint32_t ringWrite {0};
    std::atomic<float> levelLeft {0.0f};
    std::atomic<float> levelRight {0.0f};
    std::string lastError;
};

PlaybackState g_state;

constexpr std::uint32_t kFramesPerBuffer = 512;
constexpr std::uint32_t kBufferCount = 3;
constexpr std::uint32_t kRingCapacity = 32768;

struct OutputDeviceInfo
{
    AudioDeviceID id {kAudioObjectUnknown};
    std::string uid;
    std::string name;
    std::uint32_t outputChannels {0};
    Float64 nominalSampleRate {0.0};
    bool isDefaultOutput {false};
};

void setLastError(const char* text)
{
    g_state.lastError = text ? text : "unknown error";
}

void clearLastError()
{
    g_state.lastError.clear();
}

char* dupString(const std::string& s)
{
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if(!out)
        return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
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

std::string cfStringToUtf8(CFStringRef s)
{
    if(!s)
        return {};
    const CFIndex length = CFStringGetLength(s);
    const CFIndex maxSize =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(maxSize), '\0');
    if(!CFStringGetCString(s, out.data(), maxSize, kCFStringEncodingUTF8))
        return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::string asciiLower(std::string s)
{
    for(char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool hasOutputStreams(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr {
        kAudioDevicePropertyStreams,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    const OSStatus rc =
        AudioObjectGetPropertyDataSize(id, &addr, 0, nullptr, &size);
    return rc == noErr && size > 0;
}

std::uint32_t outputChannelCount(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr {
        kAudioDevicePropertyStreamConfiguration,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if(AudioObjectGetPropertyDataSize(id, &addr, 0, nullptr, &size) != noErr || size == 0)
        return 0;

    std::vector<std::uint8_t> bytes(size);
    auto* bufferList = reinterpret_cast<AudioBufferList*>(bytes.data());
    if(AudioObjectGetPropertyData(id, &addr, 0, nullptr, &size, bufferList) != noErr)
        return 0;

    std::uint32_t channels = 0;
    for(UInt32 i = 0; i < bufferList->mNumberBuffers; ++i)
        channels += bufferList->mBuffers[i].mNumberChannels;
    return channels;
}

Float64 deviceNominalSampleRate(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    Float64 rate = 0.0;
    UInt32 size = sizeof(rate);
    if(AudioObjectGetPropertyData(id, &addr, 0, nullptr, &size, &rate) != noErr)
        return 0.0;
    return rate;
}

AudioDeviceID defaultOutputDeviceId()
{
    AudioObjectPropertyAddress addr {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioDeviceID id = kAudioObjectUnknown;
    UInt32 size = sizeof(id);
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                  &addr, 0, nullptr, &size, &id) != noErr)
        return kAudioObjectUnknown;
    return id;
}

std::vector<OutputDeviceInfo> listOutputDevicesInternal()
{
    AudioObjectPropertyAddress devicesAddr {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                      &devicesAddr, 0, nullptr, &size) != noErr ||
       size == 0)
        return {};

    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                  &devicesAddr, 0, nullptr, &size, ids.data()) != noErr)
        return {};

    const AudioDeviceID defaultOutput = defaultOutputDeviceId();
    std::vector<OutputDeviceInfo> out;
    out.reserve(ids.size());
    for(AudioDeviceID id : ids)
    {
        if(id == kAudioObjectUnknown || !hasOutputStreams(id))
            continue;

        AudioObjectPropertyAddress nameAddr {
            kAudioObjectPropertyName,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioObjectPropertyAddress uidAddr {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        CFStringRef nameRef = nullptr;
        CFStringRef uidRef = nullptr;
        UInt32 nameSize = sizeof(nameRef);
        UInt32 uidSize = sizeof(uidRef);
        if(AudioObjectGetPropertyData(id, &nameAddr, 0, nullptr, &nameSize, &nameRef) != noErr)
            continue;
        if(AudioObjectGetPropertyData(id, &uidAddr, 0, nullptr, &uidSize, &uidRef) != noErr)
        {
            if(nameRef)
                CFRelease(nameRef);
            continue;
        }

        OutputDeviceInfo info;
        info.id = id;
        info.name = cfStringToUtf8(nameRef);
        info.uid = cfStringToUtf8(uidRef);
        info.outputChannels = outputChannelCount(id);
        info.nominalSampleRate = deviceNominalSampleRate(id);
        info.isDefaultOutput = (id == defaultOutput);
        out.push_back(std::move(info));

        if(nameRef)
            CFRelease(nameRef);
        if(uidRef)
            CFRelease(uidRef);
    }
    return out;
}

bool resolveOutputDevice(const char* selector, OutputDeviceInfo& out)
{
    if(!selector || !selector[0])
        return false;

    const std::string wanted = selector;
    const std::string wantedLower = asciiLower(wanted);
    const std::vector<OutputDeviceInfo> devices = listOutputDevicesInternal();

    for(const auto& device : devices)
    {
        if(device.uid == wanted)
        {
            out = device;
            return true;
        }
    }
    for(const auto& device : devices)
    {
        if(asciiLower(device.name) == wantedLower)
        {
            out = device;
            return true;
        }
    }
    for(const auto& device : devices)
    {
        if(asciiLower(device.name).find(wantedLower) != std::string::npos)
        {
            out = device;
            return true;
        }
    }
    return false;
}

bool setQueueOutputDevice(AudioQueueRef queue, const char* selector)
{
    if(!selector || !selector[0])
    {
        g_state.outputDeviceName.clear();
        return true;
    }

    OutputDeviceInfo device;
    if(!resolveOutputDevice(selector, device))
    {
        setLastError("output device not found");
        return false;
    }

    CFStringRef uid = CFStringCreateWithCString(
        kCFAllocatorDefault, device.uid.c_str(), kCFStringEncodingUTF8);
    if(!uid)
    {
        setLastError("failed to create audio device UID");
        return false;
    }

    const OSStatus rc = AudioQueueSetProperty(
        queue, kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
    CFRelease(uid);
    if(rc != noErr)
    {
        setAudioError("AudioQueueSetProperty(CurrentDevice)", rc);
        return false;
    }

    g_state.outputDeviceName = device.name;
    return true;
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

void writeRingSample(float left, float right)
{
    std::lock_guard<std::mutex> lock(g_state.ringMutex);
    if(g_state.ringLeft.empty() || g_state.ringRight.empty())
        return;
    const std::size_t idx = static_cast<std::size_t>(g_state.ringWrite);
    g_state.ringLeft[idx] = clampUnit(left);
    g_state.ringRight[idx] = clampUnit(right);
    g_state.ringWrite =
        (g_state.ringWrite + 1u) % static_cast<std::uint32_t>(g_state.ringLeft.size());
}

void clearVisualizationState()
{
    std::lock_guard<std::mutex> lock(g_state.ringMutex);
    std::fill(g_state.ringLeft.begin(), g_state.ringLeft.end(), 0.0f);
    std::fill(g_state.ringRight.begin(), g_state.ringRight.end(), 0.0f);
    g_state.ringWrite = 0;
    g_state.levelLeft.store(0.0f);
    g_state.levelRight.store(0.0f);
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
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    for(std::uint32_t i = 0; i < renderFrames; ++i)
    {
        const std::size_t idx = static_cast<std::size_t>(frameCursor + i);
        const float left = g_state.wav.left[idx];
        const float right = g_state.wav.right[idx];
        samples[i * 2] = left;
        samples[i * 2 + 1] = right;
        peakLeft = std::max(peakLeft, std::fabs(left));
        peakRight = std::max(peakRight, std::fabs(right));
        writeRingSample(left, right);
    }
    const float heldLeft = g_state.levelLeft.load();
    const float heldRight = g_state.levelRight.load();
    g_state.levelLeft.store(std::max(peakLeft, heldLeft * 0.90f));
    g_state.levelRight.store(std::max(peakRight, heldRight * 0.90f));

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

extern "C" int32_t oscilloscope_demo_start(mlang_string wavPath,
                                           mlang_string outputDeviceSelector)
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
    g_state.ringLeft.assign(kRingCapacity, 0.0f);
    g_state.ringRight.assign(kRingCapacity, 0.0f);
    clearVisualizationState();

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

    if(!setQueueOutputDevice(g_state.queue, outputDeviceSelector))
    {
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

extern "C" int32_t oscilloscope_demo_stop(void)
{
    stopPlayback();
    clearLastError();
    return 0;
}

extern "C" int32_t oscilloscope_demo_running(void)
{
    return g_state.running.load() != 0 ? 1 : 0;
}

extern "C" int64_t oscilloscope_demo_sample_rate(void)
{
    return static_cast<std::int64_t>(g_state.sampleRate.load());
}

extern "C" int64_t oscilloscope_demo_total_frames(void)
{
    return static_cast<std::int64_t>(g_state.totalFrames.load());
}

extern "C" int64_t oscilloscope_demo_current_frame(void)
{
    return static_cast<std::int64_t>(g_state.frameCursor.load());
}

extern "C" int64_t oscilloscope_demo_seek_abs_frames(int64_t frame)
{
    const std::uint64_t totalFrames = g_state.totalFrames.load();
    if(totalFrames == 0)
        return 0;

    std::int64_t clamped = frame;
    if(clamped < 0)
        clamped = 0;
    const std::int64_t maxFrame = static_cast<std::int64_t>(totalFrames);
    if(clamped > maxFrame)
        clamped = maxFrame;

    g_state.frameCursor.store(static_cast<std::uint64_t>(clamped));
    g_state.finished.store(clamped >= maxFrame ? 1 : 0);
    clearVisualizationState();
    clearLastError();
    return clamped;
}

extern "C" int64_t oscilloscope_demo_seek_rel_frames(int64_t delta)
{
    const std::int64_t current =
        static_cast<std::int64_t>(g_state.frameCursor.load());
    return oscilloscope_demo_seek_abs_frames(current + delta);
}

extern "C" mlang_string oscilloscope_demo_last_error(void)
{
    return g_state.lastError.empty() ? "" : g_state.lastError.c_str();
}

extern "C" mlang_string oscilloscope_demo_output_device(void)
{
    return g_state.outputDeviceName.empty() ? "" : g_state.outputDeviceName.c_str();
}

extern "C" mlang_string oscilloscope_demo_list_output_devices(void)
{
    const std::vector<OutputDeviceInfo> devices = listOutputDevicesInternal();
    std::string out;
    for(const auto& device : devices)
    {
        out += device.name;
        if(!device.uid.empty())
        {
            out += "  [uid: ";
            out += device.uid;
            out += "]";
        }
        out += '\n';
    }
    if(out.empty())
        out = "No CoreAudio output devices found.\n";
    return dupString(out);
}

extern "C" mlang_string oscilloscope_demo_list_output_devices_verbose(void)
{
    const std::vector<OutputDeviceInfo> devices = listOutputDevicesInternal();
    std::string out;
    for(const auto& device : devices)
    {
        out += device.name.empty() ? "(unnamed device)" : device.name;
        if(device.isDefaultOutput)
            out += "  [default output]";
        out += '\n';

        out += "  uid: ";
        out += device.uid.empty() ? "(none)" : device.uid;
        out += '\n';

        char info[128];
        std::snprintf(info, sizeof(info),
                      "  id: %u\n  output channels: %u\n  nominal sample rate: %.0f Hz\n",
                      static_cast<unsigned>(device.id),
                      static_cast<unsigned>(device.outputChannels),
                      device.nominalSampleRate);
        out += info;
        out += '\n';
    }
    if(out.empty())
        out = "No CoreAudio output devices found.\n";
    return dupString(out);
}

extern "C" mlang_list_t oscilloscope_demo_snapshot_i64(int32_t channel, int64_t window)
{
    if(window <= 0)
        return {0, nullptr};

    std::int64_t* out = static_cast<std::int64_t*>(
        std::malloc(static_cast<std::size_t>(window) * sizeof(std::int64_t)));
    if(!out)
        return {0, nullptr};

    std::lock_guard<std::mutex> lock(g_state.ringMutex);
    const std::vector<float>& ring =
        channel == 1 ? g_state.ringRight : g_state.ringLeft;
    const std::uint32_t ringSize = static_cast<std::uint32_t>(ring.size());
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
        const float sample = ring[idx];
        out[static_cast<std::size_t>(i)] =
            static_cast<std::int64_t>(std::lrint(sample * 1000.0f));
    }

    return {window, out};
}

extern "C" int64_t oscilloscope_demo_level_i64(int32_t channel)
{
    const float level = channel == 1 ? g_state.levelRight.load() : g_state.levelLeft.load();
    return static_cast<int64_t>(std::lrint(clampUnit(level) * 1000.0f));
}
