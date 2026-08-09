#include "filter_bridge.h"

extern "C" void mlang_dsp_filter_reset__f32_f32_f32(
    float sampleRateHz, float cutoffHz, float resonanceDb);
extern "C" void mlang_dsp_filter_set_target__f32_f32_i64(
    float cutoffHz, float resonanceDb, std::int64_t rampSamples);
extern "C" void mlang_dsp_filter_begin_frame__void();
extern "C" float mlang_dsp_filter_process_left__f32(float input);
extern "C" float mlang_dsp_filter_process_right__f32(float input);

namespace mlang::coreaudio_filter {

void reset(float sampleRateHz, float cutoffHz, float resonanceDb)
{
    mlang_dsp_filter_reset__f32_f32_f32(sampleRateHz, cutoffHz, resonanceDb);
}

void setTarget(float cutoffHz, float resonanceDb, std::int64_t rampSamples)
{
    mlang_dsp_filter_set_target__f32_f32_i64(
        cutoffHz, resonanceDb, rampSamples);
}

void beginFrame()
{
    mlang_dsp_filter_begin_frame__void();
}

float processLeft(float input)
{
    return mlang_dsp_filter_process_left__f32(input);
}

float processRight(float input)
{
    return mlang_dsp_filter_process_right__f32(input);
}

} // namespace mlang::coreaudio_filter
