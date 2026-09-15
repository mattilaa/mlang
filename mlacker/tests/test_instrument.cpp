// Deterministic, headless fixture. Never installed in the user's plugin folders.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
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
class TestInstrument final : public SingleComponentEffect {
    std::array<float, 2048> notes{};
    float level = 0;
public:
    static FUnknown *create(void*) { return static_cast<IComponent*>(new TestInstrument); }
    tresult PLUGIN_API initialize(FUnknown *host) override {
        auto result = SingleComponentEffect::initialize(host);
        if(result != kResultOk) return result;
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
        for(int32 f = 0; f < data.numSamples; ++f) {
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
                data.outputs[0].channelBuffers32[ch][f] = data.inputs[0].channelBuffers32[ch][f] * 0.5f;
#else
                data.outputs[0].channelBuffers32[ch][f] = level;
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
