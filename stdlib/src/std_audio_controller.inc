/* Included by std_audio.c: shares device enumeration and PCM buffer types.
 * Control APIs are not realtime. post() and the render path never allocate,
 * lock, log, or call MLang. One producer per ring, one render consumer.
 */
#define AC_QUEUE 1024u
#define AC_VOICES 128u
#define AC_SAMPLES 256u
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
    double sample_end;
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
    _Atomic uint32_t instrument_peak[32][2];
    /* Post-fader output of a track that owns a buffer (inserts or routing). */
    _Atomic uint32_t track_peak[64][2];
    _Atomic uint64_t live_target;
    _Atomic uint32_t track_volume[64], instrument_volume[32];
    /* Packed slot (6 bits), parameter index (14), step count (32).
     * Main thread arms; the single MIDI producer captures the next CC. */
    _Atomic uint64_t learn_target, learn_map[2048];
    uint64_t live_notes[2048]; /* MIDI producer owned: original destination per key. */
    uint64_t live_panic;
    ac_voice voices[AC_VOICES];
    ac_sample samples[AC_SAMPLES];
    int sample_count, running;
    double rate, gain, frequencies[128];
    int64_t buffer_frames;
    mlang_audio_processor processor;
    mlang_audio_processor instruments[32];
    mlang_audio_processor effects[8];
    mlang_audio_processor inserts[256];
    _Atomic uint32_t insert_route[96][4];
    /* Output channel per source (tracks 0-63, instruments 64-95):
     * 0 = master, 1-64 = the PCM track it feeds instead. */
    _Atomic uint32_t output_route[96];
    float track_buffer[64][8192];
    _Atomic uint32_t sends[96][8], effect_volume[8], effect_peak[8][2];
    float effect_buffer[8][8192];
    float instrument_buffer[8192];
    int32_t render_offset, processor_reset;
    _Atomic uint64_t processor_errors;
    double render_gains[4096];
#if defined(__APPLE__)
    AudioComponentInstance unit;
#endif
} ac_controller;

/* Parameter namespace: master 0, instruments 1-32, aux effects 33-40. */
static mlang_audio_processor *ac_plugin(ac_controller *c, int64_t slot) {
    return slot == 0 ? &c->processor : (slot <= 32 ? &c->instruments[slot - 1] : (slot <= 40 ? &c->effects[slot - 33] : &c->inserts[slot - 41]));
}

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
    for(int i = 0; i < 32; ++i) for(int ch = 0; ch < 2; ++ch) atomic_init(&c->instrument_peak[i][ch], 0);
    for(int i = 0; i < 64; ++i) for(int ch = 0; ch < 2; ++ch) atomic_init(&c->track_peak[i][ch], 0);
    atomic_init(&c->live_target, 0);
    for(int i = 0; i < 96; ++i) for(int fx = 0; fx < 8; ++fx) atomic_init(&c->sends[i][fx], 0);
    for(int i = 0; i < 96; ++i) for(int n = 0; n < 4; ++n) atomic_init(&c->insert_route[i][n], 0);
    for(int i = 0; i < 96; ++i) atomic_init(&c->output_route[i], 0);
    for(int fx = 0; fx < 8; ++fx) {
        atomic_init(&c->effect_volume[fx], 100);
        for(int ch = 0; ch < 2; ++ch) atomic_init(&c->effect_peak[fx][ch], 0);
    }
    for(int i = 0; i < 64; ++i) atomic_init(&c->track_volume[i], 100);
    for(int i = 0; i < 32; ++i) atomic_init(&c->instrument_volume[i], 100);
    atomic_init(&c->learn_target, 0);
    for(int i = 0; i < 2048; ++i) atomic_init(&c->learn_map[i], 0);
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
    if(kind == 4 && (sample < 0 || sample >= AC_SAMPLES || note < 0 || velocity < 0)) return -1;
    if(kind == 10 && (sample < 1 || sample > 296 || note < 0 || note > 16383)) return -1;
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
/* Control thread, atomic gain publication without using either event producer. */
int32_t __mlang_std_audio_controller_track_volume(int64_t handle, int64_t track, int64_t slot, int64_t percent) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || track < 0 || track >= 64 || slot < 0 || slot > 32 || percent < 0 || percent > 100) return -1;
    atomic_store_explicit(&c->track_volume[track], (uint32_t)percent, memory_order_relaxed);
    if(slot) atomic_store_explicit(&c->instrument_volume[slot - 1], (uint32_t)percent, memory_order_relaxed);
    return 0;
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
/* Same single MIDI producer/lane as live_note. Controllers address the
 * currently selected destination, while note-offs retain their held target. */
int32_t __mlang_std_audio_controller_live_control(int64_t handle, int64_t channel, int64_t controller, int64_t value) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || channel < 0 || channel > 15 || !((controller >= 0 && controller <= 127) || controller == 129) ||
       value < 0 || value > (controller == 129 ? 16383 : 127)) return -1;
    if(controller <= 127) {
        int key = (int)(channel * 128 + controller);
        uint64_t learned = atomic_exchange_explicit(&c->learn_target, 0, memory_order_acq_rel);
        if(learned) atomic_store_explicit(&c->learn_map[key], learned, memory_order_release);
        uint64_t mapping = atomic_load_explicit(&c->learn_map[key], memory_order_acquire);
        if(mapping) {
            double steps = (double)(mapping >> 20);
            double normalized = (double)value / 127.0;
            if(steps > 0) normalized = floor(normalized * steps + 0.5) / steps;
            return __mlang_std_audio_controller_post(handle, 1, 10, channel,
                (mapping >> 6) & 16383, 0, 0, -1, mapping & 63, normalized);
        }
    }
    uint64_t target = atomic_load_explicit(&c->live_target, memory_order_acquire);
    if(!target) return 0;
    int64_t source = (int64_t)(target & 127u) - 1;
    int64_t instrument = (int64_t)(target >> 7) - 1;
    return __mlang_std_audio_controller_post(handle, 1, instrument ? 9 : 8,
        channel, controller, value, source + 65536, -1, instrument, 1.0);
}
int32_t __mlang_std_audio_controller_master_peak(int64_t handle, int64_t channel) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || channel < 0 || channel > 1) return 0;
    return (int32_t)atomic_exchange_explicit(&c->master_peak[channel], 0, memory_order_relaxed);
}
/* Single UI consumer: post-instrument-fader, pre-master stereo peaks, 0-1000. */
int32_t __mlang_std_audio_controller_instrument_peak(int64_t handle, int64_t slot, int64_t channel) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || channel < 0 || channel > 1) return 0;
    return (int32_t)atomic_exchange_explicit(&c->instrument_peak[slot - 1][channel], 0, memory_order_relaxed);
}
/* Post-fader output of a track channel, 0 for one that mixes straight into
 * master without a buffer of its own. */
int32_t __mlang_std_audio_controller_track_peak(int64_t handle, int64_t track, int64_t channel) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || track < 0 || track >= 64 || channel < 0 || channel > 1) return 0;
    return (int32_t)atomic_exchange_explicit(&c->track_peak[track][channel], 0, memory_order_relaxed);
}

static void ac_dispatch(ac_controller *c, const ac_event *e) {
    if(e->kind == 10) {
        mlang_audio_processor *p = ac_plugin(c, e->sample);
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
    if(e->kind == 4) {
        slot->phase = (double)e->note;
        slot->sample_end = e->velocity > 0 ? slot->phase + (double)e->velocity : (double)c->samples[slot->sample].frames;
    }
}

static void ac_insert_process(ac_controller *c, const uint32_t route[4], float *buffer, uint32_t frames, uint64_t clock) {
    for(int n = 0; n < 4; ++n) {
        uint32_t id = route[n]; if(!id || id > 256) continue;
        mlang_audio_processor *p = &c->inserts[id - 1]; if(!p->context) continue;
        if(p->process(p->context, buffer, (int32_t)frames, clock) != 0) {
            memset(buffer, 0, frames * 2u * sizeof(float));
            atomic_fetch_add_explicit(&c->processor_errors, 1, memory_order_relaxed);
        }
    }
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
    uint32_t routes[96][4]; int routed[96] = {0};
    /* Output channel of each source (-1 = master) and the tracks that need a
     * buffer of their own: those with inserts, those feeding another track,
     * and those fed by one. */
    int target[96]; int buffered[64] = {0};
    for(int i = 0; i < 256; ++i) if(c->inserts[i].context) c->inserts[i].begin(c->inserts[i].context, reset);
    for(int source = 0; source < 96; ++source) {
        for(int n = 0; n < 4; ++n) {
            routes[source][n] = atomic_load_explicit(&c->insert_route[source][n], memory_order_relaxed);
            if(routes[source][n]) routed[source] = 1;
        }
        uint32_t out_route = atomic_load_explicit(&c->output_route[source], memory_order_relaxed);
        target[source] = out_route > 0 && out_route <= 64 && (int)out_route - 1 != source ? (int)out_route - 1 : -1;
        if(source < 64 && routed[source]) buffered[source] = 1;
        if(target[source] >= 0) { buffered[target[source]] = 1; if(source < 64) buffered[source] = 1; }
    }
    for(int t = 0; t < 64; ++t) if(buffered[t]) memset(c->track_buffer[t], 0, frames * 2u * sizeof(float));
    /* Feeders before their destination; a cycle falls back to master. */
    int order[64], count = 0, indegree[64] = {0}, cycled[64] = {0};
    for(int t = 0; t < 64; ++t) if(buffered[t] && target[t] >= 0) ++indegree[target[t]];
    for(int t = 0; t < 64; ++t) if(buffered[t] && indegree[t] == 0) order[count++] = t;
    for(int i = 0; i < count; ++i) {
        int destination = target[order[i]];
        if(destination >= 0 && --indegree[destination] == 0) order[count++] = destination;
    }
    for(int t = 0; t < 64; ++t) if(buffered[t] && indegree[t] > 0) { cycled[t] = 1; order[count++] = t; }
    float sends[96][8], dry[96];
    for(int source = 0; source < 96; ++source) dry[source] = 1.f;
    for(int fx = 0; fx < 8; ++fx) {
        mlang_audio_processor *p = &c->effects[fx];
        if(p->context) { p->begin(p->context, reset); memset(c->effect_buffer[fx], 0, frames * 2u * sizeof(float)); }
        for(int source = 0; source < 96; ++source) {
            sends[source][fx] = p->context ? atomic_load_explicit(&c->sends[source][fx], memory_order_relaxed) / 100.f : 0;
            // Multiple parallel effects share one dry path. Empty slots never attenuate it.
            dry[source] = fminf(dry[source], 1.f - sends[source][fx]);
        }
    }
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
            double before_l = l, before_r = r;
            if(v->sample < 0) {
                int track = v->source >= 65536 ? v->source - 65536 : v->source;
                double volume = track >= 0 && track < 64 ? atomic_load_explicit(&c->track_volume[track], memory_order_relaxed) / 100.0 : 1.0;
                if(track >= 0 && track < 64 && buffered[track]) volume = 1.0;
                double value = sin(v->phase * 6.283185307179586) * v->gain * volume;
                l += value; r += value; v->phase += v->step;
                v->phase -= floor(v->phase);
            } else {
                ac_sample *s = &c->samples[v->sample];
                int64_t pos = (int64_t)v->phase;
                if(pos >= s->frames || v->phase >= v->sample_end) { v->active = 0; continue; }
                int64_t next = pos + 1 < s->frames ? pos + 1 : pos;
                double mix = v->phase - pos;
                for(int ch = 0; ch < 2; ++ch) {
                    int channel = s->channels == 1 ? 0 : ch;
                    float a = s->data[pos * s->channels + channel], b = s->data[next * s->channels + channel];
                    int track = v->source >= 65536 ? v->source - 65536 : v->source;
                    double volume = track >= 0 && track < 64 ? atomic_load_explicit(&c->track_volume[track], memory_order_relaxed) / 100.0 : 1.0;
                    if(track >= 0 && track < 64 && buffered[track]) volume = 1.0;
                    double value = (a + (b - a) * mix) * v->gain * volume;
                    if(ch == 0) l += value; else r += value;
                }
                v->phase += v->step;
            }
            int track = v->source >= 65536 ? v->source - 65536 : v->source;
            if(track >= 0 && track < 64 && buffered[track]) {
                c->track_buffer[track][f * 2] += (float)(l - before_l);
                c->track_buffer[track][f * 2 + 1] += (float)(r - before_r);
                l = before_l; r = before_r; continue;
            }
            if(track >= 0 && track < 64) for(int fx = 0; fx < 8; ++fx) {
                if(sends[track][fx] == 0) continue;
                c->effect_buffer[fx][f * 2] += (float)(l - before_l) * sends[track][fx];
                c->effect_buffer[fx][f * 2 + 1] += (float)(r - before_r) * sends[track][fx];
            }
            if(track >= 0 && track < 64) {
                l = before_l + (l - before_l) * dry[track];
                r = before_r + (r - before_r) * dry[track];
            }
        }
        c->render_gains[f] = c->gain;
        out[f * 2] = (float)l;
        out[f * 2 + 1] = (float)r;
    }
    // Instruments first: one routed into a track must reach its buffer before
    // that track runs its own inserts and fader.
    for(int i = 0; i < 32; ++i) {
        mlang_audio_processor *p = &c->instruments[i];
        if(!p->context) continue;
        memset(c->instrument_buffer, 0, frames * 2u * sizeof(float));
        if(p->process(p->context, c->instrument_buffer, (int32_t)frames, clock) != 0) {
            atomic_fetch_add_explicit(&c->processor_errors, 1, memory_order_relaxed);
            continue;
        }
        ac_insert_process(c, routes[64 + i], c->instrument_buffer, frames, clock);
        float volume = atomic_load_explicit(&c->instrument_volume[i], memory_order_relaxed) / 100.f;
        float *destination = target[64 + i] >= 0 ? c->track_buffer[target[64 + i]] : out;
        float peaks[2] = {0, 0};
        for(uint32_t f = 0; f < frames * 2u; ++f) {
            if(isfinite(c->instrument_buffer[f])) {
                float value = c->instrument_buffer[f] * volume;
                destination[f] += value * dry[64 + i];
                for(int fx = 0; fx < 8; ++fx)
                    if(sends[64 + i][fx] > 0) c->effect_buffer[fx][f] += value * sends[64 + i][fx];
                peaks[f & 1] = fmaxf(peaks[f & 1], fabsf(value));
            }
        }
        for(int ch = 0; ch < 2; ++ch) {
            uint32_t value = (uint32_t)(fminf(peaks[ch], 1.f) * 1000.f);
            uint32_t old = atomic_load_explicit(&c->instrument_peak[i][ch], memory_order_relaxed);
            while(old < value && !atomic_compare_exchange_weak_explicit(&c->instrument_peak[i][ch], &old, value,
                memory_order_relaxed, memory_order_relaxed)) {}
        }
    }
    for(int i = 0; i < count; ++i) {
        int t = order[i];
        ac_insert_process(c, routes[t], c->track_buffer[t], frames, clock);
        float volume = atomic_load_explicit(&c->track_volume[t], memory_order_relaxed) / 100.f;
        float *destination = target[t] >= 0 && !cycled[t] ? c->track_buffer[target[t]] : out;
        float peaks[2] = {0, 0};
        for(uint32_t f = 0; f < frames * 2u; ++f) {
            float value = c->track_buffer[t][f] * volume; if(!isfinite(value)) continue;
            destination[f] += value * dry[t];
            for(int fx = 0; fx < 8; ++fx) if(sends[t][fx] > 0) c->effect_buffer[fx][f] += value * sends[t][fx];
            peaks[f & 1] = fmaxf(peaks[f & 1], fabsf(value));
        }
        /* Everything the channel carries: its own voices plus what feeds it. */
        for(int ch = 0; ch < 2; ++ch) {
            uint32_t value = (uint32_t)(fminf(peaks[ch], 1.f) * 1000.f);
            uint32_t old = atomic_load_explicit(&c->track_peak[t][ch], memory_order_relaxed);
            while(old < value && !atomic_compare_exchange_weak_explicit(&c->track_peak[t][ch], &old, value,
                memory_order_relaxed, memory_order_relaxed)) {}
        }
    }
    // Parallel post-fader aux returns. No return feeds another effect or itself.
    for(int fx = 0; fx < 8; ++fx) {
        mlang_audio_processor *p = &c->effects[fx];
        if(!p->context) continue;
        if(p->process(p->context, c->effect_buffer[fx], (int32_t)frames, clock) != 0) {
            atomic_fetch_add_explicit(&c->processor_errors, 1, memory_order_relaxed); continue;
        }
        float volume = atomic_load_explicit(&c->effect_volume[fx], memory_order_relaxed) / 100.f;
        float peaks[2] = {0, 0};
        for(uint32_t f = 0; f < frames * 2u; ++f) {
            float value = c->effect_buffer[fx][f] * volume;
            if(!isfinite(value)) continue;
            out[f] += value; peaks[f & 1] = fmaxf(peaks[f & 1], fabsf(value));
        }
        for(int ch = 0; ch < 2; ++ch) {
            uint32_t value = (uint32_t)(fminf(peaks[ch], 1.f) * 1000.f);
            uint32_t old = atomic_load_explicit(&c->effect_peak[fx][ch], memory_order_relaxed);
            while(old < value && !atomic_compare_exchange_weak_explicit(&c->effect_peak[fx][ch], &old, value, memory_order_relaxed, memory_order_relaxed)) {}
        }
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
int32_t __mlang_std_audio_controller_clear_samples(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running) return -1;
    ac_silence(c);
    for(int i = 0; i < c->sample_count; ++i) free(c->samples[i].data);
    c->sample_count = 0;
    for(int lane = 0; lane < 2; ++lane)
        atomic_store_explicit(&c->queues[lane].read, atomic_load_explicit(&c->queues[lane].write, memory_order_acquire), memory_order_release);
    return 0;
}
int64_t __mlang_std_audio_controller_sample_data(int64_t handle, mlang_list_t samples, int64_t channels, int64_t rate) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running || channels < 1 || channels > 2 || rate <= 0 ||
       samples.size <= 0 || samples.size % channels || !samples.data || c->sample_count == AC_SAMPLES ||
       (uint64_t)samples.size > SIZE_MAX / sizeof(float)) return -1;
    size_t bytes = (size_t)samples.size * sizeof(float);
    float *data = malloc(bytes); if(!data) return -1;
    memcpy(data, samples.data, bytes);
    int id = c->sample_count++;
    c->samples[id] = (ac_sample){data, samples.size / channels, (int)channels, (double)rate}; return id;
}
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
    for(int i = 0; i < 32; ++i) for(int ch = 0; ch < 2; ++ch) atomic_store_explicit(&c->instrument_peak[i][ch], 0, memory_order_relaxed);
    for(int i = 0; i < 8; ++i) for(int ch = 0; ch < 2; ++ch) atomic_store_explicit(&c->effect_peak[i][ch], 0, memory_order_relaxed);
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
    for(int i = 0; i < 8; ++i) if(c->effects[i].context) c->effects[i].destroy(c->effects[i].context);
    for(int i = 0; i < 256; ++i) if(c->inserts[i].context) c->inserts[i].destroy(c->inserts[i].context);
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
int32_t __mlang_std_audio_controller_effect_send(int64_t handle, int64_t track, int64_t slot, int64_t fx, int64_t percent) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || track < 0 || track >= 64 || slot < 0 || slot > 32 || fx < 0 || fx >= 8 || percent < 0 || percent > 100) return -1;
    atomic_store_explicit(&c->sends[track][fx], (uint32_t)percent, memory_order_relaxed);
    if(slot) atomic_store_explicit(&c->sends[63 + slot][fx], (uint32_t)percent, memory_order_relaxed);
    return 0;
}
int32_t __mlang_std_audio_controller_effect_volume(int64_t handle, int64_t fx, int64_t percent) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || fx < 0 || fx >= 8 || percent < 0 || percent > 100) return -1;
    atomic_store_explicit(&c->effect_volume[fx], (uint32_t)percent, memory_order_relaxed); return 0;
}
int32_t __mlang_std_audio_controller_effect_peak(int64_t handle, int64_t fx, int64_t channel) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || fx < 0 || fx >= 8 || channel < 0 || channel > 1) return 0;
    return (int32_t)atomic_exchange_explicit(&c->effect_peak[fx][channel], 0, memory_order_relaxed);
}
int32_t __mlang_std_audio_controller_insert_route(int64_t handle, int64_t source, int64_t position, int64_t id) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || source < 0 || source >= 96 || position < 0 || position >= 4 || id < 0 || id > 256) return -1;
    atomic_store_explicit(&c->insert_route[source][position], (uint32_t)id, memory_order_relaxed); return 0;
}
/* Output channel of a source: 0 = master, 1-64 = that PCM track's channel.
 * A source cannot feed itself; a routing cycle falls back to master. */
int32_t __mlang_std_audio_controller_output_route(int64_t handle, int64_t source, int64_t destination) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || source < 0 || source >= 96 || destination < 0 || destination > 64) return -1;
    if(destination > 0 && destination - 1 == source) return -1;
    atomic_store_explicit(&c->output_route[source], (uint32_t)destination, memory_order_relaxed); return 0;
}
int32_t __mlang_std_audio_controller_load_insert(int64_t handle, int64_t id, const char *path) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running || id < 1 || id > 256 || !ac_processor_factory) return -1;
    mlang_audio_processor next = {0};
    if(path && *path) {
        if(ac_processor_factory(path, c->rate, 4096, &next, g_audio_last_error, sizeof(g_audio_last_error)) != 0) return -1;
        if(!next.context || next.instrument || !next.begin || !next.process || !next.destroy || !next.name) {
            if(next.context && next.destroy) next.destroy(next.context);
            audio_set_error("Select a VST3 audio effect, not an instrument"); return -1;
        }
    }
    mlang_audio_processor *p = &c->inserts[id - 1];
    if(p->context) p->destroy(p->context); *p = next; c->processor_reset = 1;
    audio_clear_error(); return 0;
}
int32_t __mlang_std_audio_controller_load_effect(int64_t handle, int64_t fx, const char *path) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running || fx < 0 || fx >= 8 || !ac_processor_factory) { audio_set_error("Effect load requires stopped audio and a valid channel"); return -1; }
    mlang_audio_processor next = {0};
    if(path && *path) {
        if(ac_processor_factory(path, c->rate, 4096, &next, g_audio_last_error, sizeof(g_audio_last_error)) != 0) return -1;
        if(!next.context || next.instrument || !next.begin || !next.process || !next.destroy || !next.name) {
            if(next.context && next.destroy) next.destroy(next.context);
            audio_set_error("Select a VST3 audio effect, not an instrument"); return -1;
        }
    }
    if(c->effects[fx].context) c->effects[fx].destroy(c->effects[fx].context);
    c->effects[fx] = next; c->processor_reset = 1;
    for(int ch = 0; ch < 2; ++ch) atomic_store(&c->effect_peak[fx][ch], 0);
    audio_clear_error(); return 0;
}
double __mlang_std_audio_controller_parameter_info(int64_t handle, int64_t slot, int64_t index, int64_t key) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 0 || slot > 296 || index < 0 || index > 16383 || key < 0 || key > 4) return -1;
    mlang_audio_processor *p = ac_plugin(c, slot);
    return p->context && p->parameter_info ? p->parameter_info(p->context, (int32_t)index, (int32_t)key) : -1;
}
const char *__mlang_std_audio_controller_parameter_name(int64_t handle, int64_t slot, int64_t index) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 296 || index < 0 || index > 16383) return "";
    mlang_audio_processor *p = ac_plugin(c, slot);
    return p->context && p->parameter_name ? p->parameter_name(p->context, (int32_t)index) : "";
}
/* key -1 arms (slot 0 cancels); keys 0..2047 bind channel*128+CC.
 * Only control thread calls this; no plugin metadata is accessed by MIDI. */
int32_t __mlang_std_audio_controller_midi_learn(int64_t handle, int64_t key, int64_t slot, int64_t index) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || key < -1 || key >= 2048 || slot < 0 || slot > 32) return -1;
    uint64_t target = 0;
    if(slot) {
        double steps = __mlang_std_audio_controller_parameter_info(handle, slot, index, 1);
        if(index < 0 || index > 16383 || steps < 0 || steps > INT32_MAX ||
           __mlang_std_audio_controller_parameter_info(handle, slot, index, 3) != 0) return -1;
        target = (uint64_t)slot | ((uint64_t)index << 6) | ((uint64_t)steps << 20);
    }
    atomic_store_explicit(key == -1 ? &c->learn_target : &c->learn_map[key], target, memory_order_release);
    return 0;
}
/* Returns slot (0), index (1), or coherent packed slot/index (2).
 * Unbound index is -1; packed destination is slot | (index << 6). */
int64_t __mlang_std_audio_controller_midi_learn_info(int64_t handle, int64_t key, int64_t field) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || key < -1 || key >= 2048 || field < 0 || field > 2) return field == 1 ? -1 : 0;
    uint64_t target = atomic_load_explicit(key == -1 ? &c->learn_target : &c->learn_map[key], memory_order_acquire);
    if(field == 2) return (int64_t)(target & 1048575);
    return field == 0 ? (int64_t)(target & 63) : (target ? (int64_t)((target >> 6) & 16383) : -1);
}
static void ac_clear_learn_slot(ac_controller *c, int64_t slot) {
    /* Called only with audio stopped and MIDI producer joined. */
    for(int ch = 0; ch < 2; ++ch) atomic_store_explicit(&c->instrument_peak[slot - 1][ch], 0, memory_order_relaxed);
    if((atomic_load(&c->learn_target) & 63) == (uint64_t)slot) atomic_store(&c->learn_target, 0);
    for(int i = 0; i < 2048; ++i)
        if((atomic_load(&c->learn_map[i]) & 63) == (uint64_t)slot) atomic_store(&c->learn_map[i], 0);
}
/* Session restoration only: stopped owner, no MIDI producer. Flush one silent
 * frame so parameters take effect before hardware starts, without queue limits. */
int32_t __mlang_std_audio_controller_restore_parameter(int64_t handle, int64_t slot, int64_t index, double value) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running || slot < 0 || slot > 296 || !isfinite(value) || value < 0 || value > 1 ||
       __mlang_std_audio_controller_parameter_info(handle, slot, index, 1) < 0) return -1;
    mlang_audio_processor *p = ac_plugin(c, slot);
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
        mlang_audio_processor *p = ac_plugin(c, slot);
        if(p->parameter_edited) p->parameter_edited(p->context, (int32_t)index, normalized);
    }
    return result;
}
int32_t __mlang_std_audio_controller_unload_instrument(int64_t handle, int64_t slot) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running || slot < 1 || slot > 32) return -1;
    ac_clear_learn_slot(c, slot);
    mlang_audio_processor *p = &c->instruments[slot - 1];
    if(p->context) p->destroy(p->context);
    *p = (mlang_audio_processor){0}; c->processor_reset = 1; return 0;
}
/* Control thread; audio may run. Hands PCM to a sampler instrument's pad. */
int32_t __mlang_std_audio_controller_instrument_pad(int64_t handle, int64_t slot, int64_t pad,
    mlang_list_t samples, int64_t channels, int64_t rate, const char *name) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || pad < 0 || pad > 127 || channels < 1 || channels > 2 || rate <= 0 ||
       samples.size <= 0 || samples.size % channels || !samples.data) {
        audio_set_error("Pad sample requires a loaded instrument slot, pad and mono/stereo PCM"); return -1;
    }
    mlang_audio_processor *p = &c->instruments[slot - 1];
    if(!p->context) { audio_set_error("Select a loaded instrument"); return -1; }
    if(!p->load_pad) { audio_set_error("This instrument does not accept pad samples"); return -1; }
    char error[256] = {0};
    if(p->load_pad(p->context, (int32_t)pad, (const float*)samples.data, samples.size / channels,
                   (int32_t)channels, (double)rate, name ? name : "", error, (int32_t)sizeof(error)) != 0) {
        audio_set_error(error[0] ? error : "Instrument rejected the pad sample"); return -1;
    }
    audio_clear_error(); return 0;
}
/* Control thread; audio may run. Empties a sampler pad. */
int32_t __mlang_std_audio_controller_instrument_clear_pad(int64_t handle, int64_t slot, int64_t pad) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || pad < 0 || pad > 127) { audio_set_error("Invalid instrument slot or pad"); return -1; }
    mlang_audio_processor *p = &c->instruments[slot - 1];
    if(!p->context || !p->load_pad) { audio_set_error("This instrument does not accept pad samples"); return -1; }
    char error[256] = {0};
    if(p->load_pad(p->context, (int32_t)pad, NULL, 0, 0, 0.0, "", error, (int32_t)sizeof(error)) != 0) {
        audio_set_error(error[0] ? error : "Instrument could not clear the pad"); return -1;
    }
    audio_clear_error(); return 0;
}
/* Control thread. Sampler layout of an instrument: key 0 root key, 1 pad
 * count, 2 loaded-pad bitmask. -1 for non-samplers or invalid slots. */
int64_t __mlang_std_audio_controller_instrument_sampler(int64_t handle, int64_t slot, int64_t key) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || key < 0 || key > 2) return -1;
    mlang_audio_processor *p = &c->instruments[slot - 1];
    return p->context && p->sampler_info ? p->sampler_info(p->context, (int32_t)key) : -1;
}
const char *__mlang_std_audio_controller_instrument_name(int64_t handle, int64_t slot) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 296) return "";
    mlang_audio_processor *p = ac_plugin(c, slot);
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
    ac_clear_learn_slot(c, slot);
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
