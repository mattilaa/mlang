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
typedef struct mlang_audio_insert_stack mlang_audio_insert_stack_t;
typedef struct mlang_audio_mixer mlang_audio_mixer_t;

#define MLANG_AUDIO_MAX_INSERTS 16
#define MLANG_AUDIO_MAX_RACKS 8
#define MLANG_AUDIO_MAX_RACK_EFFECTS 8
#define MLANG_AUDIO_MAX_MIXER_TRACKS 32
#define MLANG_AUDIO_MAX_TRACK_SENDS 8

enum mlang_audio_effect_kind
{
    MLANG_AUDIO_EFFECT_GAIN = 1,
    MLANG_AUDIO_EFFECT_LOWPASS = 2,
    MLANG_AUDIO_EFFECT_DISTORTION = 3,
    MLANG_AUDIO_EFFECT_DELAY = 4
};

typedef struct
{
    int kind;
    int enabled;
    float wet;
    float p1;
    float p2;
    float state_l;
    float state_r;
    float* delay;
    uint64_t delay_frames;
    uint64_t delay_pos;
} mlang_audio_effect_t;

typedef struct
{
    int enabled;
    float dry;
    float wet;
    int effect_count;
    mlang_audio_effect_t effects[MLANG_AUDIO_MAX_RACK_EFFECTS];
} mlang_audio_effect_rack_t;

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

int32_t __mlang_std_audio_close(int64_t handle);
int32_t __mlang_std_audio_insert_stack_close(int64_t handle);
int32_t __mlang_std_audio_mixer_close(int64_t handle);

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

struct mlang_audio_insert_stack
{
    int backend;
    _Atomic int running;
    _Atomic uint64_t input_frames_received;
    _Atomic uint32_t input_peak_bits;
    int64_t input_device_id;
    int64_t output_device_id;
    double sample_rate;
    int64_t buffer_frames;
    int input_channels;
    int insert_count;
    int rack_count;
    mlang_audio_effect_t inserts[MLANG_AUDIO_MAX_INSERTS];
    mlang_audio_effect_rack_t racks[MLANG_AUDIO_MAX_RACKS];
    mlang_audio_mixer_t* mixer;
#if defined(__APPLE__)
    AudioQueueRef input_queue;
    AudioQueueRef output_queue;
    AudioQueueBufferRef input_buffers[3];
    AudioQueueBufferRef output_buffers[3];
    float* input_ring;
    uint64_t input_capacity_frames;
    _Atomic uint64_t input_read_frame;
    _Atomic uint64_t input_write_frame;
#elif defined(__linux__)
    void* jack_lib;
    void* jack_client;
    void* in_l;
    void* in_r;
    void* out_l;
    void* out_r;
#endif
};

typedef struct
{
    int enabled;
    int return_track;
    float level;
    int post_fader;
} mlang_audio_track_send_t;

typedef struct
{
    int active;
    int is_return;
    int input_kind;
    int input_track;
    int output_track;
    _Atomic uint64_t volume_command;
    _Atomic uint64_t pan_command;
    uint64_t volume_command_seen;
    uint64_t pan_command_seen;
    uint32_t volume_frames_left;
    uint32_t pan_frames_left;
    float volume_current;
    float volume_target;
    float pan_current;
    float pan_target;
    _Atomic int muted;
    char name[64];
    int insert_count;
    mlang_audio_effect_t inserts[MLANG_AUDIO_MAX_INSERTS];
    int send_count;
    mlang_audio_track_send_t sends[MLANG_AUDIO_MAX_TRACK_SENDS];
    float bus_l;
    float bus_r;
    float pre_l;
    float pre_r;
    float post_l;
    float post_r;
} mlang_audio_mixer_track_t;

struct mlang_audio_mixer
{
    _Atomic int running;
    double sample_rate;
    int64_t buffer_frames;
    int track_count;
    int return_count;
    int order_count;
    int order[MLANG_AUDIO_MAX_MIXER_TRACKS];
    _Atomic float master_gain;
    mlang_audio_mixer_track_t tracks[MLANG_AUDIO_MAX_MIXER_TRACKS];
    mlang_audio_insert_stack_t* io;
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

static float audio_clamp_unit(float value)
{
    if(value < 0.0f)
        return 0.0f;
    if(value > 1.0f)
        return 1.0f;
    return value;
}

static void audio_effect_release(mlang_audio_effect_t* effect)
{
    if(!effect)
        return;
    free(effect->delay);
    effect->delay = NULL;
    effect->delay_frames = 0;
    effect->delay_pos = 0;
}

static void audio_effect_process(mlang_audio_effect_t* effect,
                                 double sample_rate, float input_l,
                                 float input_r, float* output_l,
                                 float* output_r)
{
    float processed_l = input_l;
    float processed_r = input_r;
    if(!effect || !effect->enabled)
    {
        *output_l = input_l;
        *output_r = input_r;
        return;
    }

    switch(effect->kind)
    {
        case MLANG_AUDIO_EFFECT_GAIN:
            processed_l = input_l * effect->p1;
            processed_r = input_r * effect->p1;
            break;
        case MLANG_AUDIO_EFFECT_LOWPASS:
        {
            const double two_pi = 6.283185307179586476925286766559;
            float alpha = (float)(1.0 - exp(-two_pi * (double)effect->p1 /
                                            sample_rate));
            effect->state_l += alpha * (input_l - effect->state_l);
            effect->state_r += alpha * (input_r - effect->state_r);
            processed_l = effect->state_l;
            processed_r = effect->state_r;
            break;
        }
        case MLANG_AUDIO_EFFECT_DISTORTION:
        {
            float normalization = tanhf(effect->p1);
            if(fabsf(normalization) < 0.000001f)
                normalization = 1.0f;
            processed_l = tanhf(input_l * effect->p1) / normalization;
            processed_r = tanhf(input_r * effect->p1) / normalization;
            break;
        }
        case MLANG_AUDIO_EFFECT_DELAY:
            if(effect->delay && effect->delay_frames > 0)
            {
                const uint64_t slot = effect->delay_pos * 2u;
                const float delayed_l = effect->delay[slot];
                const float delayed_r = effect->delay[slot + 1u];
                effect->delay[slot] = input_l + delayed_l * effect->p2;
                effect->delay[slot + 1u] = input_r + delayed_r * effect->p2;
                effect->delay_pos = (effect->delay_pos + 1u) % effect->delay_frames;
                processed_l = delayed_l;
                processed_r = delayed_r;
            }
            break;
        default:
            break;
    }

    const float wet = audio_clamp_unit(effect->wet);
    *output_l = input_l * (1.0f - wet) + processed_l * wet;
    *output_r = input_r * (1.0f - wet) + processed_r * wet;
}

static void audio_insert_stack_process_sample(mlang_audio_insert_stack_t* stack,
                                              float input_l, float input_r,
                                              float* output_l, float* output_r)
{
    if(!stack || !output_l || !output_r)
        return;
    float serial_l = input_l;
    float serial_r = input_r;
    for(int i = 0; i < stack->insert_count; ++i)
        audio_effect_process(&stack->inserts[i], stack->sample_rate,
                             serial_l, serial_r, &serial_l, &serial_r);

    if(stack->rack_count == 0)
    {
        *output_l = serial_l;
        *output_r = serial_r;
        return;
    }

    float mixed_l = 0.0f;
    float mixed_r = 0.0f;
    for(int rack_index = 0; rack_index < stack->rack_count; ++rack_index)
    {
        mlang_audio_effect_rack_t* rack = &stack->racks[rack_index];
        if(!rack->enabled)
            continue;
        float rack_l = serial_l;
        float rack_r = serial_r;
        for(int effect_index = 0; effect_index < rack->effect_count;
            ++effect_index)
            audio_effect_process(&rack->effects[effect_index],
                                 stack->sample_rate, rack_l, rack_r,
                                 &rack_l, &rack_r);
        mixed_l += serial_l * rack->dry + rack_l * rack->wet;
        mixed_r += serial_r * rack->dry + rack_r * rack->wet;
    }
    *output_l = mixed_l;
    *output_r = mixed_r;
}

#if defined(__linux__)
static void audio_insert_stack_process_frames(mlang_audio_insert_stack_t* stack,
                                              const float* input_l,
                                              const float* input_r,
                                              float* output_l,
                                              float* output_r,
                                              uint64_t frames)
{
    if(!stack || !output_l || !output_r)
        return;
    for(uint64_t frame = 0; frame < frames; ++frame)
        audio_insert_stack_process_sample(
            stack, input_l ? input_l[frame] : 0.0f,
            input_r ? input_r[frame] : (input_l ? input_l[frame] : 0.0f),
            &output_l[frame], &output_r[frame]);
}
#endif

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

static uint64_t audio_control_command(float target, uint32_t ramp_frames)
{
    uint32_t target_bits = 0;
    memcpy(&target_bits, &target, sizeof(target_bits));
    return ((uint64_t)ramp_frames << 32) | (uint64_t)target_bits;
}

static void audio_control_decode(uint64_t command, float* target,
                                 uint32_t* ramp_frames)
{
    const uint32_t target_bits = (uint32_t)(command & 0xffffffffu);
    memcpy(target, &target_bits, sizeof(target_bits));
    *ramp_frames = (uint32_t)(command >> 32);
}

static float audio_control_next(_Atomic uint64_t* command,
                                uint64_t* command_seen, float* current,
                                float* target, uint32_t* frames_left)
{
    const uint64_t latest = atomic_load_explicit(command, memory_order_acquire);
    if(latest != *command_seen)
    {
        audio_control_decode(latest, target, frames_left);
        *command_seen = latest;
        if(*frames_left == 0)
            *current = *target;
    }
    if(*frames_left > 0)
    {
        *current += (*target - *current) / (float)*frames_left;
        --*frames_left;
    }
    return *current;
}

static void audio_mixer_process_sample(mlang_audio_mixer_t* mixer,
                                       float input_l, float input_r,
                                       float* output_l, float* output_r)
{
    if(!mixer || !output_l || !output_r)
        return;
    for(int i = 0; i < mixer->track_count; ++i)
    {
        mixer->tracks[i].bus_l = 0.0f;
        mixer->tracks[i].bus_r = 0.0f;
    }

    float master_l = 0.0f;
    float master_r = 0.0f;
    for(int order_index = 0; order_index < mixer->order_count; ++order_index)
    {
        const int track_id = mixer->order[order_index];
        mlang_audio_mixer_track_t* track = &mixer->tracks[track_id];
        float sample_l = track->bus_l;
        float sample_r = track->bus_r;
        if(!track->is_return && track->input_kind == 1)
        {
            sample_l += input_l;
            sample_r += input_r;
        }
        else if(!track->is_return && track->input_kind == 2 &&
                track->input_track >= 0 &&
                track->input_track < mixer->track_count)
        {
            mlang_audio_mixer_track_t* source =
                &mixer->tracks[track->input_track];
            /* Audio From + matching Audio To describe one route, not two. */
            if(source->output_track != track_id)
            {
                sample_l += source->post_l;
                sample_r += source->post_r;
            }
        }

        for(int insert = 0; insert < track->insert_count; ++insert)
            audio_effect_process(&track->inserts[insert], mixer->sample_rate,
                                 sample_l, sample_r, &sample_l, &sample_r);
        track->pre_l = sample_l;
        track->pre_r = sample_r;
        const float volume = audio_control_next(
            &track->volume_command, &track->volume_command_seen,
            &track->volume_current, &track->volume_target,
            &track->volume_frames_left);
        const float pan = audio_control_next(
            &track->pan_command, &track->pan_command_seen,
            &track->pan_current, &track->pan_target,
            &track->pan_frames_left);
        const float pan_l = pan > 0.0f ? 1.0f - pan : 1.0f;
        const float pan_r = pan < 0.0f ? 1.0f + pan : 1.0f;
        const int muted = atomic_load_explicit(&track->muted,
                                               memory_order_relaxed);
        track->post_l = muted ? 0.0f : sample_l * volume * pan_l;
        track->post_r = muted ? 0.0f : sample_r * volume * pan_r;

        for(int send_index = 0; send_index < track->send_count; ++send_index)
        {
            mlang_audio_track_send_t* send = &track->sends[send_index];
            if(!send->enabled || send->return_track < 0 ||
               send->return_track >= mixer->track_count)
                continue;
            const float send_l = send->post_fader ? track->post_l : track->pre_l;
            const float send_r = send->post_fader ? track->post_r : track->pre_r;
            mixer->tracks[send->return_track].bus_l += send_l * send->level;
            mixer->tracks[send->return_track].bus_r += send_r * send->level;
        }

        if(track->output_track >= 0 &&
           track->output_track < mixer->track_count)
        {
            mixer->tracks[track->output_track].bus_l += track->post_l;
            mixer->tracks[track->output_track].bus_r += track->post_r;
        }
        else
        {
            master_l += track->post_l;
            master_r += track->post_r;
        }
    }
    const float master_gain = atomic_load_explicit(&mixer->master_gain,
                                                   memory_order_relaxed);
    *output_l = master_l * master_gain;
    *output_r = master_r * master_gain;
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

float __mlang_std_audio_pcm_block_sample(int64_t handle, int64_t frame,
                                         int32_t channel)
{
    const mlang_pcm_block_t* block =
        (const mlang_pcm_block_t*)(intptr_t)handle;
    if(!block || !block->samples || frame < 0 ||
       frame >= block->capacity_frames || channel < 0 || channel > 1)
    {
        audio_set_error("std::audio PCM block sample is out of range");
        return 0.0f;
    }
    audio_clear_error();
    return block->samples[frame * 2 + channel];
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

static int coreaudio_device_has_input(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioDevicePropertyStreamConfiguration;
    addr.mScope = kAudioDevicePropertyScopeInput;
    addr.mElement = kAudioObjectPropertyElementMain;

    UInt32 size = 0;
    if(AudioObjectGetPropertyDataSize(id, &addr, 0, NULL, &size) != noErr ||
       size == 0)
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

static UInt32 coreaudio_input_channel_count(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioDevicePropertyStreamConfiguration;
    addr.mScope = kAudioDevicePropertyScopeInput;
    addr.mElement = kAudioObjectPropertyElementMain;

    UInt32 size = 0;
    if(AudioObjectGetPropertyDataSize(id, &addr, 0, NULL, &size) != noErr ||
       size == 0)
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
    return channels;
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

static AudioDeviceID coreaudio_input_device_for_index(int64_t device_id)
{
    AudioDeviceID* ids = NULL;
    UInt32 count = 0;
    if(coreaudio_all_devices(&ids, &count) != 0)
        return kAudioObjectUnknown;
    int64_t input_index = 0;
    AudioDeviceID selected = kAudioObjectUnknown;
    for(UInt32 i = 0; i < count; ++i)
    {
        if(!coreaudio_device_has_input(ids[i]))
            continue;
        if(input_index == device_id)
        {
            selected = ids[i];
            break;
        }
        ++input_index;
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

static int64_t coreaudio_input_index_for_device(AudioDeviceID wanted)
{
    AudioDeviceID* ids = NULL;
    UInt32 count = 0;
    if(coreaudio_all_devices(&ids, &count) != 0)
        return -1;
    int64_t input_index = 0;
    for(UInt32 i = 0; i < count; ++i)
    {
        if(!coreaudio_device_has_input(ids[i]))
            continue;
        if(ids[i] == wanted)
        {
            free(ids);
            return input_index;
        }
        ++input_index;
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

static int64_t coreaudio_default_input_index(void)
{
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL,
                                  &size, &device) != noErr)
        return -1;
    return coreaudio_input_index_for_device(device);
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

static int coreaudio_input_device_name(int64_t device_id, char* out,
                                       size_t out_size)
{
    AudioDeviceID id = coreaudio_input_device_for_index(device_id);
    if(id == kAudioObjectUnknown)
        return -1;
    CFStringRef name = NULL;
    UInt32 size = sizeof(name);
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioObjectPropertyName;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;
    if(AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &name) != noErr ||
       !name)
        return -1;
    Boolean ok = CFStringGetCString(name, out, out_size,
                                    kCFStringEncodingUTF8);
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

static double coreaudio_nominal_sample_rate(AudioDeviceID id)
{
    Float64 sr = 0.0;
    UInt32 size = sizeof(sr);
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioDevicePropertyNominalSampleRate;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;
    if(AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &sr) == noErr &&
       sr > 1000.0)
        return (double)sr;
    return 0.0;
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
    OSStatus rc = AudioQueueSetProperty(queue, kAudioQueueProperty_CurrentDevice,
                                        &uid, sizeof(uid));
    CFStringRef selected_uid = NULL;
    UInt32 selected_size = sizeof(selected_uid);
    if(rc == noErr)
        rc = AudioQueueGetProperty(queue, kAudioQueueProperty_CurrentDevice,
                                   &selected_uid, &selected_size);
    if(rc == noErr && (!selected_uid || !CFEqual(uid, selected_uid)))
        rc = kAudio_ParamError;
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

static int audio_jack_query_ports_with_flags(const char* client_name,
                                             unsigned long port_flags,
                                             void** out_lib, void** out_client,
                                             const char*** out_ports)
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
                                  MLANG_JACK_PORT_IS_PHYSICAL | port_flags);
    *out_lib = lib;
    *out_client = client;
    *out_ports = ports;
    return 0;
}

static int audio_jack_query_ports(const char* client_name, void** out_lib,
                                  void** out_client,
                                  const char*** out_ports)
{
    return audio_jack_query_ports_with_flags(
        client_name, MLANG_JACK_PORT_IS_INPUT, out_lib, out_client, out_ports);
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

int64_t __mlang_std_audio_input_device_count(void)
{
#if defined(__APPLE__)
    AudioDeviceID* ids = NULL;
    UInt32 count = 0;
    if(coreaudio_all_devices(&ids, &count) != 0)
        return 0;
    int64_t inputs = 0;
    for(UInt32 i = 0; i < count; ++i)
        if(coreaudio_device_has_input(ids[i]))
            ++inputs;
    free(ids);
    return inputs;
#elif defined(__linux__)
    void* lib = NULL;
    void* client = NULL;
    const char** ports = NULL;
    if(audio_jack_query_ports_with_flags(
           "mlang_audio_input_query", MLANG_JACK_PORT_IS_OUTPUT,
           &lib, &client, &ports) != 0 || !ports)
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

int64_t __mlang_std_audio_default_input_device_id(void)
{
#if defined(__APPLE__)
    return coreaudio_default_input_index();
#elif defined(__linux__)
    return __mlang_std_audio_input_device_count() > 0 ? 0 : -1;
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

const char* __mlang_std_audio_input_device_name(int64_t device_id)
{
    g_audio_device_name[0] = '\0';
    if(device_id < 0)
    {
        audio_set_error("std::audio input_device_name: invalid device id");
        return g_audio_device_name;
    }
#if defined(__APPLE__)
    if(coreaudio_input_device_name(device_id, g_audio_device_name,
                                   sizeof(g_audio_device_name)) != 0)
    {
        audio_set_error("std::audio CoreAudio input device name lookup failed");
        return g_audio_device_name;
    }
    audio_clear_error();
    return g_audio_device_name;
#elif defined(__linux__)
    void* lib = NULL;
    void* client = NULL;
    const char** ports = NULL;
    if(audio_jack_query_ports_with_flags(
           "mlang_audio_input_query", MLANG_JACK_PORT_IS_OUTPUT,
           &lib, &client, &ports) != 0 || !ports)
    {
        audio_jack_query_close(lib, client, ports);
        return g_audio_device_name;
    }
    const int64_t first = device_id * 2;
    if(!ports[first])
    {
        audio_jack_query_close(lib, client, ports);
        audio_set_error("std::audio JACK2 input device id is invalid");
        return g_audio_device_name;
    }
    if(ports[first + 1])
        (void)snprintf(g_audio_device_name, sizeof(g_audio_device_name),
                       "%s / %s", ports[first], ports[first + 1]);
    else
        (void)snprintf(g_audio_device_name, sizeof(g_audio_device_name),
                       "%s", ports[first]);
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
#if defined(__APPLE__)
    int requested_sample_rate = sample_rate > 0 ? 1 : 0;
#endif
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

static mlang_audio_insert_stack_t* audio_insert_stack_from_handle(int64_t handle)
{
    return (mlang_audio_insert_stack_t*)(intptr_t)handle;
}

static int audio_insert_stack_can_configure(mlang_audio_insert_stack_t* stack,
                                            const char* operation)
{
    if(!stack)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio %s: invalid insert stack handle", operation);
        return 0;
    }
    if(atomic_load_explicit(&stack->running, memory_order_acquire))
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio %s: stop the insert stack before changing its graph",
                       operation);
        return 0;
    }
    return 1;
}

static int audio_effect_initialize(mlang_audio_effect_t* effect, int kind,
                                   float p1, float p2, float wet,
                                   double sample_rate)
{
    if(!effect)
        return -1;
    memset(effect, 0, sizeof(*effect));
    effect->kind = kind;
    effect->enabled = 1;
    effect->wet = audio_clamp_unit(wet);
    effect->p1 = p1;
    effect->p2 = p2;
    if(kind == MLANG_AUDIO_EFFECT_DELAY)
    {
        if(p1 < 1.0f || p1 > 5000.0f || p2 < 0.0f || p2 >= 1.0f)
        {
            audio_set_error("std::audio delay expects 1..5000 ms and feedback in [0, 1)");
            return -1;
        }
        effect->delay_frames = (uint64_t)(sample_rate * (double)p1 / 1000.0);
        if(effect->delay_frames < 1u)
            effect->delay_frames = 1u;
        effect->delay = (float*)calloc((size_t)effect->delay_frames * 2u,
                                       sizeof(float));
        if(!effect->delay)
        {
            audio_set_error("std::audio delay buffer allocation failed");
            return -1;
        }
    }
    return 0;
}

static int64_t audio_insert_stack_add_effect(mlang_audio_insert_stack_t* stack,
                                             int rack_id, int kind, float p1,
                                             float p2, float wet)
{
    if(!audio_insert_stack_can_configure(stack, "add_effect"))
        return -1;
    if(kind == MLANG_AUDIO_EFFECT_GAIN && (p1 < 0.0f || p1 > 16.0f))
    {
        audio_set_error("std::audio gain expects a linear gain in [0, 16]");
        return -1;
    }
    if(kind == MLANG_AUDIO_EFFECT_LOWPASS &&
       (p1 < 10.0f || p1 >= (float)(stack->sample_rate * 0.5)))
    {
        audio_set_error("std::audio low-pass cutoff must be between 10 Hz and Nyquist");
        return -1;
    }
    if(kind == MLANG_AUDIO_EFFECT_DISTORTION && (p1 < 0.01f || p1 > 100.0f))
    {
        audio_set_error("std::audio distortion drive expects a value in [0.01, 100]");
        return -1;
    }

    mlang_audio_effect_t* effect = NULL;
    int64_t id = -1;
    if(rack_id < 0)
    {
        if(stack->insert_count >= MLANG_AUDIO_MAX_INSERTS)
        {
            audio_set_error("std::audio insert stack is full (maximum 16 effects)");
            return -1;
        }
        id = stack->insert_count;
        effect = &stack->inserts[stack->insert_count];
    }
    else
    {
        if(rack_id >= stack->rack_count)
        {
            audio_set_error("std::audio rack effect: invalid rack id");
            return -1;
        }
        mlang_audio_effect_rack_t* rack = &stack->racks[rack_id];
        if(rack->effect_count >= MLANG_AUDIO_MAX_RACK_EFFECTS)
        {
            audio_set_error("std::audio effect rack is full (maximum 8 effects)");
            return -1;
        }
        id = rack->effect_count;
        effect = &rack->effects[rack->effect_count];
    }
    if(audio_effect_initialize(effect, kind, p1, p2, wet,
                               stack->sample_rate) != 0)
        return -1;
    if(rack_id < 0)
        ++stack->insert_count;
    else
        ++stack->racks[rack_id].effect_count;
    audio_clear_error();
    return id;
}

int64_t __mlang_std_audio_insert_stack_new(int64_t sample_rate,
                                           int64_t buffer_frames)
{
    mlang_audio_insert_stack_t* stack =
        (mlang_audio_insert_stack_t*)calloc(1u, sizeof(*stack));
    if(!stack)
    {
        audio_set_error("std::audio insert stack allocation failed");
        return 0;
    }
    stack->sample_rate = (double)audio_normalize_sample_rate(sample_rate);
    stack->buffer_frames = audio_normalize_buffer_frames(buffer_frames);
    atomic_init(&stack->running, 0);
    atomic_init(&stack->input_frames_received, 0u);
    atomic_init(&stack->input_peak_bits, 0u);
    audio_clear_error();
    return (int64_t)(intptr_t)stack;
}

int64_t __mlang_std_audio_insert_stack_add_gain(int64_t handle, double gain,
                                                double wet)
{
    return audio_insert_stack_add_effect(audio_insert_stack_from_handle(handle),
                                         -1, MLANG_AUDIO_EFFECT_GAIN,
                                         (float)gain, 0.0f, (float)wet);
}

int64_t __mlang_std_audio_insert_stack_add_lowpass(int64_t handle,
                                                   double cutoff_hz,
                                                   double wet)
{
    return audio_insert_stack_add_effect(audio_insert_stack_from_handle(handle),
                                         -1, MLANG_AUDIO_EFFECT_LOWPASS,
                                         (float)cutoff_hz, 0.0f, (float)wet);
}

int64_t __mlang_std_audio_insert_stack_add_distortion(int64_t handle,
                                                      double drive,
                                                      double wet)
{
    return audio_insert_stack_add_effect(audio_insert_stack_from_handle(handle),
                                         -1, MLANG_AUDIO_EFFECT_DISTORTION,
                                         (float)drive, 0.0f, (float)wet);
}

int64_t __mlang_std_audio_insert_stack_add_delay(int64_t handle,
                                                 double delay_ms,
                                                 double feedback, double wet)
{
    return audio_insert_stack_add_effect(audio_insert_stack_from_handle(handle),
                                         -1, MLANG_AUDIO_EFFECT_DELAY,
                                         (float)delay_ms, (float)feedback,
                                         (float)wet);
}

int64_t __mlang_std_audio_insert_stack_add_rack(int64_t handle, double dry,
                                                double wet)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    if(!audio_insert_stack_can_configure(stack, "add_rack"))
        return -1;
    if(stack->rack_count >= MLANG_AUDIO_MAX_RACKS)
    {
        audio_set_error("std::audio insert stack is full (maximum 8 racks)");
        return -1;
    }
    const int id = stack->rack_count++;
    mlang_audio_effect_rack_t* rack = &stack->racks[id];
    memset(rack, 0, sizeof(*rack));
    rack->enabled = 1;
    rack->dry = audio_clamp_unit((float)dry);
    rack->wet = audio_clamp_unit((float)wet);
    audio_clear_error();
    return id;
}

int32_t __mlang_std_audio_insert_stack_set_rack_mix(int64_t handle,
                                                    int64_t rack_id,
                                                    double dry, double wet)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    if(!audio_insert_stack_can_configure(stack, "set_rack_mix"))
        return -1;
    if(rack_id < 0 || rack_id >= stack->rack_count)
    {
        audio_set_error("std::audio set_rack_mix: invalid rack id");
        return -1;
    }
    stack->racks[rack_id].dry = audio_clamp_unit((float)dry);
    stack->racks[rack_id].wet = audio_clamp_unit((float)wet);
    audio_clear_error();
    return 0;
}

int64_t __mlang_std_audio_insert_stack_rack_add_gain(int64_t handle,
                                                     int64_t rack_id,
                                                     double gain)
{
    return audio_insert_stack_add_effect(audio_insert_stack_from_handle(handle),
                                         (int)rack_id, MLANG_AUDIO_EFFECT_GAIN,
                                         (float)gain, 0.0f, 1.0f);
}

int64_t __mlang_std_audio_insert_stack_rack_add_lowpass(int64_t handle,
                                                        int64_t rack_id,
                                                        double cutoff_hz)
{
    return audio_insert_stack_add_effect(audio_insert_stack_from_handle(handle),
                                         (int)rack_id,
                                         MLANG_AUDIO_EFFECT_LOWPASS,
                                         (float)cutoff_hz, 0.0f, 1.0f);
}

int64_t __mlang_std_audio_insert_stack_rack_add_distortion(int64_t handle,
                                                           int64_t rack_id,
                                                           double drive)
{
    return audio_insert_stack_add_effect(audio_insert_stack_from_handle(handle),
                                         (int)rack_id,
                                         MLANG_AUDIO_EFFECT_DISTORTION,
                                         (float)drive, 0.0f, 1.0f);
}

int64_t __mlang_std_audio_insert_stack_rack_add_delay(int64_t handle,
                                                      int64_t rack_id,
                                                      double delay_ms,
                                                      double feedback)
{
    return audio_insert_stack_add_effect(audio_insert_stack_from_handle(handle),
                                         (int)rack_id,
                                         MLANG_AUDIO_EFFECT_DELAY,
                                         (float)delay_ms, (float)feedback,
                                         1.0f);
}

int64_t __mlang_std_audio_insert_stack_insert_count(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    return stack ? stack->insert_count : 0;
}

int64_t __mlang_std_audio_insert_stack_rack_count(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    return stack ? stack->rack_count : 0;
}

int64_t __mlang_std_audio_insert_stack_sample_rate(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    return stack ? (int64_t)(stack->sample_rate + 0.5) : 0;
}

int64_t __mlang_std_audio_insert_stack_buffer_frames(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    return stack ? stack->buffer_frames : 0;
}

int64_t __mlang_std_audio_insert_stack_input_frames_received(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    return stack ? (int64_t)atomic_load_explicit(
                       &stack->input_frames_received, memory_order_acquire)
                 : 0;
}

double __mlang_std_audio_insert_stack_input_peak(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    if(!stack)
        return 0.0;
    const uint32_t peak_bits = atomic_load_explicit(&stack->input_peak_bits,
                                                    memory_order_acquire);
    float peak = 0.0f;
    memcpy(&peak, &peak_bits, sizeof(peak));
    return (double)peak;
}

int32_t __mlang_std_audio_insert_stack_process_block(int64_t handle,
                                                     int64_t input_handle,
                                                     int64_t output_handle,
                                                     int64_t frames)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    mlang_pcm_block_t* input = (mlang_pcm_block_t*)(intptr_t)input_handle;
    mlang_pcm_block_t* output = (mlang_pcm_block_t*)(intptr_t)output_handle;
    if(!stack || !input || !output || !input->samples || !output->samples ||
       frames < 0 || frames > input->capacity_frames ||
       frames > output->capacity_frames)
    {
        audio_set_error("std::audio process_block: invalid arguments");
        return -1;
    }
    for(int64_t i = 0; i < frames; ++i)
    {
        const float input_l = input->samples[i * 2];
        const float input_r = input->samples[i * 2 + 1];
        audio_insert_stack_process_sample(stack, input_l, input_r,
                                          &output->samples[i * 2],
                                          &output->samples[i * 2 + 1]);
    }
    audio_clear_error();
    return 0;
}

#if defined(__APPLE__)
static void audio_insert_input_callback(void* user_data, AudioQueueRef queue,
                                        AudioQueueBufferRef buffer,
                                        const AudioTimeStamp* start_time,
                                        UInt32 packet_count,
                                        const AudioStreamPacketDescription* packets)
{
    (void)start_time;
    (void)packet_count;
    (void)packets;
    mlang_audio_insert_stack_t* stack =
        (mlang_audio_insert_stack_t*)user_data;
    if(stack && buffer && stack->input_ring)
    {
        const float* samples = (const float*)buffer->mAudioData;
        const uint64_t channels = stack->input_channels > 0
                                      ? (uint64_t)stack->input_channels
                                      : 2u;
        const uint64_t frames = buffer->mAudioDataByteSize /
                                (channels * (uint64_t)sizeof(float));
        float peak = 0.0f;
        uint64_t write = atomic_load_explicit(&stack->input_write_frame,
                                              memory_order_relaxed);
        const uint64_t read = atomic_load_explicit(&stack->input_read_frame,
                                                   memory_order_acquire);
        for(uint64_t i = 0; i < frames; ++i)
        {
            if(write - read >= stack->input_capacity_frames)
                break;
            const uint64_t slot = write % stack->input_capacity_frames;
            const float left = samples[i * channels];
            const float right = channels > 1u
                                    ? samples[i * channels + 1u]
                                    : left;
            stack->input_ring[slot * 2u] = left;
            stack->input_ring[slot * 2u + 1u] = right;
            const float left_level = fabsf(left);
            const float right_level = fabsf(right);
            if(left_level > peak)
                peak = left_level;
            if(right_level > peak)
                peak = right_level;
            ++write;
        }
        uint32_t peak_bits = 0;
        memcpy(&peak_bits, &peak, sizeof(peak_bits));
        atomic_store_explicit(&stack->input_peak_bits, peak_bits,
                              memory_order_release);
        (void)atomic_fetch_add_explicit(&stack->input_frames_received, frames,
                                        memory_order_relaxed);
        atomic_store_explicit(&stack->input_write_frame, write,
                              memory_order_release);
    }
    if(stack && atomic_load_explicit(&stack->running, memory_order_acquire))
        (void)AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

static void audio_insert_output_fill(mlang_audio_insert_stack_t* stack,
                                     AudioQueueBufferRef buffer)
{
    if(!stack || !buffer)
        return;
    const uint64_t frames = stack->buffer_frames > 0
                                ? (uint64_t)stack->buffer_frames
                                : 512u;
    float* samples = (float*)buffer->mAudioData;
    uint64_t read = atomic_load_explicit(&stack->input_read_frame,
                                         memory_order_relaxed);
    const uint64_t write = atomic_load_explicit(&stack->input_write_frame,
                                                memory_order_acquire);
    for(uint64_t i = 0; i < frames; ++i)
    {
        float input_l = 0.0f;
        float input_r = 0.0f;
        if(read < write)
        {
            const uint64_t slot = read % stack->input_capacity_frames;
            input_l = stack->input_ring[slot * 2u];
            input_r = stack->input_ring[slot * 2u + 1u];
            ++read;
        }
        if(stack->mixer)
            audio_mixer_process_sample(stack->mixer, input_l, input_r,
                                       &samples[i * 2u],
                                       &samples[i * 2u + 1u]);
        else
            audio_insert_stack_process_sample(stack, input_l, input_r,
                                              &samples[i * 2u],
                                              &samples[i * 2u + 1u]);
    }
    atomic_store_explicit(&stack->input_read_frame, read, memory_order_release);
    buffer->mAudioDataByteSize =
        (UInt32)(frames * 2u * (uint64_t)sizeof(float));
}

static void audio_insert_output_callback(void* user_data, AudioQueueRef queue,
                                         AudioQueueBufferRef buffer)
{
    mlang_audio_insert_stack_t* stack =
        (mlang_audio_insert_stack_t*)user_data;
    audio_insert_output_fill(stack, buffer);
    if(stack && atomic_load_explicit(&stack->running, memory_order_acquire))
        (void)AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

static int audio_insert_stack_coreaudio_open(mlang_audio_insert_stack_t* stack,
                                             int64_t input_device_id,
                                             int64_t output_device_id,
                                             int requested_sample_rate)
{
    const AudioDeviceID input_device = input_device_id >= 0
        ? coreaudio_input_device_for_index(input_device_id)
        : kAudioObjectUnknown;
    const AudioDeviceID output_device = output_device_id >= 0
        ? coreaudio_device_for_index(output_device_id)
        : kAudioObjectUnknown;
    if(input_device_id >= 0 && input_device == kAudioObjectUnknown)
    {
        audio_set_error("std::audio CoreAudio input device id is invalid");
        return -1;
    }
    if(output_device_id >= 0 && output_device == kAudioObjectUnknown)
    {
        audio_set_error("std::audio CoreAudio output device id is invalid");
        return -1;
    }
    if(!requested_sample_rate)
    {
        const double input_rate = coreaudio_nominal_sample_rate(input_device);
        const double output_rate = coreaudio_nominal_sample_rate(output_device);
        if(input_rate > 0.0)
            stack->sample_rate = input_rate;
        else if(output_rate > 0.0)
            stack->sample_rate = output_rate;
    }
    const UInt32 native_input_channels =
        coreaudio_input_channel_count(input_device);
    stack->input_channels = native_input_channels == 1u ? 1 : 2;

    AudioStreamBasicDescription input_fmt;
    memset(&input_fmt, 0, sizeof(input_fmt));
    input_fmt.mSampleRate = stack->sample_rate;
    input_fmt.mFormatID = kAudioFormatLinearPCM;
    input_fmt.mFormatFlags = kLinearPCMFormatFlagIsFloat |
                             kAudioFormatFlagIsPacked;
    input_fmt.mBytesPerPacket = (UInt32)stack->input_channels *
                                (UInt32)sizeof(float);
    input_fmt.mFramesPerPacket = 1;
    input_fmt.mBytesPerFrame = input_fmt.mBytesPerPacket;
    input_fmt.mChannelsPerFrame = (UInt32)stack->input_channels;
    input_fmt.mBitsPerChannel = 32;

    AudioStreamBasicDescription output_fmt = input_fmt;
    output_fmt.mBytesPerPacket = 2u * (UInt32)sizeof(float);
    output_fmt.mBytesPerFrame = 2u * (UInt32)sizeof(float);
    output_fmt.mChannelsPerFrame = 2;

    OSStatus rc = AudioQueueNewInput(&input_fmt, audio_insert_input_callback, stack,
                                     NULL, NULL, 0, &stack->input_queue);
    if(rc == noErr)
        rc = AudioQueueNewOutput(&output_fmt, audio_insert_output_callback, stack,
                                 NULL, NULL, 0, &stack->output_queue);
    if(rc != noErr)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio CoreAudio duplex queue creation failed: %d",
                       (int)rc);
        return -1;
    }
    if((input_device != kAudioObjectUnknown &&
        coreaudio_set_queue_device(stack->input_queue, input_device) != 0) ||
       (output_device != kAudioObjectUnknown &&
        coreaudio_set_queue_device(stack->output_queue, output_device) != 0))
    {
        audio_set_error("std::audio CoreAudio failed to select duplex devices");
        return -1;
    }

    stack->input_capacity_frames = (uint64_t)stack->buffer_frames * 8u;
    stack->input_ring = (float*)calloc(
        (size_t)stack->input_capacity_frames * 2u, sizeof(float));
    if(!stack->input_ring)
    {
        audio_set_error("std::audio CoreAudio input ring allocation failed");
        return -1;
    }
    atomic_init(&stack->input_read_frame, 0u);
    atomic_init(&stack->input_write_frame, 0u);

    const UInt32 input_bytes = (UInt32)(stack->buffer_frames *
                                        stack->input_channels *
                                        (int64_t)sizeof(float));
    const UInt32 output_bytes = (UInt32)(stack->buffer_frames * 2 *
                                         (int64_t)sizeof(float));
    for(int i = 0; i < 3; ++i)
    {
        if(AudioQueueAllocateBuffer(stack->input_queue, input_bytes,
                                    &stack->input_buffers[i]) != noErr ||
           AudioQueueAllocateBuffer(stack->output_queue, output_bytes,
                                    &stack->output_buffers[i]) != noErr)
        {
            audio_set_error("std::audio CoreAudio duplex buffer allocation failed");
            return -1;
        }
        memset(stack->output_buffers[i]->mAudioData, 0, output_bytes);
        stack->output_buffers[i]->mAudioDataByteSize = output_bytes;
        if(AudioQueueEnqueueBuffer(stack->input_queue, stack->input_buffers[i],
                                   0, NULL) != noErr ||
           AudioQueueEnqueueBuffer(stack->output_queue,
                                   stack->output_buffers[i], 0, NULL) != noErr)
        {
            audio_set_error("std::audio CoreAudio duplex buffer enqueue failed");
            return -1;
        }
    }
    stack->backend = 1;
    return 0;
}
#endif

#if defined(__linux__)
static int audio_insert_stack_jack_process(jack_nframes_t nframes, void* arg)
{
    mlang_audio_insert_stack_t* stack = (mlang_audio_insert_stack_t*)arg;
    float* input_l = p_jack_port_get_buffer
                         ? (float*)p_jack_port_get_buffer(stack->in_l, nframes)
                         : NULL;
    float* input_r = p_jack_port_get_buffer
                         ? (float*)p_jack_port_get_buffer(stack->in_r, nframes)
                         : NULL;
    float* output_l = p_jack_port_get_buffer
                          ? (float*)p_jack_port_get_buffer(stack->out_l, nframes)
                          : NULL;
    float* output_r = p_jack_port_get_buffer
                          ? (float*)p_jack_port_get_buffer(stack->out_r, nframes)
                          : NULL;
    float peak = 0.0f;
    for(uint64_t frame = 0; frame < (uint64_t)nframes; ++frame)
    {
        const float left = input_l ? fabsf(input_l[frame]) : 0.0f;
        const float right = input_r ? fabsf(input_r[frame]) : 0.0f;
        if(left > peak)
            peak = left;
        if(right > peak)
            peak = right;
    }
    uint32_t peak_bits = 0;
    memcpy(&peak_bits, &peak, sizeof(peak_bits));
    atomic_store_explicit(&stack->input_peak_bits, peak_bits,
                          memory_order_release);
    (void)atomic_fetch_add_explicit(&stack->input_frames_received,
                                    (uint64_t)nframes, memory_order_relaxed);
    if(stack->mixer)
    {
        for(uint64_t frame = 0; frame < (uint64_t)nframes; ++frame)
            audio_mixer_process_sample(stack->mixer,
                                       input_l ? input_l[frame] : 0.0f,
                                       input_r ? input_r[frame] : 0.0f,
                                       &output_l[frame], &output_r[frame]);
    }
    else
        audio_insert_stack_process_frames(stack, input_l, input_r, output_l,
                                          output_r, (uint64_t)nframes);
    return 0;
}

static int audio_insert_stack_jack_open(mlang_audio_insert_stack_t* stack,
                                        const char* client_name)
{
    stack->jack_lib = audio_jack_load_query_lib();
    if(!stack->jack_lib)
    {
        audio_set_error("std::audio JACK2 libjack not found");
        return -1;
    }
    jack_client_open_fn open_fn = (jack_client_open_fn)jack_sym(
        stack->jack_lib, "jack_client_open");
    p_jack_client_close = (jack_client_close_fn)jack_sym(
        stack->jack_lib, "jack_client_close");
    p_jack_activate = (jack_activate_fn)jack_sym(stack->jack_lib,
                                                 "jack_activate");
    p_jack_deactivate = (jack_deactivate_fn)jack_sym(stack->jack_lib,
                                                     "jack_deactivate");
    jack_port_register_fn register_fn = (jack_port_register_fn)jack_sym(
        stack->jack_lib, "jack_port_register");
    jack_set_process_callback_fn callback_fn =
        (jack_set_process_callback_fn)jack_sym(stack->jack_lib,
                                               "jack_set_process_callback");
    jack_get_sample_rate_fn sample_rate_fn = (jack_get_sample_rate_fn)jack_sym(
        stack->jack_lib, "jack_get_sample_rate");
    jack_get_buffer_size_fn buffer_size_fn = (jack_get_buffer_size_fn)jack_sym(
        stack->jack_lib, "jack_get_buffer_size");
    p_jack_port_get_buffer = (jack_port_get_buffer_fn)jack_sym(
        stack->jack_lib, "jack_port_get_buffer");
    p_jack_port_name = (jack_port_name_fn)jack_sym(stack->jack_lib,
                                                   "jack_port_name");
    p_jack_get_ports = (jack_get_ports_fn)jack_sym(stack->jack_lib,
                                                   "jack_get_ports");
    p_jack_connect = (jack_connect_fn)jack_sym(stack->jack_lib,
                                               "jack_connect");
    p_jack_free = (jack_free_fn)jack_sym(stack->jack_lib, "jack_free");
    if(!open_fn || !p_jack_client_close || !p_jack_activate ||
       !p_jack_deactivate || !register_fn || !callback_fn ||
       !sample_rate_fn || !buffer_size_fn || !p_jack_port_get_buffer)
        return -1;

    jack_status_t status = 0;
    stack->jack_client = open_fn(
        client_name && client_name[0] ? client_name : "mlang_audio_inserts",
        MLANG_JACK_NULL_OPTION, &status);
    if(!stack->jack_client)
    {
        audio_set_error("std::audio JACK2 jack_client_open failed; is jackd running?");
        return -1;
    }
    stack->sample_rate = (double)sample_rate_fn(stack->jack_client);
    stack->buffer_frames = (int64_t)buffer_size_fn(stack->jack_client);
    stack->in_l = register_fn(stack->jack_client, "in_l",
                              MLANG_JACK_DEFAULT_AUDIO_TYPE,
                              MLANG_JACK_PORT_IS_INPUT, 0);
    stack->in_r = register_fn(stack->jack_client, "in_r",
                              MLANG_JACK_DEFAULT_AUDIO_TYPE,
                              MLANG_JACK_PORT_IS_INPUT, 0);
    stack->out_l = register_fn(stack->jack_client, "out_l",
                               MLANG_JACK_DEFAULT_AUDIO_TYPE,
                               MLANG_JACK_PORT_IS_OUTPUT, 0);
    stack->out_r = register_fn(stack->jack_client, "out_r",
                               MLANG_JACK_DEFAULT_AUDIO_TYPE,
                               MLANG_JACK_PORT_IS_OUTPUT, 0);
    if(!stack->in_l || !stack->in_r || !stack->out_l || !stack->out_r ||
       callback_fn(stack->jack_client, audio_insert_stack_jack_process,
                   stack) != 0)
    {
        audio_set_error("std::audio JACK2 duplex port setup failed");
        return -1;
    }
    stack->backend = 2;
    return 0;
}

static void audio_insert_stack_jack_autoconnect(
    mlang_audio_insert_stack_t* stack)
{
    if(!stack || !p_jack_get_ports || !p_jack_port_name || !p_jack_connect)
        return;
    const char** captures = p_jack_get_ports(
        stack->jack_client, NULL, MLANG_JACK_DEFAULT_AUDIO_TYPE,
        MLANG_JACK_PORT_IS_PHYSICAL | MLANG_JACK_PORT_IS_OUTPUT);
    const char** playbacks = p_jack_get_ports(
        stack->jack_client, NULL, MLANG_JACK_DEFAULT_AUDIO_TYPE,
        MLANG_JACK_PORT_IS_PHYSICAL | MLANG_JACK_PORT_IS_INPUT);
    const char* in_l = p_jack_port_name(stack->in_l);
    const char* in_r = p_jack_port_name(stack->in_r);
    const char* out_l = p_jack_port_name(stack->out_l);
    const char* out_r = p_jack_port_name(stack->out_r);
    const int64_t input_first = stack->input_device_id >= 0
        ? stack->input_device_id * 2 : 0;
    const int64_t output_first = stack->output_device_id >= 0
        ? stack->output_device_id * 2 : 0;
    if(captures && captures[input_first] && in_l)
        (void)p_jack_connect(stack->jack_client, captures[input_first], in_l);
    if(captures && captures[input_first + 1] && in_r)
        (void)p_jack_connect(stack->jack_client, captures[input_first + 1], in_r);
    else if(captures && captures[input_first] && in_r)
        (void)p_jack_connect(stack->jack_client, captures[input_first], in_r);
    if(playbacks && playbacks[output_first] && out_l)
        (void)p_jack_connect(stack->jack_client, out_l, playbacks[output_first]);
    if(playbacks && playbacks[output_first + 1] && out_r)
        (void)p_jack_connect(stack->jack_client, out_r, playbacks[output_first + 1]);
    else if(playbacks && playbacks[output_first] && out_r)
        (void)p_jack_connect(stack->jack_client, out_r, playbacks[output_first]);
    if(captures && p_jack_free)
        p_jack_free((void*)captures);
    if(playbacks && p_jack_free)
        p_jack_free((void*)playbacks);
}
#endif

int64_t __mlang_std_audio_insert_stack_open_devices(int64_t input_device_id,
                                                    int64_t output_device_id,
                                                    const char* client_name,
                                                    int64_t sample_rate,
                                                    int64_t buffer_frames)
{
    const int64_t handle = __mlang_std_audio_insert_stack_new(sample_rate,
                                                              buffer_frames);
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    if(!stack)
        return 0;
    stack->input_device_id = input_device_id >= 0
        ? input_device_id : __mlang_std_audio_default_input_device_id();
    stack->output_device_id = output_device_id >= 0
        ? output_device_id : __mlang_std_audio_default_output_device_id();
    if(stack->input_device_id < 0 ||
       stack->input_device_id >= __mlang_std_audio_input_device_count())
    {
        __mlang_std_audio_insert_stack_close(handle);
        audio_set_error("std::audio selected input device is unavailable");
        return 0;
    }
    if(stack->output_device_id < 0 ||
       stack->output_device_id >= __mlang_std_audio_device_count())
    {
        __mlang_std_audio_insert_stack_close(handle);
        audio_set_error("std::audio selected output device is unavailable");
        return 0;
    }
#if defined(__APPLE__)
    (void)client_name;
    if(audio_insert_stack_coreaudio_open(stack, stack->input_device_id,
                                         stack->output_device_id,
                                         sample_rate > 0 ? 1 : 0) != 0)
    {
        char saved_error[sizeof(g_audio_last_error)];
        (void)snprintf(saved_error, sizeof(saved_error), "%s",
                       g_audio_last_error);
        __mlang_std_audio_insert_stack_close(handle);
        audio_set_error(saved_error);
        return 0;
    }
#elif defined(__linux__)
    if(audio_insert_stack_jack_open(stack, client_name) != 0)
    {
        char saved_error[sizeof(g_audio_last_error)];
        (void)snprintf(saved_error, sizeof(saved_error), "%s",
                       g_audio_last_error);
        __mlang_std_audio_insert_stack_close(handle);
        audio_set_error(saved_error);
        return 0;
    }
#else
    (void)client_name;
    __mlang_std_audio_insert_stack_close(handle);
    audio_set_error("std::audio duplex backend unsupported on this platform");
    return 0;
#endif
    audio_clear_error();
    return handle;
}

int64_t __mlang_std_audio_insert_stack_open_default(const char* client_name,
                                                     int64_t sample_rate,
                                                     int64_t buffer_frames)
{
    return __mlang_std_audio_insert_stack_open_devices(
        -1, -1, client_name, sample_rate, buffer_frames);
}

int32_t __mlang_std_audio_insert_stack_start(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    if(!stack || stack->backend == 0)
    {
        audio_set_error("std::audio start: insert stack has no duplex device");
        return -1;
    }
    atomic_store_explicit(&stack->input_frames_received, 0u,
                          memory_order_release);
    atomic_store_explicit(&stack->input_peak_bits, 0u, memory_order_release);
    atomic_store_explicit(&stack->running, 1, memory_order_release);
#if defined(__APPLE__)
    OSStatus rc = AudioQueueStart(stack->input_queue, NULL);
    if(rc != noErr)
    {
        atomic_store_explicit(&stack->running, 0, memory_order_release);
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio CoreAudio input start failed: %d; check microphone permission",
                       (int)rc);
        return -1;
    }
    rc = AudioQueueStart(stack->output_queue, NULL);
    if(rc != noErr)
    {
        atomic_store_explicit(&stack->running, 0, memory_order_release);
        (void)AudioQueueStop(stack->input_queue, true);
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio CoreAudio output start failed: %d", (int)rc);
        return -1;
    }
#elif defined(__linux__)
    if(!p_jack_activate || p_jack_activate(stack->jack_client) != 0)
    {
        atomic_store_explicit(&stack->running, 0, memory_order_release);
        audio_set_error("std::audio JACK2 duplex activation failed");
        return -1;
    }
    audio_insert_stack_jack_autoconnect(stack);
#endif
    audio_clear_error();
    return 0;
}

int32_t __mlang_std_audio_insert_stack_stop(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    if(!stack)
        return 0;
    atomic_store_explicit(&stack->running, 0, memory_order_release);
#if defined(__APPLE__)
    if(stack->input_queue)
        (void)AudioQueueStop(stack->input_queue, true);
    if(stack->output_queue)
        (void)AudioQueueStop(stack->output_queue, true);
#elif defined(__linux__)
    if(stack->jack_client && p_jack_deactivate)
        (void)p_jack_deactivate(stack->jack_client);
#endif
    return 0;
}

int32_t __mlang_std_audio_insert_stack_close(int64_t handle)
{
    mlang_audio_insert_stack_t* stack = audio_insert_stack_from_handle(handle);
    if(!stack)
        return 0;
    (void)__mlang_std_audio_insert_stack_stop(handle);
#if defined(__APPLE__)
    if(stack->input_queue)
        AudioQueueDispose(stack->input_queue, true);
    if(stack->output_queue)
        AudioQueueDispose(stack->output_queue, true);
    free(stack->input_ring);
#elif defined(__linux__)
    if(stack->jack_client && p_jack_client_close)
        (void)p_jack_client_close(stack->jack_client);
    if(stack->jack_lib)
        dlclose(stack->jack_lib);
#endif
    for(int i = 0; i < stack->insert_count; ++i)
        audio_effect_release(&stack->inserts[i]);
    for(int rack_index = 0; rack_index < stack->rack_count; ++rack_index)
        for(int effect_index = 0;
            effect_index < stack->racks[rack_index].effect_count;
            ++effect_index)
            audio_effect_release(
                &stack->racks[rack_index].effects[effect_index]);
    free(stack);
    audio_clear_error();
    return 0;
}

static mlang_audio_mixer_t* audio_mixer_from_handle(int64_t handle)
{
    return (mlang_audio_mixer_t*)(intptr_t)handle;
}

static int audio_mixer_can_configure(mlang_audio_mixer_t* mixer,
                                     const char* operation)
{
    if(!mixer)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio %s: invalid mixer handle", operation);
        return 0;
    }
    if(atomic_load_explicit(&mixer->running, memory_order_acquire))
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio %s: stop the mixer before changing routing",
                       operation);
        return 0;
    }
    return 1;
}

static mlang_audio_mixer_track_t* audio_mixer_track(
    mlang_audio_mixer_t* mixer, int64_t track_id, const char* operation)
{
    if(!mixer || track_id < 0 || track_id >= mixer->track_count ||
       !mixer->tracks[track_id].active)
    {
        (void)snprintf(g_audio_last_error, sizeof(g_audio_last_error),
                       "std::audio %s: invalid track id", operation);
        return NULL;
    }
    return &mixer->tracks[track_id];
}

static int audio_mixer_rebuild_order(mlang_audio_mixer_t* mixer)
{
    unsigned char edges[MLANG_AUDIO_MAX_MIXER_TRACKS]
                       [MLANG_AUDIO_MAX_MIXER_TRACKS];
    int indegree[MLANG_AUDIO_MAX_MIXER_TRACKS];
    int queue[MLANG_AUDIO_MAX_MIXER_TRACKS];
    memset(edges, 0, sizeof(edges));
    memset(indegree, 0, sizeof(indegree));

    for(int i = 0; i < mixer->track_count; ++i)
    {
        mlang_audio_mixer_track_t* track = &mixer->tracks[i];
        int destinations[2 + MLANG_AUDIO_MAX_TRACK_SENDS];
        int destination_count = 0;
        if(track->input_kind == 2 && track->input_track >= 0)
            if(!edges[track->input_track][i])
                edges[track->input_track][i] = 1;
        if(track->output_track >= 0)
            destinations[destination_count++] = track->output_track;
        for(int send_index = 0; send_index < track->send_count; ++send_index)
            if(track->sends[send_index].enabled)
                destinations[destination_count++] =
                    track->sends[send_index].return_track;
        for(int d = 0; d < destination_count; ++d)
            if(!edges[i][destinations[d]])
                edges[i][destinations[d]] = 1;
    }
    for(int from = 0; from < mixer->track_count; ++from)
        for(int to = 0; to < mixer->track_count; ++to)
            if(edges[from][to])
                ++indegree[to];

    int head = 0;
    int tail = 0;
    for(int i = 0; i < mixer->track_count; ++i)
        if(indegree[i] == 0)
            queue[tail++] = i;
    mixer->order_count = 0;
    while(head < tail)
    {
        const int from = queue[head++];
        mixer->order[mixer->order_count++] = from;
        for(int to = 0; to < mixer->track_count; ++to)
            if(edges[from][to] && --indegree[to] == 0)
                queue[tail++] = to;
    }
    if(mixer->order_count != mixer->track_count)
    {
        audio_set_error("std::audio routing would create an audio feedback cycle");
        return -1;
    }
    return 0;
}

int64_t __mlang_std_audio_mixer_new(int64_t sample_rate,
                                    int64_t buffer_frames)
{
    mlang_audio_mixer_t* mixer =
        (mlang_audio_mixer_t*)calloc(1u, sizeof(*mixer));
    if(!mixer)
    {
        audio_set_error("std::audio mixer allocation failed");
        return 0;
    }
    mixer->sample_rate = (double)audio_normalize_sample_rate(sample_rate);
    mixer->buffer_frames = audio_normalize_buffer_frames(buffer_frames);
    atomic_init(&mixer->master_gain, 1.0f);
    atomic_init(&mixer->running, 0);
    audio_clear_error();
    return (int64_t)(intptr_t)mixer;
}

int64_t __mlang_std_audio_mixer_open_devices(int64_t input_device_id,
                                             int64_t output_device_id,
                                             const char* client_name,
                                             int64_t sample_rate,
                                             int64_t buffer_frames)
{
    const int64_t mixer_handle = __mlang_std_audio_mixer_new(
        sample_rate, buffer_frames);
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(mixer_handle);
    if(!mixer)
        return 0;
    const int64_t io_handle = __mlang_std_audio_insert_stack_open_devices(
        input_device_id, output_device_id, client_name, sample_rate,
        buffer_frames);
    mlang_audio_insert_stack_t* io = audio_insert_stack_from_handle(io_handle);
    if(!io)
    {
        char saved_error[sizeof(g_audio_last_error)];
        (void)snprintf(saved_error, sizeof(saved_error), "%s",
                       g_audio_last_error);
        __mlang_std_audio_mixer_close(mixer_handle);
        audio_set_error(saved_error);
        return 0;
    }
    mixer->sample_rate = io->sample_rate;
    mixer->buffer_frames = io->buffer_frames;
    mixer->io = io;
    io->mixer = mixer;
    audio_clear_error();
    return mixer_handle;
}

int64_t __mlang_std_audio_mixer_open_default(const char* client_name,
                                              int64_t sample_rate,
                                              int64_t buffer_frames)
{
    return __mlang_std_audio_mixer_open_devices(
        -1, -1, client_name, sample_rate, buffer_frames);
}

static int64_t audio_mixer_add_track(mlang_audio_mixer_t* mixer,
                                     const char* name, int is_return)
{
    if(!audio_mixer_can_configure(mixer, "add_track"))
        return -1;
    if(mixer->track_count >= MLANG_AUDIO_MAX_MIXER_TRACKS)
    {
        audio_set_error("std::audio mixer is full (maximum 32 tracks)");
        return -1;
    }
    if(is_return && mixer->return_count >= MLANG_AUDIO_MAX_TRACK_SENDS)
    {
        audio_set_error("std::audio mixer is full (maximum 8 return tracks)");
        return -1;
    }
    const int id = mixer->track_count++;
    mlang_audio_mixer_track_t* track = &mixer->tracks[id];
    memset(track, 0, sizeof(*track));
    track->active = 1;
    track->is_return = is_return;
    track->input_kind = is_return ? 0 : 1;
    track->input_track = -1;
    track->output_track = -1;
    track->volume_current = 1.0f;
    track->volume_target = 1.0f;
    track->pan_current = 0.0f;
    track->pan_target = 0.0f;
    track->volume_command_seen = audio_control_command(1.0f, 0);
    track->pan_command_seen = audio_control_command(0.0f, 0);
    atomic_init(&track->volume_command, track->volume_command_seen);
    atomic_init(&track->pan_command, track->pan_command_seen);
    atomic_init(&track->muted, 0);
    if(is_return)
    {
        for(int source_id = 0; source_id < id; ++source_id)
        {
            mlang_audio_mixer_track_t* source = &mixer->tracks[source_id];
            if(source->is_return)
                continue;
            mlang_audio_track_send_t* send =
                &source->sends[source->send_count++];
            send->return_track = id;
            send->post_fader = 1;
        }
        ++mixer->return_count;
    }
    else
    {
        for(int target_id = 0; target_id < id; ++target_id)
        {
            if(!mixer->tracks[target_id].is_return)
                continue;
            mlang_audio_track_send_t* send = &track->sends[track->send_count++];
            send->return_track = target_id;
            send->post_fader = 1;
        }
    }
    (void)snprintf(track->name, sizeof(track->name), "%s",
                   name && name[0] ? name : (is_return ? "Return" : "Audio"));
    (void)audio_mixer_rebuild_order(mixer);
    audio_clear_error();
    return id;
}

int64_t __mlang_std_audio_mixer_add_audio_track(int64_t handle,
                                                const char* name)
{
    return audio_mixer_add_track(audio_mixer_from_handle(handle), name, 0);
}

int64_t __mlang_std_audio_mixer_add_return_track(int64_t handle,
                                                 const char* name)
{
    return audio_mixer_add_track(audio_mixer_from_handle(handle), name, 1);
}

int32_t __mlang_std_audio_mixer_track_set_input_device(int64_t handle,
                                                       int64_t track_id)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!audio_mixer_can_configure(mixer, "set_input_device"))
        return -1;
    mlang_audio_mixer_track_t* track = audio_mixer_track(
        mixer, track_id, "set_input_device");
    if(!track)
        return -1;
    if(track->is_return)
    {
        audio_set_error("std::audio return tracks accept sends, not device inputs");
        return -1;
    }
    const int old_kind = track->input_kind;
    const int old_track = track->input_track;
    track->input_kind = 1;
    track->input_track = -1;
    if(audio_mixer_rebuild_order(mixer) != 0)
    {
        track->input_kind = old_kind;
        track->input_track = old_track;
        (void)audio_mixer_rebuild_order(mixer);
        return -1;
    }
    audio_clear_error();
    return 0;
}

int32_t __mlang_std_audio_mixer_track_set_input_track(int64_t handle,
                                                      int64_t track_id,
                                                      int64_t source_id)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!audio_mixer_can_configure(mixer, "set_input_track"))
        return -1;
    mlang_audio_mixer_track_t* track = audio_mixer_track(
        mixer, track_id, "set_input_track");
    if(!track || !audio_mixer_track(mixer, source_id, "set_input_track"))
        return -1;
    if(track->is_return)
    {
        audio_set_error("std::audio return tracks accept sends, not track inputs");
        return -1;
    }
    const int old_kind = track->input_kind;
    const int old_track = track->input_track;
    track->input_kind = 2;
    track->input_track = (int)source_id;
    if(audio_mixer_rebuild_order(mixer) != 0)
    {
        track->input_kind = old_kind;
        track->input_track = old_track;
        (void)audio_mixer_rebuild_order(mixer);
        return -1;
    }
    audio_clear_error();
    return 0;
}

int32_t __mlang_std_audio_mixer_track_set_output(int64_t handle,
                                                 int64_t track_id,
                                                 int64_t destination_id)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!audio_mixer_can_configure(mixer, "set_output"))
        return -1;
    mlang_audio_mixer_track_t* track = audio_mixer_track(
        mixer, track_id, "set_output");
    if(!track)
        return -1;
    if(destination_id < -1)
    {
        audio_set_error("std::audio set_output: destination must be master or a track");
        return -1;
    }
    if(track->is_return && destination_id >= 0)
    {
        audio_set_error("std::audio return tracks are always routed to master");
        return -1;
    }
    if(destination_id >= 0)
    {
        mlang_audio_mixer_track_t* destination = audio_mixer_track(
            mixer, destination_id, "set_output");
        if(!destination)
            return -1;
        if(destination->is_return)
        {
            audio_set_error("std::audio effect returns are reached only through sends");
            return -1;
        }
    }
    const int old_output = track->output_track;
    track->output_track = (int)destination_id;
    if(audio_mixer_rebuild_order(mixer) != 0)
    {
        track->output_track = old_output;
        (void)audio_mixer_rebuild_order(mixer);
        return -1;
    }
    audio_clear_error();
    return 0;
}

static int32_t audio_mixer_track_set_control(int64_t handle, int64_t track_id,
                                             double value, double ramp_ms,
                                             int is_pan)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    mlang_audio_mixer_track_t* track = audio_mixer_track(
        mixer, track_id, is_pan ? "set_track_pan" : "set_track_volume");
    const int value_invalid = is_pan ? (value < -1.0 || value > 1.0)
                                     : (value < 0.0 || value > 16.0);
    if(!track || value_invalid || ramp_ms < 0.0 || ramp_ms > 60000.0)
    {
        if(track)
            audio_set_error(is_pan
                ? "std::audio pan expects [-1, 1] and ramp_ms in [0, 60000]"
                : "std::audio volume expects [0, 16] and ramp_ms in [0, 60000]");
        return -1;
    }
    uint64_t frames = (uint64_t)(mixer->sample_rate * ramp_ms / 1000.0 + 0.5);
    if(frames > UINT32_MAX)
        frames = UINT32_MAX;
    const uint64_t command = audio_control_command((float)value,
                                                    (uint32_t)frames);
    atomic_store_explicit(is_pan ? &track->pan_command : &track->volume_command,
                          command, memory_order_release);
    audio_clear_error();
    return 0;
}

int32_t __mlang_std_audio_mixer_track_set_volume(int64_t handle,
                                                 int64_t track_id,
                                                 double volume,
                                                 double ramp_ms)
{
    return audio_mixer_track_set_control(handle, track_id, volume, ramp_ms, 0);
}

int32_t __mlang_std_audio_mixer_track_set_pan(int64_t handle,
                                              int64_t track_id, double pan,
                                              double ramp_ms)
{
    return audio_mixer_track_set_control(handle, track_id, pan, ramp_ms, 1);
}

int32_t __mlang_std_audio_mixer_track_set_gain(int64_t handle,
                                               int64_t track_id, double gain)
{
    return __mlang_std_audio_mixer_track_set_volume(handle, track_id, gain, 0.0);
}

int32_t __mlang_std_audio_mixer_track_set_muted(int64_t handle,
                                                int64_t track_id,
                                                int32_t muted)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    mlang_audio_mixer_track_t* track = audio_mixer_track(
        mixer, track_id, "set_track_muted");
    if(!track)
        return -1;
    atomic_store_explicit(&track->muted, muted ? 1 : 0,
                          memory_order_relaxed);
    audio_clear_error();
    return 0;
}

int32_t __mlang_std_audio_mixer_set_master_gain(int64_t handle, double gain)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!mixer)
    {
        audio_set_error("std::audio set_master_gain: invalid mixer handle");
        return -1;
    }
    if(gain < 0.0 || gain > 16.0)
    {
        audio_set_error("std::audio master gain expects a value in [0, 16]");
        return -1;
    }
    atomic_store_explicit(&mixer->master_gain, (float)gain,
                          memory_order_relaxed);
    audio_clear_error();
    return 0;
}

int32_t __mlang_std_audio_mixer_track_set_send(int64_t handle,
                                               int64_t track_id,
                                               int64_t return_id,
                                               double level,
                                               int32_t post_fader)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!audio_mixer_can_configure(mixer, "set_send"))
        return -1;
    mlang_audio_mixer_track_t* track = audio_mixer_track(
        mixer, track_id, "set_send");
    mlang_audio_mixer_track_t* target = audio_mixer_track(
        mixer, return_id, "set_send");
    if(!track || !target)
        return -1;
    if(!target->is_return)
    {
        audio_set_error("std::audio sends must target a return track");
        return -1;
    }
    if(level < 0.0 || level > 4.0)
    {
        audio_set_error("std::audio send level expects a value in [0, 4]");
        return -1;
    }
    int send_index = -1;
    for(int i = 0; i < track->send_count; ++i)
        if(track->sends[i].return_track == return_id)
            send_index = i;
    const int is_new_send = send_index < 0;
    if(is_new_send)
    {
        if(track->send_count >= MLANG_AUDIO_MAX_TRACK_SENDS)
        {
            audio_set_error("std::audio track is full (maximum 8 sends)");
            return -1;
        }
        send_index = track->send_count++;
    }
    mlang_audio_track_send_t old_send = track->sends[send_index];
    track->sends[send_index].enabled = level > 0.0 ? 1 : 0;
    track->sends[send_index].return_track = (int)return_id;
    track->sends[send_index].level = (float)level;
    track->sends[send_index].post_fader = post_fader ? 1 : 0;
    if(audio_mixer_rebuild_order(mixer) != 0)
    {
        track->sends[send_index] = old_send;
        if(is_new_send)
            --track->send_count;
        (void)audio_mixer_rebuild_order(mixer);
        return -1;
    }
    audio_clear_error();
    return 0;
}

static int64_t audio_mixer_track_add_effect(int64_t handle, int64_t track_id,
                                            int kind, double p1, double p2,
                                            double wet)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!audio_mixer_can_configure(mixer, "add_track_insert"))
        return -1;
    mlang_audio_mixer_track_t* track = audio_mixer_track(
        mixer, track_id, "add_track_insert");
    if(!track)
        return -1;
    if(track->insert_count >= MLANG_AUDIO_MAX_INSERTS)
    {
        audio_set_error("std::audio track insert stack is full (maximum 16 effects)");
        return -1;
    }
    if(kind == MLANG_AUDIO_EFFECT_GAIN && (p1 < 0.0 || p1 > 16.0))
    {
        audio_set_error("std::audio gain expects a linear gain in [0, 16]");
        return -1;
    }
    if(kind == MLANG_AUDIO_EFFECT_LOWPASS &&
       (p1 < 10.0 || p1 >= mixer->sample_rate * 0.5))
    {
        audio_set_error("std::audio low-pass cutoff must be between 10 Hz and Nyquist");
        return -1;
    }
    if(kind == MLANG_AUDIO_EFFECT_DISTORTION && (p1 < 0.01 || p1 > 100.0))
    {
        audio_set_error("std::audio distortion drive expects a value in [0.01, 100]");
        return -1;
    }
    const int id = track->insert_count;
    if(audio_effect_initialize(&track->inserts[id], kind, (float)p1,
                               (float)p2, (float)wet,
                               mixer->sample_rate) != 0)
        return -1;
    ++track->insert_count;
    audio_clear_error();
    return id;
}

int64_t __mlang_std_audio_mixer_track_add_gain(int64_t handle,
                                               int64_t track_id,
                                               double gain, double wet)
{
    return audio_mixer_track_add_effect(handle, track_id,
                                        MLANG_AUDIO_EFFECT_GAIN, gain, 0.0, wet);
}

int64_t __mlang_std_audio_mixer_track_add_lowpass(int64_t handle,
                                                  int64_t track_id,
                                                  double cutoff_hz, double wet)
{
    return audio_mixer_track_add_effect(handle, track_id,
                                        MLANG_AUDIO_EFFECT_LOWPASS,
                                        cutoff_hz, 0.0, wet);
}

int64_t __mlang_std_audio_mixer_track_add_distortion(int64_t handle,
                                                     int64_t track_id,
                                                     double drive, double wet)
{
    return audio_mixer_track_add_effect(handle, track_id,
                                        MLANG_AUDIO_EFFECT_DISTORTION,
                                        drive, 0.0, wet);
}

int64_t __mlang_std_audio_mixer_track_add_delay(int64_t handle,
                                                int64_t track_id,
                                                double delay_ms,
                                                double feedback, double wet)
{
    return audio_mixer_track_add_effect(handle, track_id,
                                        MLANG_AUDIO_EFFECT_DELAY, delay_ms,
                                        feedback, wet);
}

int32_t __mlang_std_audio_mixer_process_block(int64_t handle,
                                              int64_t input_handle,
                                              int64_t output_handle,
                                              int64_t frames)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    mlang_pcm_block_t* input = (mlang_pcm_block_t*)(intptr_t)input_handle;
    mlang_pcm_block_t* output = (mlang_pcm_block_t*)(intptr_t)output_handle;
    if(!mixer || !input || !output || !input->samples || !output->samples ||
       frames < 0 || frames > input->capacity_frames ||
       frames > output->capacity_frames)
    {
        audio_set_error("std::audio mixer process_block: invalid arguments");
        return -1;
    }
    for(int64_t frame = 0; frame < frames; ++frame)
    {
        const float input_l = input->samples[frame * 2];
        const float input_r = input->samples[frame * 2 + 1];
        audio_mixer_process_sample(mixer, input_l, input_r,
                                   &output->samples[frame * 2],
                                   &output->samples[frame * 2 + 1]);
    }
    audio_clear_error();
    return 0;
}

int64_t __mlang_std_audio_mixer_track_count(int64_t handle)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    return mixer ? mixer->track_count : 0;
}

int64_t __mlang_std_audio_mixer_sample_rate(int64_t handle)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    return mixer ? (int64_t)(mixer->sample_rate + 0.5) : 0;
}

int64_t __mlang_std_audio_mixer_buffer_frames(int64_t handle)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    return mixer ? mixer->buffer_frames : 0;
}

int64_t __mlang_std_audio_mixer_input_frames_received(int64_t handle)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    return mixer && mixer->io
               ? __mlang_std_audio_insert_stack_input_frames_received(
                     (int64_t)(intptr_t)mixer->io)
               : 0;
}

double __mlang_std_audio_mixer_input_peak(int64_t handle)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    return mixer && mixer->io
               ? __mlang_std_audio_insert_stack_input_peak(
                     (int64_t)(intptr_t)mixer->io)
               : 0.0;
}

int32_t __mlang_std_audio_mixer_start(int64_t handle)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!mixer || !mixer->io)
    {
        audio_set_error("std::audio mixer start requires open_default()");
        return -1;
    }
    atomic_store_explicit(&mixer->running, 1, memory_order_release);
    if(__mlang_std_audio_insert_stack_start(
           (int64_t)(intptr_t)mixer->io) != 0)
    {
        atomic_store_explicit(&mixer->running, 0, memory_order_release);
        return -1;
    }
    return 0;
}

int32_t __mlang_std_audio_mixer_stop(int64_t handle)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!mixer)
        return 0;
    atomic_store_explicit(&mixer->running, 0, memory_order_release);
    if(mixer->io)
        return __mlang_std_audio_insert_stack_stop(
            (int64_t)(intptr_t)mixer->io);
    return 0;
}

int32_t __mlang_std_audio_mixer_close(int64_t handle)
{
    mlang_audio_mixer_t* mixer = audio_mixer_from_handle(handle);
    if(!mixer)
        return 0;
    (void)__mlang_std_audio_mixer_stop(handle);
    if(mixer->io)
    {
        mixer->io->mixer = NULL;
        (void)__mlang_std_audio_insert_stack_close(
            (int64_t)(intptr_t)mixer->io);
    }
    for(int track_id = 0; track_id < mixer->track_count; ++track_id)
        for(int insert = 0;
            insert < mixer->tracks[track_id].insert_count; ++insert)
            audio_effect_release(&mixer->tracks[track_id].inserts[insert]);
    free(mixer);
    audio_clear_error();
    return 0;
}
