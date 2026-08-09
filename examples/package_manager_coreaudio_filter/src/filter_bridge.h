#pragma once

#include <cstdint>

namespace mlang::coreaudio_filter {

void reset(float sampleRateHz, float cutoffHz, float resonanceDb);
void setTarget(float cutoffHz, float resonanceDb, std::int64_t rampSamples);
void beginFrame();
float processLeft(float input);
float processRight(float input);

} // namespace mlang::coreaudio_filter
