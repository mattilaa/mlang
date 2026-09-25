#ifndef MLANG_AUDIO_PROCESSOR_H
#define MLANG_AUDIO_PROCESSOR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Optional native master processor. No SDK dependency in the runtime.
 * Install the factory on the control thread before creating audio controllers.
 * Load/destroy are control-thread calls; begin/note/process run on the audio
 * thread and must be bounded, allocation-free and lock-free in the host.
 * Third-party processor realtime behavior remains that processor's contract.
 */
typedef struct mlang_audio_processor {
    void *context;
    int32_t instrument; /* suppress reference sine voices, retain PCM mix */
    void (*begin)(void *context, int32_t reset);
    void (*note)(void *context, int32_t on, int32_t channel, int32_t pitch,
                 int32_t velocity, int32_t offset);
    int32_t (*process)(void *context, float *stereo, int32_t frames, uint64_t clock);
    void (*destroy)(void *context);
    const char *(*name)(void *context);
    /* Optional MIDI CC (0-127) / pitch bend (129, value 0-16383). */
    void (*control)(void *context, int32_t channel, int32_t controller,
                    int32_t value, int32_t offset);
    /* Cached metadata, control-thread queries. key: 0=count, 1=steps,
     * 2=normalized value, 3=read-only, 4=stable parameter ID.
     * Names borrowed until destruction. */
    double (*parameter_info)(void *context, int32_t index, int32_t key);
    const char *(*parameter_name)(void *context, int32_t index);
    void (*parameter)(void *context, int32_t index, double value, int32_t offset);
    /* Control thread: mirror accepted edits into cached/controller state. */
    void (*parameter_edited)(void *context, int32_t index, double normalized);
    /* Optional, control thread, audio may be running: copy interleaved PCM
     * (frames * channels floats, 1-2 channels) into sampler pad `pad`
     * (mla_sampler_protocol.h). Returns 0, or -1 with a reason in `error`.
     * The processor owns realtime-safe hand-off to its audio thread. */
    int32_t (*load_pad)(void *context, int32_t pad, const float *interleaved, int64_t frames,
                        int32_t channels, double rate, const char *name, char *error, int32_t error_size);
    /* frames == 0 (interleaved may be NULL) empties the pad instead. */
    /* Optional, control thread: sampler layout. key 0 = root MIDI key of pad 0,
     * 1 = pad count, 2 = bitmask of loaded pads. -1 when unsupported. */
    int64_t (*sampler_info)(void *context, int32_t key);
    /* Optional, audio thread, before each process in a block: the sequencer
     * transport at the block's first frame. tempo <= 0 when none was set;
     * beat counts quarter notes from song start and keeps its last value
     * (still valid) while stopped. */
    void (*transport)(void *context, double tempo, double beat, int32_t playing);
} mlang_audio_processor;
typedef int32_t (*mlang_audio_processor_factory)(const char *path, double rate,
    int32_t max_frames, mlang_audio_processor *out, char *error, int32_t error_size);
void mlang_audio_register_processor_factory(mlang_audio_processor_factory factory);
/* Separate instrument-only factory; loaded slots mix before the master.
 * Must return an instrument processor, rejecting effects/unsupported bundles. */
void mlang_audio_register_instrument_factory(mlang_audio_processor_factory factory);
#ifdef __cplusplus
}
#endif
#endif
