/* Parallel render regression test for the std::audio controller.
 *
 * Registers deterministic fake instruments and stateful insert/aux effects,
 * renders the same song offline with one thread and with every core, and
 * requires bit-identical output: the pool sums in slot order, so the thread
 * count must never change the mix. Inserts shared between sources exercise
 * the path that keeps one plugin instance off two threads at once.
 */
#include "../stdlib/include/mlang_audio_processor.h"
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int64_t __mlang_std_audio_controller_new(int64_t rate, int64_t frames);
int32_t __mlang_std_audio_controller_close(int64_t handle);
int32_t __mlang_std_audio_controller_process(int64_t handle, int64_t block, int64_t frames);
int32_t __mlang_std_audio_controller_post(int64_t handle, int64_t lane, int64_t kind, int64_t channel,
    int64_t note, int64_t velocity, int64_t source, int64_t frame, int64_t sample, double gain);
int32_t __mlang_std_audio_controller_load_instrument(int64_t handle, int64_t slot, const char *path);
int32_t __mlang_std_audio_controller_load_insert(int64_t handle, int64_t id, const char *path);
int32_t __mlang_std_audio_controller_load_effect(int64_t handle, int64_t fx, const char *path);
int32_t __mlang_std_audio_controller_insert_route(int64_t handle, int64_t source, int64_t position, int64_t id);
int32_t __mlang_std_audio_controller_output_route(int64_t handle, int64_t source, int64_t destination);
int32_t __mlang_std_audio_controller_effect_send(int64_t handle, int64_t track, int64_t slot, int64_t fx, int64_t percent);
int32_t __mlang_std_audio_controller_set_render_threads(int64_t threads);
int64_t __mlang_std_audio_controller_max_render_threads(void);
int64_t __mlang_std_audio_controller_info(int64_t handle, int64_t key);
int64_t __mlang_std_audio_pcm_block_new(int64_t capacity_frames);
float __mlang_std_audio_pcm_block_sample(int64_t handle, int64_t frame, int64_t channel);
int32_t __mlang_std_audio_pcm_block_close(int64_t handle);

#define FRAMES 256
#define BLOCKS 64
#define INSTRUMENTS 12

typedef struct {
    int instrument, busy;
    double phase, step, level, state[2];
    _Atomic int inside; /* > 1 means two threads processed this instance at once */
} fake;
static _Atomic int overlaps;

static void fake_begin(void *p, int32_t reset) { (void)p; (void)reset; }
static void fake_note(void *p, int32_t on, int32_t channel, int32_t pitch, int32_t velocity, int32_t offset) {
    fake *f = p; (void)channel; (void)offset;
    if(on) { f->step = 440.0 * pow(2.0, (pitch - 69) / 12.0) / 48000.0; f->level = velocity / 127.0 * 0.1; }
    else f->level = 0;
}
static int32_t fake_process(void *p, float *stereo, int32_t frames, uint64_t clock) {
    fake *f = p; (void)clock;
    if(atomic_fetch_add(&f->inside, 1) != 0) atomic_fetch_add(&overlaps, 1);
    for(int32_t i = 0; i < frames; ++i) {
        if(f->instrument) {
            /* A little real work per sample so threads overlap in time. */
            double value = 0;
            for(int k = 1; k <= f->busy; ++k) value += sin(f->phase * 6.283185307179586 * k) / k;
            f->phase += f->step; f->phase -= floor(f->phase);
            stereo[i * 2] += (float)(value * f->level); stereo[i * 2 + 1] += (float)(value * f->level * 0.5);
        } else {
            /* Stateful one-pole: the result depends on the order of calls. */
            for(int ch = 0; ch < 2; ++ch) {
                f->state[ch] += (stereo[i * 2 + ch] - f->state[ch]) * 0.2;
                stereo[i * 2 + ch] = (float)f->state[ch];
            }
        }
    }
    atomic_fetch_sub(&f->inside, 1);
    return 0;
}
static void fake_destroy(void *p) { free(p); }
static const char *fake_name(void *p) { (void)p; return "fake"; }
static int32_t fake_factory(int instrument, mlang_audio_processor *out) {
    fake *f = calloc(1, sizeof(*f)); if(!f) return -1;
    f->instrument = instrument; f->busy = 24; atomic_init(&f->inside, 0);
    memset(out, 0, sizeof(*out));
    out->context = f; out->instrument = instrument;
    out->begin = fake_begin; out->note = fake_note; out->process = fake_process;
    out->destroy = fake_destroy; out->name = fake_name;
    return 0;
}
static int32_t instrument_factory(const char *path, double rate, int32_t max_frames,
    mlang_audio_processor *out, char *error, int32_t error_size) {
    (void)path; (void)rate; (void)max_frames; (void)error; (void)error_size;
    return fake_factory(1, out);
}
static int32_t effect_factory(const char *path, double rate, int32_t max_frames,
    mlang_audio_processor *out, char *error, int32_t error_size) {
    (void)path; (void)rate; (void)max_frames; (void)error; (void)error_size;
    return fake_factory(0, out);
}

static double now_seconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9;
}

/* Renders the fixed song and returns interleaved output (caller frees). */
static float *render(int threads, double *seconds, int64_t *used) {
    __mlang_std_audio_controller_set_render_threads(threads);
    int64_t c = __mlang_std_audio_controller_new(48000, FRAMES);
    int64_t block = __mlang_std_audio_pcm_block_new(FRAMES);
    float *out = malloc(sizeof(float) * 2 * FRAMES * BLOCKS);
    if(!c || !block || !out) { fprintf(stderr, "setup failed\n"); exit(1); }
    for(int i = 1; i <= INSTRUMENTS; ++i)
        if(__mlang_std_audio_controller_load_instrument(c, i, "fake") != 0) { fprintf(stderr, "load failed\n"); exit(1); }
    for(int id = 1; id <= 4; ++id) __mlang_std_audio_controller_load_insert(c, id, "fake");
    __mlang_std_audio_controller_load_effect(c, 0, "fake");
    __mlang_std_audio_controller_load_effect(c, 1, "fake");
    /* Instruments 1-2 share insert 1; instrument 3 has its own. */
    __mlang_std_audio_controller_insert_route(c, 64 + 0, 0, 1);
    __mlang_std_audio_controller_insert_route(c, 64 + 1, 0, 1);
    __mlang_std_audio_controller_insert_route(c, 64 + 2, 0, 2);
    /* Instruments 4-6 feed track 0, which feeds track 1: two track depths.
     * Tracks 2 and 3 are fed by instruments 7-8 and share insert 3. */
    for(int i = 3; i < 6; ++i) __mlang_std_audio_controller_output_route(c, 64 + i, 1);
    __mlang_std_audio_controller_output_route(c, 0, 2);
    __mlang_std_audio_controller_insert_route(c, 0, 0, 4);
    __mlang_std_audio_controller_output_route(c, 64 + 6, 3);
    __mlang_std_audio_controller_output_route(c, 64 + 7, 4);
    __mlang_std_audio_controller_insert_route(c, 2, 0, 3);
    __mlang_std_audio_controller_insert_route(c, 3, 0, 3);
    for(int i = 0; i < INSTRUMENTS; i += 3) __mlang_std_audio_controller_effect_send(c, 0, i + 1, i % 2, 30);
    __mlang_std_audio_controller_effect_send(c, 1, 0, 0, 25);
    for(int i = 0; i < INSTRUMENTS; ++i)
        __mlang_std_audio_controller_post(c, 0, 6, 0, 48 + i * 3, 100, 65536 + i, -1, i + 1, 1.0);
    double start = now_seconds();
    for(int b = 0; b < BLOCKS; ++b) {
        if(__mlang_std_audio_controller_process(c, block, FRAMES) != 0) { fprintf(stderr, "process failed\n"); exit(1); }
        for(int f = 0; f < FRAMES; ++f)
            for(int ch = 0; ch < 2; ++ch) out[(b * FRAMES + f) * 2 + ch] = __mlang_std_audio_pcm_block_sample(block, f, ch);
    }
    *seconds = now_seconds() - start;
    *used = __mlang_std_audio_controller_info(c, 7);
    if(__mlang_std_audio_controller_info(c, 4) != 0) { fprintf(stderr, "processor errors\n"); exit(1); }
    __mlang_std_audio_pcm_block_close(block);
    __mlang_std_audio_controller_close(c);
    return out;
}

int main(void) {
    mlang_audio_register_instrument_factory(instrument_factory);
    mlang_audio_register_processor_factory(effect_factory);
    atomic_init(&overlaps, 0);
    double serial_time = 0, parallel_time = 0; int64_t serial_used = 0, parallel_used = 0;
    float *serial = render(1, &serial_time, &serial_used);
    float *parallel = render(0, &parallel_time, &parallel_used);
    int failures = 0;
    double energy = 0;
    for(int i = 0; i < 2 * FRAMES * BLOCKS; ++i) {
        energy += fabs(serial[i]);
        if(memcmp(&serial[i], &parallel[i], sizeof(float)) != 0 && failures++ < 5)
            fprintf(stderr, "sample %d differs: %.9g vs %.9g\n", i, serial[i], parallel[i]);
    }
    if(energy == 0) { fprintf(stderr, "render was silent\n"); failures++; }
    if(serial_used != 1) { fprintf(stderr, "thread limit 1 used %lld threads\n", (long long)serial_used); failures++; }
    int64_t cores = __mlang_std_audio_controller_max_render_threads();
    if(parallel_used != cores) { fprintf(stderr, "expected %lld threads, used %lld\n", (long long)cores, (long long)parallel_used); failures++; }
    if(atomic_load(&overlaps) != 0) { fprintf(stderr, "a plugin instance ran on two threads at once\n"); failures++; }
    printf("1 thread: %.1f ms, %lld threads: %.1f ms\n", serial_time * 1000, (long long)parallel_used, parallel_time * 1000);
    __mlang_std_audio_controller_set_render_threads(0);
    free(serial); free(parallel);
    if(failures) { fprintf(stderr, "FAILED\n"); return 1; }
    printf("PASSED\n");
    return 0;
}
