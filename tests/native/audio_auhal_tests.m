/* Run on macOS:
 * clang -std=gnu17 -fblocks tests/native/audio_auhal_tests.m \
 *   -framework AVFoundation -framework Foundation -framework CoreAudio \
 *   -framework AudioToolbox -framework AudioUnit -o /tmp/audio_auhal_tests
 * /tmp/audio_auhal_tests
 */
#include <assert.h>
#include "../../stdlib/src/std_audio.c"

int main(void)
{
    mlang_audio_device_t device = {0};
    float ring[8] = {0};
    device.pcm_ring = ring;
    device.pcm_capacity_frames = 4;
    /* Exercise wrapping, stereo separation and partial underrun silence. */
    atomic_store(&device.pcm_read_frame, 3);
    atomic_store(&device.pcm_write_frame, 3);
    float samples[] = {0.25f, -0.25f, 0.5f, -0.5f};
    assert(__mlang_std_audio_queue_interleaved_f32(
        (int64_t)(intptr_t)&device, (mlang_list_t){4, samples}) == 2);
    float left[4] = {9, 9, 9, 9}, right[4] = {9, 9, 9, 9};
    struct { UInt32 count; AudioBuffer buffers[2]; } output = {
        2, {{1, sizeof(left), left}, {1, sizeof(right), right}}};
    assert(audio_output_unit_callback(&device, NULL, NULL, 0, 3,
        (AudioBufferList*)&output) == noErr);
    assert(left[0] == .25f && right[0] == -.25f);
    assert(left[1] == .5f && right[1] == -.5f);
    assert(left[2] == 0 && right[2] == 0);
    assert(left[3] == 9 && right[3] == 9);
    assert(audio_pcm_queued_frames(&device) == 0);
    assert(atomic_load(&device.pcm_underruns) == 1);
    output.buffers[1].mDataByteSize = 0;
    assert(audio_output_unit_callback(&device, NULL, NULL, 0, 3,
        (AudioBufferList*)&output) == kAudio_ParamError);
    assert(atomic_load(&device.pcm_underruns) == 1);
    puts("AUHAL callback tests passed");
    return 0;
}
