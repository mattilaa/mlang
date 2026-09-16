#include "plugin.h"

#include <algorithm>
#include <string>

#include "messages.h"
#include "plug_ids.h"

#include "base/source/fstreamer.h"
#include "base/source/fstring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"

namespace mlacker_drum {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
constexpr int32 kStateVersion = 1;
constexpr int16 kBasePitch = 36; // C1 maps to pad 0, tracker-friendly
} // namespace

Plugin::Plugin() = default;

FUnknown* Plugin::createInstance(void*)
{
    return static_cast<IComponent*>(new Plugin());
}

int Plugin::noteToPad(int16 pitch)
{
    return std::clamp(static_cast<int>(pitch) - kBasePitch, 0, kNumPads - 1);
}

tresult PLUGIN_API Plugin::initialize(FUnknown* context)
{
    const tresult result = SingleComponentEffect::initialize(context);
    if(result != kResultOk)
        return result;

    addEventInput(STR16("Event In"), 16);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    parameters.addParameter(new RangeParameter(STR16("Master Gain"),
                                               kMasterGainParam, STR16(""),
                                               0.0, 1.0, 0.8));

    engine_.reset();
    dsp_.reset();
    applyMasterGainParam(masterGainValue_);
    return kResultOk;
}

tresult PLUGIN_API Plugin::setBusArrangements(SpeakerArrangement* inputs,
                                              int32 numIns,
                                              SpeakerArrangement* outputs,
                                              int32 numOuts)
{
    if(numIns != 0 || numOuts != 1 || outputs == nullptr)
        return kResultFalse;
    if(outputs[0] != SpeakerArr::kStereo)
        return kResultFalse;

    return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs,
                                                     numOuts);
}

tresult PLUGIN_API Plugin::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Plugin::setupProcessing(ProcessSetup& setup)
{
    const tresult result = SingleComponentEffect::setupProcessing(setup);
    if(result == kResultOk)
        engine_.setSampleRate(static_cast<float>(setup.sampleRate));
    return result;
}

tresult PLUGIN_API Plugin::setActive(TBool state)
{
    if(!state)
        engine_.allVoicesOff();
    return SingleComponentEffect::setActive(state);
}

void Plugin::applyMasterGainParam(ParamValue normalizedValue)
{
    masterGainValue_ = normalizedValue;
    dsp_.setMasterGain(static_cast<float>(normalizedValue));
}

void Plugin::handleParameterChanges(IParameterChanges* changes)
{
    if(changes == nullptr)
        return;

    const int32 numParams = changes->getParameterCount();
    for(int32 index = 0; index < numParams; ++index)
    {
        IParamValueQueue* queue = changes->getParameterData(index);
        if(queue == nullptr || queue->getParameterId() != kMasterGainParam)
            continue;

        int32 sampleOffset = 0;
        ParamValue value = masterGainValue_;
        const int32 pointCount = queue->getPointCount();
        if(pointCount > 0 &&
           queue->getPoint(pointCount - 1, sampleOffset, value) == kResultTrue)
        {
            applyMasterGainParam(value);
        }
    }
}

void Plugin::handleEvents(IEventList* events)
{
    if(events == nullptr)
        return;

    Event event;
    const int32 eventCount = events->getEventCount();
    for(int32 index = 0; index < eventCount; ++index)
    {
        if(events->getEvent(index, event) != kResultOk)
            continue;

        if(event.type == Event::kNoteOnEvent && event.noteOn.velocity > 0.0f)
        {
            engine_.trigger(noteToPad(event.noteOn.pitch),
                            static_cast<float>(event.noteOn.velocity));
        }
        // Note-off is ignored: drum pads are one-shots that ring out.
    }
}

tresult PLUGIN_API Plugin::process(ProcessData& data)
{
    handleParameterChanges(data.inputParameterChanges);
    handleEvents(data.inputEvents);

    if(data.numOutputs < 1 || data.symbolicSampleSize != kSample32)
        return data.numOutputs < 1 ? kResultOk : kResultFalse;

    AudioBusBuffers& outBus = data.outputs[0];
    if(outBus.channelBuffers32 == nullptr || outBus.numChannels <= 0)
        return kResultOk;

    float* left = outBus.channelBuffers32[0];
    float* right = outBus.numChannels > 1 ? outBus.channelBuffers32[1] : left;
    engine_.render(left, right, data.numSamples, dsp_);

    if(right != left && outBus.numChannels > 2)
    {
        for(int32 channel = 2; channel < outBus.numChannels; ++channel)
            std::copy(left, left + data.numSamples, outBus.channelBuffers32[channel]);
    }

    outBus.silenceFlags = 0;
    return kResultOk;
}

void Plugin::handleUpload(IMessage* message)
{
    IAttributeList* attr = message->getAttributes();
    if(attr == nullptr)
        return;

    int64 pad = 0;
    attr->getInt(kAttrPad, pad);

    const void* data = nullptr;
    uint32 sizeInBytes = 0;
    if(attr->getBinary(kAttrPcm, data, sizeInBytes) == kResultOk && data != nullptr)
    {
        int64 channels = 1;
        int64 sampleRate = 48000;
        attr->getInt(kAttrChannels, channels);
        attr->getInt(kAttrSampleRate, sampleRate);
        if(channels < 1)
            channels = 1;

        const int frames = static_cast<int>(sizeInBytes / sizeof(float) /
                                             static_cast<uint32>(channels));
        engine_.loadPadPcm(static_cast<int>(pad),
                           static_cast<const float*>(data), frames,
                           static_cast<int>(channels),
                           static_cast<int>(sampleRate));
        return;
    }

    Steinberg::Vst::TChar pathBuffer[1024] = {};
    if(attr->getString(kAttrPath, pathBuffer,
                       sizeof(pathBuffer) - sizeof(Steinberg::Vst::TChar)) == kResultOk)
    {
        Steinberg::String utf8(pathBuffer);
        utf8.toMultiByte(Steinberg::kCP_Utf8);
        engine_.loadPadWav(static_cast<int>(pad), std::string(utf8.text8()));
    }
}

tresult PLUGIN_API Plugin::notify(IMessage* message)
{
    if(message != nullptr && message->getMessageID() != nullptr &&
       FIDStringsEqual(message->getMessageID(), kUploadMessageId))
    {
        handleUpload(message);
        return kResultOk;
    }
    return SingleComponentEffect::notify(message);
}

tresult PLUGIN_API Plugin::setState(IBStream* state)
{
    if(state == nullptr)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if(!streamer.readInt32(version))
        return kResultFalse;

    float gain = 0.8f;
    streamer.readFloat(gain);
    applyMasterGainParam(gain);
    setParamNormalized(kMasterGainParam, gain);

    int32 padCount = 0;
    streamer.readInt32(padCount);
    for(int32 pad = 0; pad < padCount && pad < kNumPads; ++pad)
    {
        char* path = streamer.readStr8();
        engine_.clearPad(pad);
        if(path != nullptr && path[0] != '\0')
            engine_.loadPadWav(pad, std::string(path));
        if(path != nullptr)
            delete[] path;
    }
    return kResultOk;
}

tresult PLUGIN_API Plugin::getState(IBStream* state)
{
    if(state == nullptr)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    streamer.writeInt32(kStateVersion);
    streamer.writeFloat(static_cast<float>(masterGainValue_));
    streamer.writeInt32(kNumPads);
    for(int pad = 0; pad < kNumPads; ++pad)
        streamer.writeStr8(engine_.padPath(pad).c_str());
    return kResultOk;
}

void Plugin::previewTriggerPad(int pad, float velocity)
{
    engine_.trigger(pad, velocity);
}

void Plugin::previewSetMasterGain(float gain)
{
    applyMasterGainParam(gain);
}

void Plugin::previewRender(float** channels, int32 numChannels, int32 numSamples)
{
    if(numChannels <= 0 || channels == nullptr)
        return;
    float* left = channels[0];
    float* right = numChannels > 1 ? channels[1] : left;
    engine_.render(left, right, numSamples, dsp_);
}

} // namespace mlacker_drum
