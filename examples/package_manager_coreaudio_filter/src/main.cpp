#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "filter_bridge.h"

namespace {

enum class FilterMode : std::int32_t
{
    Lowpass24 = 1,
    Highpass24 = 2,
    Bandpass24 = 3,
    Lowpass12 = 4,
    Highpass12 = 5,
    Bandpass12 = 6,
};

enum class InterpolationMode
{
    Nearest,
    Linear,
    Hermite,
    Cubic,
    Bicubic,
};

const char* interpolationModeName(InterpolationMode mode)
{
    switch(mode)
    {
    case InterpolationMode::Nearest: return "nearest (1-point selection)";
    case InterpolationMode::Linear: return "linear (2-point)";
    case InterpolationMode::Hermite: return "Hermite 3rd-order (4-point)";
    case InterpolationMode::Cubic: return "cubic Catmull-Rom (4-point)";
    case InterpolationMode::Bicubic:
        return "bicubic (1D waveform reduction to 4-point cubic)";
    }
    return "unknown";
}

bool parseInterpolationMode(const std::string& value, InterpolationMode& mode)
{
    if(value == "nearest")
        mode = InterpolationMode::Nearest;
    else if(value == "linear")
        mode = InterpolationMode::Linear;
    else if(value == "hermite")
        mode = InterpolationMode::Hermite;
    else if(value == "cubic")
        mode = InterpolationMode::Cubic;
    else if(value == "bicubic")
        mode = InterpolationMode::Bicubic;
    else
        return false;
    return true;
}

const char* filterModeName(FilterMode mode)
{
    switch(mode)
    {
    case FilterMode::Lowpass24: return "low-pass 24 dB/octave";
    case FilterMode::Highpass24: return "high-pass 24 dB/octave";
    case FilterMode::Bandpass24: return "band-pass 24 dB/octave";
    case FilterMode::Lowpass12: return "low-pass 12 dB/octave";
    case FilterMode::Highpass12: return "high-pass 12 dB/octave";
    case FilterMode::Bandpass12: return "band-pass 12 dB/octave";
    }
    return "unknown";
}

bool parseFilterMode(const std::string& value, FilterMode& mode)
{
    if(value == "lowpass24" || value == "lowpass")
        mode = FilterMode::Lowpass24;
    else if(value == "highpass24" || value == "highpass")
        mode = FilterMode::Highpass24;
    else if(value == "bandpass24" || value == "bandpass")
        mode = FilterMode::Bandpass24;
    else if(value == "lowpass12")
        mode = FilterMode::Lowpass12;
    else if(value == "highpass12")
        mode = FilterMode::Highpass12;
    else if(value == "bandpass12")
        mode = FilterMode::Bandpass12;
    else
        return false;
    return true;
}

bool parseResonanceLimit(const std::string& value, float& result)
{
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if(end == value.c_str() || *end != '\0' || !std::isfinite(parsed) ||
       parsed < 0.0f || parsed > 36.0f)
        return false;
    result = parsed;
    return true;
}

bool parsePlaybackRate(const std::string& value, float& result)
{
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if(end == value.c_str() || *end != '\0' || !std::isfinite(parsed) ||
       parsed < 0.25f || parsed > 4.0f)
        return false;
    result = parsed;
    return true;
}

std::string expandUserPath(const std::string& path)
{
    if(path != "~" && path.rfind("~/", 0) != 0)
        return path;
    const char* home = std::getenv("HOME");
    if(!home || !home[0])
        return path;
    if(path == "~")
        return home;
    return std::string(home) + path.substr(1);
}

void printUsage(const char* program)
{
    std::printf("CoreAudio real-time 12/24 dB filter sweep demo\n\n");
    std::printf("Usage:\n");
    std::printf("  %s [--filter MODE] [AUDIO]\n", program);
    std::printf("  %s --validate [AUDIO]\n\n", program);
    std::printf("Options:\n");
    std::printf("  --filter MODE  Select filter topology and slope (default: lowpass24)\n");
    std::printf("  --max-resonance-db DB  Realtime resonance peak, 0..36 (default: 12)\n");
    std::printf("  --interpolation TYPE   nearest, linear, hermite, cubic, or bicubic\n");
    std::printf("  --playback-rate RATE   Fractional source speed, 0.25..4 (default: 1)\n");
    std::printf("  --output-device DEVICE  Select output by name, name substring, or UID\n");
    std::printf("  --list-devices          List CoreAudio output devices and UIDs\n");
    std::printf("  --verbose               Add ids, channels, and rates to device listing\n");
    std::printf("  --validate     Validate audio decoding without opening an audio device\n");
    std::printf("  -h, --help     Show this help\n\n");
    std::printf("Filter modes:\n");
    std::printf("  lowpass12      Second-order low-pass cutoff sweeps\n");
    std::printf("  highpass12     Second-order high-pass cutoff sweeps\n");
    std::printf("  bandpass12     Second-order band-pass center sweeps\n");
    std::printf("  lowpass24      Fourth-order low-pass cutoff sweeps\n");
    std::printf("  highpass24     Fourth-order high-pass cutoff sweeps\n");
    std::printf("  bandpass24     Cascaded fourth-order band-pass center sweeps\n");
    std::printf("  Short aliases: lowpass, highpass, bandpass\n\n");
    std::printf("Interpolation filters:\n");
    std::printf("  nearest        Lowest CPU; selects the closest sample\n");
    std::printf("  linear         Low CPU; blends two adjacent samples\n");
    std::printf("  hermite        Four-point 3rd-order Hermite; smooth audio derivatives\n");
    std::printf("  cubic          Four-point Catmull-Rom; same polynomial as Hermite here\n");
    std::printf("  bicubic        1D waveform reduction to cubic; 2D API remains available\n\n");
    std::printf("Input:\n");
    std::printf("  WAV, AIFF, or uncompressed AIFF-C must be mono or stereo 16-bit PCM.\n");
    std::printf("  The default is illusion.wav. Sweeps span the complete file length.\n\n");
    std::printf("Display note:\n");
    std::printf("  Filter slope and resonance gain are independent. Resonance ramps to the\n");
    std::printf("  selected peak in realtime; the default peak is +12 dB.\n\n");
    std::printf("Examples:\n");
    std::printf("  %s --filter lowpass24\n", program);
    std::printf("  %s --filter lowpass12 --max-resonance-db 6\n", program);
    std::printf("  %s --interpolation hermite --playback-rate 0.75\n", program);
    std::printf("  %s --filter highpass24 /path/to/sample.wav\n", program);
    std::printf("  %s --filter bandpass24 --output-device BlackHole /path/to/sample.aif\n", program);
    std::printf("  %s --list-devices --verbose\n", program);
    std::printf("  %s --validate /path/to/sample.aif\n", program);
}

struct AudioData
{
    std::uint32_t sampleRate {0};
    std::uint16_t channels {0};
    std::vector<float> samples;
};

struct OutputDeviceInfo
{
    AudioDeviceID id {kAudioObjectUnknown};
    std::string uid;
    std::string name;
    std::uint32_t outputChannels {0};
    Float64 nominalSampleRate {0.0};
    bool isDefaultOutput {false};
};

std::string cfStringToUtf8(CFStringRef value)
{
    if(!value)
        return {};
    const CFIndex length = CFStringGetLength(value);
    const CFIndex maxSize =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<std::size_t>(maxSize), '\0');
    if(!CFStringGetCString(value, result.data(), maxSize, kCFStringEncodingUTF8))
        return {};
    result.resize(std::strlen(result.c_str()));
    return result;
}

std::string asciiLower(std::string value)
{
    for(char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool hasOutputStreams(AudioDeviceID id)
{
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyStreams,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize(
        id, &address, 0, nullptr, &size) == noErr && size > 0;
}

std::uint32_t outputChannelCount(AudioDeviceID id)
{
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyStreamConfiguration,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if(AudioObjectGetPropertyDataSize(id, &address, 0, nullptr, &size) != noErr ||
       size == 0)
        return 0;
    std::vector<unsigned char> storage(size);
    auto* buffers = reinterpret_cast<AudioBufferList*>(storage.data());
    if(AudioObjectGetPropertyData(
           id, &address, 0, nullptr, &size, buffers) != noErr)
        return 0;
    std::uint32_t channels = 0;
    for(UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
        channels += buffers->mBuffers[i].mNumberChannels;
    return channels;
}

Float64 deviceNominalSampleRate(AudioDeviceID id)
{
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    Float64 rate = 0.0;
    UInt32 size = sizeof(rate);
    if(AudioObjectGetPropertyData(
           id, &address, 0, nullptr, &size, &rate) != noErr)
        return 0.0;
    return rate;
}

AudioDeviceID defaultOutputDeviceId()
{
    AudioObjectPropertyAddress address {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioDeviceID id = kAudioObjectUnknown;
    UInt32 size = sizeof(id);
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &address,
                                  0, nullptr, &size, &id) != noErr)
        return kAudioObjectUnknown;
    return id;
}

std::vector<OutputDeviceInfo> listOutputDevices()
{
    AudioObjectPropertyAddress devicesAddress {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devicesAddress,
                                      0, nullptr, &size) != noErr || size == 0)
        return {};
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &devicesAddress,
                                  0, nullptr, &size, ids.data()) != noErr)
        return {};

    const AudioDeviceID defaultOutput = defaultOutputDeviceId();
    std::vector<OutputDeviceInfo> devices;
    for(AudioDeviceID id : ids)
    {
        if(id == kAudioObjectUnknown || !hasOutputStreams(id))
            continue;
        AudioObjectPropertyAddress nameAddress {
            kAudioObjectPropertyName,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioObjectPropertyAddress uidAddress {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        CFStringRef nameRef = nullptr;
        CFStringRef uidRef = nullptr;
        UInt32 nameSize = sizeof(nameRef);
        UInt32 uidSize = sizeof(uidRef);
        if(AudioObjectGetPropertyData(
               id, &nameAddress, 0, nullptr, &nameSize, &nameRef) != noErr)
            continue;
        if(AudioObjectGetPropertyData(
               id, &uidAddress, 0, nullptr, &uidSize, &uidRef) != noErr)
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
        info.isDefaultOutput = id == defaultOutput;
        devices.push_back(info);
        if(nameRef)
            CFRelease(nameRef);
        if(uidRef)
            CFRelease(uidRef);
    }
    return devices;
}

void printOutputDevices(bool verbose)
{
    const std::vector<OutputDeviceInfo> devices = listOutputDevices();
    if(devices.empty())
    {
        std::printf("No CoreAudio output devices found.\n");
        return;
    }
    for(const OutputDeviceInfo& device : devices)
    {
        std::printf("%s%s\n", device.name.empty() ? "(unnamed device)" : device.name.c_str(),
                    device.isDefaultOutput ? "  [default output]" : "");
        std::printf("  uid: %s\n", device.uid.empty() ? "(none)" : device.uid.c_str());
        if(verbose)
        {
            std::printf("  id: %u\n  output channels: %u\n  nominal sample rate: %.0f Hz\n",
                        static_cast<unsigned>(device.id), device.outputChannels,
                        device.nominalSampleRate);
        }
    }
}

bool resolveOutputDevice(const std::string& selector, OutputDeviceInfo& result)
{
    const std::string wantedLower = asciiLower(selector);
    const std::vector<OutputDeviceInfo> devices = listOutputDevices();
    for(const OutputDeviceInfo& device : devices)
    {
        if(device.uid == selector)
        {
            result = device;
            return true;
        }
    }
    for(const OutputDeviceInfo& device : devices)
    {
        if(asciiLower(device.name) == wantedLower)
        {
            result = device;
            return true;
        }
    }
    for(const OutputDeviceInfo& device : devices)
    {
        if(asciiLower(device.name).find(wantedLower) != std::string::npos)
        {
            result = device;
            return true;
        }
    }
    return false;
}

bool setQueueOutputDevice(AudioQueueRef queue, const std::string& selector,
                          std::string& selectedName, std::string& error)
{
    if(selector.empty())
    {
        const AudioDeviceID defaultId = defaultOutputDeviceId();
        for(const OutputDeviceInfo& device : listOutputDevices())
        {
            if(device.id == defaultId)
            {
                selectedName = device.name;
                break;
            }
        }
        if(selectedName.empty())
            selectedName = "system default";
        return true;
    }

    OutputDeviceInfo device;
    if(!resolveOutputDevice(selector, device))
    {
        error = "output device not found: " + selector;
        return false;
    }
    CFStringRef uid = CFStringCreateWithCString(
        kCFAllocatorDefault, device.uid.c_str(), kCFStringEncodingUTF8);
    if(!uid)
    {
        error = "failed to create CoreAudio device UID";
        return false;
    }
    const OSStatus status = AudioQueueSetProperty(
        queue, kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
    CFRelease(uid);
    if(status != noErr)
    {
        error = "AudioQueueSetProperty(CurrentDevice) failed: " +
            std::to_string(static_cast<int>(status));
        return false;
    }
    selectedName = device.name;
    return true;
}

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

std::uint16_t readU16Be(const unsigned char* p)
{
    return (static_cast<std::uint16_t>(p[0]) << 8) |
           static_cast<std::uint16_t>(p[1]);
}

std::uint32_t readU32Be(const unsigned char* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

double readExtended80(const unsigned char* p)
{
    const std::uint16_t signAndExponent = readU16Be(p);
    if((signAndExponent & 0x8000U) != 0)
        return 0.0;
    const std::uint16_t exponent = signAndExponent & 0x7fffU;
    std::uint64_t mantissa = 0;
    for(int i = 0; i < 8; ++i)
        mantissa = (mantissa << 8) | p[2 + i];
    if(exponent == 0 && mantissa == 0)
        return 0.0;
    if(exponent == 0x7fffU)
        return 0.0;
    return std::ldexp(static_cast<double>(mantissa),
                      static_cast<int>(exponent) - 16383 - 63);
}

bool loadPcm16Wav(const std::string& path, AudioData& out, std::string& error)
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

bool loadPcm16Aiff(const std::string& path, AudioData& out, std::string& error)
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
    if(size < 12)
    {
        error = "AIFF file is too short";
        return false;
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    const bool isAiff = std::memcmp(bytes.data(), "FORM", 4) == 0 &&
        std::memcmp(bytes.data() + 8, "AIFF", 4) == 0;
    const bool isAifc = std::memcmp(bytes.data(), "FORM", 4) == 0 &&
        std::memcmp(bytes.data() + 8, "AIFC", 4) == 0;
    if(!stream || (!isAiff && !isAifc))
    {
        error = "not an AIFF/AIFF-C file";
        return false;
    }

    std::uint16_t bits = 0;
    std::uint32_t declaredFrames = 0;
    bool foundCommon = false;
    bool supportedCompression = isAiff;
    bool littleEndianPcm = false;
    const unsigned char* pcm = nullptr;
    std::size_t pcmBytes = 0;
    std::size_t offset = 12;
    while(offset + 8 <= bytes.size())
    {
        const unsigned char* chunk = bytes.data() + offset;
        const std::uint32_t chunkSize = readU32Be(chunk + 4);
        const std::size_t dataOffset = offset + 8;
        if(dataOffset > bytes.size() || chunkSize > bytes.size() - dataOffset)
            break;

        if(std::memcmp(chunk, "COMM", 4) == 0 && chunkSize >= 18)
        {
            foundCommon = true;
            out.channels = readU16Be(bytes.data() + dataOffset);
            declaredFrames = readU32Be(bytes.data() + dataOffset + 2);
            bits = readU16Be(bytes.data() + dataOffset + 6);
            const double rate = readExtended80(bytes.data() + dataOffset + 8);
            if(std::isfinite(rate) && rate >= 1.0 && rate <= 1000000.0)
                out.sampleRate = static_cast<std::uint32_t>(std::llround(rate));

            if(isAifc && chunkSize >= 22)
            {
                const unsigned char* compression = bytes.data() + dataOffset + 18;
                supportedCompression =
                    std::memcmp(compression, "NONE", 4) == 0 ||
                    std::memcmp(compression, "twos", 4) == 0 ||
                    std::memcmp(compression, "sowt", 4) == 0;
                littleEndianPcm = std::memcmp(compression, "sowt", 4) == 0;
            }
        }
        else if(std::memcmp(chunk, "SSND", 4) == 0 && chunkSize >= 8)
        {
            const std::uint32_t soundOffset = readU32Be(bytes.data() + dataOffset);
            if(soundOffset <= chunkSize - 8)
            {
                pcm = bytes.data() + dataOffset + 8 + soundOffset;
                pcmBytes = chunkSize - 8 - soundOffset;
            }
        }
        offset = dataOffset + chunkSize + (chunkSize & 1U);
    }

    if(!foundCommon || !supportedCompression || bits != 16 ||
       out.channels == 0 || out.channels > 2 || out.sampleRate == 0 || pcm == nullptr)
    {
        error = "example requires mono or stereo 16-bit PCM AIFF input";
        return false;
    }

    std::size_t sampleCount = pcmBytes / sizeof(std::int16_t);
    const std::uint64_t declaredSamples =
        static_cast<std::uint64_t>(declaredFrames) * out.channels;
    if(declaredFrames > 0 && declaredSamples < sampleCount)
        sampleCount = static_cast<std::size_t>(declaredSamples);
    out.samples.resize(sampleCount);
    for(std::size_t i = 0; i < sampleCount; ++i)
    {
        const std::uint16_t encoded = littleEndianPcm
            ? readU16(pcm + i * 2)
            : readU16Be(pcm + i * 2);
        const auto raw = static_cast<std::int16_t>(encoded);
        out.samples[i] = static_cast<float>(raw) / 32768.0f;
    }
    return true;
}

bool loadPcm16Audio(const std::string& path, AudioData& out, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    unsigned char header[12] {};
    if(!stream)
    {
        error = "cannot open " + path;
        return false;
    }
    stream.read(reinterpret_cast<char*>(header), sizeof(header));
    if(stream.gcount() != static_cast<std::streamsize>(sizeof(header)))
    {
        error = "audio file is too short";
        return false;
    }
    if(std::memcmp(header, "RIFF", 4) == 0 &&
       std::memcmp(header + 8, "WAVE", 4) == 0)
        return loadPcm16Wav(path, out, error);
    if(std::memcmp(header, "FORM", 4) == 0 &&
       (std::memcmp(header + 8, "AIFF", 4) == 0 ||
        std::memcmp(header + 8, "AIFC", 4) == 0))
        return loadPcm16Aiff(path, out, error);
    error = "unsupported audio container; expected WAV, AIFF, or AIFF-C";
    return false;
}

struct Sweep
{
    const char* name;
    bool filtered;
    float cutoffStart;
    float cutoffEnd;
    bool resonant;
    std::uint32_t durationWeight;
};

constexpr Sweep kSweeps[] = {
    {"dry reference", false, 12000.0f, 12000.0f, false, 3},
    {"falling cutoff, no resonance", true, 12000.0f, 180.0f, false, 5},
    {"rising cutoff, no resonance", true, 180.0f, 12000.0f, false, 5},
    {"falling cutoff, increasing resonance", true, 12000.0f, 180.0f, true, 5},
    {"rising cutoff, resonant", true, 180.0f, 12000.0f, true, 5},
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
    const AudioData* wav {nullptr};
    std::uint64_t framesRendered {0};
    std::uint64_t totalFrames {0};
    double sourcePosition {0.0};
    float playbackRate {1.0f};
    InterpolationMode interpolation {InterpolationMode::Hermite};
    std::array<std::uint64_t, kSweepCount + 1> sweepBoundaries {};
    float maxResonanceDb {12.0f};
    std::uint32_t framesPerBuffer {512};
    int activeSweep {-1};
    std::atomic<int> visibleSweep {-1};
    std::atomic<bool> finished {false};
};

float sourceSample(const AudioData& audio, std::size_t frame,
                   std::uint16_t channel)
{
    const std::size_t frameCount = audio.samples.size() / audio.channels;
    const std::size_t safeFrame = std::min(frame, frameCount - 1);
    return audio.samples[safeFrame * audio.channels + channel];
}

float interpolatedSample(const PlaybackState& state, std::uint16_t channel)
{
    const AudioData& audio = *state.wav;
    const std::size_t frameCount = audio.samples.size() / audio.channels;
    const std::size_t x0Index = std::min(
        static_cast<std::size_t>(state.sourcePosition), frameCount - 1);
    const std::size_t xm1Index = x0Index > 0 ? x0Index - 1 : 0;
    const std::size_t x1Index = std::min(x0Index + 1, frameCount - 1);
    const std::size_t x2Index = std::min(x0Index + 2, frameCount - 1);
    const float fraction = static_cast<float>(
        state.sourcePosition - static_cast<double>(x0Index));
    const float x0 = sourceSample(audio, x0Index, channel);
    const float x1 = sourceSample(audio, x1Index, channel);
    if(state.interpolation == InterpolationMode::Nearest)
        return mlang::coreaudio_filter::interpolateNearest(x0, x1, fraction);
    if(state.interpolation == InterpolationMode::Linear)
        return mlang::coreaudio_filter::interpolateLinear(x0, x1, fraction);
    // Hermite, cubic, and bicubic's 1D waveform reduction share this polynomial.
    return mlang::coreaudio_filter::interpolateHermite(
        sourceSample(audio, xm1Index, channel), x0, x1,
        sourceSample(audio, x2Index, channel), fraction);
}

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
    const float resonanceDb = sweep.resonant ? state.maxResonanceDb : 0.0f;
    const auto rampSamples = static_cast<std::int64_t>(
        state.sweepBoundaries[index + 1] - state.sweepBoundaries[index]);
    mlang::coreaudio_filter::setTarget(
        sweep.cutoffEnd, resonanceDb, rampSamples);
    const auto crossfadeSamples = std::min<std::int64_t>(
        rampSamples, static_cast<std::int64_t>(state.wav->sampleRate / 50));
    mlang::coreaudio_filter::setWetTarget(
        sweep.filtered ? 1.0f : 0.0f, crossfadeSamples);
}

void fillBuffer(AudioQueueRef queue, AudioQueueBufferRef buffer,
                PlaybackState& state)
{
    float* output = static_cast<float*>(buffer->mAudioData);
    const AudioData& wav = *state.wav;
    const std::uint64_t sourceFrames = wav.samples.size() / wav.channels;
    const std::uint32_t channels = 2;

    for(std::uint32_t i = 0; i < state.framesPerBuffer; ++i)
    {
        if(state.framesRendered >= state.totalFrames ||
           state.sourcePosition >= static_cast<double>(sourceFrames))
        {
            output[i * channels] = 0.0f;
            output[i * channels + 1] = 0.0f;
            continue;
        }

        const int sweepIndex = sweepForFrame(state, state.framesRendered);
        if(sweepIndex != state.activeSweep)
            enterSweep(state, sweepIndex);

        float left = interpolatedSample(state, 0);
        float right = wav.channels == 2 ? interpolatedSample(state, 1) : left;
        mlang::coreaudio_filter::beginFrame();
        left = mlang::coreaudio_filter::processLeft(left);
        right = mlang::coreaudio_filter::processRight(right);

        output[i * channels] = std::clamp(left * 0.55f, -0.98f, 0.98f);
        output[i * channels + 1] = std::clamp(right * 0.55f, -0.98f, 0.98f);
        ++state.framesRendered;
        state.sourcePosition += state.playbackRate;
    }

    buffer->mAudioDataByteSize =
        state.framesPerBuffer * channels * sizeof(float);
    if(state.framesRendered >= state.totalFrames ||
       state.sourcePosition >= static_cast<double>(sourceFrames))
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
    bool listDevicesOnly = false;
    bool verboseDevices = false;
    FilterMode filterMode = FilterMode::Lowpass24;
    InterpolationMode interpolationMode = InterpolationMode::Hermite;
    float maxResonanceDb = 12.0f;
    float playbackRate = 1.0f;
    std::string outputDeviceSelector;
    std::string audioPath = "../../examples/fft_example/illusion.wav";
    bool hasAudioPath = false;
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
        if(argument == "--list-devices")
        {
            listDevicesOnly = true;
            continue;
        }
        if(argument == "--verbose")
        {
            verboseDevices = true;
            continue;
        }
        if(argument == "--output-device" || argument == "--device")
        {
            if(i + 1 >= argc)
            {
                std::fprintf(stderr, "%s requires a device name or UID\n",
                             argument.c_str());
                return 2;
            }
            outputDeviceSelector = argv[++i];
            continue;
        }
        if(argument == "--filter")
        {
            if(i + 1 >= argc || !parseFilterMode(argv[++i], filterMode))
            {
                std::fprintf(stderr,
                    "--filter requires a lowpass, highpass, or bandpass 12/24 mode\n");
                return 2;
            }
            continue;
        }
        if(argument == "--max-resonance-db")
        {
            if(i + 1 >= argc ||
               !parseResonanceLimit(argv[++i], maxResonanceDb))
            {
                std::fprintf(stderr,
                    "--max-resonance-db requires a value from 0 to 36\n");
                return 2;
            }
            continue;
        }
        if(argument == "--interpolation" || argument == "--interpolation-filter")
        {
            if(i + 1 >= argc ||
               !parseInterpolationMode(argv[++i], interpolationMode))
            {
                std::fprintf(stderr,
                    "--interpolation requires nearest, linear, hermite, cubic, or bicubic\n");
                return 2;
            }
            continue;
        }
        if(argument == "--playback-rate")
        {
            if(i + 1 >= argc || !parsePlaybackRate(argv[++i], playbackRate))
            {
                std::fprintf(stderr,
                    "--playback-rate requires a value from 0.25 to 4\n");
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
        if(hasAudioPath)
        {
            std::fprintf(stderr, "only one audio path may be provided\n");
            return 2;
        }
        audioPath = argument;
        hasAudioPath = true;
    }
    if(listDevicesOnly)
    {
        printOutputDevices(verboseDevices);
        return 0;
    }
    audioPath = expandUserPath(audioPath);
    AudioData wav;
    std::string error;
    if(!loadPcm16Audio(audioPath, wav, error))
    {
        std::fprintf(stderr, "audio load failed: %s\n", error.c_str());
        return 1;
    }
    if(wav.samples.empty())
    {
        std::fprintf(stderr, "audio load failed: file contains no PCM frames\n");
        return 1;
    }
    if(validateOnly)
    {
        const std::size_t frameCount = wav.samples.size() / wav.channels;
        std::printf("validated %s: %u Hz, %u channel%s, %zu frames, %.2f seconds\n",
                    audioPath.c_str(), wav.sampleRate, wav.channels,
                    wav.channels == 1 ? "" : "s",
                    frameCount, static_cast<double>(frameCount) / wav.sampleRate);
        std::printf("audio filter: %s\n", filterModeName(filterMode));
        std::printf("interpolation filter: %s\n",
                    interpolationModeName(interpolationMode));
        std::printf("playback rate: %.3fx; maximum resonance peak: %+.1f dB\n",
                    playbackRate, maxResonanceDb);
        return 0;
    }

    PlaybackState state;
    state.wav = &wav;
    state.maxResonanceDb = maxResonanceDb;
    state.playbackRate = playbackRate;
    state.interpolation = interpolationMode;
    const std::size_t sourceFrames = wav.samples.size() / wav.channels;
    state.totalFrames = static_cast<std::uint64_t>(
        std::ceil(static_cast<double>(sourceFrames) / playbackRate));
    buildSweepSchedule(state);
    mlang::coreaudio_filter::setResonanceLimit(maxResonanceDb);
    mlang::coreaudio_filter::reset(
        static_cast<std::int32_t>(filterMode), static_cast<float>(wav.sampleRate),
        kSweeps[0].cutoffStart, 0.0f);

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

    std::string outputDeviceName;
    if(!setQueueOutputDevice(state.queue, outputDeviceSelector,
                             outputDeviceName, error))
    {
        std::fprintf(stderr, "output device selection failed: %s\n", error.c_str());
        AudioQueueDispose(state.queue, true);
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

    std::printf("CoreAudio realtime dsp::filter demo\n");
    std::printf("audio filter: %s\n", filterModeName(filterMode));
    std::printf("interpolation filter: %s\n",
                interpolationModeName(interpolationMode));
    std::printf("playback rate: %.3fx\n", playbackRate);
    std::printf("source: %s (%u Hz, %u channel%s)\n", audioPath.c_str(),
                wav.sampleRate, wav.channels, wav.channels == 1 ? "" : "s");
    std::printf("output: %s\n", outputDeviceName.c_str());
    std::printf("maximum resonance peak: %+.1f dB (realtime ramped)\n",
                maxResonanceDb);
    std::printf("demo duration: %.2f seconds (full source)\n",
                static_cast<double>(state.totalFrames) / wav.sampleRate);

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
            const double startSeconds =
                static_cast<double>(state.sweepBoundaries[current]) /
                wav.sampleRate;
            const double endSeconds =
                static_cast<double>(state.sweepBoundaries[current + 1]) /
                wav.sampleRate;
            if(sweep.filtered)
            {
                std::printf("step %d/%zu  %.2f-%.2f s: %s\n",
                            current + 1, kSweepCount, startSeconds, endSeconds,
                            sweep.name);
                std::printf("  filter: %s; resonance target: %+.1f dB\n",
                            filterModeName(filterMode),
                            sweep.resonant ? maxResonanceDb : 0.0f);
            }
            else
            {
                std::printf("step %d/%zu  %.2f-%.2f s: %s [unfiltered]\n",
                            current + 1, kSweepCount, startSeconds, endSeconds,
                            sweep.name);
            }
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    AudioQueueDispose(state.queue, true);
    return 0;
}
