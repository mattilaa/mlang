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
