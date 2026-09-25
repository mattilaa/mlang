// Deterministic, headless fixture. Never installed in the user's plugin folders.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <array>
#include <algorithm>
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
#ifdef MLACKER_TEST_EFFECT
const FUID testUID(0x1C8EAF01, 0x39AA4DEC, 0x8CE2E11B, 0x30E82967);
#define TEST_NAME "Mlacker Test Effect"
#define TEST_TYPE "Fx"
#else
const FUID testUID(0x1C8EAF00, 0x39AA4DEC, 0x8CE2E11B, 0x30E82967);
#define TEST_NAME "Mlacker Test Instrument"
#define TEST_TYPE PlugType::kInstrumentSynth
#endif
class TestInstrument final : public SingleComponentEffect, public IMidiMapping {
    std::array<float, 2048> notes{};
    float level = 0;
    double controls[3] = {1, 1, 1};
public:
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID &id) override {
        if(bus != 0 || channel < 0 || channel > 15) return kResultFalse;
        if(cc == 1) id = 100; else if(cc == 74) id = 101; else if(cc == 129) id = 102; else return kResultFalse;
        return kResultOk;
    }
    static FUnknown *create(void*) { return static_cast<IComponent*>(new TestInstrument); }
    tresult PLUGIN_API initialize(FUnknown *host) override {
        auto result = SingleComponentEffect::initialize(host);
        if(result != kResultOk) return result;
        parameters.addParameter(STR16("Modulation"), nullptr, 0, 1, ParameterInfo::kCanAutomate, 100);
        parameters.addParameter(STR16("Brightness"), nullptr, 0, 1, ParameterInfo::kCanAutomate, 101);
        parameters.addParameter(STR16("Pitch bend"), nullptr, 0, 1, ParameterInfo::kCanAutomate, 102);
        parameters.addParameter(STR16("Mode"), nullptr, 3, 0, ParameterInfo::kCanAutomate, 103);
        parameters.addParameter(STR16("Read only"), nullptr, 0, 0.5, ParameterInfo::kIsReadOnly, 104);
        // Enough controls to exercise multiple horizontal pages in the TUI.
        for(int i = 0; i < 35; ++i) parameters.addParameter(STR16("Extra control"), nullptr, 0, i == 34 ? 0.0 : 0.5, ParameterInfo::kCanAutomate, 200 + i);
        addAudioOutput(STR16("Stereo"), SpeakerArr::kStereo);
#ifdef MLACKER_TEST_EFFECT
        addAudioInput(STR16("Stereo"), SpeakerArr::kStereo);
#endif
        addEventInput(STR16("MIDI"), 16); return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultOk : kResultFalse; }
    tresult PLUGIN_API setActive(TBool active) override {
        notes.fill(0); level = 0; return SingleComponentEffect::setActive(active);
    }
    tresult PLUGIN_API setState(IBStream*) override { return kResultOk; }
    tresult PLUGIN_API getState(IBStream*) override { return kResultOk; }
    tresult PLUGIN_API process(ProcessData &data) override {
        if(data.numOutputs != 1 || data.symbolicSampleSize != kSample32) return kResultFalse;
        int32 next = 0, count = data.inputEvents ? data.inputEvents->getEventCount() : 0;
        // Observable transport: while the host plays at a valid tempo and
        // beat, the effect scales by tempo / 240.
        float transport = 1;
        const uint32 playing = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
        if(data.processContext && (data.processContext->state & playing) == playing && data.processContext->projectTimeMusic >= 0)
            transport = static_cast<float>(data.processContext->tempo / 240);
        for(int32 f = 0; f < data.numSamples; ++f) {
            if(data.inputParameterChanges) for(int32 q = 0; q < data.inputParameterChanges->getParameterCount(); ++q) {
                auto *queue = data.inputParameterChanges->getParameterData(q);
                // Virtual command parameter: default 0 means no command has
                // been sent, not that the DSP's current volume is zero.
                // Replaying it unnecessarily during restore mutes the synth.
                if(queue->getParameterId() == 234) {
                    for(int32 p = 0; p < queue->getPointCount(); ++p) {
                        int32 offset = 0; ParamValue value = 0;
                        if(queue->getPoint(p, offset, value) == kResultOk && offset == f) controls[1] = value;
                    }
                }
                if(queue->getParameterId() < 100 || queue->getParameterId() > 102) continue;
                for(int32 p = 0; p < queue->getPointCount(); ++p) {
                    int32 offset = 0; ParamValue value = 0;
                    if(queue->getPoint(p, offset, value) == kResultOk && offset == f) controls[queue->getParameterId() - 100] = value;
                }
            }
            while(next < count) {
                Event event{}; data.inputEvents->getEvent(next, event);
                if(event.sampleOffset > f) break;
                ++next;
                int ch = -1, pitch = -1; float value = 0;
                if(event.type == Event::kNoteOnEvent) { ch = event.noteOn.channel; pitch = event.noteOn.pitch; value = event.noteOn.velocity * 0.5f; }
                if(event.type == Event::kNoteOffEvent) { ch = event.noteOff.channel; pitch = event.noteOff.pitch; }
                if(ch >= 0 && ch < 16 && pitch >= 0 && pitch < 128) {
                    auto &old = notes[ch * 128 + pitch]; level += value - old; old = value;
                }
            }
            for(int32 ch = 0; ch < data.outputs[0].numChannels; ++ch) {
#ifdef MLACKER_TEST_EFFECT
                data.outputs[0].channelBuffers32[ch][f] = data.inputs[0].channelBuffers32[ch][f] * 0.5f * controls[0] * controls[1] * controls[2] * transport;
#else
                data.outputs[0].channelBuffers32[ch][f] = level * controls[0] * controls[1] * controls[2];
#endif
            }
        }
        // Conservative flag: a note can end after sounding earlier in this block.
        data.outputs[0].silenceFlags = 0; return kResultOk;
    }
};
}
BEGIN_FACTORY_DEF("mlacker", "https://example.invalid", "test@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(testUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
    TEST_NAME, 0, TEST_TYPE, "0.1.0", kVstVersionString, TestInstrument::create)
END_FACTORY
