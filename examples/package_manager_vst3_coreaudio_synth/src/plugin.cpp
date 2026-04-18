#include "plugin.h"

#include <algorithm>
#include <cstring>

#include "plug_ids.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

namespace mlang_vst3_example {

using namespace Steinberg;
using namespace Steinberg::Vst;

Plugin::Plugin() = default;

FUnknown* Plugin::createInstance(void*)
{
    return static_cast<IComponent*>(new Plugin());
}

tresult PLUGIN_API Plugin::initialize(FUnknown* context)
{
    const tresult result = SingleComponentEffect::initialize(context);
    if(result != kResultOk)
        return result;

    addEventInput(STR16("Event In"), 16);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    auto* waveform = new StringListParameter(STR16("Waveform"), kWaveformParam);
    waveform->appendString(STR16("Sine"));
    waveform->appendString(STR16("Square"));
    waveform->setNormalized(0.0);
    parameters.addParameter(waveform);

    bridge_.reset();
    bridge_.setWaveform(Waveform::Sine);
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
        bridge_.setSampleRate(static_cast<float>(setup.sampleRate));
    return result;
}

tresult PLUGIN_API Plugin::setActive(TBool state)
{
    if(!state)
    {
        bridge_.noteOff();
        bridge_.reset();
        applyWaveformParam(waveformValue_);
    }
    return SingleComponentEffect::setActive(state);
}

void Plugin::applyWaveformParam(ParamValue normalizedValue)
{
    waveformValue_ = normalizedValue;
    bridge_.setWaveform(normalizedValue >= 0.5 ? Waveform::Square
                                               : Waveform::Sine);
}

void Plugin::handleParameterChanges(IParameterChanges* changes)
{
    if(changes == nullptr)
        return;

    const int32 numParams = changes->getParameterCount();
    for(int32 index = 0; index < numParams; ++index)
    {
        IParamValueQueue* queue = changes->getParameterData(index);
        if(queue == nullptr || queue->getParameterId() != kWaveformParam)
            continue;

        int32 sampleOffset = 0;
        ParamValue value = waveformValue_;
        const int32 pointCount = queue->getPointCount();
        if(pointCount > 0 &&
           queue->getPoint(pointCount - 1, sampleOffset, value) == kResultTrue)
        {
            applyWaveformParam(value);
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

        switch(event.type)
        {
            case Event::kNoteOnEvent:
                bridge_.noteOn(event.noteOn.pitch,
                               static_cast<float>(event.noteOn.velocity));
                break;
            case Event::kNoteOffEvent:
                bridge_.noteOff();
                break;
            default:
                break;
        }
    }
}

tresult PLUGIN_API Plugin::process(ProcessData& data)
{
    handleParameterChanges(data.inputParameterChanges);
    handleEvents(data.inputEvents);

    if(data.numOutputs < 1)
        return kResultOk;

    if(data.symbolicSampleSize != kSample32)
        return kResultFalse;

    AudioBusBuffers& outBus = data.outputs[0];
    if(outBus.channelBuffers32 == nullptr || outBus.numChannels <= 0)
        return kResultOk;

    const int32 numChannels = outBus.numChannels;
    for(int32 sampleIndex = 0; sampleIndex < data.numSamples; ++sampleIndex)
    {
        const float sample = bridge_.nextSample();
        for(int32 channel = 0; channel < numChannels; ++channel)
            outBus.channelBuffers32[channel][sampleIndex] = sample;
    }

    outBus.silenceFlags = 0;
    return kResultOk;
}

tresult PLUGIN_API Plugin::setState(IBStream*)
{
    return kResultOk;
}

tresult PLUGIN_API Plugin::getState(IBStream*)
{
    return kResultOk;
}

void Plugin::previewSetWaveform(Waveform waveform)
{
    applyWaveformParam(waveform == Waveform::Square ? 1.0 : 0.0);
}

void Plugin::previewNoteOn(int32 midiNote, float velocity)
{
    bridge_.noteOn(midiNote, velocity);
}

void Plugin::previewNoteOff()
{
    bridge_.noteOff();
}

void Plugin::previewRender(float** channels, int32 numChannels, int32 numSamples)
{
    AudioBusBuffers outBus {};
    outBus.numChannels = numChannels;
    outBus.channelBuffers32 = channels;
    outBus.silenceFlags = 0;

    ProcessData data {};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = numSamples;
    data.numOutputs = 1;
    data.outputs = &outBus;

    process(data);
}

} // namespace mlang_vst3_example
