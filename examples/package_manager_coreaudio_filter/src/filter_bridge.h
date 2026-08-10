#pragma once

#include <cstdint>

namespace mlang::coreaudio_filter {

void reset(std::int32_t filterMode, float sampleRateHz, float cutoffHz,
           float resonanceDb);
void setResonanceLimit(float maxResonanceDb);
void setTarget(float cutoffHz, float resonanceDb, std::int64_t rampSamples);
void setWetTarget(float wetMix, std::int64_t rampSamples);
void beginFrame();
float processLeft(float input);
float processRight(float input);

} // namespace mlang::coreaudio_filter
