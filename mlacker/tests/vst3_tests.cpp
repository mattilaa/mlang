#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>
extern "C" {
void mlacker_install_vst3_host();
int64_t __mlang_std_audio_controller_new(int64_t, int64_t);
int64_t __mlang_std_audio_controller_open(int64_t, int64_t);
int32_t __mlang_std_audio_controller_start(int64_t);
int32_t __mlang_std_audio_controller_load_processor(int64_t, const char*);
int32_t __mlang_std_audio_controller_load_instrument(int64_t, int64_t, const char*);
const char *__mlang_std_audio_controller_instrument_name(int64_t, int64_t);
double __mlang_std_audio_controller_parameter_info(int64_t, int64_t, int64_t, int64_t);
const char *__mlang_std_audio_controller_parameter_name(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_set_parameter(int64_t, int64_t, int64_t, double);
int32_t __mlang_std_audio_controller_restore_parameter(int64_t, int64_t, int64_t, double);
int32_t __mlang_std_audio_controller_unload_instrument(int64_t, int64_t);
int32_t __mlang_std_audio_controller_midi_target(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_live_note(int64_t, int64_t, int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_live_control(int64_t, int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_midi_learn(int64_t, int64_t, int64_t, int64_t);
int64_t __mlang_std_audio_controller_midi_learn_info(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_master_peak(int64_t, int64_t);
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
    if(argc == 2 && std::strcmp(argv[1], "--handover-probe") == 0) {
        // Silent hardware diagnostic: no plugins, samples, or note events.
        const int64_t old = __mlang_std_audio_controller_open(-1, 128);
        CHECK(old && __mlang_std_audio_controller_start(old) == 0);
        const int64_t next = __mlang_std_audio_controller_open(-1, 128);
        CHECK(next && __mlang_std_audio_controller_stop(old) == 0);
        CHECK(__mlang_std_audio_controller_start(next) == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto before = __mlang_std_audio_controller_info(next, 2);
        CHECK(__mlang_std_audio_controller_close(old) == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto after = __mlang_std_audio_controller_info(next, 2);
        std::printf("Output handover frame clock: before=%lld after=%lld\n", (long long)before, (long long)after);
        CHECK(__mlang_std_audio_controller_close(next) == 0);
        CHECK(before > 0 && after > before);
        return 0;
    }
    if(argc == 3 && (std::strcmp(argv[1], "--restore-probe") == 0 || std::strcmp(argv[1], "--session-probe") == 0)) {
        const bool session = std::strcmp(argv[1], "--session-probe") == 0;
        std::string path = argv[2];
        std::vector<double> saved;
        std::vector<int64_t> ids;
        if(session) {
            // Read-only diagnostic for a single-plugin, sample-free 1.0 session.
            std::ifstream input(path, std::ios::binary);
            auto integer = [&]() { int64_t v = 0; CHECK(input.read(reinterpret_cast<char*>(&v), 8)); return v; };
            auto string = [&]() { const auto size = integer(); CHECK(size >= 0 && size <= 4096); std::string v(size, '\0'); CHECK(input.read(v.data(), size)); return v; };
            CHECK(string() == "MLACK" && integer() == 1 && integer() == 0);
            for(int i = 0; i < 24; ++i) integer();
            CHECK(integer() == 0); CHECK(integer() == 1); CHECK(integer() == 1);
            path = string();
            const auto count = integer(); CHECK(count > 0 && count <= 16384);
            for(int i = 0; i < count; ++i) {
                ids.push_back(integer()); double v = 0;
                CHECK(input.read(reinterpret_cast<char*>(&v), 8)); CHECK(std::isfinite(v) && v >= 0 && v <= 1); saved.push_back(v);
            }
        }
        mlacker_install_vst3_host();
        const int64_t block = __mlang_std_audio_pcm_block_new(128);
        std::vector<double> values;
        std::vector<double> defaults;
        for(int pass = 0; pass < (session ? 3 : 2); ++pass) {
            const int64_t controller = __mlang_std_audio_controller_new(48000, 128);
            CHECK(__mlang_std_audio_controller_load_instrument(controller, 1, path.c_str()) == 0);
            for(int n = 0; pass == 0 && n < 375; ++n) {
                CHECK(__mlang_std_audio_controller_process(controller, block, 128) == 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            const int count = (int)__mlang_std_audio_controller_parameter_info(controller, 1, 0, 0);
            if(session) CHECK(count == (int)saved.size());
            for(int i = 0; i < count; ++i) {
                if(pass == 0) {
                    const double initial = __mlang_std_audio_controller_parameter_info(controller, 1, i, 2);
                    defaults.push_back(initial);
                    values.push_back(session ? saved[i] : initial);
                    if(session) {
                        CHECK(ids[i] == __mlang_std_audio_controller_parameter_info(controller, 1, i, 4));
                        if(std::fabs(initial - saved[i]) > 0.000001)
                            std::printf("%d %s: fresh=%.6f saved=%.6f\n", i, __mlang_std_audio_controller_parameter_name(controller, 1, i), initial, saved[i]);
                    }
                }
                else if(pass == 1 || std::fabs(defaults[i] - values[i]) > 0.000001)
                    CHECK(__mlang_std_audio_controller_restore_parameter(controller, 1, i, values.at(i)) == 0);
            }
            for(int n = 0; pass > 0 && n < 375; ++n) {
                CHECK(__mlang_std_audio_controller_process(controller, block, 128) == 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            CHECK(__mlang_std_audio_controller_midi_target(controller, 0, 1) == 0);
            CHECK(__mlang_std_audio_controller_live_note(controller, 1, 0, 60, 100) == 0);
            double peak = 0;
            for(int n = 0; n < 1500; ++n) {
                CHECK(__mlang_std_audio_controller_process(controller, block, 128) == 0);
                for(int f = 0; f < 128; ++f)
                    peak = std::fmax(peak, std::fabs(__mlang_std_audio_pcm_block_sample(block, f, 0)));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::printf("%s: parameters=%d peak=%f errors=%lld\n", pass == 2 ? "edits only" : (pass ? "restored" : "fresh"), count, peak,
                (long long)__mlang_std_audio_controller_info(controller, 4));
            std::fflush(stdout);
            CHECK(__mlang_std_audio_controller_close(controller) == 0);
        }
        CHECK(__mlang_std_audio_pcm_block_close(block) == 0);
        return 0;
    }
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
    __mlang_std_audio_controller_master_peak(c, 0);
    __mlang_std_audio_controller_master_peak(c, 1);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 3, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .125f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 125);
    CHECK(__mlang_std_audio_controller_master_peak(c, 1) == 125);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 0);
    // Changing selection while held: the next key addresses slot 2; first off
    // must still reach slot 1. Both instrument PCMs contribute to master.
    CHECK(__mlang_std_audio_controller_midi_target(c, 1, 2) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 3, 62, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .25f);
    CHECK(__mlang_std_audio_controller_live_note(c, 0, 3, 60, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .125f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 250); // peak hold across blocks
    CHECK(__mlang_std_audio_controller_master_peak(c, 1) == 250);
    CHECK(__mlang_std_audio_controller_midi_target(c, 2, -1) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 0, 3, 62, 0) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 3, 64, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_load_processor(c, argv[2]) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 3, 0, 0, 0, 0, -1, 0, 1.0) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .25f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 250); // post-effect and gain
    CHECK(__mlang_std_audio_controller_stop(c) == 0);
    CHECK(__mlang_std_audio_controller_master_peak(c, 1) == 0);
    CHECK(__mlang_std_audio_controller_load_processor(c, "") == 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    clock = __mlang_std_audio_controller_info(c, 2);
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 0, clock, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 1, 0, 0, clock + 32, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 1, 127, 0, clock + 64, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 74, 64, 0, clock + 96, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 129, 0, 0, clock + 128, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 129, 16383, 0, clock + 160, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 23, 0, 0, clock + 192, 1, 1) == 0); // unmapped: ignored
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 1, 128, 0, -1, 1, 1) == -1);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    for(int f = 0; f < 256; ++f) {
        float expected = (f >= 32 && f < 64) || (f >= 128 && f < 160) ? 0.f : (f >= 96 ? .5f * 64.f / 127.f : .5f);
        CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, f, 0) - expected) < 1.e-7f);
    }
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 0) == 40);
    CHECK(std::strcmp(__mlang_std_audio_controller_parameter_name(c, 1, 0), "Modulation") == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 1, 2) == 64.0 / 127.0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 3, 1) == 3);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 4, 3) == 1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 3, 1.5) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 3, 4) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 4, 0.2) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 40, 0.2) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 0, 0, 0.2) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, NAN) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, INFINITY) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, 1.01) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, 0.5) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 2) == 0.5); // also cached without hardware rendering
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 3, 2) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 2) == 0.5);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 3, 2) == 2.0 / 3.0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .25f * 64.f / 127.f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_stop(c) == 0);
    CHECK(__mlang_std_audio_controller_unload_instrument(c, 1) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 0) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, 0.5) == -1);
    CHECK(*__mlang_std_audio_controller_instrument_name(c, 1) == 0);
    CHECK(*__mlang_std_audio_controller_instrument_name(c, 2) != 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    // Empty parameter blocks preserve the plugin's last value; exercise idle processing.
    for(int i = 0; i < 20000; ++i) CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_load_processor(c, argv[2]) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 8, 0, 74, 0, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_close(c) == 0);
    // Session restore must produce PCM, not merely expose cached parameters.
    c = __mlang_std_audio_controller_new(48000, 128);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    const int count = (int)__mlang_std_audio_controller_parameter_info(c, 1, 0, 0);
    for(int i = 0; i < count; ++i) {
        double value = __mlang_std_audio_controller_parameter_info(c, 1, i, 2);
        CHECK(__mlang_std_audio_controller_restore_parameter(c, 1, i, value) == 0);
    }
    CHECK(__mlang_std_audio_controller_restore_parameter(c, 1, 0, 0.25) == 0);
    CHECK(__mlang_std_audio_controller_restore_parameter(c, 1, 0, 0.25) == 0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.03125f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) > 0);
    CHECK(__mlang_std_audio_controller_master_peak(c, 1) > 0);
    // Live CC and velocity reach the same processor as sequenced events.
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 1, 127) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 32) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    const float soft = __mlang_std_audio_pcm_block_sample(b, 200, 0);
    CHECK(soft > 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 96) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    const float hard = __mlang_std_audio_pcm_block_sample(b, 200, 0);
    CHECK(std::fabs(hard - soft * 3) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 1, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 1, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == hard);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 129, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 129, 16383) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == hard);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 2, argv[1]) == 0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 1, 2) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 1, 64) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 2) == 1.0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 64.0 / 127.0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 1, -1) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 1, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 64.0 / 127.0);
    CHECK(__mlang_std_audio_controller_live_control(c, 16, 1, 0) == -1);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 128, 0) == -1);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 1, 128) == -1);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 129, 16384) == -1);
    // Learn is global to this controller/session, not the selected track.
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, -1, 0) == 2);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 7, 32) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, -1, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, 3 * 128 + 7, 0) == 2);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 32.0 / 127.0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 7, 127) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 2, 7, 0) == 0); // different channel, not bound
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 1);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 4) == -1); // read-only
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 40) == -1);
    CHECK(__mlang_std_audio_controller_midi_learn(c, 2048, 2, 0) == -1);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 3) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 7, 64) == 0); // replaces binding, quantized
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 3, 2) == 2.0 / 3.0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, 3 * 128 + 7, 1) == 3);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 0, 0) == 0); // cancel
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 8, 64) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, 3 * 128 + 8, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_unload_instrument(c, 2) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, -1, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, 3 * 128 + 7, 0) == 0);
    CHECK(__mlang_std_audio_controller_close(c) == 0);
    CHECK(__mlang_std_audio_pcm_block_close(b) == 0);
    std::puts("PASS: real VST3 bundle load, frame-timed MIDI, output, panic, failed replacement, reload");
}
