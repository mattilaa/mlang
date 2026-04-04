#include <cstdio>
#include <cmath>
#include "AudioFile.h"

extern "C" int ml_audio_print_analysis(const char* path)
{
    AudioFile<double> audioFile;
    if(!audioFile.load(path))
    {
        std::fprintf(stderr,
                     "[c++] AudioFile failed to load sample: %s\n",
                     path);
        return 1;
    }

    const int channels = audioFile.getNumChannels();
    const int samplesPerChannel = audioFile.getNumSamplesPerChannel();
    double peak = 0.0;

    for(int channel = 0; channel < channels; ++channel)
    {
        for(int index = 0; index < samplesPerChannel; ++index)
        {
            const double value = std::fabs(audioFile.samples[channel][index]);
            if(value > peak)
                peak = value;
        }
    }

    std::printf("[c++] Loaded %s\n", path);
    std::printf("[c++] Sample rate: %d Hz\n", audioFile.getSampleRate());
    std::printf("[c++] Bit depth: %d-bit\n", audioFile.getBitDepth());
    std::printf("[c++] Channels: %d\n", channels);
    std::printf("[c++] Samples / channel: %d\n", samplesPerChannel);
    std::printf("[c++] Length: %.3f seconds\n",
                audioFile.getLengthInSeconds());
    std::printf("[c++] Peak amplitude: %.6f\n", peak);
    return 0;
}
