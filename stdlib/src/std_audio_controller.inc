/* Included by std_audio.c: shares device enumeration and PCM buffer types.
 * Control APIs are not realtime. post() and the render path never allocate,
 * lock, log, or call MLang. One producer per ring, one render consumer.
 */
#define AC_QUEUE 1024u
#define AC_LANES 3
#define AC_VOICES 128u
#define AC_SAMPLES 256u
/* Stereo output buses per instrument, main included. */
#define AC_OUTPUTS 16
/* Master bus: 24 dB/oct high-pass, four peaking bands, then volume. Parameter
 * slots: 0 = HP Hz (0 = off), 1 + 3*band = band Hz, gain dB, Q; 13 = linear
 * volume; 14 = EQ (high-pass and bands) enabled, 1 or 0. Defaults are
 * transparent: HP off, flat bands, unity volume, EQ enabled. */
#define AC_MASTER_PARAMS 15
#define AC_MASTER_FILTERS 6
/* Post-master mono tap for the analyzer: power of two, >= largest FFT. */
#define AC_SPECTRUM_RING 16384u
/* Live audio input ring: interleaved stereo frames at the input's rate. */
#define AC_INPUT_RING 131072u
#define AC_SPECTRUM_MAX 8192
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "../include/mlang_audio_processor.h"
#if defined(_WIN32)
#include <windows.h>
static void ac_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void ac_sleep_ms(int ms) { struct timespec t = {0, (long)ms * 1000000L}; nanosleep(&t, NULL); }
#endif
/* Render worker pool. The render thread splits a stage (instruments, one level
 * of tracks, aux returns) into jobs that only touch their own buffer and
 * plugin, runs a share of them itself, waits for the rest, then sums the
 * results in slot order on its own, so the mix is identical for any thread
 * count. Workers sleep on a semaphore between blocks and never allocate,
 * lock or call MLang while rendering. */
#define AC_MAX_THREADS 32
#if !defined(_WIN32)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <os/workgroup.h>
#include <sys/sysctl.h>
typedef dispatch_semaphore_t ac_sem;
static int ac_sem_init(ac_sem *s) { *s = dispatch_semaphore_create(0); return *s ? 0 : -1; }
static void ac_sem_post(ac_sem *s) { dispatch_semaphore_signal(*s); }
static void ac_sem_wait(ac_sem *s) { dispatch_semaphore_wait(*s, DISPATCH_TIME_FOREVER); }
static void ac_sem_destroy(ac_sem *s) { if(*s) dispatch_release(*s); *s = NULL; }
static uint64_t ac_now_ns(void) {
    static mach_timebase_info_data_t base;
    if(!base.denom) mach_timebase_info(&base);
    return mach_absolute_time() * base.numer / base.denom;
}
#elif !defined(_WIN32)
#include <semaphore.h>
typedef sem_t ac_sem;
static int ac_sem_init(ac_sem *s) { return sem_init(s, 0, 0); }
static void ac_sem_post(ac_sem *s) { sem_post(s); }
static void ac_sem_wait(ac_sem *s) { while(sem_wait(s) != 0) {} }
static void ac_sem_destroy(ac_sem *s) { sem_destroy(s); }
static uint64_t ac_now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
#endif
#if defined(_WIN32)
static uint64_t ac_now_ns(void) {
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&counter);
    return (uint64_t)((double)counter.QuadPart * 1e9 / (double)frequency.QuadPart);
}
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
static void ac_relax(void) { _mm_pause(); }
#elif defined(__aarch64__)
static void ac_relax(void) { __asm__ __volatile__("yield"); }
#else
static void ac_relax(void) {}
#endif
/* Logical cores the OS reports online. */
static int ac_cpu_count(void) {
#if defined(_WIN32)
    SYSTEM_INFO info; GetSystemInfo(&info); return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 1;
#endif
}
/* Global cap on threads (render thread included) per controller; 0 = every
 * core. Read once per block, so it applies to running controllers too. */
static _Atomic int ac_thread_limit = 0;
typedef void (*ac_job_fn)(void *context, int job);
typedef struct ac_pool {
    int workers; /* spawned, excluding the render thread */
    /* (generation << 32) | (job count << 16) | next job. Claimed by CAS, so a
     * worker that wakes late can never run a job of a finished batch. */
    _Atomic uint64_t cursor;
    _Atomic uint32_t finished;
    _Atomic int stop;
    ac_job_fn run; void *context;
    uint32_t generation;
    /* Nanoseconds workers spent running jobs, for the load meter. */
    _Atomic uint64_t worker_busy_ns;
#if !defined(_WIN32)
    ac_sem wake;
    pthread_t threads[AC_MAX_THREADS];
#endif
#if defined(__APPLE__)
    /* Realtime period for worker threads and the device IO workgroup they
     * join, so the scheduler keeps them on performance cores with the
     * AUHAL thread. Published by the control thread; joined by each worker. */
    _Atomic uint64_t period_ns;
    _Atomic(void *) workgroup;
#endif
} ac_pool;
/* Runs claimed jobs until the batch is exhausted. Returns the count run. */
static int ac_pool_drain(ac_pool *pool, uint32_t generation) {
    int ran = 0;
    for(;;) {
        uint64_t cursor = atomic_load_explicit(&pool->cursor, memory_order_acquire);
        uint32_t count = (uint32_t)(cursor >> 16) & 0xffffu, next = (uint32_t)cursor & 0xffffu;
        if((uint32_t)(cursor >> 32) != generation || next >= count) return ran;
        if(!atomic_compare_exchange_weak_explicit(&pool->cursor, &cursor, cursor + 1,
            memory_order_acq_rel, memory_order_acquire)) continue;
        pool->run(pool->context, (int)next);
        atomic_fetch_add_explicit(&pool->finished, 1, memory_order_release);
        ++ran;
    }
}
#if !defined(_WIN32)
/* Worker-owned realtime state: the period and workgroup last applied. */
typedef struct { uint64_t period; void *workgroup_seen, *joined; } ac_worker_state;
static void ac_worker_realtime(ac_pool *pool, ac_worker_state *state, void *token) {
#if defined(__APPLE__)
    uint64_t period = atomic_load_explicit(&pool->period_ns, memory_order_acquire);
    if(period && period != state->period) {
        static mach_timebase_info_data_t base;
        if(!base.denom) mach_timebase_info(&base);
        uint64_t ticks = period * base.denom / base.numer;
        thread_time_constraint_policy_data_t policy = {(uint32_t)ticks, (uint32_t)(ticks / 2), (uint32_t)ticks, 1};
        thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
            (thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
        state->period = period;
    }
    void *workgroup = atomic_load_explicit(&pool->workgroup, memory_order_acquire);
    if(workgroup != state->workgroup_seen) {
        state->workgroup_seen = workgroup;
        if(__builtin_available(macOS 11.0, *)) {
            if(state->joined) { os_workgroup_leave((os_workgroup_t)state->joined, token); state->joined = NULL; }
            if(workgroup && os_workgroup_join((os_workgroup_t)workgroup, token) == 0) state->joined = workgroup;
        }
    }
#else
    (void)pool; (void)state; (void)token;
#endif
}
static void *ac_worker_main(void *argument) {
    ac_pool *pool = argument;
    ac_worker_state state = {0};
#if defined(__APPLE__)
    os_workgroup_join_token_s token = {0};
    void *token_ptr = &token;
#else
    void *token_ptr = NULL;
    struct sched_param param = {0};
    param.sched_priority = sched_get_priority_min(SCHED_FIFO) + 10;
    (void)pthread_setschedparam(pthread_self(), SCHED_FIFO, &param); /* best effort */
#endif
    for(;;) {
        ac_sem_wait(&pool->wake);
        if(atomic_load_explicit(&pool->stop, memory_order_acquire)) break;
        ac_worker_realtime(pool, &state, token_ptr);
        uint32_t generation = (uint32_t)(atomic_load_explicit(&pool->cursor, memory_order_acquire) >> 32);
        uint64_t start = ac_now_ns();
        if(ac_pool_drain(pool, generation))
            atomic_fetch_add_explicit(&pool->worker_busy_ns, ac_now_ns() - start, memory_order_relaxed);
    }
#if defined(__APPLE__)
    if(state.joined) { if(__builtin_available(macOS 11.0, *)) os_workgroup_leave((os_workgroup_t)state.joined, &token); }
#endif
    return NULL;
}
#endif
/* Control thread, while stopped: spawn up to cores - 1 sleeping workers. */
static void ac_pool_ensure(ac_pool *pool) {
#if !defined(_WIN32)
    if(pool->workers < 0) return; /* spawning failed before; stay serial */
    int wanted = ac_cpu_count() - 1;
    if(wanted > AC_MAX_THREADS - 1) wanted = AC_MAX_THREADS - 1;
    if(pool->workers >= wanted) return;
    if(pool->workers == 0 && ac_sem_init(&pool->wake) != 0) { pool->workers = -1; return; }
    while(pool->workers < wanted) {
        if(pthread_create(&pool->threads[pool->workers], NULL, ac_worker_main, pool) != 0) break;
        ++pool->workers;
    }
#else
    (void)pool;
#endif
}
static void ac_pool_shutdown(ac_pool *pool) {
#if !defined(_WIN32)
    if(pool->workers <= 0) return;
    atomic_store_explicit(&pool->stop, 1, memory_order_release);
    for(int i = 0; i < pool->workers; ++i) ac_sem_post(&pool->wake);
    for(int i = 0; i < pool->workers; ++i) pthread_join(pool->threads[i], NULL);
    ac_sem_destroy(&pool->wake);
    pool->workers = 0;
#else
    (void)pool;
#endif
}
/* Threads the next block may use, render thread included. */
static int ac_pool_threads(const ac_pool *pool) {
    int limit = atomic_load_explicit(&ac_thread_limit, memory_order_relaxed);
    int available = (pool->workers > 0 ? pool->workers : 0) + 1;
    return limit > 0 && limit < available ? limit : available;
}
/* Render thread: run jobs 0..count-1 on up to `threads` threads and return
 * when every job has finished. One job, or one thread, runs inline. */
static void ac_pool_run(ac_pool *pool, int threads, int count, ac_job_fn run, void *context) {
    if(count <= 0) return;
    if(threads <= 1 || count == 1) { for(int i = 0; i < count; ++i) run(context, i); return; }
#if !defined(_WIN32)
    pool->run = run; pool->context = context;
    uint32_t generation = ++pool->generation;
    if(generation == 0) generation = ++pool->generation; /* 0 is the idle cursor */
    atomic_store_explicit(&pool->finished, 0, memory_order_relaxed);
    atomic_store_explicit(&pool->cursor, ((uint64_t)generation << 32) | ((uint64_t)count << 16), memory_order_release);
    int helpers = (threads < count ? threads : count) - 1;
    for(int i = 0; i < helpers; ++i) ac_sem_post(&pool->wake);
    ac_pool_drain(pool, generation);
    while(atomic_load_explicit(&pool->finished, memory_order_acquire) < (uint32_t)count) ac_relax();
#else
    for(int i = 0; i < count; ++i) run(context, i);
#endif
}

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
    /* 0 main (immediate), 1 MIDI worker, 2 sequencer notes stamped with the
     * frame they sound on, posted ahead in frame order. A future frame holds
     * back only its own lane. */
    ac_queue queues[AC_LANES];
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
    /* Live MIDI timing: each render publishes the clock at its first frame,
     * the wall time it started and its length (under a sequence count), so a
     * live event can be stamped with the frame it arrived on, one block
     * later (see ac_live_frame). live_timing: 0 immediate, 1 stamped while a
     * device runs (default), 2 also when the host calls process. */
    _Atomic uint64_t live_sequence, live_anchor_clock, live_anchor_ns;
    _Atomic uint32_t live_block;
    _Atomic int live_timing;
    ac_voice voices[AC_VOICES];
    ac_sample samples[AC_SAMPLES];
    /* Registration appends while the device runs: the entry is written before
     * the count publishes it, and only a stopped controller may free them. */
    _Atomic int sample_count;
    int running;
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
    /* Aux output buses 1-15 of each instrument: 0 = mixed into the
     * instrument's own output before its inserts (the default), 1 = master,
     * 2-65 = PCM track 1-64. `instrument_outputs` counts the buses, main
     * included; it changes only while the controller is stopped. */
    _Atomic uint32_t aux_route[32][AC_OUTPUTS];
    int instrument_outputs[32];
    /* Audio input of each instrument (e.g. a vocoder's modulator): 0 = none
     * (silence), 1-64 = the voices of that PCM track, taken before its
     * inserts and fader, 65 = the live audio input. */
    _Atomic uint32_t input_route[32];
    /* Live audio input. One writer (the input device callback, or
     * input_feed while no device is open) appends frames at `input_rate` and
     * publishes `input_write`; the render thread reads behind it at its own
     * position, resampling to the controller rate, and skips ahead if it
     * falls too far behind. `live_input` holds the current block. */
    float input_ring[AC_INPUT_RING * 2];
    _Atomic uint64_t input_write;
    _Atomic uint64_t input_rate_bits;
    _Atomic uint32_t input_peak[2];
    double input_position; int input_primed;
    float live_input[8192];
    float *input_capture; uint32_t input_capture_frames, input_channels;
    float track_buffer[64][8192];
    _Atomic uint32_t sends[96][8], effect_volume[8], effect_peak[8][2];
    float effect_buffer[8][8192];
    float instrument_buffer[32][8192];
    int32_t render_offset, processor_reset;
    /* Render-owned job table for the worker pool: the sources of the stage
     * being run, and what each job needs. `failed` is written by the job that
     * owns the source and read after the stage joins. */
    ac_pool pool;
    int job_slot[96];
    uint8_t failed[96], effect_failed[8];
    uint32_t job_frames;
    int job_input[32]; /* Track whose voices feed each instrument, or -1. */
    uint64_t job_clock;
    uint32_t (*job_routes)[4];
    uint32_t (*job_aux)[AC_OUTPUTS];
    /* Load meter: the longest block's render time over its real duration, in
     * permille, since the last read; threads the last block could use. */
    _Atomic uint32_t dsp_load;
    _Atomic uint32_t render_threads;
    _Atomic uint64_t processor_errors;
    double render_gains[4096];
    /* Control thread publishes targets and bumps the serial; the render
     * thread glides toward them and owns everything after that. */
    _Atomic uint32_t master_target[AC_MASTER_PARAMS];
    _Atomic uint64_t master_serial;
    uint64_t master_seen;
    int master_gliding;
    double master_value[AC_MASTER_PARAMS];
    double master_coef[AC_MASTER_FILTERS][5];
    double master_state[AC_MASTER_FILTERS][2][4];
    /* EQ wet share: crossfades toward the enabled switch over ~20 ms. */
    double master_eq_mix;
    /* Sequencer transport, seqlocked: the control thread makes the serial odd,
     * writes, then even. The render thread adopts a stable snapshot, then
     * advances the beat itself while playing. */
    _Atomic uint64_t transport_serial, transport_bits[2], transport_beat_out;
    _Atomic uint32_t transport_flags;
    uint64_t transport_seen;
    double transport_tempo, transport_beat;
    int transport_playing;
    float master_dry[8192];
    /* Audition clip (e.g. a file browser preview) mixed into the master bus,
     * outside every track. The control thread swaps the clip and bumps the
     * serial; the render thread acknowledges the serial of each block it
     * rendered, so a replaced clip is freed only once nothing reads it. */
    _Atomic(ac_sample *) preview;
    _Atomic uint64_t preview_serial, preview_ack, preview_done;
    ac_sample *preview_current; /* Render-owned: the clip `preview_phase` plays. */
    double preview_phase;
    float spectrum_ring[AC_SPECTRUM_RING];
    _Atomic uint64_t spectrum_write;
    /* Control-thread analyzer snapshot (single UI consumer). */
    double *spectrum_db;
    int spectrum_bins;
    double spectrum_bin_hz;
#if defined(__APPLE__)
    AudioComponentInstance unit;
    AudioComponentInstance input_unit;
#endif
} ac_controller;

/* Parameter namespace: master 0, instruments 1-32, aux effects 33-40. */
static mlang_audio_processor *ac_plugin(ac_controller *c, int64_t slot) {
    return slot == 0 ? &c->processor : (slot <= 32 ? &c->instruments[slot - 1] : (slot <= 40 ? &c->effects[slot - 33] : &c->inserts[slot - 41]));
}

static void ac_silence(ac_controller *c) {
    memset(c->voices, 0, sizeof(c->voices));
    memset(c->master_state, 0, sizeof(c->master_state));
}

static uint32_t ac_float_bits(float value) { uint32_t bits; memcpy(&bits, &value, sizeof(bits)); return bits; }
static float ac_bits_float(uint32_t bits) { float value; memcpy(&value, &bits, sizeof(value)); return value; }
static const float ac_master_defaults[AC_MASTER_PARAMS] = {
    0.f, 100.f, 0.f, 1.f, 500.f, 0.f, 1.f, 2500.f, 0.f, 1.f, 8000.f, 0.f, 1.f, 1.f, 1.f };

/* RBJ biquads, normalized by a0: {b0, b1, b2, a1, a2}. */
static void ac_biquad(double *k, double b0, double b1, double b2, double a0, double a1, double a2) {
    k[0] = b0 / a0; k[1] = b1 / a0; k[2] = b2 / a0; k[3] = a1 / a0; k[4] = a2 / a0;
}
static void ac_master_design(ac_controller *c) {
    const double nyquist = c->rate * 0.45;
    double hp = fmin(fmax(c->master_value[0], 5.0), nyquist);
    double w = 2.0 * M_PI * hp / c->rate, cw = cos(w), sw = sin(w);
    /* Fourth-order Butterworth as two sections. */
    const double q[2] = {0.54119610014619698, 1.30656296487637653};
    for(int s = 0; s < 2; ++s) {
        double alpha = sw / (2.0 * q[s]);
        ac_biquad(c->master_coef[s], (1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }
    for(int band = 0; band < 4; ++band) {
        double hz = fmin(fmax(c->master_value[1 + band * 3], 10.0), nyquist);
        double gain = c->master_value[2 + band * 3];
        double qv = fmin(fmax(c->master_value[3 + band * 3], 0.1), 18.0);
        double a = pow(10.0, gain / 40.0);
        double bw = 2.0 * M_PI * hz / c->rate, bc = cos(bw), alpha = sin(bw) / (2.0 * qv);
        ac_biquad(c->master_coef[2 + band], 1 + alpha * a, -2 * bc, 1 - alpha * a, 1 + alpha / a, -2 * bc, 1 - alpha / a);
    }
}
/* Once per block: follow the latest targets with a ~30 ms glide (frequencies
 * in log space), redesigning filters only while something moves. */
static void ac_master_update(ac_controller *c, uint32_t frames) {
    uint64_t serial = atomic_load_explicit(&c->master_serial, memory_order_acquire);
    if(serial != c->master_seen) { c->master_seen = serial; c->master_gliding = 1; }
    if(!c->master_gliding) return;
    double k = 1.0 - exp(-(double)frames / (c->rate * 0.03));
    int moving = 0;
    for(int i = 0; i < AC_MASTER_PARAMS; ++i) {
        double target = ac_bits_float(atomic_load_explicit(&c->master_target[i], memory_order_relaxed));
        double *value = &c->master_value[i];
        int log_scale = i == 0 || (i >= 1 && i <= 12 && (i - 1) % 3 == 0);
        if(i == 0 && (target <= 0 || *value <= 0)) {
            /* Switching the high-pass on or off is immediate, from rest. */
            if(*value != target) memset(c->master_state, 0, 2 * sizeof(c->master_state[0]));
            *value = target;
        } else if(log_scale) {
            double next = exp(log(*value) + (log(target) - log(*value)) * k);
            if(fabs(log(target) - log(next)) < 1e-4) next = target; else moving = 1;
            *value = next;
        } else if(i < 13) {
            double next = *value + (target - *value) * k;
            if(fabs(target - next) < 1e-4) next = target; else moving = 1;
            *value = next;
        }
    }
    c->master_gliding = moving;
    ac_master_design(c);
}
static void ac_master_process(ac_controller *c, float *out, uint32_t frames) {
    ac_master_update(c, frames);
    double mix_to = ac_bits_float(atomic_load_explicit(&c->master_target[14], memory_order_relaxed)) >= 0.5f ? 1.0 : 0.0;
    double mix = c->master_eq_mix;
    if(mix == 0.0 && mix_to == 0.0) {
        /* Bypassed: no filtering, and a clean start when switched back on. */
        memset(c->master_state, 0, sizeof(c->master_state));
    } else {
    int blend = mix != 1.0 || mix_to != 1.0;
    if(blend) memcpy(c->master_dry, out, frames * 2u * sizeof(float));
    for(int s = 0; s < AC_MASTER_FILTERS; ++s) {
        const double *k = c->master_coef[s];
        /* An off high-pass or a flat peaking band is exact identity; skip it. */
        if(s < 2 && c->master_value[0] <= 0) continue;
        if(s >= 2 && c->master_value[2 + (s - 2) * 3] == 0.0) {
            memset(c->master_state[s], 0, sizeof(c->master_state[s])); continue;
        }
        for(int ch = 0; ch < 2; ++ch) {
            double *z = c->master_state[s][ch];
            for(uint32_t f = 0; f < frames; ++f) {
                double x = out[f * 2 + ch];
                if(!isfinite(x)) x = 0;
                double y = k[0] * x + k[1] * z[0] + k[2] * z[1] - k[3] * z[2] - k[4] * z[3];
                z[1] = z[0]; z[0] = x; z[3] = z[2]; z[2] = y;
                out[f * 2 + ch] = (float)y;
            }
        }
    }
    if(blend) {
        double step = 1.0 / (c->rate * 0.02);
        for(uint32_t f = 0; f < frames; ++f) {
            mix = mix_to > mix ? fmin(mix_to, mix + step) : fmax(mix_to, mix - step);
            for(int ch = 0; ch < 2; ++ch) {
                float dry = c->master_dry[f * 2 + ch];
                out[f * 2 + ch] = (float)(dry + (out[f * 2 + ch] - dry) * mix);
            }
        }
        c->master_eq_mix = mix;
    }
    }
    /* Volume ramps linearly across the block. */
    double from = c->master_value[13];
    double to = ac_bits_float(atomic_load_explicit(&c->master_target[13], memory_order_relaxed));
    for(uint32_t f = 0; f < frames; ++f) {
        double g = from + (to - from) * (double)(f + 1) / (double)frames;
        out[f * 2] = (float)(out[f * 2] * g); out[f * 2 + 1] = (float)(out[f * 2 + 1] * g);
    }
    c->master_value[13] = to;
}

int64_t __mlang_std_audio_controller_new(int64_t rate, int64_t frames) {
    if(rate < 8000 || rate > 384000 || frames < 16 || frames > 4096) {
        audio_set_error("audio controller: invalid sample rate or buffer size"); return 0;
    }
    ac_controller *c = calloc(1, sizeof(*c));
    if(!c) { audio_set_error("audio controller: allocation failed"); return 0; }
    for(int i = 0; i < AC_LANES; ++i) {
        atomic_init(&c->queues[i].read, 0); atomic_init(&c->queues[i].write, 0);
    }
    atomic_init(&c->dropped, 0); atomic_init(&c->clock, 0);
    atomic_init(&c->live_sequence, 0); atomic_init(&c->live_anchor_clock, 0); atomic_init(&c->live_anchor_ns, 0);
    atomic_init(&c->live_block, 0); atomic_init(&c->live_timing, 1);
    atomic_init(&c->panic, 0); atomic_init(&c->peak, 0);
    atomic_init(&c->master_peak[0], 0); atomic_init(&c->master_peak[1], 0);
    for(int i = 0; i < 32; ++i) for(int ch = 0; ch < 2; ++ch) atomic_init(&c->instrument_peak[i][ch], 0);
    for(int i = 0; i < 64; ++i) for(int ch = 0; ch < 2; ++ch) atomic_init(&c->track_peak[i][ch], 0);
    atomic_init(&c->live_target, 0);
    for(int i = 0; i < 96; ++i) for(int fx = 0; fx < 8; ++fx) atomic_init(&c->sends[i][fx], 0);
    for(int i = 0; i < 96; ++i) for(int n = 0; n < 4; ++n) atomic_init(&c->insert_route[i][n], 0);
    for(int i = 0; i < 96; ++i) atomic_init(&c->output_route[i], 0);
    for(int i = 0; i < 32; ++i) atomic_init(&c->input_route[i], 0);
    atomic_init(&c->input_write, 0); atomic_init(&c->input_rate_bits, 0);
    atomic_init(&c->input_peak[0], 0); atomic_init(&c->input_peak[1], 0);
    for(int fx = 0; fx < 8; ++fx) {
        atomic_init(&c->effect_volume[fx], 100);
        for(int ch = 0; ch < 2; ++ch) atomic_init(&c->effect_peak[fx][ch], 0);
    }
    for(int i = 0; i < 64; ++i) atomic_init(&c->track_volume[i], 100);
    for(int i = 0; i < 32; ++i) atomic_init(&c->instrument_volume[i], 100);
    atomic_init(&c->learn_target, 0);
    for(int i = 0; i < 2048; ++i) atomic_init(&c->learn_map[i], 0);
    atomic_init(&c->processor_errors, 0);
    atomic_init(&c->dsp_load, 0); atomic_init(&c->render_threads, 1);
    atomic_init(&c->pool.cursor, 0); atomic_init(&c->pool.finished, 0);
    atomic_init(&c->pool.stop, 0); atomic_init(&c->pool.worker_busy_ns, 0);
#if defined(__APPLE__)
    atomic_init(&c->pool.period_ns, 0); atomic_init(&c->pool.workgroup, NULL);
#endif
    if(!atomic_is_lock_free(&c->queues[0].read) || !atomic_is_lock_free(&c->peak)) {
        free(c); audio_set_error("audio controller: lock-free atomics unavailable"); return 0;
    }
    c->rate = rate; c->buffer_frames = frames; c->gain = 0.25;
    for(int i = 0; i < AC_MASTER_PARAMS; ++i) {
        atomic_init(&c->master_target[i], ac_float_bits(ac_master_defaults[i]));
        c->master_value[i] = ac_master_defaults[i];
    }
    atomic_init(&c->master_serial, 0); atomic_init(&c->spectrum_write, 0);
    atomic_init(&c->transport_serial, 0); atomic_init(&c->transport_flags, 0);
    atomic_init(&c->transport_bits[0], 0); atomic_init(&c->transport_bits[1], 0);
    atomic_init(&c->transport_beat_out, 0);
    c->master_eq_mix = 1.0;
    ac_master_design(c);
    for(int i = 0; i < 128; ++i) c->frequencies[i] = 440.0 * pow(2.0, (i - 69) / 12.0);
    return (int64_t)(intptr_t)c;
}

/* Returns 0 posted, 1 full (panic requested), -1 invalid. No error-string work. */
int32_t __mlang_std_audio_controller_post(int64_t handle, int64_t lane,
    int64_t kind, int64_t channel, int64_t note, int64_t velocity,
    int64_t source, int64_t frame, int64_t sample, double gain) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || lane < 0 || lane >= AC_LANES || kind < 1 || kind > 10 || frame < -1 ||
       source < 0 || source > INT32_MAX || !isfinite(gain) || gain < 0 || gain > 1) return -1;
    if((kind == 1 || kind == 2 || kind == 6 || kind == 7) && (channel < 0 || channel > 15 ||
        note < 0 || note > 127 || velocity < 0 || velocity > 127)) return -1;
    if(kind == 4 && (sample < 0 || sample >= AC_SAMPLES || note < 0 || velocity < 0)) return -1;
    if(kind == 10 && (sample < 1 || sample > 296 || note < 0 || note > 16383)) return -1;
    if((kind == 6 || kind == 7) && (sample < 0 || sample > 32)) return -1;
    if((kind == 8 || kind == 9) && (channel < 0 || channel > 15 ||
        note < 0 || note > 129 || velocity < 0 ||
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
/* The frame a live event arriving now plays on: where the audio clock is
 * now (the last render's first frame plus the time since it started), one
 * block later, so every event keeps its place within the block instead of
 * all landing on the next block's first frame. -1 (now) when timing is off,
 * before the first stamped render, or when the audio has stalled. */
static int64_t ac_live_frame(ac_controller *c) {
    if(atomic_load_explicit(&c->live_timing, memory_order_relaxed) == 0) return -1;
    uint64_t sequence, anchor_clock, anchor_ns; uint32_t block;
    do {
        sequence = atomic_load_explicit(&c->live_sequence, memory_order_acquire);
        anchor_clock = atomic_load_explicit(&c->live_anchor_clock, memory_order_relaxed);
        anchor_ns = atomic_load_explicit(&c->live_anchor_ns, memory_order_relaxed);
        block = atomic_load_explicit(&c->live_block, memory_order_relaxed);
    } while((sequence & 1u) || sequence != atomic_load_explicit(&c->live_sequence, memory_order_acquire));
    if(!anchor_ns || !block) return -1;
    uint64_t now = ac_now_ns();
    double elapsed = now > anchor_ns ? (double)(now - anchor_ns) * c->rate / 1e9 : 0.0;
    if(elapsed > 4.0 * block) return -1;
    return (int64_t)(anchor_clock + (uint64_t)elapsed + block);
}
static int32_t ac_post_live(ac_controller *c, uint64_t target, int on, int64_t channel, int64_t pitch, int64_t velocity) {
    if(!target) return 0;
    int64_t source = (int64_t)(target & 127u) - 1;
    int64_t instrument = (int64_t)(target >> 7) - 1;
    return __mlang_std_audio_controller_post((int64_t)(intptr_t)c, 1,
        instrument ? (on ? 6 : 7) : (on ? 1 : 2), channel, pitch, velocity,
        source + 65536, ac_live_frame(c), instrument, 1.0);
}
/* 0 plays live MIDI at once, 1 stamps it while a device runs (the default),
 * 2 stamps it when the host renders with process too. */
int32_t __mlang_std_audio_controller_live_timing(int64_t handle, int64_t mode) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || mode < 0 || mode > 2) return -1;
    atomic_store_explicit(&c->live_timing, (int)mode, memory_order_relaxed);
    return 0;
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
    if(!c || channel < 0 || channel > 15 || controller < 0 || controller > 129 ||
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
                (mapping >> 6) & 16383, 0, 0, ac_live_frame(c), mapping & 63, normalized);
        }
    }
    uint64_t target = atomic_load_explicit(&c->live_target, memory_order_acquire);
    if(!target) return 0;
    int64_t source = (int64_t)(target & 127u) - 1;
    int64_t instrument = (int64_t)(target >> 7) - 1;
    return __mlang_std_audio_controller_post(handle, 1, instrument ? 9 : 8,
        channel, controller, value, source + 65536, ac_live_frame(c), instrument, 1.0);
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

/* A pattern CC for instrument `slot` drives the parameter that CC is MIDI
 * learned to on that instrument, as the knob did when it was recorded. The
 * event's channel is the track's, not the keyboard's, so it is tried first
 * and then every other channel. Returns 1 when a learned parameter took it. */
static int ac_learned_control(ac_controller *c, int64_t slot, int64_t channel, int64_t controller, int64_t value) {
    if(slot < 1 || slot > 32 || controller < 0 || controller > 127 || channel < 0 || channel > 15) return 0;
    for(int n = -1; n < 16; ++n) {
        if(n == channel) continue;
        int64_t ch = n < 0 ? channel : n;
        uint64_t mapping = atomic_load_explicit(&c->learn_map[ch * 128 + controller], memory_order_acquire);
        if(!mapping || (int64_t)(mapping & 63) != slot) continue;
        mlang_audio_processor *p = ac_plugin(c, slot);
        if(!p->context || !p->parameter) return 0;
        double steps = (double)(mapping >> 20);
        double normalized = (double)value / 127.0;
        if(steps > 0) normalized = floor(normalized * steps + 0.5) / steps;
        p->parameter(p->context, (int32_t)((mapping >> 6) & 16383), normalized, c->render_offset);
        return 1;
    }
    return 0;
}

static void ac_dispatch(ac_controller *c, const ac_event *e) {
    if(e->kind == 10) {
        mlang_audio_processor *p = ac_plugin(c, e->sample);
        if(p->context && p->parameter) p->parameter(p->context, (int32_t)e->note, e->gain, c->render_offset);
        return;
    }
    /* Live knobs (source >= 65536) were resolved against MIDI learn already. */
    if(e->kind == 9 && e->source < 65536 && ac_learned_control(c, e->sample, e->channel, e->note, e->velocity)) return;
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
    if(e->kind == 4 && e->sample >= atomic_load_explicit(&c->sample_count, memory_order_acquire)) return;
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
/* Pool jobs. Each touches only its own source's buffer, plugin and inserts. */
static void ac_instrument_job(void *context, int job) {
    ac_controller *c = context; int i = c->job_slot[job];
    mlang_audio_processor *p = &c->instruments[i];
    float *buffer = c->instrument_buffer[i];
    /* The input track's buffer holds only its own voices at this stage. */
    if(c->job_input[i] >= 0) memcpy(buffer, c->track_buffer[c->job_input[i]], c->job_frames * 2u * sizeof(float));
    else if(c->job_input[i] == -2) memcpy(buffer, c->live_input, c->job_frames * 2u * sizeof(float));
    else memset(buffer, 0, c->job_frames * 2u * sizeof(float));
    c->failed[64 + i] = p->process(p->context, buffer, (int32_t)c->job_frames, c->job_clock) != 0;
    if(c->failed[64 + i]) { atomic_fetch_add_explicit(&c->processor_errors, 1, memory_order_relaxed); return; }
    /* Aux buses that follow the main output join it before the inserts. */
    for(int bus = 1; p->aux_output && bus < c->instrument_outputs[i]; ++bus) {
        if(c->job_aux[i][bus] != 0) continue;
        const float *aux = p->aux_output(p->context, bus); if(!aux) continue;
        for(uint32_t f = 0; f < c->job_frames * 2u; ++f) if(isfinite(aux[f])) buffer[f] += aux[f];
    }
    ac_insert_process(c, c->job_routes[64 + i], buffer, c->job_frames, c->job_clock);
}
static void ac_track_job(void *context, int job) {
    ac_controller *c = context; int t = c->job_slot[job];
    ac_insert_process(c, c->job_routes[t], c->track_buffer[t], c->job_frames, c->job_clock);
}
static void ac_effect_job(void *context, int job) {
    ac_controller *c = context; int fx = c->job_slot[job];
    mlang_audio_processor *p = &c->effects[fx];
    c->effect_failed[fx] = p->process(p->context, c->effect_buffer[fx], (int32_t)c->job_frames, c->job_clock) != 0;
    if(c->effect_failed[fx]) atomic_fetch_add_explicit(&c->processor_errors, 1, memory_order_relaxed);
}
/* Run the jobs for `count` sources (ids as job_routes indexes them, in slot
 * order). Sources that share an insert instance with an earlier source run
 * after the parallel batch, one by one in slot order, so no plugin instance is
 * ever processed on two threads at once and each still sees the serial order. */
static void ac_run_stage(ac_controller *c, int threads, const int *sources, const int *ids, int count, ac_job_fn run) {
    uint8_t claimed[257]; memset(claimed, 0, sizeof(claimed));
    int serial[96], parallel = 0, deferred = 0;
    for(int k = 0; k < count; ++k) {
        int shared = 0;
        if(ids) for(int n = 0; n < 4; ++n) {
            uint32_t id = c->job_routes[ids[k]][n];
            if(id && id <= 256 && claimed[id]) shared = 1;
        }
        if(ids) for(int n = 0; n < 4; ++n) {
            uint32_t id = c->job_routes[ids[k]][n];
            if(id && id <= 256) claimed[id] = 1;
        }
        if(shared) serial[deferred++] = sources[k]; else c->job_slot[parallel++] = sources[k];
    }
    ac_pool_run(&c->pool, threads, parallel, run, c);
    for(int k = 0; k < deferred; ++k) { c->job_slot[parallel + k] = serial[k]; run(c, parallel + k); }
}

static uint64_t ac_double_bits(double value) { uint64_t bits; memcpy(&bits, &value, sizeof(bits)); return bits; }
static double ac_bits_double(uint64_t bits) { double value; memcpy(&value, &bits, sizeof(value)); return value; }
/* Once per block: adopt a published transport, then hand it to every loaded
 * processor. flags bit 0 = playing, bit 1 = the beat is new (else keep ours). */
static void ac_transport_begin(ac_controller *c) {
    uint64_t serial = atomic_load_explicit(&c->transport_serial, memory_order_acquire);
    if(serial != c->transport_seen && !(serial & 1)) {
        double tempo = ac_bits_double(atomic_load_explicit(&c->transport_bits[0], memory_order_relaxed));
        double beat = ac_bits_double(atomic_load_explicit(&c->transport_bits[1], memory_order_relaxed));
        uint32_t flags = atomic_load_explicit(&c->transport_flags, memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        if(atomic_load_explicit(&c->transport_serial, memory_order_relaxed) == serial) {
            c->transport_seen = serial; c->transport_tempo = tempo; c->transport_playing = flags & 1;
            if(flags & 2) c->transport_beat = beat;
        }
    }
    mlang_audio_processor *groups[4] = {&c->processor, c->instruments, c->effects, c->inserts};
    const int sizes[4] = {1, 32, 8, 256};
    for(int g = 0; g < 4; ++g)
        for(int i = 0; i < sizes[g]; ++i) {
            mlang_audio_processor *p = &groups[g][i];
            if(p->context && p->transport) p->transport(p->context, c->transport_tempo, c->transport_beat, c->transport_playing);
        }
}
static void ac_transport_end(ac_controller *c, uint32_t frames) {
    if(c->transport_playing && c->transport_tempo > 0)
        c->transport_beat += frames * c->transport_tempo / (60.0 * c->rate);
    atomic_store_explicit(&c->transport_beat_out, ac_double_bits(c->transport_beat), memory_order_relaxed);
}
/* Writer side of the live input: append interleaved frames (1 or 2
 * channels) and track their peak. */
static void ac_input_write(ac_controller *c, const float *data, uint64_t frames, uint32_t channels) {
    uint64_t write = atomic_load_explicit(&c->input_write, memory_order_relaxed);
    float peaks[2] = {0, 0};
    for(uint64_t f = 0; f < frames; ++f) {
        float l = data[f * channels], r = channels > 1 ? data[f * channels + 1] : l;
        if(!isfinite(l)) l = 0; if(!isfinite(r)) r = 0;
        uint64_t slot = (write + f) % AC_INPUT_RING;
        c->input_ring[slot * 2] = l; c->input_ring[slot * 2 + 1] = r;
        peaks[0] = fmaxf(peaks[0], fabsf(l)); peaks[1] = fmaxf(peaks[1], fabsf(r));
    }
    atomic_store_explicit(&c->input_write, write + frames, memory_order_release);
    for(int ch = 0; ch < 2; ++ch) {
        uint32_t value = (uint32_t)(fminf(peaks[ch], 1.f) * 1000.f);
        uint32_t old = atomic_load_explicit(&c->input_peak[ch], memory_order_relaxed);
        while(old < value && !atomic_compare_exchange_weak_explicit(&c->input_peak[ch], &old, value,
            memory_order_relaxed, memory_order_relaxed)) {}
    }
}
/* Render thread: the next `frames` of live input into `live_input`, linearly
 * resampled from the input rate. It reads a little behind the writer (one
 * block plus a margin) and re-anchors there after an underrun or when it
 * has fallen far behind (clock drift between two devices). */
static void ac_input_block(ac_controller *c, uint32_t frames) {
    double rate = 0; uint64_t bits = atomic_load_explicit(&c->input_rate_bits, memory_order_acquire);
    memcpy(&rate, &bits, sizeof(rate));
    uint64_t write = atomic_load_explicit(&c->input_write, memory_order_acquire);
    if(rate <= 0 || write == 0) { memset(c->live_input, 0, frames * 2u * sizeof(float)); c->input_primed = 0; return; }
    double step = rate / c->rate, need = frames * step + 2.0;
    double margin = (double)(c->buffer_frames > 0 ? c->buffer_frames : 256) * step + 64.0;
    double behind = (double)write - c->input_position;
    if(!c->input_primed || behind > need + margin * 4.0 + 4096.0 || behind > (double)AC_INPUT_RING / 2) {
        if((double)write < need + margin) { memset(c->live_input, 0, frames * 2u * sizeof(float)); return; }
        c->input_position = (double)write - need - margin; c->input_primed = 1;
    }
    if(c->input_position + need > (double)write) {
        /* Underrun: silence this block and re-anchor on the next one. */
        memset(c->live_input, 0, frames * 2u * sizeof(float)); c->input_primed = 0; return;
    }
    for(uint32_t f = 0; f < frames; ++f) {
        uint64_t whole = (uint64_t)c->input_position; double mix = c->input_position - (double)whole;
        uint64_t a = whole % AC_INPUT_RING, b = (whole + 1) % AC_INPUT_RING;
        for(int ch = 0; ch < 2; ++ch)
            c->live_input[f * 2 + ch] = (float)(c->input_ring[a * 2 + ch] + (c->input_ring[b * 2 + ch] - c->input_ring[a * 2 + ch]) * mix);
        c->input_position += step;
    }
}
static void ac_render(ac_controller *c, float *out, uint32_t frames) {
    if(frames == 0) return; /* Do not consume events/reset on an empty block. */
    uint64_t render_start = ac_now_ns();
    uint64_t panic = atomic_load_explicit(&c->panic, memory_order_acquire);
    int reset = c->processor_reset || panic != c->seen_panic;
    c->processor_reset = 0;
    if(c->processor.context) c->processor.begin(c->processor.context, reset);
    for(int i = 0; i < 32; ++i)
        if(c->instruments[i].context) c->instruments[i].begin(c->instruments[i].context, reset);
    if(panic != c->seen_panic) {
        ac_silence(c); c->seen_panic = panic;
        for(int lane = 0; lane < AC_LANES; ++lane) {
            ac_queue *q = &c->queues[lane];
            atomic_store_explicit(&q->read, atomic_load_explicit(&q->write, memory_order_acquire), memory_order_release);
        }
    }
    uint64_t clock = atomic_load_explicit(&c->clock, memory_order_relaxed);
    /* Anchor live MIDI timing to this block (see ac_live_frame). */
    int timing = atomic_load_explicit(&c->live_timing, memory_order_relaxed);
    if(timing == 2 || (timing == 1 && c->running)) {
        uint64_t sequence = atomic_load_explicit(&c->live_sequence, memory_order_relaxed);
        atomic_store_explicit(&c->live_sequence, sequence + 1, memory_order_release);
        atomic_store_explicit(&c->live_anchor_clock, clock, memory_order_relaxed);
        atomic_store_explicit(&c->live_anchor_ns, render_start, memory_order_relaxed);
        atomic_store_explicit(&c->live_block, frames, memory_order_relaxed);
        atomic_store_explicit(&c->live_sequence, sequence + 2, memory_order_release);
    }
    ac_transport_begin(c);
    ac_input_block(c, frames);
    /* Serial before clip: an acknowledged serial then covers this clip. */
    uint64_t preview_serial = atomic_load_explicit(&c->preview_serial, memory_order_acquire);
    ac_sample *preview = atomic_load_explicit(&c->preview, memory_order_acquire);
    if(preview != c->preview_current) { c->preview_current = preview; c->preview_phase = 0; }
    double preview_step = preview ? (double)preview->rate / c->rate : 0;
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
    /* Aux buses routed to a track give that track a buffer too. */
    uint32_t aux_routes[32][AC_OUTPUTS];
    for(int i = 0; i < 32; ++i)
        for(int bus = 0; bus < AC_OUTPUTS; ++bus) {
            uint32_t route = bus > 0 && bus < c->instrument_outputs[i] && c->instruments[i].context
                ? atomic_load_explicit(&c->aux_route[i][bus], memory_order_relaxed) : 0;
            aux_routes[i][bus] = route <= 74 ? route : 0;
            if(aux_routes[i][bus] >= 2 && aux_routes[i][bus] <= 65) buffered[aux_routes[i][bus] - 2] = 1;
        }
    /* A track feeding an instrument's input keeps its voices in its buffer. */
    for(int i = 0; i < 32; ++i) {
        uint32_t input = c->instruments[i].context ? atomic_load_explicit(&c->input_route[i], memory_order_relaxed) : 0;
        c->job_input[i] = input >= 1 && input <= 64 ? (int)input - 1 : (input == 65 ? -2 : -1);
        if(c->job_input[i] >= 0) buffered[c->job_input[i]] = 1;
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
    unsigned budget[AC_LANES] = {256, 256, 256};
    float peak = 0;
    for(uint32_t f = 0; f < frames; ++f) {
        c->render_offset = (int32_t)f;
        for(int lane = 0; lane < AC_LANES; ++lane) {
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
        if(preview && c->preview_phase < (double)preview->frames) {
            int64_t pos = (int64_t)c->preview_phase;
            int64_t next = pos + 1 < preview->frames ? pos + 1 : pos;
            double mix = c->preview_phase - (double)pos;
            for(int ch = 0; ch < 2; ++ch) {
                int channel = preview->channels == 1 ? 0 : ch;
                float a = preview->data[pos * preview->channels + channel], b = preview->data[next * preview->channels + channel];
                double value = a + (b - a) * mix;
                if(ch == 0) l += value; else r += value;
            }
            c->preview_phase += preview_step;
        }
        c->render_gains[f] = c->gain;
        out[f * 2] = (float)l;
        out[f * 2 + 1] = (float)r;
    }
    /* Plugins run on the worker pool, one stage at a time; each stage's
     * results are summed here in slot order, as a single thread would. */
    int threads = ac_pool_threads(&c->pool);
    atomic_store_explicit(&c->render_threads, (uint32_t)threads, memory_order_relaxed);
    c->job_frames = frames; c->job_clock = clock; c->job_routes = routes; c->job_aux = aux_routes;
    // Instruments first: one routed into a track must reach its buffer before
    // that track runs its own inserts and fader.
    int sources[96], ids[96], jobs = 0;
    for(int i = 0; i < 32; ++i) if(c->instruments[i].context) { sources[jobs] = i; ids[jobs++] = 64 + i; }
    ac_run_stage(c, threads, sources, ids, jobs, ac_instrument_job);
    for(int i = 0; i < 32; ++i) {
        if(!c->instruments[i].context || c->failed[64 + i]) continue;
        const float *buffer = c->instrument_buffer[i];
        float volume = atomic_load_explicit(&c->instrument_volume[i], memory_order_relaxed) / 100.f;
        float *destination = target[64 + i] >= 0 ? c->track_buffer[target[64 + i]] : out;
        float peaks[2] = {0, 0};
        for(uint32_t f = 0; f < frames * 2u; ++f) {
            if(isfinite(buffer[f])) {
                float value = buffer[f] * volume;
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
    /* Aux buses routed away from their instrument are separate channels: they
     * skip its fader, sends and inserts, and take the destination's own. */
    for(int i = 0; i < 32; ++i) {
        mlang_audio_processor *p = &c->instruments[i];
        if(!p->context || !p->aux_output || c->failed[64 + i]) continue;
        for(int bus = 1; bus < c->instrument_outputs[i]; ++bus) {
            uint32_t route = aux_routes[i][bus]; if(route == 0 || route == 74) continue;
            // An aux effect channel takes the bus as a send: post its own
            // level, into the effect's input only.
            if(route >= 66 && !c->effects[route - 66].context) continue;
            const float *aux = p->aux_output(p->context, bus); if(!aux) continue;
            float *destination = route == 1 ? out : (route >= 66 ? c->effect_buffer[route - 66] : c->track_buffer[route - 2]);
            for(uint32_t f = 0; f < frames * 2u; ++f) if(isfinite(aux[f])) destination[f] += aux[f];
        }
    }
    /* Tracks by depth: a track runs once everything feeding it has been
     * summed into its buffer. The feeder order is sorted by depth already;
     * tracks left in a routing cycle go last, straight to master. */
    int depth[64] = {0}, deepest = 0;
    for(int i = 0; i < count; ++i) {
        int t = order[i];
        if(cycled[t]) continue;
        if(depth[t] > deepest) deepest = depth[t];
        if(target[t] >= 0 && !cycled[target[t]] && depth[target[t]] < depth[t] + 1) depth[target[t]] = depth[t] + 1;
    }
    for(int t = 0; t < 64; ++t) if(cycled[t]) depth[t] = deepest + 1;
    for(int begin = 0; begin < count;) {
        int end = begin, level = depth[order[begin]];
        while(end < count && depth[order[end]] == level) ++end;
        ac_run_stage(c, threads, order + begin, order + begin, end - begin, ac_track_job);
        for(int i = begin; i < end; ++i) {
            int t = order[i];
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
        begin = end;
    }
    // Parallel post-fader aux returns. No return feeds another effect or itself.
    jobs = 0;
    for(int fx = 0; fx < 8; ++fx) if(c->effects[fx].context) sources[jobs++] = fx;
    ac_run_stage(c, threads, sources, NULL, jobs, ac_effect_job);
    for(int fx = 0; fx < 8; ++fx) {
        if(!c->effects[fx].context || c->effect_failed[fx]) continue;
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
    /* The master bus is the last stage before the device. */
    ac_master_process(c, out, frames);
    float master[2] = {0, 0};
    uint64_t tap = atomic_load_explicit(&c->spectrum_write, memory_order_relaxed);
    for(uint32_t f = 0; f < frames; ++f) {
        double l = isfinite(out[f * 2]) ? out[f * 2] * c->render_gains[f] : 0;
        double r = isfinite(out[f * 2 + 1]) ? out[f * 2 + 1] * c->render_gains[f] : 0;
        out[f * 2] = (float)fmax(-1, fmin(1, l));
        out[f * 2 + 1] = (float)fmax(-1, fmin(1, r));
        master[0] = fmaxf(master[0], fabsf(out[f * 2]));
        master[1] = fmaxf(master[1], fabsf(out[f * 2 + 1]));
        c->spectrum_ring[(tap + f) & (AC_SPECTRUM_RING - 1u)] = 0.5f * (out[f * 2] + out[f * 2 + 1]);
        peak = fmaxf(peak, fmaxf(fabsf(out[f * 2]), fabsf(out[f * 2 + 1])));
    }
    atomic_store_explicit(&c->spectrum_write, tap + frames, memory_order_release);
    uint32_t bits; memcpy(&bits, &peak, sizeof(bits));
    atomic_store_explicit(&c->peak, bits, memory_order_relaxed);
    for(int ch = 0; ch < 2; ++ch) {
        uint32_t value = (uint32_t)lroundf(master[ch] * 1000.f);
        uint32_t old = atomic_load_explicit(&c->master_peak[ch], memory_order_relaxed);
        while(old < value && !atomic_compare_exchange_weak_explicit(&c->master_peak[ch], &old, value,
            memory_order_relaxed, memory_order_relaxed)) {}
    }
    ac_transport_end(c, frames);
    if(preview && c->preview_phase >= (double)preview->frames)
        atomic_store_explicit(&c->preview_done, preview_serial, memory_order_release);
    atomic_store_explicit(&c->preview_ack, preview_serial, memory_order_release);
    atomic_store_explicit(&c->clock, clock + frames, memory_order_release);
    /* Wall time spent against the block's real duration; keep the worst. */
    double block_ns = (double)frames / c->rate * 1e9;
    uint32_t load = (uint32_t)fmin(9999.0, (double)(ac_now_ns() - render_start) / block_ns * 1000.0);
    uint32_t seen = atomic_load_explicit(&c->dsp_load, memory_order_relaxed);
    while(seen < load && !atomic_compare_exchange_weak_explicit(&c->dsp_load, &seen, load,
        memory_order_relaxed, memory_order_relaxed)) {}
}
/* Control thread: audition PCM on the master bus from its start, replacing any
 * clip playing; no samples stops. The copy is owned by the controller. A
 * replaced clip is freed once the render thread acknowledges the swap (at
 * most ~250 ms); a stalled device leaks it rather than racing the render. */
int32_t __mlang_std_audio_controller_preview(int64_t handle, mlang_list_t samples, int64_t channels, int64_t rate) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || channels < 1 || channels > 2 || rate <= 0 || samples.size < 0 || samples.size % channels ||
       (samples.size > 0 && !samples.data) || (uint64_t)samples.size > SIZE_MAX / sizeof(float)) return -1;
    ac_sample *clip = NULL;
    if(samples.size > 0) {
        clip = malloc(sizeof(*clip));
        float *data = clip ? malloc((size_t)samples.size * sizeof(float)) : NULL;
        if(!data) { free(clip); return -1; }
        memcpy(data, samples.data, (size_t)samples.size * sizeof(float));
        *clip = (ac_sample){data, samples.size / channels, channels, rate};
    }
    ac_sample *old = atomic_exchange_explicit(&c->preview, clip, memory_order_acq_rel);
    uint64_t serial = atomic_fetch_add_explicit(&c->preview_serial, 1, memory_order_acq_rel) + 1;
    if(old) {
        for(int i = 0; c->running && i < 250 && atomic_load_explicit(&c->preview_ack, memory_order_acquire) < serial; ++i) ac_sleep_ms(1);
        if(!c->running || atomic_load_explicit(&c->preview_ack, memory_order_acquire) >= serial) { free(old->data); free(old); }
    }
    return 0;
}
/* 1 while a preview clip is loaded and has not played to its end. */
int32_t __mlang_std_audio_controller_preview_playing(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || !atomic_load_explicit(&c->preview, memory_order_acquire)) return 0;
    return atomic_load_explicit(&c->preview_done, memory_order_acquire) != atomic_load_explicit(&c->preview_serial, memory_order_acquire);
}

/* Copy a sample on the control thread, only while stopped. Controller owns it. */
int32_t __mlang_std_audio_controller_clear_samples(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || c->running) return -1;
    ac_silence(c);
    int count = atomic_load_explicit(&c->sample_count, memory_order_relaxed);
    for(int i = 0; i < count; ++i) free(c->samples[i].data);
    atomic_store_explicit(&c->sample_count, 0, memory_order_release);
    for(int lane = 0; lane < AC_LANES; ++lane)
        atomic_store_explicit(&c->queues[lane].read, atomic_load_explicit(&c->queues[lane].write, memory_order_acquire), memory_order_release);
    return 0;
}
/* Control thread, safe while rendering: a new entry is published only after it
 * is complete, and no voice can name an id before registration returns. */
int64_t __mlang_std_audio_controller_sample_data(int64_t handle, mlang_list_t samples, int64_t channels, int64_t rate) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    int count = c ? atomic_load_explicit(&c->sample_count, memory_order_relaxed) : 0;
    if(!c || channels < 1 || channels > 2 || rate <= 0 ||
       samples.size <= 0 || samples.size % channels || !samples.data || count == AC_SAMPLES ||
       (uint64_t)samples.size > SIZE_MAX / sizeof(float)) return -1;
    size_t bytes = (size_t)samples.size * sizeof(float);
    float *data = malloc(bytes); if(!data) return -1;
    memcpy(data, samples.data, bytes);
    c->samples[count] = (ac_sample){data, samples.size / channels, (int)channels, (double)rate};
    atomic_store_explicit(&c->sample_count, count + 1, memory_order_release);
    return count;
}
int64_t __mlang_std_audio_controller_sample(int64_t handle, int64_t pcm) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    mlang_pcm_audio_t *p = (mlang_pcm_audio_t*)(intptr_t)pcm;
    int count = c ? atomic_load_explicit(&c->sample_count, memory_order_relaxed) : 0;
    if(!c || !p || p->frame_count <= 0 || count == AC_SAMPLES) return -1;
    size_t bytes = (size_t)p->frame_count * (size_t)p->channels * sizeof(float);
    float *data = malloc(bytes); if(!data) return -1;
    memcpy(data, p->samples, bytes);
    c->samples[count] = (ac_sample){data, p->frame_count, p->channels, p->sample_rate};
    atomic_store_explicit(&c->sample_count, count + 1, memory_order_release);
    return count;
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
    if(data->mNumberBuffers == 1 && data->mBuffers[0].mData &&
       data->mBuffers[0].mDataByteSize >= (uint64_t)frames * 2u * sizeof(float)) {
        /* Rate conversion can request more client frames than hardware frames.
           Keep every DSP/plugin call within the fixed 4096-frame capacity. */
        float *samples = data->mBuffers[0].mData;
        for(UInt32 offset = 0; offset < frames;) {
            UInt32 count = frames - offset;
            if(count > 4096) count = 4096;
            ac_render(context, samples + (size_t)offset * 2u, count);
            offset += count;
        }
    }
    return noErr;
}
#endif

#if defined(__APPLE__)
static void ac_input_close(ac_controller *c);
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
    atomic_store_explicit(&c->live_anchor_ns, 0, memory_order_relaxed); /* live MIDI plays at once until the next render */
    atomic_store_explicit(&c->master_peak[0], 0, memory_order_relaxed);
    atomic_store_explicit(&c->master_peak[1], 0, memory_order_relaxed);
    for(int i = 0; i < 32; ++i) for(int ch = 0; ch < 2; ++ch) atomic_store_explicit(&c->instrument_peak[i][ch], 0, memory_order_relaxed);
    for(int i = 0; i < 8; ++i) for(int ch = 0; ch < 2; ++ch) atomic_store_explicit(&c->effect_peak[i][ch], 0, memory_order_relaxed);
    for(int lane = 0; lane < AC_LANES; ++lane) {
        ac_queue *q = &c->queues[lane];
        atomic_store_explicit(&q->read, atomic_load_explicit(&q->write, memory_order_acquire), memory_order_release);
    }
    return 0;
}
int32_t __mlang_std_audio_controller_close(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return 0;
    if(__mlang_std_audio_controller_stop(handle) != 0) return -1;
    /* Workers leave the device workgroup before the unit that owns it goes. */
    ac_pool_shutdown(&c->pool);
#if defined(__APPLE__)
    void *workgroup = atomic_exchange_explicit(&c->pool.workgroup, NULL, memory_order_acq_rel);
    if(workgroup) { if(__builtin_available(macOS 11.0, *)) os_release((os_workgroup_t)workgroup); }
    ac_input_close(c);
    if(c->unit) { AudioUnitUninitialize(c->unit); AudioComponentInstanceDispose(c->unit); }
#endif
    int registered = atomic_load_explicit(&c->sample_count, memory_order_relaxed);
    for(int i = 0; i < registered; ++i) free(c->samples[i].data);
    ac_sample *preview = atomic_load_explicit(&c->preview, memory_order_relaxed);
    if(preview) { free(preview->data); free(preview); }
    if(c->processor.context) c->processor.destroy(c->processor.context);
    for(int i = 0; i < 32; ++i)
        if(c->instruments[i].context) c->instruments[i].destroy(c->instruments[i].context);
    for(int i = 0; i < 8; ++i) if(c->effects[i].context) c->effects[i].destroy(c->effects[i].context);
    for(int i = 0; i < 256; ++i) if(c->inserts[i].context) c->inserts[i].destroy(c->inserts[i].context);
    free(c->spectrum_db);
    free(c); return 0;
}
#if defined(__APPLE__)
/* Control thread, before start: publish the block period and the AUHAL IO
 * workgroup, which the workers apply on their next wake. */
static void ac_audio_workgroup(ac_controller *c) {
    atomic_store_explicit(&c->pool.period_ns, (uint64_t)((double)c->buffer_frames / c->rate * 1e9), memory_order_release);
    if(atomic_load_explicit(&c->pool.workgroup, memory_order_acquire)) return;
    if(__builtin_available(macOS 11.0, *)) {
        os_workgroup_t workgroup = NULL; UInt32 size = sizeof(workgroup);
        if(AudioUnitGetProperty(c->unit, kAudioOutputUnitProperty_OSWorkgroup, kAudioUnitScope_Global, 0, &workgroup, &size) == noErr && workgroup)
            atomic_store_explicit(&c->pool.workgroup, (void *)workgroup, memory_order_release);
    }
}
#endif
int32_t __mlang_std_audio_controller_start(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return -1;
#if defined(__APPLE__)
    if(c->unit) {
        if(c->running) return 0;
        ac_audio_workgroup(c);
        c->running = 1;
        if(AudioOutputUnitStart(c->unit) == noErr) return 0;
        c->running = 0; audio_set_error("AUHAL start failed"); return -1;
    }
#endif
    audio_set_error("audio controller has no hardware output"); return -1;
}
int64_t __mlang_std_audio_controller_open_rate(int64_t device, int64_t frames, int64_t requested_rate) {
    if(requested_rate != 0 && (requested_rate < 8000 || requested_rate > 384000)) {
        audio_set_error("Invalid audio sample rate"); return 0;
    }
#if defined(__APPLE__)
    AudioDeviceID id = coreaudio_device_for_index(device < 0 ? coreaudio_default_output_index() : device);
    if(id == kAudioObjectUnknown) { audio_set_error("AUHAL output device unavailable"); return 0; }
    /* Set the AUHAL client stream rate. CoreAudio converts to the device rate
       when necessary, without changing the format of other running clients. */
    double rate = requested_rate ? (double)requested_rate : coreaudio_nominal_sample_rate(id);
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
    (void)device; (void)frames; (void)requested_rate; audio_set_error("AUHAL output requires macOS"); return 0;
#endif
}
#if defined(__APPLE__)
static OSStatus ac_input_callback(void *context, AudioUnitRenderActionFlags *flags,
    const AudioTimeStamp *time, UInt32 bus, UInt32 frames, AudioBufferList *data) {
    (void)data;
    ac_controller *c = context;
    if(!c->input_unit || !c->input_capture || frames > c->input_capture_frames) return noErr;
    AudioBufferList list = {1, {{c->input_channels, frames * c->input_channels * (UInt32)sizeof(float), c->input_capture}}};
    OSStatus rc = AudioUnitRender(c->input_unit, flags, time, bus, frames, &list);
    if(rc == noErr) ac_input_write(c, c->input_capture, frames, c->input_channels);
    return rc;
}
static void ac_input_close(ac_controller *c) {
    if(c->input_unit) {
        AudioOutputUnitStop(c->input_unit); AudioUnitUninitialize(c->input_unit);
        AudioComponentInstanceDispose(c->input_unit); c->input_unit = NULL;
    }
    free(c->input_capture); c->input_capture = NULL; c->input_capture_frames = 0;
}
#endif
/* Open the live audio input on input device `device` (an input device index
 * as std::audio counts them, -1 = the system default), replacing any open
 * one; -2 closes it. The input runs on its own device clock: blocks read it
 * a little behind its writer, resampled to the controller rate. Control
 * thread; audio may run. Returns -1 (see last_error) on failure. */
int32_t __mlang_std_audio_controller_input_open(int64_t handle, int64_t device) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || device < -2) { audio_set_error("Invalid audio input selection"); return -1; }
#if defined(__APPLE__)
    ac_input_close(c);
    double none = 0; uint64_t bits; memcpy(&bits, &none, sizeof(bits));
    atomic_store_explicit(&c->input_rate_bits, bits, memory_order_release);
    if(device == -2) return 0;
    AudioDeviceID id = coreaudio_input_device_for_index(device == -1 ? coreaudio_default_input_index() : device);
    if(id == kAudioObjectUnknown) { audio_set_error("Audio input device unavailable"); return -1; }
    UInt32 channels = coreaudio_input_channel_count(id);
    c->input_channels = channels >= 2 ? 2 : 1;
    double rate = coreaudio_nominal_sample_rate(id);
    if(!(rate >= 8000 && rate <= 384000)) rate = c->rate;
    c->input_capture_frames = 8192;
    c->input_capture = calloc((size_t)c->input_capture_frames * c->input_channels, sizeof(float));
    AudioComponentDescription desc = {kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    OSStatus rc = component && c->input_capture ? noErr : kAudio_ParamError;
    if(rc == noErr) rc = AudioComponentInstanceNew(component, &c->input_unit);
    UInt32 enabled = 1, disabled = 0;
    if(rc == noErr) rc = AudioUnitSetProperty(c->input_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enabled, sizeof(enabled));
    if(rc == noErr) rc = AudioUnitSetProperty(c->input_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &disabled, sizeof(disabled));
    if(rc == noErr) rc = AudioUnitSetProperty(c->input_unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &id, sizeof(id));
    /* AUHAL does not convert the input's rate: ask for the device rate,
     * interleaved float, and resample in ac_input_block. */
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = rate; fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = c->input_channels * (UInt32)sizeof(float);
    fmt.mFramesPerPacket = 1; fmt.mChannelsPerFrame = c->input_channels; fmt.mBitsPerChannel = 32;
    if(rc == noErr) rc = AudioUnitSetProperty(c->input_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &fmt, sizeof(fmt));
    if(rc == noErr) rc = AudioUnitSetProperty(c->input_unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &c->input_capture_frames, sizeof(c->input_capture_frames));
    AURenderCallbackStruct callback = {ac_input_callback, c};
    if(rc == noErr) rc = AudioUnitSetProperty(c->input_unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callback, sizeof(callback));
    if(rc == noErr) {
        memcpy(&bits, &rate, sizeof(bits));
        atomic_store_explicit(&c->input_rate_bits, bits, memory_order_release);
        rc = AudioUnitInitialize(c->input_unit);
    }
    if(rc == noErr) rc = AudioOutputUnitStart(c->input_unit);
    if(rc != noErr) {
        ac_input_close(c);
        memcpy(&bits, &none, sizeof(bits)); atomic_store_explicit(&c->input_rate_bits, bits, memory_order_release);
        snprintf(g_audio_last_error, sizeof(g_audio_last_error), "Audio input setup failed (%d); check microphone access", (int)rc); return -1;
    }
    return 0;
#else
    if(device == -2) return 0;
    audio_set_error("Audio input requires macOS"); return -1;
#endif
}
/* Feed interleaved input frames (1 or 2 channels at `rate`) as if a device
 * had captured them: for tests and hosts without an input device. Only one
 * writer may run, so this is refused while a device input is open. */
int32_t __mlang_std_audio_controller_input_feed(int64_t handle, mlang_list_t samples, int64_t channels, int64_t rate) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || channels < 1 || channels > 2 || rate < 8000 || rate > 384000 || samples.size < 0 || (samples.size && !samples.data)) return -1;
#if defined(__APPLE__)
    if(c->input_unit) return -1;
#endif
    double value = (double)rate; uint64_t bits; memcpy(&bits, &value, sizeof(bits));
    atomic_store_explicit(&c->input_rate_bits, bits, memory_order_release);
    ac_input_write(c, (const float *)samples.data, (uint64_t)(samples.size / channels), (uint32_t)channels);
    return 0;
}
/* Peak of the live input since the last read, 0-1000 per channel; the read
 * resets it. */
int32_t __mlang_std_audio_controller_input_peak(int64_t handle, int64_t channel) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || channel < 0 || channel > 1) return 0;
    return (int32_t)atomic_exchange_explicit(&c->input_peak[channel], 0, memory_order_relaxed);
}
/* 1 while a device input is open. */
int32_t __mlang_std_audio_controller_input_active(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return 0;
#if defined(__APPLE__)
    return c->input_unit != NULL;
#else
    return 0;
#endif
}
int64_t __mlang_std_audio_controller_open(int64_t device, int64_t frames) {
    return __mlang_std_audio_controller_open_rate(device, frames, 0);
}
int64_t __mlang_std_audio_controller_info(int64_t handle, int64_t key) {
    ac_controller *c = (ac_controller*)(intptr_t)handle; if(!c) return 0;
    if(key == 0) return (int64_t)c->rate;
    if(key == 1) return c->buffer_frames;
    if(key == 2) return atomic_load_explicit(&c->clock, memory_order_acquire);
    if(key == 3) return atomic_load_explicit(&c->dropped, memory_order_relaxed);
    if(key == 4) return atomic_load_explicit(&c->processor_errors, memory_order_relaxed);
    if(key == 6) return (int64_t)atomic_load_explicit(&c->master_serial, memory_order_relaxed);
    /* 7: threads the last block could use; 8: worst DSP load since the last
     * read, in permille of real time (read resets); 9: spawned workers. */
    if(key == 7) return atomic_load_explicit(&c->render_threads, memory_order_relaxed);
    if(key == 8) return atomic_exchange_explicit(&c->dsp_load, 0, memory_order_relaxed);
    if(key == 9) return c->pool.workers > 0 ? c->pool.workers : 0;
    /* 10: live input frames captured (or fed) so far. */
    if(key == 10) return (int64_t)atomic_load_explicit(&c->input_write, memory_order_acquire);
#if defined(__APPLE__)
    if(key == 5) return c->unit != NULL;
#endif
    return 0;
}

int32_t __mlang_std_audio_controller_processor_support(void) { return ac_processor_factory != NULL; }

/* Any thread. Caps the threads each controller renders with, the render
 * thread included: 0 = every core, 1 = no workers. Applies from the next block. */
int32_t __mlang_std_audio_controller_set_render_threads(int64_t threads) {
    if(threads < 0 || threads > AC_MAX_THREADS) return -1;
    atomic_store_explicit(&ac_thread_limit, (int)threads, memory_order_relaxed); return 0;
}
int64_t __mlang_std_audio_controller_render_thread_limit(void) {
    return atomic_load_explicit(&ac_thread_limit, memory_order_relaxed);
}
/* Most threads a controller spawns: the render thread plus one worker per
 * further core, up to AC_MAX_THREADS. */
int64_t __mlang_std_audio_controller_max_render_threads(void) {
    int cores = ac_cpu_count(); return cores < AC_MAX_THREADS ? cores : AC_MAX_THREADS;
}

/* Control thread, safe while rendering. index as in AC_MASTER_PARAMS. */
int32_t __mlang_std_audio_controller_master_param(int64_t handle, int64_t index, double value) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || index < 0 || index >= AC_MASTER_PARAMS || !isfinite(value)) return -1;
    int frequency = index == 0 || (index <= 12 && (index - 1) % 3 == 0);
    int gain = index <= 12 && index >= 1 && (index - 1) % 3 == 1;
    if(frequency && (value < 5 || value > 40000) && !(index == 0 && value == 0)) return -1;
    if(gain && (value < -24 || value > 24)) return -1;
    if(index <= 12 && index >= 1 && (index - 1) % 3 == 2 && (value < 0.1 || value > 18)) return -1;
    if(index == 13 && (value < 0 || value > 4)) return -1;
    if(index == 14 && value != 0 && value != 1) return -1;
    atomic_store_explicit(&c->master_target[index], ac_float_bits((float)value), memory_order_relaxed);
    atomic_fetch_add_explicit(&c->master_serial, 1, memory_order_release);
    return 0;
}

/* Control thread (single writer), safe while rendering. tempo 0 clears it,
 * otherwise 20-999 BPM. beat < 0 keeps the render thread's running position. */
int32_t __mlang_std_audio_controller_transport(int64_t handle, double tempo, double beat, int32_t playing) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || !isfinite(tempo) || !isfinite(beat) || (tempo != 0 && (tempo < 20 || tempo > 999))) return -1;
    uint64_t serial = atomic_load_explicit(&c->transport_serial, memory_order_relaxed);
    atomic_store_explicit(&c->transport_serial, serial + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&c->transport_bits[0], ac_double_bits(tempo), memory_order_relaxed);
    atomic_store_explicit(&c->transport_bits[1], ac_double_bits(beat < 0 ? 0 : beat), memory_order_relaxed);
    atomic_store_explicit(&c->transport_flags, (playing ? 1u : 0u) | (beat >= 0 ? 2u : 0u), memory_order_relaxed);
    atomic_store_explicit(&c->transport_serial, serial + 2, memory_order_release);
    return 0;
}
/* Beat position after the latest rendered block. */
double __mlang_std_audio_controller_transport_beat(int64_t handle) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    return c ? ac_bits_double(atomic_load_explicit(&c->transport_beat_out, memory_order_relaxed)) : 0;
}

/* In-place iterative radix-2 FFT over interleaved complex doubles. */
static void ac_fft(double *data, int n) {
    for(int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for(; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if(i < j) {
            double tr = data[i * 2], ti = data[i * 2 + 1];
            data[i * 2] = data[j * 2]; data[i * 2 + 1] = data[j * 2 + 1];
            data[j * 2] = tr; data[j * 2 + 1] = ti;
        }
    }
    for(int len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / len, wr = cos(angle), wi = sin(angle);
        for(int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for(int k = 0; k < len / 2; ++k) {
                double *a = &data[(i + k) * 2], *b = &data[(i + k + len / 2) * 2];
                double xr = b[0] * cr - b[1] * ci, xi = b[0] * ci + b[1] * cr;
                b[0] = a[0] - xr; b[1] = a[1] - xi; a[0] += xr; a[1] += xi;
                double next = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = next;
            }
        }
    }
}
/* Single UI consumer: Hann-windowed FFT of the newest `size` post-master
 * samples (power of two, 256..8192). Returns the bin count, or -1. */
int32_t __mlang_std_audio_controller_spectrum_capture(int64_t handle, int64_t size) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || size < 256 || size > AC_SPECTRUM_MAX || (size & (size - 1))) return -1;
    if(!c->spectrum_db) {
        c->spectrum_db = calloc(AC_SPECTRUM_MAX / 2 + 1, sizeof(double));
        if(!c->spectrum_db) return -1;
    }
    double *work = malloc((size_t)size * 2 * sizeof(double));
    if(!work) return -1;
    uint64_t end = atomic_load_explicit(&c->spectrum_write, memory_order_acquire);
    double window_sum = 0;
    for(int64_t i = 0; i < size; ++i) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(size - 1));
        uint64_t at = end - (uint64_t)size + (uint64_t)i;
        float sample = end >= (uint64_t)(size - i) ? c->spectrum_ring[at & (AC_SPECTRUM_RING - 1u)] : 0.f;
        work[i * 2] = (isfinite(sample) ? sample : 0.0) * w; work[i * 2 + 1] = 0; window_sum += w;
    }
    ac_fft(work, (int)size);
    int bins = (int)(size / 2) + 1;
    for(int b = 0; b < bins; ++b) {
        double magnitude = sqrt(work[b * 2] * work[b * 2] + work[b * 2 + 1] * work[b * 2 + 1]) * 2.0 / window_sum;
        c->spectrum_db[b] = magnitude > 1e-9 ? 20.0 * log10(magnitude) : -180.0;
    }
    free(work);
    c->spectrum_bins = bins; c->spectrum_bin_hz = c->rate / (double)size;
    return bins;
}
/* Loudest level (dBFS) of the last capture between two frequencies. A span
 * narrower than a bin interpolates between the neighbours of its centre. */
double __mlang_std_audio_controller_spectrum_level(int64_t handle, double low_hz, double high_hz) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || !c->spectrum_db || c->spectrum_bins < 2 || !(high_hz >= low_hz) || low_hz < 0) return -180.0;
    double first = low_hz / c->spectrum_bin_hz, last = high_hz / c->spectrum_bin_hz;
    int lo = (int)ceil(first), hi = (int)floor(last);
    if(hi >= c->spectrum_bins) hi = c->spectrum_bins - 1;
    if(lo < 1) lo = 1;
    if(lo <= hi) {
        double best = -180.0;
        for(int b = lo; b <= hi; ++b) if(c->spectrum_db[b] > best) best = c->spectrum_db[b];
        return best;
    }
    double centre = (first + last) / 2;
    int below = (int)floor(centre); double t = centre - below;
    if(below < 1) { below = 1; t = 0; }
    if(below + 1 >= c->spectrum_bins) return c->spectrum_db[c->spectrum_bins - 1];
    return c->spectrum_db[below] * (1 - t) + c->spectrum_db[below + 1] * t;
}
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
/* Audio input of the instrument in `slot`: 0 = none (silence), 1-64 = the
 * voices of that PCM track before its inserts and fader, 65 = the live audio
 * input (see input_open). The track still plays through its own channel.
 * Audio may run. */
int32_t __mlang_std_audio_controller_instrument_input(int64_t handle, int64_t slot, int64_t source) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || source < 0 || source > 65) return -1;
    atomic_store_explicit(&c->input_route[slot - 1], (uint32_t)source, memory_order_relaxed); return 0;
}
/* Stereo output buses of the instrument in `slot`, main included (1 for a
 * single-output instrument), or -1 when the slot is empty. */
int64_t __mlang_std_audio_controller_instrument_outputs(int64_t handle, int64_t slot) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || !c->instruments[slot - 1].context) return -1;
    return c->instrument_outputs[slot - 1];
}
/* Name of output bus `bus` (0 = main); "" when unknown. Borrowed. */
const char *__mlang_std_audio_controller_instrument_output_name(int64_t handle, int64_t slot, int64_t bus) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || bus < 0 || bus >= c->instrument_outputs[slot - 1]) return "";
    mlang_audio_processor *p = &c->instruments[slot - 1];
    const char *name = p->context && p->output_name ? p->output_name(p->context, (int32_t)bus) : NULL;
    return name ? name : "";
}
/* Where aux bus `bus` (1..outputs-1) of an instrument goes: -1 = with the
 * instrument's main output (the default), 0 = master, 1-64 = that PCM track's
 * channel. Audio may run; loading an instrument resets its routes. */
int32_t __mlang_std_audio_controller_instrument_output_route(int64_t handle, int64_t slot, int64_t bus, int64_t destination) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || bus < 1 || bus >= AC_OUTPUTS || destination < -1 || destination > 73) return -1;
    atomic_store_explicit(&c->aux_route[slot - 1][bus], (uint32_t)(destination + 1), memory_order_relaxed); return 0;
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
    if(p->context) p->destroy(p->context); *p = next; c->processor_reset = 1; if(next.context) ac_pool_ensure(&c->pool);
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
    if(next.context) ac_pool_ensure(&c->pool);
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
    *p = (mlang_audio_processor){0}; c->instrument_outputs[slot - 1] = 0; c->processor_reset = 1; return 0;
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
/* Control thread. A sampler pad's slice markers (frames); empty when the
 * instrument has none for it. */
mlang_list_t __mlang_std_audio_controller_instrument_markers(int64_t handle, int64_t slot, int64_t pad) {
    mlang_list_t out = {0, NULL};
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || pad < 0 || pad > 127) return out;
    mlang_audio_processor *p = &c->instruments[slot - 1];
    if(!p->context || !p->pad_markers) return out;
    double probe[1];
    int32_t count = p->pad_markers(p->context, (int32_t)pad, probe, 0);
    if(count <= 0 || count > 65536) return out;
    double *frames = malloc((size_t)count * sizeof(double));
    if(!frames) return out;
    int32_t got = p->pad_markers(p->context, (int32_t)pad, frames, count);
    if(got <= 0) { free(frames); return out; }
    out.data = frames; out.size = got < count ? got : count;
    return out;
}
/* Control thread. Replace a sampler pad's slice markers with `frames`, or
 * detect them again (`detect`). 0 or -1. */
int32_t __mlang_std_audio_controller_instrument_set_markers(int64_t handle, int64_t slot, int64_t pad, mlang_list_t frames, int64_t detect) {
    ac_controller *c = (ac_controller*)(intptr_t)handle;
    if(!c || slot < 1 || slot > 32 || pad < 0 || pad > 127 || frames.size < 0 || frames.size > 65536) return -1;
    mlang_audio_processor *p = &c->instruments[slot - 1];
    if(!p->context || !p->set_pad_markers) return -1;
    return p->set_pad_markers(p->context, (int32_t)pad, (const double *)frames.data, detect ? -1 : (int32_t)frames.size);
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
    *old = next; c->processor_reset = 1;
    int outputs = next.output_count ? next.output_count(next.context) : 1;
    c->instrument_outputs[slot - 1] = outputs < 1 ? 1 : (outputs > AC_OUTPUTS ? AC_OUTPUTS : outputs);
    for(int bus = 0; bus < AC_OUTPUTS; ++bus) atomic_store_explicit(&c->aux_route[slot - 1][bus], 0, memory_order_relaxed);
    ac_pool_ensure(&c->pool); audio_clear_error(); return 0;
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

/* Exclusive float WAV export for a duplicated, possibly edited session sample.
 * No existing path is ever truncated; PCM channels and precision are retained. */
int32_t __mlang_std_audio_sample_copy_file(const char *path, mlang_list_t pcm,
                                         int64_t channels, int64_t rate) {
    if (!path || !*path || !pcm.data || pcm.size <= 0 || channels < 1 || channels > 32 ||
        pcm.size % channels || rate < 1 || rate > 384000 ||
        (uint64_t)pcm.size > (UINT32_MAX - 36u) / 4u) return -1;
    FILE *file = fopen(path, "wbx");
    if (!file) return -1;
    uint32_t size = (uint32_t)pcm.size * 4u;
    unsigned char header[44] = {0};
    memcpy(header, "RIFF", 4); memcpy(header + 8, "WAVEfmt ", 8);
    memcpy(header + 36, "data", 4);
    uint32_t values[] = {size + 36u, 16, (uint32_t)rate,
                        (uint32_t)(rate * channels * 4), size};
    unsigned offsets[] = {4, 16, 24, 28, 40};
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned b = 0; b < 4; ++b) header[offsets[i] + b] = (unsigned char)(values[i] >> (b * 8));
    header[20] = 3; header[22] = (unsigned char)channels;
    header[32] = (unsigned char)(channels * 4); header[34] = 32;
    int ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    const float *samples = (const float *)pcm.data;
    unsigned char bytes[4096];
    for (int64_t start = 0; ok && start < pcm.size; start += 1024) {
        size_t count = (size_t)(pcm.size - start); if (count > 1024) count = 1024;
        for (size_t i = 0; i < count; ++i) {
            uint32_t bits; memcpy(&bits, samples + start + i, sizeof(bits));
            for (unsigned b = 0; b < 4; ++b) bytes[i * 4 + b] = (unsigned char)(bits >> (b * 8));
        }
        ok = fwrite(bytes, 4, count, file) == count;
    }
    if (fclose(file) != 0) ok = 0;
    if (!ok) remove(path);
    return ok ? 0 : -1;
}
