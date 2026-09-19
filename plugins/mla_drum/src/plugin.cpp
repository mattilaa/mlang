// Mla Drum - VST3 drum sampler instrument.
//
// Sixteen pads on consecutive keys from a root note. Each pad holds one
// sample, with its own level, pan and tune; the instance has one amp ADSR,
// a Poly/Mono voice mode (Mono chokes whatever is sounding when a new pad is
// hit, e.g. open/closed hi-hats) and a One-shot/Gated trigger.
//
// The per-sample voice math (playhead, interpolation, envelope, pan, choke)
// lives in MLang (`src/mla_drum_dsp.mla` on top of `modules/dsp/envelope.mla`).
// This file owns the sample buffers and the voice pool, schedules MIDI events
// sample-accurately, maps VST3 parameters and persists state. Samples arrive
// through the IConnectionPoint messages in `mla_sampler_protocol.h`.
//
// Threading: pads are immutable `Sample` objects. The control thread owns
// them and publishes raw pointers through atomics; the audio thread picks up
// changes at block start. Replaced samples are freed only after the audio
// thread has finished two further blocks, so no voice can still read them.
// That handshake (publish, then read the block counter) needs store-load
// ordering, so those atomics use the default sequentially-consistent order.

#include "mla_sampler_protocol.h"

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// --- MLang DSP bridge (compiled from src/mla_drum_dsp.mla) --------------------
struct DrumVoice;
extern "C" DrumVoice *mladrum_voice_create__f32(float sampleRate);
extern "C" void mladrum_voice_destroy__ptr_struct_DrumVoice(DrumVoice *voice);
extern "C" void mladrum_voice_set_envelope__ptr_struct_DrumVoice_f32_f32_f32_f32(
    DrumVoice *voice, float attack, float decay, float sustain, float release);
extern "C" void mladrum_voice_set_one_shot__ptr_struct_DrumVoice_i32(DrumVoice *voice, int32_t oneShot);
extern "C" void mladrum_voice_start__ptr_struct_DrumVoice_f64_f64_f32_f32(
    DrumVoice *voice, double frames, double step, float gain, float pan);
extern "C" void mladrum_voice_release__ptr_struct_DrumVoice(DrumVoice *voice);
extern "C" void mladrum_voice_choke__ptr_struct_DrumVoice_f32(DrumVoice *voice, float seconds);
extern "C" void mladrum_voice_stop__ptr_struct_DrumVoice(DrumVoice *voice);
extern "C" int32_t mladrum_voice_is_active__ptr_struct_DrumVoice(DrumVoice *voice);
extern "C" double mladrum_voice_frame__ptr_struct_DrumVoice(DrumVoice *voice);
extern "C" float mladrum_voice_render__ptr_struct_DrumVoice_f32_f32_f32_f32_ptr_f32(
    DrumVoice *voice, float left0, float right0, float left1, float right1, float *outRight);

// --- MLang runtime PCM decoder (libmlang_std.a, std::audio::PcmAudio) --------
struct MlangList {
    int64_t size;
    void *data;
};
extern "C" int64_t __mlang_std_audio_pcm_load(const char *path);
extern "C" int64_t __mlang_std_audio_pcm_file_sample_rate(int64_t handle);
extern "C" int64_t __mlang_std_audio_pcm_file_channels(int64_t handle);
extern "C" int64_t __mlang_std_audio_pcm_file_frame_count(int64_t handle);
extern "C" MlangList __mlang_std_audio_pcm_file_samples(int64_t handle);
extern "C" int32_t __mlang_std_audio_pcm_file_close(int64_t handle);
extern "C" const char *__mlang_std_audio_last_error(void);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_drum {

// Stable class id. Distinct from Mla Verb, Mla Distortion and the SDK examples.
static const FUID kProcessorUID(0x4D6C6144, 0x72756D31, 0x5C3A81E7, 0x29D04B16);

constexpr int kNumPads = 16;
constexpr int kNumVoices = 32;
constexpr float kChokeSeconds = 0.003f; // Mono-mode choke fade, short but click-free.
constexpr uint32 kStateMagic = 0x52444C4D; // "MLDR" little-endian
constexpr uint32 kStateVersion = 1;

enum ParamId : ParamID {
    kLevelParam = 100,
    kTuneParam,
    kVelocityParam,
    kModeParam,
    kTriggerParam,
    kRootKeyParam,
    kAttackParam,
    kDecayParam,
    kSustainParam,
    kReleaseParam,
    kPadParamBase = 200, // pad p: 200 + 3p level, +1 pan, +2 tune
};

constexpr int kNumGlobalParams = 10;
constexpr int kParamsPerPad = 3;
constexpr int kNumParams = kNumGlobalParams + kNumPads * kParamsPerPad;

// Flat index <-> ParamID. Globals occupy 0..9, pads follow.
static ParamID paramIdAt(int index)
{
    if(index < kNumGlobalParams)
        return static_cast<ParamID>(kLevelParam + index);
    return static_cast<ParamID>(kPadParamBase + (index - kNumGlobalParams));
}

static int indexOf(ParamID id)
{
    if(id >= kLevelParam && id < kLevelParam + kNumGlobalParams)
        return static_cast<int>(id - kLevelParam);
    if(id >= kPadParamBase && id < kPadParamBase + kNumPads * kParamsPerPad)
        return kNumGlobalParams + static_cast<int>(id - kPadParamBase);
    return -1;
}

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;

static float gainFromNorm(double norm)
{
    // -60 dB .. +6 dB; the bottom of the range is silence.
    if(norm <= 0.0)
        return 0.0f;
    const double db = -60.0 + norm * 66.0;
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

static double semitonesFromNorm(double norm) { return norm * 48.0 - 24.0; } // +-2 octaves
static float panFromNorm(double norm) { return static_cast<float>(norm * 2.0 - 1.0); }
static float attackFromNorm(double norm) { return static_cast<float>(2.0 * norm * norm * norm); } // 0..2 s
static float timeFromNorm(double norm) { return static_cast<float>(0.001 * std::pow(10000.0, norm)); } // 1 ms..10 s
static double normFromTime(double seconds) { return std::log10(seconds / 0.001) / 4.0; }
static int rootKeyFromNorm(double norm) { return static_cast<int>(std::lround(norm * 127.0)); }

// Immutable decoded sample: stereo interleaved with one silent guard frame at
// the end, so interpolation may always read frame + 1.
struct Sample {
    std::vector<float> stereo;
    int64_t frames = 0;
    double rate = 44100.0;
    std::string name;
};

static std::unique_ptr<Sample> makeSample(const float *data, int64_t frames, int channels, double rate,
                                          std::string name)
{
    auto sample = std::make_unique<Sample>();
    sample->frames = frames;
    sample->rate = rate;
    sample->name = std::move(name);
    sample->stereo.resize(static_cast<size_t>(frames + 1) * 2u, 0.0f);
    for(int64_t f = 0; f < frames; ++f) {
        const float left = data[f * channels];
        const float right = channels == 2 ? data[f * channels + 1] : left;
        sample->stereo[static_cast<size_t>(f) * 2u] = std::isfinite(left) ? left : 0.0f;
        sample->stereo[static_cast<size_t>(f) * 2u + 1u] = std::isfinite(right) ? right : 0.0f;
    }
    return sample;
}

static bool validFormat(int64_t frames, int64_t channels, double rate)
{
    return frames >= 1 && frames <= mla_sampler::kMaxFrames && (channels == 1 || channels == 2) &&
           std::isfinite(rate) && rate >= 1000.0 && rate <= 384000.0;
}

static std::string baseName(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::unique_ptr<Sample> decodeFile(const std::string &path, std::string &error)
{
    const int64_t handle = __mlang_std_audio_pcm_load(path.c_str());
    if(handle == 0) {
        const char *why = __mlang_std_audio_last_error();
        error = (why && *why) ? why : "could not decode audio file";
        return nullptr;
    }
    const int64_t rate = __mlang_std_audio_pcm_file_sample_rate(handle);
    const int64_t channels = __mlang_std_audio_pcm_file_channels(handle);
    const int64_t frames = __mlang_std_audio_pcm_file_frame_count(handle);
    std::unique_ptr<Sample> sample;
    if(!validFormat(frames, channels, static_cast<double>(rate))) {
        error = "unsupported sample length, channel count or rate";
    } else {
        MlangList samples = __mlang_std_audio_pcm_file_samples(handle);
        if(samples.data && samples.size == frames * channels)
            sample = makeSample(static_cast<const float *>(samples.data), frames, static_cast<int>(channels),
                                static_cast<double>(rate), baseName(path));
        else
            error = "could not read decoded samples";
        std::free(samples.data);
    }
    __mlang_std_audio_pcm_file_close(handle);
    return sample;
}

static std::string binaryString(IAttributeList *attributes, IAttributeList::AttrID id)
{
    const void *data = nullptr;
    uint32 size = 0;
    if(attributes->getBinary(id, data, size) != kResultOk || data == nullptr)
        return {};
    return std::string(static_cast<const char *>(data), size);
}

class Processor final : public SingleComponentEffect, public IMidiMapping {
  public:
    Processor()
    {
        for(auto &pad : published_)
            pad.store(nullptr, std::memory_order_relaxed);
        for(auto &value : norm_)
            value.store(0.0, std::memory_order_relaxed);
    }

    ~Processor() override
    {
        destroyVoices();
    }

    // SingleComponentEffect hides IConnectionPoint ("no need to expose it to
    // the host"); expose it again so hosts can send the sampler messages.
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
        DEF_INTERFACE(IConnectionPoint)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

    tresult PLUGIN_API connect(IConnectionPoint *other) SMTG_OVERRIDE
    {
        // A host pairing the component with its own controller would connect
        // this object to itself; accept without keeping a self-reference.
        if(other == static_cast<IConnectionPoint *>(this))
            return kResultTrue;
        return SingleComponentEffect::connect(other);
    }

    tresult PLUGIN_API disconnect(IConnectionPoint *other) SMTG_OVERRIDE
    {
        if(other == static_cast<IConnectionPoint *>(this))
            return kResultTrue;
        return SingleComponentEffect::disconnect(other);
    }

    static FUnknown *createInstance(void *) { return static_cast<IComponent *>(new Processor()); }

    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::initialize(context);
        if(result != kResultOk)
            return result;

        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI In"), 16);

        const auto flags = ParameterInfo::kCanAutomate;
        addParam(kLevelParam, STR16("Level"), STR16("dB"), kUnityLevelNorm);
        addParam(kTuneParam, STR16("Tune"), STR16("st"), 0.5);
        addParam(kVelocityParam, STR16("Velocity"), nullptr, 1.0);

        auto *mode = new StringListParameter(STR16("Mode"), kModeParam, nullptr, flags | ParameterInfo::kIsList);
        mode->appendString(STR16("Poly"));
        mode->appendString(STR16("Mono"));
        parameters.addParameter(mode);
        norm_[indexOf(kModeParam)].store(0.0);

        auto *trigger =
            new StringListParameter(STR16("Trigger"), kTriggerParam, nullptr, flags | ParameterInfo::kIsList);
        trigger->appendString(STR16("One-shot"));
        trigger->appendString(STR16("Gated"));
        parameters.addParameter(trigger);
        norm_[indexOf(kTriggerParam)].store(0.0);

        parameters.addParameter(STR16("Root Key"), nullptr, 127, 36.0 / 127.0, flags, kRootKeyParam);
        norm_[indexOf(kRootKeyParam)].store(36.0 / 127.0);

        addParam(kAttackParam, STR16("Attack"), STR16("s"), 0.0);
        addParam(kDecayParam, STR16("Decay"), STR16("s"), normFromTime(0.5));
        addParam(kSustainParam, STR16("Sustain"), nullptr, 1.0);
        addParam(kReleaseParam, STR16("Release"), STR16("s"), normFromTime(0.1));

        for(int pad = 0; pad < kNumPads; ++pad) {
            const ParamID base = kPadParamBase + pad * kParamsPerPad;
            addParam(base, padTitle(pad, "Level").c_str(), STR16("dB"), kUnityLevelNorm);
            addParam(base + 1, padTitle(pad, "Pan").c_str(), nullptr, 0.5);
            addParam(base + 2, padTitle(pad, "Tune").c_str(), STR16("st"), 0.5);
        }
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        destroyVoices();
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            for(int pad = 0; pad < kNumPads; ++pad) {
                published_[pad].store(nullptr, std::memory_order_release);
                owned_[pad].reset();
            }
            graveyard_.clear();
        }
        return SingleComponentEffect::terminate();
    }

    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc,
                                                   ParamID &id) SMTG_OVERRIDE
    {
        if(bus != 0 || channel < 0 || channel > 15)
            return kResultFalse;
        // Raw MIDI CC numbers, matching mlacker's pattern/live CC routing.
        switch(cc) {
            case 7: id = kLevelParam; return kResultOk;    // Channel volume
            case 73: id = kAttackParam; return kResultOk;  // Sound controller 4 (attack)
            case 75: id = kDecayParam; return kResultOk;   // Sound controller 6 (decay)
            case 72: id = kReleaseParam; return kResultOk; // Sound controller 3 (release)
        }
        return kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                          SpeakerArrangement *outputs, int32 numOuts) SMTG_OVERRIDE
    {
        if(numIns != 0 || numOuts != 1 || outputs == nullptr || outputs[0] != SpeakerArr::kStereo)
            return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup &setup) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::setupProcessing(setup);
        if(result != kResultOk)
            return result;
        // Called while inactive: nothing is rendering, voices may be rebuilt.
        destroyVoices();
        sampleRate_ = setup.sampleRate > 0 ? setup.sampleRate : 44100.0;
        for(auto &slot : voices_)
            slot.dsp = mladrum_voice_create__f32(static_cast<float>(sampleRate_));
        pushVoiceParameters();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(!state)
            stopAllVoices();
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        syncPads();
        if(paramsDirty_.exchange(false, std::memory_order_acquire))
            pushVoiceParameters();
        handleParameterChanges(data.inputParameterChanges);

        if(data.symbolicSampleSize != kSample32) {
            finishBlock();
            return kResultFalse;
        }
        if(data.numOutputs < 1 || data.outputs[0].numChannels < 2 || data.outputs[0].channelBuffers32 == nullptr) {
            finishBlock();
            return kResultOk;
        }
        AudioBusBuffers &out = data.outputs[0];
        float *outL = out.channelBuffers32[0];
        float *outR = out.channelBuffers32[1];
        std::fill_n(outL, data.numSamples, 0.0f);
        std::fill_n(outR, data.numSamples, 0.0f);

        // Render between events so note starts land on their exact frame.
        int32 rendered = 0;
        IEventList *events = data.inputEvents;
        const int32 eventCount = events ? events->getEventCount() : 0;
        for(int32 i = 0; i < eventCount; ++i) {
            Event event{};
            if(events->getEvent(i, event) != kResultOk)
                continue;
            const int32 at = std::clamp(event.sampleOffset, rendered, data.numSamples);
            render(outL, outR, rendered, at);
            rendered = at;
            if(event.type == Event::kNoteOnEvent) {
                if(event.noteOn.velocity <= 0.0f)
                    noteOff(event.noteOn.channel, event.noteOn.pitch);
                else
                    noteOn(event.noteOn.channel, event.noteOn.pitch, event.noteOn.velocity);
            } else if(event.type == Event::kNoteOffEvent) {
                noteOff(event.noteOff.channel, event.noteOff.pitch);
            }
        }
        render(outL, outR, rendered, data.numSamples);
        out.silenceFlags = 0;
        finishBlock();
        return kResultOk;
    }

    // --- Sample loading protocol -------------------------------------------
    tresult PLUGIN_API notify(IMessage *message) SMTG_OVERRIDE
    {
        if(message == nullptr || message->getMessageID() == nullptr)
            return kInvalidArgument;
        const char *id = message->getMessageID();
        const bool loadFile = std::strcmp(id, mla_sampler::kLoadFileMessage) == 0;
        const bool loadPcm = std::strcmp(id, mla_sampler::kLoadPcmMessage) == 0;
        const bool clear = std::strcmp(id, mla_sampler::kClearMessage) == 0;
        if(!loadFile && !loadPcm && !clear)
            return SingleComponentEffect::notify(message);

        IAttributeList *attributes = message->getAttributes();
        if(attributes == nullptr)
            return kInvalidArgument;
        int64 pad = -1;
        if(attributes->getInt("pad", pad) != kResultOk || pad < 0 || pad >= kNumPads)
            return fail(attributes, "pad must be 0-15");

        std::unique_ptr<Sample> sample;
        if(loadFile) {
            const std::string path = binaryString(attributes, "path");
            if(path.empty())
                return fail(attributes, "missing path");
            std::string error;
            sample = decodeFile(path, error);
            if(!sample)
                return fail(attributes, error);
        } else if(loadPcm) {
            int64 channels = 0, frames = 0;
            double rate = 0;
            const void *data = nullptr;
            uint32 size = 0;
            if(attributes->getInt("channels", channels) != kResultOk ||
               attributes->getInt("frames", frames) != kResultOk ||
               attributes->getFloat("rate", rate) != kResultOk ||
               attributes->getBinary("data", data, size) != kResultOk || data == nullptr)
                return fail(attributes, "missing channels, frames, rate or data");
            if(!validFormat(frames, channels, rate))
                return fail(attributes, "unsupported sample length, channel count or rate");
            if(static_cast<uint64_t>(size) != static_cast<uint64_t>(frames * channels) * sizeof(float))
                return fail(attributes, "data size does not match frames * channels");
            // The attribute buffer has no alignment guarantee; copy first.
            std::vector<float> pcm(static_cast<size_t>(frames * channels));
            std::memcpy(pcm.data(), data, size);
            sample = makeSample(pcm.data(), frames, static_cast<int>(channels), rate,
                                binaryString(attributes, "name"));
        }
        replacePad(static_cast<int>(pad), std::move(sample));
        return kResultOk;
    }

    // --- State ---------------------------------------------------------------
    tresult PLUGIN_API setState(IBStream *state) SMTG_OVERRIDE
    {
        if(state == nullptr)
            return kResultFalse;
        IBStreamer streamer(state, kLittleEndian);
        uint32 magic = 0, version = 0;
        if(!streamer.readInt32u(magic) || magic != kStateMagic || !streamer.readInt32u(version) ||
           version != kStateVersion)
            return kResultFalse;
        int32 count = 0;
        if(!streamer.readInt32(count) || count < 0 || count > 4096)
            return kResultFalse;
        for(int32 i = 0; i < count; ++i) {
            int32 id = 0;
            double value = 0;
            if(!streamer.readInt32(id) || !streamer.readDouble(value))
                return kResultFalse;
            const int index = indexOf(static_cast<ParamID>(id));
            if(index < 0 || !std::isfinite(value))
                continue; // Unknown parameter from a newer version.
            value = std::clamp(value, 0.0, 1.0);
            norm_[index].store(value, std::memory_order_relaxed);
            setParamNormalized(static_cast<ParamID>(id), value);
        }
        paramsDirty_.store(true, std::memory_order_release);

        std::unique_ptr<Sample> pads[kNumPads];
        for(int pad = 0; pad < kNumPads; ++pad) {
            int8 present = 0;
            if(!streamer.readInt8(present))
                return kResultFalse;
            if(!present)
                continue;
            std::string name;
            double rate = 0;
            int64 frames = 0;
            if(!readString(streamer, name) || !streamer.readDouble(rate) || !streamer.readInt64(frames) ||
               !validFormat(frames, 2, rate))
                return kResultFalse;
            std::vector<float> pcm(static_cast<size_t>(frames) * 2u);
            if(!streamer.readFloatArray(pcm.data(), static_cast<int32>(pcm.size())))
                return kResultFalse;
            pads[pad] = makeSample(pcm.data(), frames, 2, rate, std::move(name));
        }
        for(int pad = 0; pad < kNumPads; ++pad)
            replacePad(pad, std::move(pads[pad]));
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream *state) SMTG_OVERRIDE
    {
        if(state == nullptr)
            return kResultFalse;
        IBStreamer streamer(state, kLittleEndian);
        streamer.writeInt32u(kStateMagic);
        streamer.writeInt32u(kStateVersion);
        streamer.writeInt32(kNumParams);
        for(int i = 0; i < kNumParams; ++i) {
            streamer.writeInt32(static_cast<int32>(paramIdAt(i)));
            streamer.writeDouble(norm_[i].load(std::memory_order_relaxed));
        }
        std::lock_guard<std::mutex> lock(controlMutex_);
        for(int pad = 0; pad < kNumPads; ++pad) {
            const Sample *sample = owned_[pad].get();
            streamer.writeInt8(sample ? 1 : 0);
            if(!sample)
                continue;
            writeString(streamer, sample->name);
            streamer.writeDouble(sample->rate);
            streamer.writeInt64(sample->frames);
            // Pads are embedded without the guard frame.
            streamer.writeFloatArray(sample->stereo.data(), static_cast<int32>(sample->frames * 2));
        }
        return kResultOk;
    }

  private:
    struct VoiceSlot {
        DrumVoice *dsp = nullptr;
        const Sample *sample = nullptr;
        int pad = -1;
        int16 channel = -1;
        int16 pitch = -1;
        uint64_t serial = 0;
    };

    struct Retired {
        std::unique_ptr<Sample> sample;
        uint64_t freeAfterBlock = 0;
    };

    VoiceSlot voices_[kNumVoices];
    uint64_t nextSerial_ = 1;
    double sampleRate_ = 44100.0;
    std::atomic<double> norm_[kNumParams];
    std::atomic<bool> paramsDirty_{false};

    // Audio-thread view of the pads.
    const Sample *active_[kNumPads] = {};
    std::atomic<const Sample *> published_[kNumPads];
    std::atomic<uint64_t> blocksDone_{0};

    // Control-thread ownership.
    std::mutex controlMutex_;
    std::unique_ptr<Sample> owned_[kNumPads];
    std::vector<Retired> graveyard_;

    void addParam(ParamID id, const TChar *title, const TChar *units, double defaultNorm)
    {
        parameters.addParameter(title, units, 0, defaultNorm, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(defaultNorm, std::memory_order_relaxed);
    }

    static std::u16string padTitle(int pad, const char *what)
    {
        const std::string text = "Pad " + std::to_string(pad + 1) + " " + what;
        return std::u16string(text.begin(), text.end());
    }

    static tresult fail(IAttributeList *attributes, const std::string &why)
    {
        attributes->setBinary("error", why.data(), static_cast<uint32>(why.size()));
        return kResultFalse;
    }

    static bool readString(IBStreamer &streamer, std::string &out)
    {
        uint32 size = 0;
        if(!streamer.readInt32u(size) || size > 4096)
            return false;
        out.resize(size);
        return size == 0 || streamer.readRaw(out.data(), size) == static_cast<int32>(size);
    }

    static void writeString(IBStreamer &streamer, const std::string &text)
    {
        const uint32 size = static_cast<uint32>(std::min<size_t>(text.size(), 4096));
        streamer.writeInt32u(size);
        if(size)
            streamer.writeRaw(text.data(), size);
    }

    double norm(ParamID id) const { return norm_[indexOf(id)].load(std::memory_order_relaxed); }

    // --- Control thread ------------------------------------------------------
    void replacePad(int pad, std::unique_ptr<Sample> sample)
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        collectGarbage();
        published_[pad].store(sample.get());
        if(owned_[pad])
            graveyard_.push_back(
                {std::move(owned_[pad]), blocksDone_.load() + 2});
        owned_[pad] = std::move(sample);
    }

    // A block that started after the swap has seen the new pointer and
    // stopped every voice on the old one; two completed blocks guarantee that.
    void collectGarbage()
    {
        const uint64_t done = blocksDone_.load();
        graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(),
                                        [&](const Retired &r) { return done >= r.freeAfterBlock; }),
                         graveyard_.end());
    }

    void destroyVoices()
    {
        for(auto &slot : voices_) {
            if(slot.dsp)
                mladrum_voice_destroy__ptr_struct_DrumVoice(slot.dsp);
            slot = VoiceSlot{};
        }
    }

    // --- Audio thread --------------------------------------------------------
    void finishBlock() { blocksDone_.fetch_add(1); }

    void syncPads()
    {
        for(int pad = 0; pad < kNumPads; ++pad) {
            const Sample *current = published_[pad].load();
            if(current == active_[pad])
                continue;
            for(auto &slot : voices_)
                if(slot.dsp && slot.sample == active_[pad] && slot.pad == pad) {
                    mladrum_voice_stop__ptr_struct_DrumVoice(slot.dsp);
                    slot.sample = nullptr;
                }
            active_[pad] = current;
        }
    }

    void stopAllVoices()
    {
        for(auto &slot : voices_) {
            if(slot.dsp)
                mladrum_voice_stop__ptr_struct_DrumVoice(slot.dsp);
            slot.sample = nullptr;
        }
    }

    void pushVoiceParameters()
    {
        const float attack = attackFromNorm(norm(kAttackParam));
        const float decay = timeFromNorm(norm(kDecayParam));
        const float sustain = static_cast<float>(norm(kSustainParam));
        const float release = timeFromNorm(norm(kReleaseParam));
        const int32_t oneShot = norm(kTriggerParam) < 0.5 ? 1 : 0;
        for(auto &slot : voices_) {
            if(!slot.dsp)
                continue;
            mladrum_voice_set_envelope__ptr_struct_DrumVoice_f32_f32_f32_f32(slot.dsp, attack, decay, sustain,
                                                                             release);
            mladrum_voice_set_one_shot__ptr_struct_DrumVoice_i32(slot.dsp, oneShot);
        }
    }

    void handleParameterChanges(IParameterChanges *changes)
    {
        if(changes == nullptr)
            return;
        bool voicesChanged = false;
        const int32 count = changes->getParameterCount();
        for(int32 q = 0; q < count; ++q) {
            IParamValueQueue *queue = changes->getParameterData(q);
            if(queue == nullptr)
                continue;
            const int32 points = queue->getPointCount();
            int32 offset = 0;
            ParamValue value = 0;
            if(points <= 0 || queue->getPoint(points - 1, offset, value) != kResultOk)
                continue;
            const ParamID id = queue->getParameterId();
            const int index = indexOf(id);
            if(index < 0 || !std::isfinite(value))
                continue;
            norm_[index].store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
            if(id == kTriggerParam || (id >= kAttackParam && id <= kReleaseParam))
                voicesChanged = true;
        }
        if(voicesChanged)
            pushVoiceParameters();
    }

    void noteOn(int16 channel, int16 pitch, float velocity)
    {
        const int pad = pitch - rootKeyFromNorm(norm(kRootKeyParam));
        if(pad < 0 || pad >= kNumPads || active_[pad] == nullptr)
            return;
        const Sample *sample = active_[pad];

        if(norm(kModeParam) >= 0.5) // Mono: a new hit chokes everything sounding.
            for(auto &slot : voices_)
                if(slot.dsp && mladrum_voice_is_active__ptr_struct_DrumVoice(slot.dsp))
                    mladrum_voice_choke__ptr_struct_DrumVoice_f32(slot.dsp, kChokeSeconds);

        VoiceSlot *target = nullptr;
        for(auto &slot : voices_) {
            if(!slot.dsp)
                continue;
            if(!mladrum_voice_is_active__ptr_struct_DrumVoice(slot.dsp)) {
                target = &slot;
                break;
            }
            if(target == nullptr || slot.serial < target->serial)
                target = &slot; // Steal the oldest if all are busy.
        }
        if(target == nullptr)
            return;

        const int padBase = kNumGlobalParams + pad * kParamsPerPad;
        const double semitones =
            semitonesFromNorm(norm(kTuneParam)) + semitonesFromNorm(norm_[padBase + 2].load(std::memory_order_relaxed));
        const double step = sample->rate / sampleRate_ * std::pow(2.0, semitones / 12.0);
        const float sensitivity = static_cast<float>(norm(kVelocityParam));
        const float velocityGain = 1.0f - sensitivity + sensitivity * std::clamp(velocity, 0.0f, 1.0f);
        const float gain = gainFromNorm(norm(kLevelParam)) *
                           gainFromNorm(norm_[padBase].load(std::memory_order_relaxed)) * velocityGain;
        const float pan = panFromNorm(norm_[padBase + 1].load(std::memory_order_relaxed));

        mladrum_voice_start__ptr_struct_DrumVoice_f64_f64_f32_f32(target->dsp, static_cast<double>(sample->frames),
                                                                   step, gain, pan);
        target->sample = sample;
        target->pad = pad;
        target->channel = channel;
        target->pitch = pitch;
        target->serial = nextSerial_++;
    }

    void noteOff(int16 channel, int16 pitch)
    {
        for(auto &slot : voices_)
            if(slot.dsp && slot.channel == channel && slot.pitch == pitch)
                mladrum_voice_release__ptr_struct_DrumVoice(slot.dsp);
    }

    void render(float *outL, float *outR, int32 from, int32 to)
    {
        if(from >= to)
            return;
        for(auto &slot : voices_) {
            if(!slot.dsp || !slot.sample || !mladrum_voice_is_active__ptr_struct_DrumVoice(slot.dsp))
                continue;
            const float *pcm = slot.sample->stereo.data();
            const int64_t last = slot.sample->frames; // Index of the silent guard frame.
            for(int32 i = from; i < to; ++i) {
                int64_t frame = static_cast<int64_t>(mladrum_voice_frame__ptr_struct_DrumVoice(slot.dsp));
                frame = std::clamp<int64_t>(frame, 0, last - 1);
                const float *a = pcm + frame * 2;
                float right = 0.0f;
                const float left = mladrum_voice_render__ptr_struct_DrumVoice_f32_f32_f32_f32_ptr_f32(
                    slot.dsp, a[0], a[1], a[2], a[3], &right);
                outL[i] += left;
                outR[i] += right;
                if(!mladrum_voice_is_active__ptr_struct_DrumVoice(slot.dsp)) {
                    slot.sample = nullptr;
                    break;
                }
            }
        }
    }
};

} // namespace mla_drum

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_drum::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla Drum", 0, PlugType::kInstrumentDrum, "0.1.0", kVstVersionString,
           mla_drum::Processor::createInstance)
END_FACTORY
