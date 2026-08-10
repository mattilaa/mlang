#include "filter_bridge.h"

extern "C" void mlang_dsp_filter_reset__i32_f32_f32_f32(
    std::int32_t filterMode, float sampleRateHz, float cutoffHz,
    float resonanceDb);
extern "C" void mlang_dsp_filter_set_resonance_limit__f32(
    float maxResonanceDb);
extern "C" void mlang_dsp_filter_set_target__f32_f32_i64(
    float cutoffHz, float resonanceDb, std::int64_t rampSamples);
extern "C" void mlang_dsp_filter_set_wet_target__f32_i64(
    float wetMix, std::int64_t rampSamples);
extern "C" void mlang_dsp_filter_begin_frame__void();
extern "C" float mlang_dsp_filter_process_left__f32(float input);
extern "C" float mlang_dsp_filter_process_right__f32(float input);
extern "C" float mlang_dsp_interpolate_nearest__f32_f32_f32(
    float x0, float x1, float fraction);
extern "C" float mlang_dsp_interpolate_linear__f32_f32_f32(
    float x0, float x1, float fraction);
extern "C" float mlang_dsp_interpolate_hermite__f32_f32_f32_f32_f32(
    float xm1, float x0, float x1, float x2, float fraction);

namespace mlang::coreaudio_filter {

void setResonanceLimit(float maxResonanceDb)
{
    mlang_dsp_filter_set_resonance_limit__f32(maxResonanceDb);
}

void reset(std::int32_t filterMode, float sampleRateHz, float cutoffHz,
           float resonanceDb)
{
    mlang_dsp_filter_reset__i32_f32_f32_f32(
        filterMode, sampleRateHz, cutoffHz, resonanceDb);
}

void setTarget(float cutoffHz, float resonanceDb, std::int64_t rampSamples)
{
    mlang_dsp_filter_set_target__f32_f32_i64(
        cutoffHz, resonanceDb, rampSamples);
}

void setWetTarget(float wetMix, std::int64_t rampSamples)
{
    mlang_dsp_filter_set_wet_target__f32_i64(wetMix, rampSamples);
}

float interpolateNearest(float x0, float x1, float fraction)
{
    return mlang_dsp_interpolate_nearest__f32_f32_f32(x0, x1, fraction);
}

float interpolateLinear(float x0, float x1, float fraction)
{
    return mlang_dsp_interpolate_linear__f32_f32_f32(x0, x1, fraction);
}

float interpolateHermite(float xm1, float x0, float x1, float x2,
                         float fraction)
{
    return mlang_dsp_interpolate_hermite__f32_f32_f32_f32_f32(
        xm1, x0, x1, x2, fraction);
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
