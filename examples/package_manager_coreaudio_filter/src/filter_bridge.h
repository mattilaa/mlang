#pragma once

#include <cstdint>

namespace mlang::coreaudio_filter {

void reset(std::int32_t filterMode, float sampleRateHz, float cutoffHz,
           float resonanceDb);
void setResonanceLimit(float maxResonanceDb);
void setTarget(float cutoffHz, float resonanceDb, std::int64_t rampSamples);
void setWetTarget(float wetMix, std::int64_t rampSamples);
float interpolateNearest(float x0, float x1, float fraction);
float interpolateLinear(float x0, float x1, float fraction);
float interpolateHermite(float xm1, float x0, float x1, float x2,
                         float fraction);
void beginFrame();
float processLeft(float input);
float processRight(float input);

} // namespace mlang::coreaudio_filter
