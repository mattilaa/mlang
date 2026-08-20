#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

typedef struct mlang_audio_device mlang_audio_device_t;
typedef struct mlang_pcm_audio mlang_pcm_audio_t;
typedef struct mlang_pcm_block mlang_pcm_block_t;
typedef struct mlang_pcm_wav_writer mlang_pcm_wav_writer_t;

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

int32_t __mlang_std_audio_close(int64_t handle);

struct mlang_audio_device
{
    int backend;
    int running;
    int64_t device_id;
    double sample_rate;
    int64_t buffer_frames;
    double phase;
    double frequency_hz;
    double gain;
    int64_t frames_left;
    float* pcm_ring;
    uint64_t pcm_capacity_frames;
    _Atomic uint64_t pcm_read_frame;
    _Atomic uint64_t pcm_write_frame;
    _Atomic uint64_t pcm_underruns;
    _Atomic int source_mode;
#if defined(__APPLE__)
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[3];
#elif defined(__linux__)
    void* jack_lib;
    void* jack_client;
    void* out_l;
    void* out_r;
#endif
};

struct mlang_pcm_audio
{
    float* samples;
    int64_t sample_rate;
    int64_t channels;
    int64_t frame_count;
};

struct mlang_pcm_block
{
    float* samples;
    int64_t capacity_frames;
};

struct mlang_pcm_wav_writer
{
    FILE* file;
    uint32_t sample_rate;
    uint64_t frames_written;
};

static char g_audio_last_error[512];
static char g_audio_device_name[512];

static void audio_set_error(const char* msg)
{
    (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error), "%s",
                   msg ? msg : "std::audio: unknown error");
}

static void audio_clear_error(void)
{
    g_audio_last_error[0] = '\0';
}

const char* __mlang_std_audio_last_error(void)
{
    return g_audio_last_error;
}

const char* __mlang_std_audio_backend_name(void)
{
#if defined(__APPLE__)
    return "coreaudio";
#elif defined(__linux__)
    return "jack2";
#else
    return "unsupported";
#endif
}

static int64_t audio_normalize_sample_rate(int64_t sample_rate)
{
    if(sample_rate <= 0)
        return 48000;
    if(sample_rate < 8000)
        return 8000;
    if(sample_rate > 384000)
        return 384000;
    return sample_rate;
}

static int64_t audio_normalize_buffer_frames(int64_t buffer_frames)
{
    if(buffer_frames <= 0)
        return 512;
    if(buffer_frames < 16)
        return 16;
    if(buffer_frames > 32768)
        return 32768;
    return buffer_frames;
}

static float audio_next_sample(mlang_audio_device_t* d)
{
    if(!d || !d->running || d->frames_left == 0)
        return 0.0f;

    const double two_pi = 6.283185307179586476925286766559;
    float out = (float)(sin(d->phase) * d->gain);
    d->phase += two_pi * d->frequency_hz / d->sample_rate;
    if(d->phase >= two_pi)
        d->phase -= two_pi;
    if(d->frames_left > 0)
    {
        --d->frames_left;
        if(d->frames_left == 0)
            d->running = 0;
    }
    return out;
}

static uint64_t audio_pcm_queued_frames(const mlang_audio_device_t* d)
{
    if(!d || !d->pcm_ring || d->pcm_capacity_frames == 0)
        return 0;
    const uint64_t read_frame = atomic_load_explicit(
        &d->pcm_read_frame, memory_order_acquire);
    const uint64_t write_frame = atomic_load_explicit(
        &d->pcm_write_frame, memory_order_acquire);
    const uint64_t queued = write_frame - read_frame;
    return queued > d->pcm_capacity_frames ? d->pcm_capacity_frames : queued;
}

static void audio_render_frames(mlang_audio_device_t* d, float* interleaved,
                                float* left, float* right, uint64_t frames)
{
    if(!d)
        return;
    uint64_t read_frame = atomic_load_explicit(
        &d->pcm_read_frame, memory_order_relaxed);
    const uint64_t write_frame = atomic_load_explicit(
        &d->pcm_write_frame, memory_order_acquire);
    const int mode = atomic_load_explicit(&d->source_mode, memory_order_acquire);
    int underrun = 0;

    for(uint64_t i = 0; i < frames; ++i)
    {
        float sample_l = 0.0f;
        float sample_r = 0.0f;
        if(mode == 2 && read_frame < write_frame)
        {
            const uint64_t slot = read_frame % d->pcm_capacity_frames;
            sample_l = d->pcm_ring[slot * 2];
            sample_r = d->pcm_ring[slot * 2 + 1];
            ++read_frame;
        }
        else if(mode == 1)
        {
            sample_l = audio_next_sample(d);
            sample_r = sample_l;
        }
        else if(mode == 2)
        {
            underrun = 1;
        }

        if(interleaved)
        {
            interleaved[i * 2] = sample_l;
            interleaved[i * 2 + 1] = sample_r;
        }
        else
        {
            left[i] = sample_l;
            right[i] = sample_r;
        }
    }

    atomic_store_explicit(&d->pcm_read_frame, read_frame, memory_order_release);
    if(underrun)
        (void)atomic_fetch_add_explicit(
            &d->pcm_underruns, 1u, memory_order_relaxed);
}

static uint16_t audio_read_u16_le(const unsigned char* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t audio_read_u32_le(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void audio_write_u16_le(unsigned char* p, uint16_t value)
{
    p[0] = (unsigned char)(value & 0xffu);
    p[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void audio_write_u32_le(unsigned char* p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffu);
    p[1] = (unsigned char)((value >> 8) & 0xffu);
    p[2] = (unsigned char)((value >> 16) & 0xffu);
    p[3] = (unsigned char)((value >> 24) & 0xffu);
}

static int audio_write_wav_header(FILE* file, uint32_t sample_rate,
                                  uint32_t data_bytes)
{
    unsigned char header[44] = {0};
    memcpy(header, "RIFF", 4);
    audio_write_u32_le(header + 4, 36u + data_bytes);
    memcpy(header + 8, "WAVEfmt ", 8);
    audio_write_u32_le(header + 16, 16u);
    audio_write_u16_le(header + 20, 1u);
    audio_write_u16_le(header + 22, 2u);
    audio_write_u32_le(header + 24, sample_rate);
    audio_write_u32_le(header + 28, sample_rate * 4u);
    audio_write_u16_le(header + 32, 4u);
    audio_write_u16_le(header + 34, 16u);
    memcpy(header + 36, "data", 4);
    audio_write_u32_le(header + 40, data_bytes);
    return fwrite(header, 1u, sizeof(header), file) == sizeof(header) ? 0 : -1;
}

static uint16_t audio_read_u16_be(const unsigned char* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t audio_read_u32_be(const unsigned char* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static double audio_read_extended80(const unsigned char* p)
{
    const uint16_t sign_exponent = audio_read_u16_be(p);
    const uint16_t exponent = sign_exponent & 0x7fffu;
    uint64_t mantissa = 0;
    if((sign_exponent & 0x8000u) != 0 || exponent == 0x7fffu)
        return 0.0;
    for(int i = 0; i < 8; ++i)
        mantissa = (mantissa << 8) | p[2 + i];
    if(exponent == 0 && mantissa == 0)
        return 0.0;
    return ldexp((double)mantissa, (int)exponent - 16383 - 63);
}

static char* audio_expand_path(const char* path)
{
    if(!path)
        return NULL;
    if(path[0] == '~' && path[1] == '/')
    {
        const char* home = getenv("HOME");
        if(home && home[0])
        {
            const size_t home_len = strlen(home);
            const size_t tail_len = strlen(path + 1);
            char* expanded = (char*)malloc(home_len + tail_len + 1u);
            if(!expanded)
                return NULL;
            memcpy(expanded, home, home_len);
            memcpy(expanded + home_len, path + 1, tail_len + 1u);
            return expanded;
        }
    }
    const size_t len = strlen(path);
    char* copy = (char*)malloc(len + 1u);
    if(copy)
        memcpy(copy, path, len + 1u);
    return copy;
}

static unsigned char* audio_read_file(const char* path, size_t* out_size)
{
    *out_size = 0;
    char* expanded = audio_expand_path(path);
    if(!expanded)
    {
        audio_set_error("std::audio PCM path allocation failed");
        return NULL;
    }
    FILE* file = fopen(expanded, "rb");
    if(!file)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio cannot open %s", expanded);
        free(expanded);
        return NULL;
    }
    free(expanded);
    if(fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        audio_set_error("std::audio failed to seek PCM file");
        return NULL;
    }
    const long file_size = ftell(file);
    if(file_size < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        audio_set_error("std::audio failed to size PCM file");
        return NULL;
    }
    unsigned char* bytes = (unsigned char*)malloc(
        file_size > 0 ? (size_t)file_size : 1u);
    if(!bytes)
    {
        fclose(file);
        audio_set_error("std::audio PCM file allocation failed");
        return NULL;
    }
    const size_t size = (size_t)file_size;
    if(size > 0 && fread(bytes, 1u, size, file) != size)
    {
        free(bytes);
        fclose(file);
        audio_set_error("std::audio failed to read PCM file");
        return NULL;
    }
    fclose(file);
    *out_size = size;
    return bytes;
}

static int audio_decode_wav(mlang_pcm_audio_t* out,
                            const unsigned char* bytes, size_t size)
{
    uint16_t format = 0;
    uint16_t bits = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    const unsigned char* pcm = NULL;
    size_t pcm_bytes = 0;
    size_t offset = 12;
    if(size < 12 || memcmp(bytes, "RIFF", 4) != 0 ||
       memcmp(bytes + 8, "WAVE", 4) != 0)
        return -1;
    while(offset + 8 <= size)
    {
        const uint32_t chunk_size = audio_read_u32_le(bytes + offset + 4);
        const size_t data_offset = offset + 8;
        if(data_offset > size || chunk_size > size - data_offset)
            break;
        if(memcmp(bytes + offset, "fmt ", 4) == 0 && chunk_size >= 16)
        {
            format = audio_read_u16_le(bytes + data_offset);
            channels = audio_read_u16_le(bytes + data_offset + 2);
            sample_rate = audio_read_u32_le(bytes + data_offset + 4);
            bits = audio_read_u16_le(bytes + data_offset + 14);
        }
        else if(memcmp(bytes + offset, "data", 4) == 0)
        {
            pcm = bytes + data_offset;
            pcm_bytes = chunk_size;
        }
        offset = data_offset + chunk_size + (chunk_size & 1u);
    }
    if(format != 1 || bits != 16 || channels < 1 || channels > 2 ||
       sample_rate == 0 || !pcm)
        return -1;
    const size_t sample_count = pcm_bytes / 2u;
    if(sample_count == 0 || sample_count % channels != 0)
        return -1;
    out->samples = (float*)malloc(sample_count * sizeof(float));
    if(!out->samples)
        return -2;
    for(size_t i = 0; i < sample_count; ++i)
        out->samples[i] = (float)(int16_t)audio_read_u16_le(pcm + i * 2u) /
                          32768.0f;
    out->sample_rate = sample_rate;
    out->channels = channels;
    out->frame_count = (int64_t)(sample_count / channels);
    return 0;
}

static int audio_decode_aiff(mlang_pcm_audio_t* out,
                             const unsigned char* bytes, size_t size)
{
    const int is_aiff = size >= 12 && memcmp(bytes, "FORM", 4) == 0 &&
                        memcmp(bytes + 8, "AIFF", 4) == 0;
    const int is_aifc = size >= 12 && memcmp(bytes, "FORM", 4) == 0 &&
                        memcmp(bytes + 8, "AIFC", 4) == 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t declared_frames = 0;
    int64_t sample_rate = 0;
    int found_common = 0;
    int supported_compression = is_aiff;
    int little_endian = 0;
    const unsigned char* pcm = NULL;
    size_t pcm_bytes = 0;
    size_t offset = 12;
    if(!is_aiff && !is_aifc)
        return -1;
    while(offset + 8 <= size)
    {
        const uint32_t chunk_size = audio_read_u32_be(bytes + offset + 4);
        const size_t data_offset = offset + 8;
        if(data_offset > size || chunk_size > size - data_offset)
            break;
        if(memcmp(bytes + offset, "COMM", 4) == 0 && chunk_size >= 18)
        {
            found_common = 1;
            channels = audio_read_u16_be(bytes + data_offset);
            declared_frames = audio_read_u32_be(bytes + data_offset + 2);
            bits = audio_read_u16_be(bytes + data_offset + 6);
            const double rate = audio_read_extended80(bytes + data_offset + 8);
            if(isfinite(rate) && rate >= 1.0 && rate <= 1000000.0)
                sample_rate = (int64_t)llround(rate);
            if(is_aifc && chunk_size >= 22)
            {
                const unsigned char* compression = bytes + data_offset + 18;
                supported_compression = memcmp(compression, "NONE", 4) == 0 ||
                                        memcmp(compression, "twos", 4) == 0 ||
                                        memcmp(compression, "sowt", 4) == 0;
                little_endian = memcmp(compression, "sowt", 4) == 0;
            }
        }
        else if(memcmp(bytes + offset, "SSND", 4) == 0 && chunk_size >= 8)
        {
            const uint32_t sound_offset = audio_read_u32_be(bytes + data_offset);
            if(sound_offset <= chunk_size - 8)
            {
                pcm = bytes + data_offset + 8 + sound_offset;
                pcm_bytes = chunk_size - 8 - sound_offset;
            }
        }
        offset = data_offset + chunk_size + (chunk_size & 1u);
    }
    if(!found_common || !supported_compression || bits != 16 ||
       channels < 1 || channels > 2 || sample_rate == 0 || !pcm)
        return -1;
    size_t sample_count = pcm_bytes / 2u;
    const uint64_t declared_samples = (uint64_t)declared_frames * channels;
    if(declared_frames > 0 && declared_samples < sample_count)
        sample_count = (size_t)declared_samples;
    if(sample_count == 0 || sample_count % channels != 0)
        return -1;
    out->samples = (float*)malloc(sample_count * sizeof(float));
    if(!out->samples)
        return -2;
    for(size_t i = 0; i < sample_count; ++i)
    {
        const uint16_t encoded = little_endian
            ? audio_read_u16_le(pcm + i * 2u)
            : audio_read_u16_be(pcm + i * 2u);
        out->samples[i] = (float)(int16_t)encoded / 32768.0f;
    }
    out->sample_rate = sample_rate;
    out->channels = channels;
    out->frame_count = (int64_t)(sample_count / channels);
    return 0;
}

int64_t __mlang_std_audio_pcm_load(const char* path)
{
    size_t size = 0;
    unsigned char* bytes = audio_read_file(path, &size);
    if(!bytes)
        return 0;
    mlang_pcm_audio_t* audio = (mlang_pcm_audio_t*)calloc(1u, sizeof(*audio));
    if(!audio)
    {
        free(bytes);
        audio_set_error("std::audio PCM object allocation failed");
        return 0;
    }
    int rc = -1;
    if(size >= 12 && memcmp(bytes, "RIFF", 4) == 0)
        rc = audio_decode_wav(audio, bytes, size);
    else if(size >= 12 && memcmp(bytes, "FORM", 4) == 0)
        rc = audio_decode_aiff(audio, bytes, size);
    free(bytes);
    if(rc != 0)
    {
        free(audio->samples);
        free(audio);
        audio_set_error(rc == -2
            ? "std::audio PCM sample allocation failed"
            : "std::audio requires mono/stereo 16-bit PCM WAV, AIFF, or AIFF-C");
        return 0;
    }
    audio_clear_error();
    return (int64_t)(intptr_t)audio;
}

int64_t __mlang_std_audio_pcm_file_sample_rate(int64_t handle)
{
    const mlang_pcm_audio_t* audio = (const mlang_pcm_audio_t*)(intptr_t)handle;
    return audio ? audio->sample_rate : 0;
}

int64_t __mlang_std_audio_pcm_file_channels(int64_t handle)
{
    const mlang_pcm_audio_t* audio = (const mlang_pcm_audio_t*)(intptr_t)handle;
    return audio ? audio->channels : 0;
}

int64_t __mlang_std_audio_pcm_file_frame_count(int64_t handle)
{
    const mlang_pcm_audio_t* audio = (const mlang_pcm_audio_t*)(intptr_t)handle;
    return audio ? audio->frame_count : 0;
}

mlang_list_t __mlang_std_audio_pcm_file_samples(int64_t handle)
{
    mlang_list_t out = {0, NULL};
    const mlang_pcm_audio_t* audio = (const mlang_pcm_audio_t*)(intptr_t)handle;
    if(!audio || !audio->samples)
    {
        audio_set_error("std::audio PCM samples: invalid handle");
        return out;
    }
    const int64_t count = audio->frame_count * audio->channels;
    out.data = malloc((size_t)count * sizeof(float));
    if(!out.data)
    {
        audio_set_error("std::audio PCM sample copy allocation failed");
        return out;
    }
    memcpy(out.data, audio->samples, (size_t)count * sizeof(float));
    out.size = count;
    audio_clear_error();
    return out;
}

int32_t __mlang_std_audio_pcm_file_close(int64_t handle)
{
    mlang_pcm_audio_t* audio = (mlang_pcm_audio_t*)(intptr_t)handle;
    if(!audio)
        return 0;
    free(audio->samples);
    free(audio);
    return 0;
}

int64_t __mlang_std_audio_pcm_block_new(int64_t capacity_frames)
{
    if(capacity_frames <= 0 || capacity_frames > 1048576)
    {
        audio_set_error("std::audio PCM block capacity is invalid");
        return 0;
    }
    mlang_pcm_block_t* block = (mlang_pcm_block_t*)calloc(1u, sizeof(*block));
    if(!block)
    {
        audio_set_error("std::audio PCM block allocation failed");
        return 0;
    }
    block->samples = (float*)calloc(
        (size_t)capacity_frames * 2u, sizeof(float));
    if(!block->samples)
    {
        free(block);
        audio_set_error("std::audio PCM block sample allocation failed");
        return 0;
    }
    block->capacity_frames = capacity_frames;
    audio_clear_error();
    return (int64_t)(intptr_t)block;
}

int64_t __mlang_std_audio_pcm_block_capacity_frames(int64_t handle)
{
    const mlang_pcm_block_t* block =
        (const mlang_pcm_block_t*)(intptr_t)handle;
    return block ? block->capacity_frames : 0;
}

int32_t __mlang_std_audio_pcm_block_set_stereo(
    int64_t handle, int64_t frame, float left, float right)
{
    mlang_pcm_block_t* block = (mlang_pcm_block_t*)(intptr_t)handle;
    if(!block || !block->samples || frame < 0 ||
       frame >= block->capacity_frames)
    {
        audio_set_error("std::audio PCM block frame is out of range");
        return -1;
    }
    block->samples[frame * 2] = left;
    block->samples[frame * 2 + 1] = right;
    return 0;
}

int32_t __mlang_std_audio_pcm_block_clear(int64_t handle)
{
    mlang_pcm_block_t* block = (mlang_pcm_block_t*)(intptr_t)handle;
    if(!block || !block->samples)
        return -1;
    memset(block->samples, 0,
           (size_t)block->capacity_frames * 2u * sizeof(float));
    return 0;
}

int32_t __mlang_std_audio_pcm_block_close(int64_t handle)
{
    mlang_pcm_block_t* block = (mlang_pcm_block_t*)(intptr_t)handle;
    if(!block)
        return 0;
    free(block->samples);
    free(block);
    return 0;
}

int64_t __mlang_std_audio_pcm_wav_writer_new(const char* path,
                                             int64_t sample_rate)
{
    if(!path || !path[0] || sample_rate < 8000 || sample_rate > 384000)
    {
        audio_set_error("std::audio WAV writer path or sample rate is invalid");
        return 0;
    }
    char* expanded = audio_expand_path(path);
    if(!expanded)
    {
        audio_set_error("std::audio WAV writer path allocation failed");
        return 0;
    }
    FILE* file = fopen(expanded, "wb+");
    if(!file)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio cannot create %s", expanded);
        free(expanded);
        return 0;
    }
    free(expanded);
    mlang_pcm_wav_writer_t* writer =
        (mlang_pcm_wav_writer_t*)calloc(1u, sizeof(*writer));
    if(!writer)
    {
        fclose(file);
        audio_set_error("std::audio WAV writer allocation failed");
        return 0;
    }
    writer->file = file;
    writer->sample_rate = (uint32_t)sample_rate;
    if(audio_write_wav_header(file, writer->sample_rate, 0u) != 0)
    {
        fclose(file);
        free(writer);
        audio_set_error("std::audio failed to write WAV header");
        return 0;
    }
    audio_clear_error();
    return (int64_t)(intptr_t)writer;
}

int64_t __mlang_std_audio_pcm_wav_writer_write_block(
    int64_t writer_handle, int64_t block_handle, int64_t frames)
{
    mlang_pcm_wav_writer_t* writer =
        (mlang_pcm_wav_writer_t*)(intptr_t)writer_handle;
    const mlang_pcm_block_t* block =
        (const mlang_pcm_block_t*)(intptr_t)block_handle;
    if(!writer || !writer->file || !block || !block->samples || frames < 0 ||
       frames > block->capacity_frames)
    {
        audio_set_error("std::audio WAV writer block or frame count is invalid");
        return -1;
    }
    if(writer->frames_written + (uint64_t)frames >
       ((uint64_t)UINT32_MAX - 36u) / 4u)
    {
        audio_set_error("std::audio WAV output exceeds the 4 GiB RIFF limit");
        return -1;
    }
    unsigned char encoded[4096];
    int64_t frame_offset = 0;
    while(frame_offset < frames)
    {
        int64_t chunk_frames = frames - frame_offset;
        if(chunk_frames > 1024)
            chunk_frames = 1024;
        for(int64_t frame = 0; frame < chunk_frames; ++frame)
        {
            for(int channel = 0; channel < 2; ++channel)
            {
                float sample = block->samples[(frame_offset + frame) * 2 + channel];
                if(sample < -1.0f)
                    sample = -1.0f;
                else if(sample > 1.0f)
                    sample = 1.0f;
                const int16_t pcm = (int16_t)lrintf(sample * 32767.0f);
                audio_write_u16_le(encoded + (frame * 2 + channel) * 2,
                                   (uint16_t)pcm);
            }
        }
        const size_t bytes = (size_t)chunk_frames * 4u;
        if(fwrite(encoded, 1u, bytes, writer->file) != bytes)
        {
            audio_set_error("std::audio failed to write WAV samples");
            return -1;
        }
        frame_offset += chunk_frames;
    }
    writer->frames_written += (uint64_t)frames;
    audio_clear_error();
    return frames;
}

int64_t __mlang_std_audio_pcm_wav_writer_frames_written(int64_t handle)
{
    const mlang_pcm_wav_writer_t* writer =
        (const mlang_pcm_wav_writer_t*)(intptr_t)handle;
    return writer ? (int64_t)writer->frames_written : 0;
}

int32_t __mlang_std_audio_pcm_wav_writer_close(int64_t handle)
{
    mlang_pcm_wav_writer_t* writer =
        (mlang_pcm_wav_writer_t*)(intptr_t)handle;
    if(!writer)
        return 0;
    const uint32_t data_bytes = (uint32_t)(writer->frames_written * 4u);
    int failed = fseek(writer->file, 0, SEEK_SET) != 0 ||
                 audio_write_wav_header(writer->file, writer->sample_rate,
                                        data_bytes) != 0;
    if(fclose(writer->file) != 0)
        failed = 1;
    free(writer);
    if(failed)
    {
        audio_set_error("std::audio failed to finalize WAV output");
        return -1;
    }
    audio_clear_error();
    return 0;
}

#if defined(__APPLE__)
static int coreaudio_device_has_output(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioDevicePropertyStreamConfiguration;
    addr.mScope = kAudioDevicePropertyScopeOutput;
    addr.mElement = kAudioObjectPropertyElementMain;

    UInt32 size = 0;
    if(AudioObjectGetPropertyDataSize(id, &addr, 0, NULL, &size) != noErr || size == 0)
        return 0;

    AudioBufferList* list = (AudioBufferList*)malloc(size);
    if(!list)
        return 0;
    if(AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, list) != noErr)
    {
        free(list);
        return 0;
    }

    UInt32 channels = 0;
    for(UInt32 i = 0; i < list->mNumberBuffers; ++i)
        channels += list->mBuffers[i].mNumberChannels;
    free(list);
    return channels > 0 ? 1 : 0;
}

static int coreaudio_all_devices(AudioDeviceID** out_ids, UInt32* out_count)
{
    *out_ids = NULL;
    *out_count = 0;
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioHardwarePropertyDevices;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;

    UInt32 size = 0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size) != noErr || size == 0)
        return -1;

    AudioDeviceID* ids = (AudioDeviceID*)malloc(size);
    if(!ids)
        return -1;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, ids) != noErr)
    {
        free(ids);
        return -1;
    }
    *out_ids = ids;
    *out_count = size / (UInt32)sizeof(AudioDeviceID);
    return 0;
}

static AudioDeviceID coreaudio_device_for_index(int64_t device_id)
{
    AudioDeviceID* ids = NULL;
    UInt32 count = 0;
    if(coreaudio_all_devices(&ids, &count) != 0)
        return kAudioObjectUnknown;

    int64_t out_index = 0;
    AudioDeviceID selected = kAudioObjectUnknown;
    for(UInt32 i = 0; i < count; ++i)
    {
        if(!coreaudio_device_has_output(ids[i]))
            continue;
        if(out_index == device_id)
        {
            selected = ids[i];
            break;
        }
        ++out_index;
    }
    free(ids);
    return selected;
}

static int64_t coreaudio_index_for_device(AudioDeviceID wanted)
{
    AudioDeviceID* ids = NULL;
    UInt32 count = 0;
    if(coreaudio_all_devices(&ids, &count) != 0)
        return -1;

    int64_t out_index = 0;
    for(UInt32 i = 0; i < count; ++i)
    {
        if(!coreaudio_device_has_output(ids[i]))
            continue;
        if(ids[i] == wanted)
        {
            free(ids);
            return out_index;
        }
        ++out_index;
    }
    free(ids);
    return -1;
}

static int64_t coreaudio_default_output_index(void)
{
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, &device) != noErr)
        return -1;
    return coreaudio_index_for_device(device);
}

static int coreaudio_device_name(int64_t device_id, char* out, size_t out_size)
{
    AudioDeviceID id = coreaudio_device_for_index(device_id);
    if(id == kAudioObjectUnknown)
        return -1;

    CFStringRef name = NULL;
    UInt32 size = sizeof(name);
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioObjectPropertyName;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;
    if(AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &name) != noErr || !name)
        return -1;
    Boolean ok = CFStringGetCString(name, out, out_size, kCFStringEncodingUTF8);
    CFRelease(name);
    return ok ? 0 : -1;
}

static void coreaudio_apply_nominal_sample_rate(mlang_audio_device_t* d, AudioDeviceID id)
{
    Float64 sr = 0.0;
    UInt32 size = sizeof(sr);
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioDevicePropertyNominalSampleRate;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;
    if(AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &sr) == noErr && sr > 1000.0)
        d->sample_rate = (double)sr;
}

static int coreaudio_set_queue_device(AudioQueueRef queue, AudioDeviceID id)
{
    if(id == kAudioObjectUnknown)
        return 0;
    CFStringRef uid = NULL;
    UInt32 size = sizeof(uid);
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioDevicePropertyDeviceUID;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;
    if(AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &uid) != noErr || !uid)
        return -1;
    OSStatus rc = AudioQueueSetProperty(queue, kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
    CFRelease(uid);
    return rc == noErr ? 0 : -1;
}

static void audioqueue_fill(mlang_audio_device_t* d, AudioQueueBufferRef buffer)
{
    if(!d || !buffer)
        return;
    const int64_t frames = d->buffer_frames > 0 ? d->buffer_frames : 512;
    float* samples = (float*)buffer->mAudioData;
    audio_render_frames(d, samples, NULL, NULL, (uint64_t)frames);
    buffer->mAudioDataByteSize = (UInt32)(frames * 2 * (int64_t)sizeof(float));
}

static void audioqueue_callback(void* user_data, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)user_data;
    audioqueue_fill(d, buffer);
    (void)AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

static int audio_coreaudio_open(mlang_audio_device_t* d, int64_t device_id, int requested_sample_rate)
{
    d->backend = 1;
    d->device_id = device_id;

    AudioDeviceID selected = kAudioObjectUnknown;
    if(device_id >= 0)
    {
        selected = coreaudio_device_for_index(device_id);
        if(selected == kAudioObjectUnknown)
        {
            audio_set_error("std::audio CoreAudio output device id is invalid");
            return -1;
        }
        if(!requested_sample_rate)
            coreaudio_apply_nominal_sample_rate(d, selected);
    }

    AudioStreamBasicDescription fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.mSampleRate = d->sample_rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket = 2u * (UInt32)sizeof(float);
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 2u * (UInt32)sizeof(float);
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;

    OSStatus rc = AudioQueueNewOutput(&fmt, audioqueue_callback, d, NULL, NULL, 0, &d->queue);
    if(rc != noErr)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio CoreAudio AudioQueueNewOutput failed: %d", (int)rc);
        return -1;
    }

    if(selected != kAudioObjectUnknown && coreaudio_set_queue_device(d->queue, selected) != 0)
    {
        audio_set_error("std::audio CoreAudio failed to select output device");
        return -1;
    }

    UInt32 buffer_bytes = (UInt32)(d->buffer_frames * 2 * (int64_t)sizeof(float));
    for(int i = 0; i < 3; ++i)
    {
        rc = AudioQueueAllocateBuffer(d->queue, buffer_bytes, &d->buffers[i]);
        if(rc != noErr)
        {
            (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                           "std::audio CoreAudio AudioQueueAllocateBuffer failed: %d", (int)rc);
            return -1;
        }
        audioqueue_fill(d, d->buffers[i]);
        rc = AudioQueueEnqueueBuffer(d->queue, d->buffers[i], 0, NULL);
        if(rc != noErr)
        {
            (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                           "std::audio CoreAudio AudioQueueEnqueueBuffer failed: %d", (int)rc);
            return -1;
        }
    }
    return 0;
}
#endif

#if defined(__linux__)
typedef uint32_t jack_nframes_t;
typedef unsigned long jack_options_t;
typedef unsigned long jack_status_t;
typedef int (*JackProcessCallback)(jack_nframes_t, void*);

#define MLANG_JACK_NULL_OPTION 0
#define MLANG_JACK_PORT_IS_INPUT 0x1UL
#define MLANG_JACK_PORT_IS_OUTPUT 0x2UL
#define MLANG_JACK_PORT_IS_PHYSICAL 0x4UL
#define MLANG_JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"

typedef void* (*jack_client_open_fn)(const char*, jack_options_t, jack_status_t*);
typedef int (*jack_client_close_fn)(void*);
typedef int (*jack_activate_fn)(void*);
typedef int (*jack_deactivate_fn)(void*);
typedef void* (*jack_port_register_fn)(void*, const char*, const char*, unsigned long, unsigned long);
typedef int (*jack_set_process_callback_fn)(void*, JackProcessCallback, void*);
typedef jack_nframes_t (*jack_get_sample_rate_fn)(void*);
typedef jack_nframes_t (*jack_get_buffer_size_fn)(void*);
typedef int (*jack_set_buffer_size_fn)(void*, jack_nframes_t);
typedef void* (*jack_port_get_buffer_fn)(void*, jack_nframes_t);
typedef const char* (*jack_port_name_fn)(const void*);
typedef const char** (*jack_get_ports_fn)(void*, const char*, const char*, unsigned long);
typedef int (*jack_connect_fn)(void*, const char*, const char*);
typedef void (*jack_free_fn)(void*);

static jack_client_close_fn p_jack_client_close;
static jack_activate_fn p_jack_activate;
static jack_deactivate_fn p_jack_deactivate;
static jack_port_get_buffer_fn p_jack_port_get_buffer;
static jack_port_name_fn p_jack_port_name;
static jack_get_ports_fn p_jack_get_ports;
static jack_connect_fn p_jack_connect;
static jack_free_fn p_jack_free;

static void* jack_sym(void* lib, const char* name)
{
    void* p = dlsym(lib, name);
    if(!p)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio JACK2 missing symbol: %s", name);
    }
    return p;
}

static int jack_process(jack_nframes_t nframes, void* arg)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)arg;
    float* out_l = p_jack_port_get_buffer ? (float*)p_jack_port_get_buffer(d->out_l, nframes) : NULL;
    float* out_r = p_jack_port_get_buffer ? (float*)p_jack_port_get_buffer(d->out_r, nframes) : NULL;
    if(!out_l || !out_r)
        return 0;
    audio_render_frames(d, NULL, out_l, out_r, (uint64_t)nframes);
    return 0;
}

static int audio_jack_open(mlang_audio_device_t* d, const char* client_name)
{
    d->backend = 2;
    d->jack_lib = dlopen("libjack.so.0", RTLD_NOW | RTLD_LOCAL);
    if(!d->jack_lib)
        d->jack_lib = dlopen("libjack.so", RTLD_NOW | RTLD_LOCAL);
    if(!d->jack_lib)
    {
        audio_set_error("std::audio JACK2 libjack not found");
        return -1;
    }

    jack_client_open_fn p_jack_client_open = (jack_client_open_fn)jack_sym(d->jack_lib, "jack_client_open");
    p_jack_client_close = (jack_client_close_fn)jack_sym(d->jack_lib, "jack_client_close");
    p_jack_activate = (jack_activate_fn)jack_sym(d->jack_lib, "jack_activate");
    p_jack_deactivate = (jack_deactivate_fn)jack_sym(d->jack_lib, "jack_deactivate");
    jack_port_register_fn p_jack_port_register = (jack_port_register_fn)jack_sym(d->jack_lib, "jack_port_register");
    jack_set_process_callback_fn p_jack_set_process_callback = (jack_set_process_callback_fn)jack_sym(d->jack_lib, "jack_set_process_callback");
    jack_get_sample_rate_fn p_jack_get_sample_rate = (jack_get_sample_rate_fn)jack_sym(d->jack_lib, "jack_get_sample_rate");
    jack_get_buffer_size_fn p_jack_get_buffer_size = (jack_get_buffer_size_fn)jack_sym(d->jack_lib, "jack_get_buffer_size");
    jack_set_buffer_size_fn p_jack_set_buffer_size = (jack_set_buffer_size_fn)dlsym(d->jack_lib, "jack_set_buffer_size");
    p_jack_port_get_buffer = (jack_port_get_buffer_fn)jack_sym(d->jack_lib, "jack_port_get_buffer");
    p_jack_port_name = (jack_port_name_fn)jack_sym(d->jack_lib, "jack_port_name");
    p_jack_get_ports = (jack_get_ports_fn)jack_sym(d->jack_lib, "jack_get_ports");
    p_jack_connect = (jack_connect_fn)jack_sym(d->jack_lib, "jack_connect");
    p_jack_free = (jack_free_fn)jack_sym(d->jack_lib, "jack_free");
    if(!p_jack_client_open || !p_jack_client_close || !p_jack_activate || !p_jack_deactivate ||
       !p_jack_port_register || !p_jack_set_process_callback || !p_jack_get_sample_rate ||
       !p_jack_get_buffer_size || !p_jack_port_get_buffer)
        return -1;

    jack_status_t status = 0;
    const char* name = (client_name && client_name[0]) ? client_name : "mlang_audio";
    d->jack_client = p_jack_client_open(name, MLANG_JACK_NULL_OPTION, &status);
    if(!d->jack_client)
    {
        audio_set_error("std::audio JACK2 jack_client_open failed; is jackd running?");
        return -1;
    }

    if(d->buffer_frames > 0 && p_jack_set_buffer_size)
        (void)p_jack_set_buffer_size(d->jack_client, (jack_nframes_t)d->buffer_frames);
    d->sample_rate = (double)p_jack_get_sample_rate(d->jack_client);
    d->buffer_frames = (int64_t)p_jack_get_buffer_size(d->jack_client);
    d->out_l = p_jack_port_register(d->jack_client, "out_l", MLANG_JACK_DEFAULT_AUDIO_TYPE, MLANG_JACK_PORT_IS_OUTPUT, 0);
    d->out_r = p_jack_port_register(d->jack_client, "out_r", MLANG_JACK_DEFAULT_AUDIO_TYPE, MLANG_JACK_PORT_IS_OUTPUT, 0);
    if(!d->out_l || !d->out_r)
    {
        audio_set_error("std::audio JACK2 output port registration failed");
        return -1;
    }
    if(p_jack_set_process_callback(d->jack_client, jack_process, d) != 0)
    {
        audio_set_error("std::audio JACK2 set_process_callback failed");
        return -1;
    }
    return 0;
}

static void audio_jack_autoconnect(mlang_audio_device_t* d)
{
    if(!d || !p_jack_get_ports || !p_jack_port_name || !p_jack_connect)
        return;
    const char** ports = p_jack_get_ports(d->jack_client, NULL, MLANG_JACK_DEFAULT_AUDIO_TYPE,
                                          MLANG_JACK_PORT_IS_PHYSICAL | MLANG_JACK_PORT_IS_INPUT);
    if(!ports)
        return;
    const char* l = p_jack_port_name(d->out_l);
    const char* r = p_jack_port_name(d->out_r);
    int64_t first = d->device_id > 0 ? d->device_id * 2 : 0;
    if(ports[first] && l)
        (void)p_jack_connect(d->jack_client, l, ports[first]);
    if(ports[first + 1] && r)
        (void)p_jack_connect(d->jack_client, r, ports[first + 1]);
    else if(ports[first] && r)
        (void)p_jack_connect(d->jack_client, r, ports[first]);
    if(p_jack_free)
        p_jack_free((void*)ports);
}

static void* audio_jack_load_query_lib(void)
{
    void* lib = dlopen("libjack.so.0", RTLD_NOW | RTLD_LOCAL);
    if(!lib)
        lib = dlopen("libjack.so", RTLD_NOW | RTLD_LOCAL);
    return lib;
}

static int audio_jack_query_ports(const char* client_name, void** out_lib, void** out_client, const char*** out_ports)
{
    *out_lib = NULL;
    *out_client = NULL;
    *out_ports = NULL;
    void* lib = audio_jack_load_query_lib();
    if(!lib)
    {
        audio_set_error("std::audio JACK2 libjack not found");
        return -1;
    }
    jack_client_open_fn open_fn = (jack_client_open_fn)dlsym(lib, "jack_client_open");
    jack_client_close_fn close_fn = (jack_client_close_fn)dlsym(lib, "jack_client_close");
    jack_get_ports_fn ports_fn = (jack_get_ports_fn)dlsym(lib, "jack_get_ports");
    if(!open_fn || !close_fn || !ports_fn)
    {
        dlclose(lib);
        audio_set_error("std::audio JACK2 query symbols unavailable");
        return -1;
    }
    jack_status_t status = 0;
    void* client = open_fn(client_name ? client_name : "mlang_audio_query", MLANG_JACK_NULL_OPTION, &status);
    if(!client)
    {
        dlclose(lib);
        audio_set_error("std::audio JACK2 jack_client_open failed; is jackd running?");
        return -1;
    }
    const char** ports = ports_fn(client, NULL, MLANG_JACK_DEFAULT_AUDIO_TYPE,
                                  MLANG_JACK_PORT_IS_PHYSICAL | MLANG_JACK_PORT_IS_INPUT);
    *out_lib = lib;
    *out_client = client;
    *out_ports = ports;
    return 0;
}

static void audio_jack_query_close(void* lib, void* client, const char** ports)
{
    if(lib)
    {
        jack_free_fn free_fn = (jack_free_fn)dlsym(lib, "jack_free");
        jack_client_close_fn close_fn = (jack_client_close_fn)dlsym(lib, "jack_client_close");
        if(ports && free_fn)
            free_fn((void*)ports);
        if(client && close_fn)
            (void)close_fn(client);
        dlclose(lib);
    }
}
#endif

int64_t __mlang_std_audio_device_count(void)
{
#if defined(__APPLE__)
    AudioDeviceID* ids = NULL;
    UInt32 count = 0;
    if(coreaudio_all_devices(&ids, &count) != 0)
        return 0;
    int64_t out = 0;
    for(UInt32 i = 0; i < count; ++i)
    {
        if(coreaudio_device_has_output(ids[i]))
            ++out;
    }
    free(ids);
    return out;
#elif defined(__linux__)
    void* lib = NULL;
    void* client = NULL;
    const char** ports = NULL;
    if(audio_jack_query_ports("mlang_audio_query", &lib, &client, &ports) != 0 || !ports)
    {
        audio_jack_query_close(lib, client, ports);
        return 0;
    }
    int64_t n = 0;
    while(ports[n])
        ++n;
    audio_jack_query_close(lib, client, ports);
    return (n + 1) / 2;
#else
    return 0;
#endif
}

int64_t __mlang_std_audio_default_output_device_id(void)
{
#if defined(__APPLE__)
    return coreaudio_default_output_index();
#elif defined(__linux__)
    return __mlang_std_audio_device_count() > 0 ? 0 : -1;
#else
    return -1;
#endif
}

const char* __mlang_std_audio_device_name(int64_t device_id)
{
    g_audio_device_name[0] = '\0';
    if(device_id < 0)
    {
        audio_set_error("std::audio device_name: invalid device id");
        return g_audio_device_name;
    }
#if defined(__APPLE__)
    if(coreaudio_device_name(device_id, g_audio_device_name, sizeof(g_audio_device_name)) != 0)
    {
        audio_set_error("std::audio CoreAudio device name lookup failed");
        return g_audio_device_name;
    }
    audio_clear_error();
    return g_audio_device_name;
#elif defined(__linux__)
    void* lib = NULL;
    void* client = NULL;
    const char** ports = NULL;
    if(audio_jack_query_ports("mlang_audio_query", &lib, &client, &ports) != 0 || !ports)
    {
        audio_jack_query_close(lib, client, ports);
        return g_audio_device_name;
    }
    int64_t first = device_id * 2;
    if(!ports[first])
    {
        audio_jack_query_close(lib, client, ports);
        audio_set_error("std::audio JACK2 device id is invalid");
        return g_audio_device_name;
    }
    if(ports[first + 1])
        (void)snprintf(g_audio_device_name, sizeof(g_audio_device_name), "%s / %s", ports[first], ports[first + 1]);
    else
        (void)snprintf(g_audio_device_name, sizeof(g_audio_device_name), "%s", ports[first]);
    audio_jack_query_close(lib, client, ports);
    audio_clear_error();
    return g_audio_device_name;
#else
    audio_set_error("std::audio backend unsupported on this platform");
    return g_audio_device_name;
#endif
}

int64_t __mlang_std_audio_open_output_device_config(int64_t device_id, const char* client_name, int64_t sample_rate, int64_t buffer_frames)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)calloc(1u, sizeof(*d));
    if(!d)
    {
        audio_set_error("std::audio allocation failed");
        return 0;
    }
    int requested_sample_rate = sample_rate > 0 ? 1 : 0;
    d->sample_rate = (double)audio_normalize_sample_rate(sample_rate);
    d->buffer_frames = audio_normalize_buffer_frames(buffer_frames);
    d->frequency_hz = 440.0;
    d->gain = 0.15;
    d->frames_left = 0;
    d->device_id = device_id;
    d->pcm_capacity_frames = (uint64_t)d->buffer_frames * 64u;
    if(d->pcm_capacity_frames < 4096u)
        d->pcm_capacity_frames = 4096u;
    if(d->pcm_capacity_frames > 1048576u)
        d->pcm_capacity_frames = 1048576u;
    d->pcm_ring = (float*)calloc(
        (size_t)d->pcm_capacity_frames * 2u, sizeof(float));
    if(!d->pcm_ring)
    {
        free(d);
        audio_set_error("std::audio PCM queue allocation failed");
        return 0;
    }
    atomic_init(&d->pcm_read_frame, 0u);
    atomic_init(&d->pcm_write_frame, 0u);
    atomic_init(&d->pcm_underruns, 0u);
    atomic_init(&d->source_mode, 0);

#if defined(__APPLE__)
    (void)client_name;
    if(device_id < 0)
        d->device_id = coreaudio_default_output_index();
    if(audio_coreaudio_open(d, d->device_id, requested_sample_rate) != 0)
    {
        __mlang_std_audio_close((int64_t)(intptr_t)d);
        return 0;
    }
#elif defined(__linux__)
    if(device_id < 0)
        d->device_id = __mlang_std_audio_default_output_device_id();
    if(d->device_id < 0)
    {
        __mlang_std_audio_close((int64_t)(intptr_t)d);
        audio_set_error("std::audio JACK2 no output devices available");
        return 0;
    }
    if(audio_jack_open(d, client_name) != 0)
    {
        __mlang_std_audio_close((int64_t)(intptr_t)d);
        return 0;
    }
#else
    (void)client_name;
    free(d->pcm_ring);
    free(d);
    audio_set_error("std::audio backend unsupported on this platform");
    return 0;
#endif
    audio_clear_error();
    return (int64_t)(intptr_t)d;
}

int64_t __mlang_std_audio_open_output_device(int64_t device_id, const char* client_name)
{
    return __mlang_std_audio_open_output_device_config(device_id, client_name, 0, 0);
}

int64_t __mlang_std_audio_open_default_output_config(const char* client_name, int64_t sample_rate, int64_t buffer_frames)
{
    return __mlang_std_audio_open_output_device_config(-1, client_name, sample_rate, buffer_frames);
}

int64_t __mlang_std_audio_open_default_output(const char* client_name)
{
    return __mlang_std_audio_open_default_output_config(client_name, 0, 0);
}

int32_t __mlang_std_audio_start(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    if(!d)
    {
        audio_set_error("std::audio start: invalid handle");
        return -1;
    }
#if defined(__APPLE__)
    OSStatus rc = AudioQueueStart(d->queue, NULL);
    if(rc != noErr)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio CoreAudio AudioQueueStart failed: %d", (int)rc);
        return -1;
    }
#elif defined(__linux__)
    if(p_jack_activate && p_jack_activate(d->jack_client) != 0)
    {
        audio_set_error("std::audio JACK2 jack_activate failed");
        return -1;
    }
    audio_jack_autoconnect(d);
#endif
    d->running = 1;
    audio_clear_error();
    return 0;
}

int32_t __mlang_std_audio_stop(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    if(!d)
        return 0;
    d->running = 0;
    d->frames_left = 0;
#if defined(__APPLE__)
    if(d->queue)
        (void)AudioQueueStop(d->queue, true);
#elif defined(__linux__)
    if(p_jack_deactivate && d->jack_client)
        (void)p_jack_deactivate(d->jack_client);
#endif
    audio_clear_error();
    return 0;
}

int32_t __mlang_std_audio_close(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    if(!d)
        return 0;
    (void)__mlang_std_audio_stop(handle);
#if defined(__APPLE__)
    if(d->queue)
        AudioQueueDispose(d->queue, true);
#elif defined(__linux__)
    if(p_jack_client_close && d->jack_client)
        (void)p_jack_client_close(d->jack_client);
    if(d->jack_lib)
        dlclose(d->jack_lib);
#endif
    free(d->pcm_ring);
    free(d);
    audio_clear_error();
    return 0;
}

int64_t __mlang_std_audio_sample_rate(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    return d ? (int64_t)(d->sample_rate + 0.5) : 0;
}

int64_t __mlang_std_audio_buffer_frames(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    return d ? d->buffer_frames : 0;
}

int64_t __mlang_std_audio_pcm_capacity_frames(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    return d ? (int64_t)d->pcm_capacity_frames : 0;
}

int64_t __mlang_std_audio_pcm_queued_frames(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    return d ? (int64_t)audio_pcm_queued_frames(d) : 0;
}

int64_t __mlang_std_audio_pcm_available_frames(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    if(!d)
        return 0;
    const uint64_t queued = audio_pcm_queued_frames(d);
    return (int64_t)(d->pcm_capacity_frames - queued);
}

int64_t __mlang_std_audio_pcm_underrun_count(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    if(!d)
        return 0;
    return (int64_t)atomic_load_explicit(
        &d->pcm_underruns, memory_order_relaxed);
}

int32_t __mlang_std_audio_pcm_clear(int64_t handle)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    if(!d)
    {
        audio_set_error("std::audio pcm_clear: invalid handle");
        return -1;
    }
    const uint64_t write_frame = atomic_load_explicit(
        &d->pcm_write_frame, memory_order_acquire);
    atomic_store_explicit(
        &d->pcm_read_frame, write_frame, memory_order_release);
    atomic_store_explicit(&d->pcm_underruns, 0u, memory_order_relaxed);
    audio_clear_error();
    return 0;
}

int64_t __mlang_std_audio_queue_interleaved_f32(
    int64_t handle, mlang_list_t samples)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    if(!d || !d->pcm_ring)
    {
        audio_set_error("std::audio queue_interleaved_f32: invalid handle");
        return -1;
    }
    if(samples.size < 0 || (samples.size & 1) != 0 ||
       (samples.size > 0 && !samples.data))
    {
        audio_set_error(
            "std::audio queue_interleaved_f32: expected stereo sample pairs");
        return -1;
    }
    const uint64_t frames = (uint64_t)samples.size / 2u;
    if(frames == 0)
    {
        audio_clear_error();
        return 0;
    }
    const uint64_t queued = audio_pcm_queued_frames(d);
    if(frames > d->pcm_capacity_frames - queued)
    {
        audio_set_error("std::audio PCM queue has insufficient free frames");
        return -1;
    }

    const float* input = (const float*)samples.data;
    uint64_t write_frame = atomic_load_explicit(
        &d->pcm_write_frame, memory_order_relaxed);
    for(uint64_t i = 0; i < frames; ++i)
    {
        const uint64_t slot = (write_frame + i) % d->pcm_capacity_frames;
        d->pcm_ring[slot * 2] = input[i * 2];
        d->pcm_ring[slot * 2 + 1] = input[i * 2 + 1];
    }
    atomic_store_explicit(
        &d->pcm_write_frame, write_frame + frames, memory_order_release);
    atomic_store_explicit(&d->source_mode, 2, memory_order_release);
    audio_clear_error();
    return (int64_t)frames;
}

int64_t __mlang_std_audio_queue_pcm_block(
    int64_t handle, int64_t block_handle, int64_t frames)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    const mlang_pcm_block_t* block =
        (const mlang_pcm_block_t*)(intptr_t)block_handle;
    if(!d || !d->pcm_ring || !block || !block->samples || frames < 0 ||
       frames > block->capacity_frames)
    {
        audio_set_error("std::audio queue_pcm_block: invalid arguments");
        return -1;
    }
    if(frames == 0)
        return 0;
    const uint64_t frame_count = (uint64_t)frames;
    const uint64_t queued = audio_pcm_queued_frames(d);
    if(frame_count > d->pcm_capacity_frames - queued)
    {
        audio_set_error("std::audio PCM queue has insufficient free frames");
        return -1;
    }
    const uint64_t write_frame = atomic_load_explicit(
        &d->pcm_write_frame, memory_order_relaxed);
    for(uint64_t i = 0; i < frame_count; ++i)
    {
        const uint64_t slot = (write_frame + i) % d->pcm_capacity_frames;
        d->pcm_ring[slot * 2] = block->samples[i * 2];
        d->pcm_ring[slot * 2 + 1] = block->samples[i * 2 + 1];
    }
    atomic_store_explicit(
        &d->pcm_write_frame, write_frame + frame_count, memory_order_release);
    atomic_store_explicit(&d->source_mode, 2, memory_order_release);
    audio_clear_error();
    return frames;
}

int32_t __mlang_std_audio_play_sine(int64_t handle, double frequency_hz, double gain, int64_t duration_ms)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)(intptr_t)handle;
    if(!d || frequency_hz <= 0.0 || duration_ms <= 0)
    {
        audio_set_error("std::audio play_sine: invalid arguments");
        return -1;
    }
    if(frequency_hz < 20.0)
        frequency_hz = 20.0;
    if(frequency_hz > 20000.0)
        frequency_hz = 20000.0;
    if(gain < 0.0)
        gain = 0.0;
    if(gain > 1.0)
        gain = 1.0;
    d->frequency_hz = frequency_hz;
    d->gain = gain;
    d->frames_left = (int64_t)((d->sample_rate * (double)duration_ms) / 1000.0);
    if(d->frames_left < 1)
        d->frames_left = 1;
    d->running = 1;
    atomic_store_explicit(&d->source_mode, 1, memory_order_release);
    audio_clear_error();
    return 0;
}
