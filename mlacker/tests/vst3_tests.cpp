#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
void mlacker_install_vst3_host();
int64_t __mlang_std_audio_controller_new(int64_t, int64_t);
int32_t __mlang_std_audio_controller_load_processor(int64_t, const char*);
int32_t __mlang_std_audio_controller_load_instrument(int64_t, int64_t, const char*);
const char *__mlang_std_audio_controller_instrument_name(int64_t, int64_t);
const char *__mlang_std_audio_controller_processor_name(int64_t);
int32_t __mlang_std_audio_controller_processor_support();
int32_t __mlang_std_audio_controller_post(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, double);
int32_t __mlang_std_audio_controller_process(int64_t, int64_t, int64_t);
void __mlang_std_audio_controller_panic(int64_t);
int32_t __mlang_std_audio_controller_stop(int64_t);
int32_t __mlang_std_audio_controller_close(int64_t);
int64_t __mlang_std_audio_controller_info(int64_t, int64_t);
int64_t __mlang_std_audio_pcm_block_new(int64_t);
float __mlang_std_audio_pcm_block_sample(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_pcm_block_close(int64_t);
const char *__mlang_std_audio_last_error();
}
#define CHECK(condition) do { if(!(condition)) { std::fprintf(stderr, "FAIL line %d: %s; %s\n", __LINE__, #condition, __mlang_std_audio_last_error()); std::exit(1); } } while(0)
int main(int argc, char **argv) {
    CHECK(argc == 3);
    mlacker_install_vst3_host(); CHECK(__mlang_std_audio_controller_processor_support() == 1);
    int64_t c = __mlang_std_audio_controller_new(48000, 128), b = __mlang_std_audio_pcm_block_new(256);
    CHECK(c && b);
    CHECK(__mlang_std_audio_controller_load_processor(c, argv[1]) == 0);
    CHECK(std::strcmp(__mlang_std_audio_controller_processor_name(c), "Mlacker Test Instrument") == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 1, 2, 69, 127, 2, 64, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 2, 2, 69, 0, 2, 192, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    for(int f = 0; f < 256; ++f) {
        float expected = f >= 64 && f < 192 ? 0.125f : 0.f;
        CHECK(__mlang_std_audio_pcm_block_sample(b, f, 0) == expected);
        CHECK(__mlang_std_audio_pcm_block_sample(b, f, 1) == expected);
    }
    CHECK(__mlang_std_audio_controller_load_processor(c, "/nonexistent/mlacker.vst3") != 0);
    CHECK(std::strcmp(__mlang_std_audio_controller_processor_name(c), "Mlacker Test Instrument") == 0);
    // A future main-thread event cannot block the live MIDI producer lane.
    CHECK(__mlang_std_audio_controller_post(c, 0, 3, 0, 0, 0, 0, 4096, 0, 0.5) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 1, 1, 0, 60, 127, 2147483647, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 20, 0) == 0.125f);
    __mlang_std_audio_controller_panic(c);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 20, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_info(c, 4) == 0);
    CHECK(__mlang_std_audio_controller_stop(c) == 0);
    CHECK(__mlang_std_audio_controller_load_processor(c, "") == 0);
    CHECK(*__mlang_std_audio_controller_processor_name(c) == 0);
    // Module can be unloaded and reloaded repeatedly without dangling factories.
    for(int i = 0; i < 3; ++i) {
        CHECK(__mlang_std_audio_controller_load_processor(c, argv[1]) == 0);
        CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
        CHECK(__mlang_std_audio_controller_load_processor(c, "") == 0);
    }
    CHECK(__mlang_std_audio_controller_load_processor(c, argv[2]) == 0);
    int64_t dry = __mlang_std_audio_controller_new(48000, 128), dryBlock = __mlang_std_audio_pcm_block_new(256);
    CHECK(dry && dryBlock);
    CHECK(__mlang_std_audio_controller_post(c, 0, 1, 0, 69, 127, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(dry, 0, 1, 0, 69, 127, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_process(dry, dryBlock, 256) == 0);
    for(int f = 0; f < 256; ++f)
        CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, f, 0) - __mlang_std_audio_pcm_block_sample(dryBlock, f, 0) * 0.5f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_close(dry) == 0);
    CHECK(__mlang_std_audio_pcm_block_close(dryBlock) == 0);
    CHECK(__mlang_std_audio_controller_load_processor(c, "") == 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 0, argv[1]) != 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 33, argv[1]) != 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[2]) != 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 2, argv[1]) == 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[2]) != 0);
    CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(c, 1), "Mlacker Test Instrument") == 0);
    __mlang_std_audio_controller_panic(c);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    int64_t clock = __mlang_std_audio_controller_info(c, 2);
    // Same pitch/channel in separate instances: independent, additive, exact offsets.
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 0, clock + 32, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 1, clock + 64, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 7, 0, 60, 0, 0, clock + 96, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 7, 0, 60, 0, 1, clock + 128, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    for(int f = 0; f < 256; ++f) {
        float expected = (f >= 32 && f < 96 ? .125f : 0.f) + (f >= 64 && f < 128 ? .125f : 0.f);
        CHECK(__mlang_std_audio_pcm_block_sample(b, f, 0) == expected);
    }
    // Unassigned instrument notes never fall back to a sine voice.
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 0, -1, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    __mlang_std_audio_controller_panic(c);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_close(c) == 0);
    CHECK(__mlang_std_audio_pcm_block_close(b) == 0);
    std::puts("PASS: real VST3 bundle load, frame-timed MIDI, output, panic, failed replacement, reload");
}
