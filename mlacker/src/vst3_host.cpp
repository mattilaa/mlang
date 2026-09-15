#include "mlang_audio_processor.h"
#include "parameter_changes.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/base/ustring.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include "public.sdk/source/common/commonstringconvert.h"
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;

class Application final : public HostApplication {
    tresult PLUGIN_API getName(String128 name) override {
        UString(name, 128).fromAscii("mlacker"); return kResultOk;
    }
};
Application application;

class Processor {
public:
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    HostProcessData data;
    EventList incoming{4096}, outgoing{512};
    ProcessContext context{};
    mlacker::ParameterChanges parameters;
    ParamID midiParameters[16][130];
    struct CachedParameter {
        ParameterInfo info{};
        std::string title;
        std::atomic<double> value{0};
    };
    static_assert(std::atomic<double>::is_always_lock_free);
    std::unique_ptr<CachedParameter[]> cached;
    int32 parameterCount = 0;
    std::string name;
    bool active = false, processing = false, instrument = false, overflow = false;
    int32 maxFrames = 0;

    ~Processor() {
        // Called after AUHAL has stopped, on the owning main thread.
        if(processing) processor->setProcessing(false);
        if(active) component->setActive(false);
        data.unprepare(); processor.reset(); component.reset(); provider.reset();
        module.reset();
    }

    bool open(const char *path, double rate, int32 frames, std::string &error, bool instrumentOnly) {
        module = VST3::Hosting::Module::create(path, error);
        if(!module) return false;
        const auto &factory = module->getFactory();
        factory.setHostContext(&application);
        for(const auto &info : factory.classInfos()) {
            if(info.category() != kVstAudioEffectClass) continue;
            if(instrumentOnly && std::find(info.subCategories().begin(), info.subCategories().end(), "Instrument") == info.subCategories().end()) continue;
            name = info.name();
            provider = owned(new PlugProvider(factory, info, true));
            if(!provider->initialize()) { error = "VST3 component/controller initialization failed"; return false; }
            component = provider->getComponentPtr();
            processor = U::cast<IAudioProcessor>(component);
            break;
        }
        if(!component || !processor) { error = instrumentOnly ? "Bundle contains no VST3 instrument" : "Bundle contains no VST3 audio processor"; return false; }
        if(processor->canProcessSampleSize(kSample32) != kResultOk) {
            error = "VST3 processor does not support 32-bit float audio"; return false;
        }
        const int32 ins = component->getBusCount(kAudio, kInput);
        const int32 outs = component->getBusCount(kAudio, kOutput);
        if(ins < 0 || ins > 1 || outs != 1) {
            error = "Only zero/one audio input bus and one output bus are supported"; return false;
        }
        BusInfo outInfo{}, inInfo{};
        if(component->getBusInfo(kAudio, kOutput, 0, outInfo) != kResultOk ||
           outInfo.channelCount < 1 || outInfo.channelCount > 2) {
            error = "VST3 output must be mono or stereo"; return false;
        }
        if(ins && (component->getBusInfo(kAudio, kInput, 0, inInfo) != kResultOk ||
                   inInfo.channelCount < 1 || inInfo.channelCount > 2)) {
            error = "VST3 input must be mono or stereo"; return false;
        }
        SpeakerArrangement input = inInfo.channelCount == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        SpeakerArrangement output = outInfo.channelCount == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        if(processor->setBusArrangements(ins ? &input : nullptr, ins, &output, 1) != kResultOk) {
            error = "VST3 processor rejected its mono/stereo bus arrangement"; return false;
        }
        if(component->activateBus(kAudio, kOutput, 0, true) != kResultOk ||
           (ins && component->activateBus(kAudio, kInput, 0, true) != kResultOk)) {
            error = "Could not activate VST3 audio buses"; return false;
        }
        const int32 eventInputs = component->getBusCount(kEvent, kInput);
        if(instrumentOnly && eventInputs < 1) { error = "Instrument requires a MIDI event input"; return false; }
        const int32 eventOutputs = component->getBusCount(kEvent, kOutput);
        if(eventInputs < 0 || eventInputs > 16 || eventOutputs < 0 || eventOutputs > 16) {
            error = "Unsupported VST3 event bus count"; return false;
        }
        for(int32 bus = 0; bus < eventInputs; ++bus)
            if(component->activateBus(kEvent, kInput, bus, bus == 0) != kResultOk && bus == 0) {
                error = "Could not activate VST3 MIDI input"; return false;
            }
        for(int32 bus = 0; bus < eventOutputs; ++bus) component->activateBus(kEvent, kOutput, bus, false);
        ProcessSetup setup{kRealtime, kSample32, frames, rate};
        if(processor->setupProcessing(setup) != kResultOk) { error = "VST3 processing setup failed"; return false; }
        if(!data.prepare(*component, frames, kSample32)) { error = "VST3 buffer allocation failed"; return false; }
        if(data.numOutputs != 1 || data.outputs[0].numChannels < 1 || data.outputs[0].numChannels > 2 ||
           data.numInputs != ins || (ins && (data.inputs[0].numChannels < 1 || data.inputs[0].numChannels > 2))) {
            error = "VST3 processor changed to an unsupported bus layout"; return false;
        }
        context.sampleRate = rate; context.tempo = 120;
        data.processContext = &context; data.processMode = kRealtime;
        data.inputEvents = eventInputs ? &incoming : nullptr; data.outputEvents = &outgoing;
        data.inputParameterChanges = &parameters; data.outputParameterChanges = nullptr;
        auto controller = provider->getControllerPtr();
        // Separate controllers may initially expose defaults unrelated to the
        // processor's loaded patch. Synchronize before caching/saving values.
        if(controller) {
            MemoryStream state;
            if(component->getState(&state) == kResultOk && state.getSize() > 0) {
                state.seek(0, IBStream::kIBSeekSet, nullptr);
                controller->setComponentState(&state);
            }
        }
        parameterCount = controller ? controller->getParameterCount() : 0;
        if(parameterCount < 0 || parameterCount > 16384) { error = "Unsupported parameter count"; return false; }
        cached = std::make_unique<CachedParameter[]>(parameterCount);
        for(int32 i = 0; i < parameterCount; ++i) {
            auto &p = cached[i];
            if(controller->getParameterInfo(i, p.info) != kResultOk || p.info.stepCount < 0) {
                error = "Invalid plugin parameter metadata"; return false;
            }
            p.info.title[127] = 0;
            p.title = StringConvert::convert(std::u16string(p.info.title));
            double value = controller->getParamNormalized(p.info.id);
            p.value.store(std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0);
        }
        auto mapping = U::cast<IMidiMapping>(provider->getControllerPtr());
        for(int ch = 0; ch < 16; ++ch) for(int cc = 0; cc < 130; ++cc) {
            ParamID id = kNoParamId;
            if(mapping && mapping->getMidiControllerAssignment(0, ch, cc, id) != kResultOk) id = kNoParamId;
            midiParameters[ch][cc] = id;
        }
        maxFrames = frames; instrument = instrumentOnly || ins == 0;
        if(component->setActive(true) != kResultOk) { error = "VST3 activation failed"; return false; }
        active = true;
        const auto started = processor->setProcessing(true);
        // The SDK's base AudioEffect legitimately returns kNotImplemented.
        if(started != kResultOk && started != kNotImplemented) { error = "VST3 processing activation failed"; return false; }
        processing = true; return true;
    }

    void note(int32 on, int32 channel, int32 pitch, int32 velocity, int32 offset) noexcept {
        if(!data.inputEvents) return;
        Event event{}; event.busIndex = 0; event.sampleOffset = offset;
        event.flags = Event::kIsLive;
        if(on) {
            event.type = Event::kNoteOnEvent; event.noteOn.channel = static_cast<int16>(channel);
            event.noteOn.pitch = static_cast<int16>(pitch); event.noteOn.velocity = velocity / 127.f;
            event.noteOn.noteId = -1;
        } else {
            event.type = Event::kNoteOffEvent; event.noteOff.channel = static_cast<int16>(channel);
            event.noteOff.pitch = static_cast<int16>(pitch); event.noteOff.velocity = velocity / 127.f;
            event.noteOff.noteId = -1;
        }
        if(incoming.addEvent(event) != kResultOk) overflow = true;
    }

    void begin(bool reset) noexcept {
        incoming.clear(); outgoing.clear(); parameters.clear();
        reset = reset || overflow; overflow = false;
        // Bounded all-notes-off fallback also covers dropped note-offs.
        if(reset)
            for(int32 channel = 0; channel < 16; ++channel)
                for(int32 pitch = 0; pitch < 128; ++pitch) note(0, channel, pitch, 0, 0);
    }

    void control(int32 channel, int32 controller, int32 value, int32 offset) noexcept {
        if(channel < 0 || channel >= 16 || controller < 0 || controller >= 130) return;
        ParamID id = midiParameters[channel][controller];
        if(id == kNoParamId) return;
        int32 index = 0;
        auto *queue = parameters.addParameterData(id, index);
        double normalized = value / (controller == 129 ? 16383.0 : 127.0);
        if(!queue || queue->addPoint(offset, normalized, index) != kResultOk) overflow = true;
        else for(int32 i = 0; i < parameterCount; ++i)
            if(cached[i].info.id == id) cached[i].value.store(normalized, std::memory_order_relaxed);
    }

    double parameterInfo(int32 index, int32 key) const noexcept {
        if(key == 0) return parameterCount;
        if(index < 0 || index >= parameterCount) return -1;
        const auto &p = cached[index];
        if(key == 1) return p.info.stepCount;
        if(key == 2) return p.value.load(std::memory_order_relaxed);
        if(key == 3) return (p.info.flags & ParameterInfo::kIsReadOnly) != 0;
        if(key == 4) return p.info.id;
        return -1;
    }
    void parameter(int32 index, double value, int32 offset) noexcept {
        if(index < 0 || index >= parameterCount || !std::isfinite(value) || value < 0 || value > 1) return;
        auto &p = cached[index];
        if(p.info.flags & ParameterInfo::kIsReadOnly) return;
        int32 point = 0;
        auto *queue = parameters.addParameterData(p.info.id, point);
        if(!queue || queue->addPoint(offset, value, point) != kResultOk) { overflow = true; return; }
        p.value.store(value, std::memory_order_relaxed);
    }

    int32 render(float *stereo, int32 frames, uint64_t clock) noexcept {
        if(frames < 0 || frames > maxFrames || overflow) return -1;
        data.numSamples = frames; context.projectTimeSamples = static_cast<TSamples>(clock);
        context.continousTimeSamples = static_cast<TSamples>(clock);
        context.state = ProcessContext::kContTimeValid;
        if(data.numInputs) {
            auto &bus = data.inputs[0]; bus.silenceFlags = 0;
            for(int32 f = 0; f < frames; ++f) {
                if(bus.numChannels == 1) bus.channelBuffers32[0][f] = (stereo[2*f] + stereo[2*f+1]) * 0.5f;
                else { bus.channelBuffers32[0][f] = stereo[2*f]; bus.channelBuffers32[1][f] = stereo[2*f+1]; }
            }
        }
        auto &output = data.outputs[0]; output.silenceFlags = 0;
        for(int32 ch = 0; ch < output.numChannels; ++ch) std::fill_n(output.channelBuffers32[ch], frames, 0.f);
        try {
            if(processor->process(data) != kResultOk) return -1;
        } catch(...) { return -1; }
        for(int32 f = 0; f < frames; ++f) {
            float l = output.channelBuffers32[0][f];
            float r = output.channelBuffers32[output.numChannels == 1 ? 0 : 1][f];
            if(instrument) { stereo[2*f] += l; stereo[2*f+1] += r; }
            else { stereo[2*f] = l; stereo[2*f+1] = r; }
        }
        return 0;
    }
};

template<bool InstrumentOnly = false>
int32_t load(const char *path, double rate, int32_t frames,
    mlang_audio_processor *out, char *error, int32_t errorSize) {
    try {
        auto plugin = std::make_unique<Processor>();
        std::string why;
        if(!plugin->open(path, rate, frames, why, InstrumentOnly)) {
            std::snprintf(error, errorSize, "%s", why.c_str()); return -1;
        }
        *out = {};
        out->context = plugin.get(); out->instrument = plugin->instrument;
        out->begin = [](void *p, int32_t reset) { static_cast<Processor*>(p)->begin(reset != 0); };
        out->note = [](void *p, int32_t on, int32_t ch, int32_t note, int32_t vel, int32_t offset) { static_cast<Processor*>(p)->note(on, ch, note, vel, offset); };
        out->process = [](void *p, float *buffer, int32_t frames, uint64_t clock) { return static_cast<Processor*>(p)->render(buffer, frames, clock); };
        out->destroy = [](void *p) { delete static_cast<Processor*>(p); };
        out->name = [](void *p) { return static_cast<Processor*>(p)->name.c_str(); };
        out->control = [](void *p, int32_t ch, int32_t cc, int32_t value, int32_t offset) { static_cast<Processor*>(p)->control(ch, cc, value, offset); };
        out->parameter_info = [](void *p, int32_t i, int32_t k) { return static_cast<Processor*>(p)->parameterInfo(i, k); };
        out->parameter_name = [](void *p, int32_t i) -> const char * {
            auto *host = static_cast<Processor*>(p);
            return i >= 0 && i < host->parameterCount ? host->cached[i].title.c_str() : "";
        };
        out->parameter = [](void *p, int32_t i, double v, int32_t offset) { static_cast<Processor*>(p)->parameter(i, v, offset); };
        out->parameter_edited = [](void *p, int32_t i, double value) {
            auto *host = static_cast<Processor*>(p);
            if(i < 0 || i >= host->parameterCount) return;
            auto &parameter = host->cached[i];
            parameter.value.store(value, std::memory_order_relaxed);
            if(auto controller = host->provider->getControllerPtr()) controller->setParamNormalized(parameter.info.id, value);
        };
        plugin.release(); return 0;
    } catch(const std::exception &e) { std::snprintf(error, errorSize, "VST3 load failed: %s", e.what()); }
    catch(...) { std::snprintf(error, errorSize, "VST3 load failed with an unknown exception"); }
    return -1;
}
} // namespace

extern "C" void mlacker_install_vst3_host() {
    PluginContextFactory::instance().setPluginContext(&application);
    // SDK diagnostics must not corrupt the terminal screen.
    PlugProvider::setErrorStream(nullptr);
    mlang_audio_register_processor_factory(load<false>);
    mlang_audio_register_instrument_factory(load<true>);
}
