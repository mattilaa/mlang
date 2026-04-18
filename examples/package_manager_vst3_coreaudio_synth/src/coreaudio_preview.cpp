#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "mlang_osc_bridge.h"

namespace {

using mlang_vst3_example::OscillatorBridge;
using mlang_vst3_example::Waveform;

struct PreviewState
{
    AudioQueueRef queue {nullptr};
    OscillatorBridge bridge;
    std::uint64_t framesRendered {0};
    std::uint64_t totalFrames {0};
    std::uint32_t sampleRate {48000};
    std::uint32_t channels {2};
    std::uint32_t framesPerBuffer {512};
    bool finished {false};
};

void fillBuffer(AudioQueueRef queue, AudioQueueBufferRef buffer, PreviewState* state)
{
    float* samples = static_cast<float*>(buffer->mAudioData);
    const std::uint32_t frames = state->framesPerBuffer;

    if(state->framesRendered >= state->totalFrames)
    {
        std::memset(samples, 0, frames * state->channels * sizeof(float));
        buffer->mAudioDataByteSize = frames * state->channels * sizeof(float);
        state->finished = true;
        AudioQueueStop(queue, false);
        return;
    }

    const std::uint64_t remaining = state->totalFrames - state->framesRendered;
    const std::uint32_t toRender =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, remaining));

    for(std::uint32_t frame = 0; frame < toRender; ++frame)
    {
        const float s = state->bridge.nextSample();
        for(std::uint32_t channel = 0; channel < state->channels; ++channel)
            samples[frame * state->channels + channel] = s;
    }

    if(toRender < frames)
    {
        const std::size_t tail = static_cast<std::size_t>(frames - toRender) *
                                 state->channels;
        std::memset(samples + toRender * state->channels, 0,
                    tail * sizeof(float));
    }

    buffer->mAudioDataByteSize = frames * state->channels * sizeof(float);
    state->framesRendered += toRender;
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

void outputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    auto* state = static_cast<PreviewState*>(userData);
    fillBuffer(queue, buffer, state);
}

} // namespace

int main(int argc, char** argv)
{
    Waveform waveform = Waveform::Sine;
    if(argc > 1)
    {
        const std::string arg = argv[1];
        if(arg == "square")
            waveform = Waveform::Square;
    }

    PreviewState state;
    state.totalFrames = static_cast<std::uint64_t>(state.sampleRate) * 2;

    state.bridge.reset();
    state.bridge.setSampleRate(static_cast<float>(state.sampleRate));
    state.bridge.setWaveform(waveform);
    state.bridge.noteOn(60, 0.9f);

    AudioStreamBasicDescription format {};
    format.mSampleRate = static_cast<Float64>(state.sampleRate);
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsFloat |
                          kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = state.channels * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = state.channels * sizeof(float);
    format.mChannelsPerFrame = state.channels;
    format.mBitsPerChannel = 32;

    OSStatus rc = AudioQueueNewOutput(&format, outputCallback, &state, nullptr,
                                      nullptr, 0, &state.queue);
    if(rc != noErr)
    {
        std::fprintf(stderr, "AudioQueueNewOutput failed: %d\n", static_cast<int>(rc));
        return 1;
    }

    constexpr std::uint32_t kBufferCount = 3;
    const std::uint32_t bufferBytes =
        state.framesPerBuffer * state.channels * sizeof(float);
    for(std::uint32_t i = 0; i < kBufferCount; ++i)
    {
        AudioQueueBufferRef buffer = nullptr;
        rc = AudioQueueAllocateBuffer(state.queue, bufferBytes, &buffer);
        if(rc != noErr)
        {
            std::fprintf(stderr, "AudioQueueAllocateBuffer failed: %d\n",
                         static_cast<int>(rc));
            AudioQueueDispose(state.queue, true);
            return 1;
        }
        fillBuffer(state.queue, buffer, &state);
    }

    std::printf("[coreaudio] previewing %s wave for 2 seconds\n",
                waveform == Waveform::Square ? "square" : "sine");

    rc = AudioQueueStart(state.queue, nullptr);
    if(rc != noErr)
    {
        std::fprintf(stderr, "AudioQueueStart failed: %d\n", static_cast<int>(rc));
        AudioQueueDispose(state.queue, true);
        return 1;
    }

    while(!state.finished)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    state.bridge.noteOff();
    AudioQueueDispose(state.queue, true);
    return 0;
}
