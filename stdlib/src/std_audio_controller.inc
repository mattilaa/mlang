/* Included by std_audio.c: shares device enumeration and PCM buffer types.
 * Control APIs are not realtime. post() and the render path never allocate,
 * lock, log, or call MLang. One producer per ring, one render consumer.
 */
#define AC_QUEUE 1024u
#define AC_VOICES 128u
#define AC_SAMPLES 64u
#include "../include/mlang_audio_processor.h"
static mlang_audio_processor_factory ac_processor_factory;
static mlang_audio_processor_factory ac_instrument_factory;
void mlang_audio_register_instrument_factory(mlang_audio_processor_factory factory) {
    ac_instrument_factory = factory;
}
void mlang_audio_register_processor_factory(mlang_audio_processor_factory factory) {
    ac_processor_factory = factory;
}
typedef struct {
    int64_t kind, channel, note, velocity, source, frame, sample;
    double gain;
} ac_event;
typedef struct {
    _Atomic uint64_t read, write;
    ac_event events[AC_QUEUE];
} ac_queue;
typedef struct {
    int active, channel, note, source, sample;
    double phase, step, gain, target;
} ac_voice;
typedef struct {
    float *data;
    int64_t frames, channels, rate;
} ac_sample;
typedef struct {
    ac_queue queues[2]; /* 0 main/sequencer, 1 MIDI worker */
    _Atomic uint64_t dropped, clock, panic;
    uint64_t seen_panic;
    _Atomic uint32_t peak;
    _Atomic uint32_t master_peak[2];
    _Atomic uint64_t live_target;
    uint64_t live_notes[2048]; /* MIDI producer owned: original destination per key. */
    uint64_t live_panic;
    ac_voice voices[AC_VOICES];
    ac_sample samples[AC_SAMPLES];
    int sample_count, running;
    double rate, gain, frequencies[128];
    int64_t buffer_frames;
    mlang_audio_processor processor;
    mlang_audio_processor instruments[32];
    float instrument_buffer[8192];
    int32_t render_offset, processor_reset;
    _Atomic uint64_t processor_errors;
    double render_gains[4096];
#if defined(__APPLE__)
    AudioComponentInstance unit;
#endif
} ac_controller;

static void ac_silence(ac_controller *c) {
    memset(c->voices, 0, sizeof(c->voices));
}

int64_t __mlang_std_audio_controller_new(int64_t rate, int64_t frames) {
    if(rate < 8000 || rate > 384000 || frames < 16 || frames > 4096) {
        audio_set_error("audio controller: invalid sample rate or buffer size"); return 0;
    }
    ac_controller *c = calloc(1, sizeof(*c));
    if(!c) { audio_set_error("audio controller: allocation failed"); return 0; }
    for(int i = 0; i < 2; ++i) {
        atomic_init(&c->queues[i].read, 0); atomic_init(&c->queues[i].write, 0);
    }
    atomic_init(&c->dropped, 0); atomic_init(&c->clock, 0);
    atomic_init(&c->panic, 0); atomic_init(&c->peak, 0);
    atomic_init(&c->master_peak[0], 0); atomic_init(&c->master_peak[1], 0);
    atomic_init(&c->live_target, 0);
    atomic_init(&c->processor_errors, 0);
    if(!atomic_is_lock_free(&c->queues[0].read) || !atomic_is_lock_free(&c->peak)) {
        free(c); audio_set_error("audio controller: lock-free atomics unavailable"); return 0;
    }
    c->rate = rate; c->buffer_frames = frames; c->gain = 0.25;
    for(int i = 0; i < 128; ++i) c->frequencies[i] = 440.0 * pow(2.0, (i - 69) / 12.0);
    return (int64_t)(intptr_t)c;
}

/* Returns 0 posted, 1 full (panic requested), -1 invalid. No error-string work. */
int32_t __mlang_std_audio_controller_post(int64_t handle, int64_t lane,
    int64_t kind, int64_t channel, int64_t note, int64_t velocity,
    int64_t source, int64_t frame, int64_t sample, double gain) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || lane < 0 || lane > 1 || kind < 1 || kind > 10 || frame < -1 ||
       source < 0 || source > INT32_MAX || !isfinite(gain) || gain < 0 || gain > 1) return -1;
    if((kind == 1 || kind == 2 || kind == 6 || kind == 7) && (channel < 0 || channel > 15 ||
        note < 0 || note > 127 || velocity < 0 || velocity > 127)) return -1;
    if(kind == 4 && (sample < 0 || sample >= AC_SAMPLES)) return -1;
    if(kind == 10 && (sample < 1 || sample > 32 || note < 0 || note > 16383)) return -1;
    if((kind == 6 || kind == 7) && (sample < 0 || sample > 32)) return -1;
    if((kind == 8 || kind == 9) && (channel < 0 || channel > 15 ||
        !((note >= 0 && note <= 127) || note == 129) || velocity < 0 ||
        velocity > (note == 129 ? 16383 : 127) || sample < 0 || sample > 32)) return -1;
    ac_queue *q = &c->queues[lane];
    uint64_t w = atomic_load_explicit(&q->write, memory_order_relaxed);
    uint64_t r = atomic_load_explicit(&q->read, memory_order_acquire);
    if(w - r >= AC_QUEUE) {
        atomic_fetch_add_explicit(&c->dropped, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&c->panic, 1, memory_order_release); return 1;
    }
    q->events[w % AC_QUEUE] = (ac_event){kind, channel, note, velocity, source, frame, sample, gain};
    atomic_store_explicit(&q->write, w + 1, memory_order_release); return 0;
}

void __mlang_std_audio_controller_panic(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(c) atomic_fetch_add_explicit(&c->panic, 1, memory_order_release);
}

/* Main/control thread publishes one coherent destination. -1 silences new notes,
 * 0 uses preview/master MIDI, 1-32 addresses a loaded instrument. */
int32_t __mlang_std_audio_controller_midi_target(int64_t handle, int64_t source, int64_t instrument) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || source < 0 || source >= 64 || instrument < -1 || instrument > 32) return -1;
    uint64_t target = instrument < 0 ? 0 : ((uint64_t)(instrument + 1) << 7) | (uint64_t)(source + 1);
    atomic_store_explicit(&c->live_target, target, memory_order_release); return 0;
}
static int32_t ac_post_live(ac_controller *c, uint64_t target, int on, int64_t channel, int64_t pitch, int64_t velocity) {
    if(!target) return 0;
    int64_t source = (int64_t)(target & 127u) - 1;
    int64_t instrument = (int64_t)(target >> 7) - 1;
    return __mlang_std_audio_controller_post((int64_t)(intptr_t)c, 1,
        instrument ? (on ? 6 : 7) : (on ? 1 : 2), channel, pitch, velocity,
        source + 65536, -1, instrument, 1.0);
}
/* Exactly one MIDI producer, sharing lane 1 with no other producer. UI focus
 * changes affect new note-ons; held-note offs keep their original destination. */
int32_t __mlang_std_audio_controller_live_note(int64_t handle, int64_t on, int64_t channel, int64_t pitch, int64_t velocity) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || channel < 0 || channel > 15 || pitch < 0 || pitch > 127 || velocity < 0 || velocity > 127) return -1;
    uint64_t panic = atomic_load_explicit(&c->panic, memory_order_acquire);
    if(panic != c->live_panic) { memset(c->live_notes, 0, sizeof(c->live_notes)); c->live_panic = panic; }
    uint64_t *held = &c->live_notes[channel * 128 + pitch];
    uint64_t previous = *held;
    *held = 0;
    if(previous && ac_post_live(c, previous, 0, channel, pitch, on ? 0 : velocity) != 0) return 1;
    if(!on || !velocity) return 0;
    uint64_t target = atomic_load_explicit(&c->live_target, memory_order_acquire);
    int32_t result = ac_post_live(c, target, 1, channel, pitch, velocity);
    if(result == 0) *held = target;
    return result;
}
int32_t __mlang_std_audio_controller_master_peak(int64_t handle, int64_t channel) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || channel < 0 || channel > 1) return 0;
    return (int32_t)atomic_exchange_explicit(&c->master_peak[channel], 0, memory_order_relaxed);
}

static void ac_dispatch(ac_controller *c, const ac_event *e) {
    if(e->kind == 10) {
        mlang_audio_processor *p = &c->instruments[e->sample - 1];
        if(p->context && p->parameter) p->parameter(p->context, (int32_t)e->note, e->gain, c->render_offset);
        return;
    }
    if(e->kind == 8 || e->kind == 9) {
        mlang_audio_processor *p = e->kind == 8 ? &c->processor :
            (e->sample > 0 ? &c->instruments[e->sample - 1] : NULL);
        if(p && p->context && p->control)
            p->control(p->context, (int32_t)e->channel, (int32_t)e->note, (int32_t)e->velocity, c->render_offset);
        return;
    }
    if(e->kind == 6 || e->kind == 7) {
        if(e->sample > 0) {
            mlang_audio_processor *p = &c->instruments[e->sample - 1];
            if(p->context) p->note(p->context, e->kind == 6 && e->velocity > 0,
                (int32_t)e->channel, (int32_t)e->note, (int32_t)e->velocity, c->render_offset);
        }
        return; /* Unassigned instrument tracks are silent, never preview sine. */
    }
    if(e->kind == 3) { c->gain = e->gain; return; }
    if(c->processor.context && (e->kind == 1 || e->kind == 2)) {
        c->processor.note(c->processor.context, e->kind == 1 && e->velocity > 0,
            (int32_t)e->channel, (int32_t)e->note, (int32_t)e->velocity, c->render_offset);
        if(c->processor.instrument) return;
    }
    int off = e->kind == 2 || (e->kind == 1 && e->velocity == 0);
    ac_voice *slot = NULL;
    for(unsigned i = 0; i < AC_VOICES; ++i) {
        ac_voice *v = &c->voices[i];
        if(!v->active && !slot) slot = v;
        if(v->active && v->source == e->source) {
            if(e->kind == 5) v->target = 0;
            if(v->sample < 0 && v->channel == e->channel && v->note == e->note) {
                if(off) v->target = 0;
                else if(e->kind == 1) slot = v;
            }
        }
    }
    if(off || e->kind == 5 || !slot) return;
    if(e->kind == 4 && e->sample >= c->sample_count) return;
    memset(slot, 0, sizeof(*slot)); slot->active = 1;
    slot->source = (int)e->source; slot->channel = (int)e->channel; slot->note = (int)e->note;
    slot->sample = e->kind == 4 ? (int)e->sample : -1;
    slot->step = slot->sample >= 0 ? c->samples[slot->sample].rate / c->rate : c->frequencies[e->note] / c->rate;
    slot->target = e->kind == 4 ? e->gain : e->velocity / 127.0 * 0.15;
}

static void ac_render(ac_controller *c, float *out, uint32_t frames) {
    if(frames == 0) return; /* Do not consume events/reset on an empty block. */
    uint64_t panic = atomic_load_explicit(&c->panic, memory_order_acquire);
    int reset = c->processor_reset || panic != c->seen_panic;
    c->processor_reset = 0;
    if(c->processor.context) c->processor.begin(c->processor.context, reset);
    for(int i = 0; i < 32; ++i)
        if(c->instruments[i].context) c->instruments[i].begin(c->instruments[i].context, reset);
    if(panic != c->seen_panic) {
        ac_silence(c); c->seen_panic = panic;
        for(int lane = 0; lane < 2; ++lane) {
            ac_queue *q = &c->queues[lane];
            atomic_store_explicit(&q->read, atomic_load_explicit(&q->write, memory_order_acquire), memory_order_release);
        }
    }
    uint64_t clock = atomic_load_explicit(&c->clock, memory_order_relaxed);
    unsigned budget[2] = {256, 256};
    float peak = 0;
    for(uint32_t f = 0; f < frames; ++f) {
        c->render_offset = (int32_t)f;
        for(int lane = 0; lane < 2; ++lane) {
            ac_queue *q = &c->queues[lane];
            while(budget[lane]) {
                uint64_t r = atomic_load_explicit(&q->read, memory_order_relaxed);
                if(r == atomic_load_explicit(&q->write, memory_order_acquire)) break;
                ac_event event = q->events[r % AC_QUEUE];
                if(event.frame >= 0 && (uint64_t)event.frame > clock + f) break;
                atomic_store_explicit(&q->read, r + 1, memory_order_release);
                ac_dispatch(c, &event); --budget[lane];
            }
        }
        double l = 0, r = 0;
        for(unsigned i = 0; i < AC_VOICES; ++i) {
            ac_voice *v = &c->voices[i]; if(!v->active) continue;
            double slew = 1.0 / (c->rate * 0.003);
            if(v->gain < v->target) v->gain = fmin(v->target, v->gain + slew);
            else v->gain = fmax(v->target, v->gain - slew);
            if(v->gain == 0 && v->target == 0) { v->active = 0; continue; }
            if(v->sample < 0) {
                double value = sin(v->phase * 6.283185307179586) * v->gain;
                l += value; r += value; v->phase += v->step;
                v->phase -= floor(v->phase);
            } else {
                ac_sample *s = &c->samples[v->sample];
                int64_t pos = (int64_t)v->phase;
                if(pos >= s->frames) { v->active = 0; continue; }
                int64_t next = pos + 1 < s->frames ? pos + 1 : pos;
                double mix = v->phase - pos;
                for(int ch = 0; ch < 2; ++ch) {
                    int channel = s->channels == 1 ? 0 : ch;
                    float a = s->data[pos * s->channels + channel], b = s->data[next * s->channels + channel];
                    double value = (a + (b - a) * mix) * v->gain;
                    if(ch == 0) l += value; else r += value;
                }
                v->phase += v->step;
            }
        }
        c->render_gains[f] = c->gain;
        out[f * 2] = (float)l;
        out[f * 2 + 1] = (float)r;
    }
    for(int i = 0; i < 32; ++i) {
        mlang_audio_processor *p = &c->instruments[i];
        if(!p->context) continue;
        memset(c->instrument_buffer, 0, frames * 2u * sizeof(float));
        if(p->process(p->context, c->instrument_buffer, (int32_t)frames, clock) != 0) {
            atomic_fetch_add_explicit(&c->processor_errors, 1, memory_order_relaxed);
            continue;
        }
        for(uint32_t f = 0; f < frames * 2u; ++f)
            if(isfinite(c->instrument_buffer[f])) out[f] += c->instrument_buffer[f];
    }
    if(c->processor.context && c->processor.process(c->processor.context, out, (int32_t)frames, clock) != 0) {
        memset(out, 0, frames * 2u * sizeof(float));
        atomic_fetch_add_explicit(&c->processor_errors, 1, memory_order_relaxed);
    }
    float master[2] = {0, 0};
    for(uint32_t f = 0; f < frames; ++f) {
        double l = isfinite(out[f * 2]) ? out[f * 2] * c->render_gains[f] : 0;
        double r = isfinite(out[f * 2 + 1]) ? out[f * 2 + 1] * c->render_gains[f] : 0;
        out[f * 2] = (float)fmax(-1, fmin(1, l));
        out[f * 2 + 1] = (float)fmax(-1, fmin(1, r));
        master[0] = fmaxf(master[0], fabsf(out[f * 2]));
        master[1] = fmaxf(master[1], fabsf(out[f * 2 + 1]));
        peak = fmaxf(peak, fmaxf(fabsf(out[f * 2]), fabsf(out[f * 2 + 1])));
    }
    uint32_t bits; memcpy(&bits, &peak, sizeof(bits));
    atomic_store_explicit(&c->peak, bits, memory_order_relaxed);
    for(int ch = 0; ch < 2; ++ch) {
        uint32_t value = (uint32_t)lroundf(master[ch] * 1000.f);
        uint32_t old = atomic_load_explicit(&c->master_peak[ch], memory_order_relaxed);
        while(old < value && !atomic_compare_exchange_weak_explicit(&c->master_peak[ch], &old, value,
            memory_order_relaxed, memory_order_relaxed)) {}
    }
    atomic_store_explicit(&c->clock, clock + frames, memory_order_release);
}

/* Copy a sample on the control thread, only while stopped. Controller owns it. */
int64_t __mlang_std_audio_controller_sample(int64_t handle, int64_t pcm) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    mlang_pcm_audio_t *p = (mlang_pcm_audio_t*)(intptr_t)pcm;
    if(!c || c->running || !p || p->frame_count <= 0 || c->sample_count == AC_SAMPLES) return -1;
    size_t bytes = (size_t)p->frame_count * (size_t)p->channels * sizeof(float);
    float *data = malloc(bytes); if(!data) return -1;
    memcpy(data, p->samples, bytes);
    int id = c->sample_count++;
    c->samples[id] = (ac_sample){data, p->frame_count, p->channels, p->sample_rate}; return id;
}

int32_t __mlang_std_audio_controller_process(int64_t handle, int64_t block, int64_t frames) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    mlang_pcm_block_t *b = (mlang_pcm_block_t*)(intptr_t)block;
    if(!c || c->running || !b || frames < 0 || frames > b->capacity_frames || frames > 4096) return -1;
    ac_render(c, b->samples, (uint32_t)frames); return 0;
}

#if defined(__APPLE__)
static OSStatus ac_callback(void *context, AudioUnitRenderActionFlags *flags,
    const AudioTimeStamp *time, UInt32 bus, UInt32 frames, AudioBufferList *data) {
    (void)flags; (void)time; (void)bus;
    if(!data) return noErr;
    for(UInt32 i = 0; i < data->mNumberBuffers; ++i)
        if(data->mBuffers[i].mData) memset(data->mBuffers[i].mData, 0, data->mBuffers[i].mDataByteSize);
    if(frames <= 4096 && data->mNumberBuffers == 1 && data->mBuffers[0].mData &&
       data->mBuffers[0].mDataByteSize >= frames * 2u * sizeof(float))
        ac_render(context, data->mBuffers[0].mData, frames);
    return noErr;
}
#endif

int32_t __mlang_std_audio_controller_stop(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return -1;
#if defined(__APPLE__)
    if(c->unit && c->running) {
        OSStatus rc = AudioOutputUnitStop(c->unit);
        if(rc != noErr) { audio_set_error("AUHAL stop failed"); return -1; }
    }
#endif
    c->running = 0; ac_silence(c); c->processor_reset = 1;
    atomic_store_explicit(&c->master_peak[0], 0, memory_order_relaxed);
    atomic_store_explicit(&c->master_peak[1], 0, memory_order_relaxed);
    for(int lane = 0; lane < 2; ++lane) {
        ac_queue *q = &c->queues[lane];
        atomic_store_explicit(&q->read, atomic_load_explicit(&q->write, memory_order_acquire), memory_order_release);
    }
    return 0;
}
int32_t __mlang_std_audio_controller_close(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return 0;
    if(__mlang_std_audio_controller_stop(handle) != 0) return -1;
#if defined(__APPLE__)
    if(c->unit) { AudioUnitUninitialize(c->unit); AudioComponentInstanceDispose(c->unit); }
#endif
    for(int i = 0; i < c->sample_count; ++i) free(c->samples[i].data);
    if(c->processor.context) c->processor.destroy(c->processor.context);
    for(int i = 0; i < 32; ++i)
        if(c->instruments[i].context) c->instruments[i].destroy(c->instruments[i].context);
    free(c); return 0;
}
int32_t __mlang_std_audio_controller_start(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return -1;
#if defined(__APPLE__)
    if(c->unit) {
        if(c->running) return 0;
        c->running = 1;
        if(AudioOutputUnitStart(c->unit) == noErr) return 0;
        c->running = 0; audio_set_error("AUHAL start failed"); return -1;
    }
#endif
    audio_set_error("audio controller has no hardware output"); return -1;
}
int64_t __mlang_std_audio_controller_open(int64_t device, int64_t frames) {
#if defined(__APPLE__)
    AudioDeviceID id = coreaudio_device_for_index(device < 0 ? coreaudio_default_output_index() : device);
    if(id == kAudioObjectUnknown) { audio_set_error("AUHAL output device unavailable"); return 0; }
    double rate = coreaudio_nominal_sample_rate(id);
    int64_t handle = __mlang_std_audio_controller_new((int64_t)rate, frames);
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return 0;
    UInt32 size = sizeof(UInt32), actual = (UInt32)frames;
    AudioObjectPropertyAddress address = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    /* The requested size is a hint: devices may reject it. Always report actual. */
    AudioObjectSetPropertyData(id, &address, 0, NULL, sizeof(actual), &actual);
    OSStatus rc = AudioObjectGetPropertyData(id, &address, 0, NULL, &size, &actual);
    if(rc == noErr && (actual < 16 || actual > 4096)) rc = kAudio_ParamError;
    c->buffer_frames = actual;
    AudioComponentDescription desc = {kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if(!component) rc = kAudio_ParamError;
    if(rc == noErr) rc = AudioComponentInstanceNew(component, &c->unit);
    UInt32 enabled = 1, disabled = 0;
    if(rc == noErr) rc = AudioUnitSetProperty(c->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &disabled, sizeof(disabled));
    if(rc == noErr) rc = AudioUnitSetProperty(c->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enabled, sizeof(enabled));
    if(rc == noErr) rc = AudioUnitSetProperty(c->unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &id, sizeof(id));
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = rate; fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = 2 * sizeof(float);
    fmt.mFramesPerPacket = 1; fmt.mChannelsPerFrame = 2; fmt.mBitsPerChannel = 32;
    if(rc == noErr) rc = AudioUnitSetProperty(c->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
    UInt32 maximum = 4096;
    if(rc == noErr) rc = AudioUnitSetProperty(c->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maximum, sizeof(maximum));
    AURenderCallbackStruct callback = {ac_callback, c};
    if(rc == noErr) rc = AudioUnitSetProperty(c->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback));
    if(rc == noErr) rc = AudioUnitInitialize(c->unit);
    if(rc != noErr) {
        __mlang_std_audio_controller_close(handle);
        snprintf(g_audio_last_error, sizeof(g_audio_last_error), "AUHAL output setup failed (%d)", (int)rc); return 0;
    }
    return handle;
#else
    (void)device; (void)frames; audio_set_error("AUHAL output requires macOS"); return 0;
#endif
}
int64_t __mlang_std_audio_controller_info(int64_t handle, int64_t key) {
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return 0;
    if(key == 0) return (int64_t)c->rate;
    if(key == 1) return c->buffer_frames;
    if(key == 2) return atomic_load_explicit(&c->clock, memory_order_acquire);
    if(key == 3) return atomic_load_explicit(&c->dropped, memory_order_relaxed);
    if(key == 4) return atomic_load_explicit(&c->processor_errors, memory_order_relaxed);
#if defined(__APPLE__)
    if(key == 5) return c->unit != NULL;
#endif
    return 0;
}

int32_t __mlang_std_audio_controller_processor_support(void) { return ac_processor_factory != NULL; }
double __mlang_std_audio_controller_parameter_info(int64_t handle, int64_t slot, int64_t index, int64_t key) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 0 || slot > 32 || index < 0 || index > 16383 || key < 0 || key > 4) return -1;
    mlang_audio_processor *p = slot ? &c->instruments[slot - 1] : &c->processor;
    return p->context && p->parameter_info ? p->parameter_info(p->context, (int32_t)index, (int32_t)key) : -1;
}
const char *__mlang_std_audio_controller_parameter_name(int64_t handle, int64_t slot, int64_t index) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || index < 0 || index > 16383) return "";
    mlang_audio_processor *p = &c->instruments[slot - 1];
    return p->context && p->parameter_name ? p->parameter_name(p->context, (int32_t)index) : "";
}
/* Session restoration only: stopped owner, no MIDI producer. Flush one silent
 * frame so parameters take effect before hardware starts, without queue limits. */
int32_t __mlang_std_audio_controller_restore_parameter(int64_t handle, int64_t slot, int64_t index, double value) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running || slot < 0 || slot > 32 || !isfinite(value) || value < 0 || value > 1 ||
       __mlang_std_audio_controller_parameter_info(handle, slot, index, 1) < 0) return -1;
    mlang_audio_processor *p = slot ? &c->instruments[slot - 1] : &c->processor;
    if(__mlang_std_audio_controller_parameter_info(handle, slot, index, 3) != 0) return 0;
    /* Some plugins expose MIDI/command parameters as well as persistent
     * controls. Replaying their unchanged defaults can trigger commands (and
     * alter the patch). A fresh instance already owns this value: do not send
     * a redundant event or render an extra restoration block for it. */
    if(__mlang_std_audio_controller_parameter_info(handle, slot, index, 2) == value) return 0;
    if(!p->parameter) return -1;
    float silent[2] = {0, 0};
    p->begin(p->context, 0); p->parameter(p->context, (int32_t)index, value, 0);
    if(p->process(p->context, silent, 1, 0) != 0) return -1;
    if(p->parameter_edited) p->parameter_edited(p->context, (int32_t)index, value);
    return 0;
}
int32_t __mlang_std_audio_controller_set_parameter(int64_t handle, int64_t slot, int64_t index, double value) {
    double steps = __mlang_std_audio_controller_parameter_info(handle, slot, index, 1);
    if(steps < 0 || !isfinite(value) || value < 0 || value > (steps > 0 ? steps : 1) ||
       (steps > 0 && floor(value) != value) ||
       __mlang_std_audio_controller_parameter_info(handle, slot, index, 3) != 0) return -1;
    double normalized = steps > 0 ? value / steps : value;
    int32_t result = __mlang_std_audio_controller_post(handle, 0, 10, 0, index, 0, 0, -1, slot, normalized);
    if(result == 0) {
        ac_controller *c = (ac_controller*)(intptr_t)handle;
        mlang_audio_processor *p = &c->instruments[slot - 1];
        if(p->parameter_edited) p->parameter_edited(p->context, (int32_t)index, normalized);
    }
    return result;
}
int32_t __mlang_std_audio_controller_unload_instrument(int64_t handle, int64_t slot) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running || slot < 1 || slot > 32) return -1;
    mlang_audio_processor *p = &c->instruments[slot - 1];
    if(p->context) p->destroy(p->context);
    *p = (mlang_audio_processor){0}; c->processor_reset = 1; return 0;
}
const char *__mlang_std_audio_controller_instrument_name(int64_t handle, int64_t slot) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32) return "";
    mlang_audio_processor *p = &c->instruments[slot - 1];
    return p->context ? p->name(p->context) : "";
}
int32_t __mlang_std_audio_controller_load_instrument(int64_t handle, int64_t slot, const char *path) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running || slot < 1 || slot > 32 || !ac_instrument_factory || !path || !*path) {
        audio_set_error("Instrument load requires a stopped controller, host and slot 1-32"); return -1;
    }
    mlang_audio_processor next = {0};
    if(ac_instrument_factory(path, c->rate, 4096, &next, g_audio_last_error, sizeof(g_audio_last_error)) != 0) return -1;
    if(!next.context || !next.instrument || !next.begin || !next.note || !next.process || !next.destroy || !next.name) {
        if(next.context && next.destroy) next.destroy(next.context);
        audio_set_error("Not a supported instrument processor"); return -1;
    }
    mlang_audio_processor *old = &c->instruments[slot - 1];
    if(old->context) old->destroy(old->context);
    *old = next; c->processor_reset = 1; audio_clear_error(); return 0;
}
const char *__mlang_std_audio_controller_processor_name(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    return c && c->processor.context ? c->processor.name(c->processor.context) : "";
}
int32_t __mlang_std_audio_controller_load_processor(int64_t handle, const char *path) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running) { audio_set_error("Stop audio before changing the master processor"); return -1; }
    mlang_audio_processor next = {0};
    if(path && *path) {
        if(!ac_processor_factory) { audio_set_error("No processor host installed; run mlacker for VST3 support"); return -1; }
        if(ac_processor_factory(path, c->rate, 4096, &next, g_audio_last_error, sizeof(g_audio_last_error)) != 0) return -1;
        if(!next.context || !next.begin || !next.note || !next.process || !next.destroy || !next.name) {
            if(next.context && next.destroy) next.destroy(next.context);
            audio_set_error("Incomplete audio processor callbacks"); return -1;
        }
    }
    if(c->processor.context) c->processor.destroy(c->processor.context);
    c->processor = next; c->processor_reset = 1; ac_silence(c);
    audio_clear_error(); return 0;
}
