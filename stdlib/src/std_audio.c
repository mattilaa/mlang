#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

typedef struct mlang_audio_device mlang_audio_device_t;

int32_t __mlang_std_audio_close(int64_t handle);

struct mlang_audio_device
{
    int backend;
    int running;
    double sample_rate;
    int64_t buffer_frames;
    double phase;
    double frequency_hz;
    double gain;
    int64_t frames_left;
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

static char g_audio_last_error[512];

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

#if defined(__APPLE__)
static void audioqueue_fill(mlang_audio_device_t* d, AudioQueueBufferRef buffer)
{
    if(!d || !buffer)
        return;
    const int64_t frames = d->buffer_frames > 0 ? d->buffer_frames : 512;
    float* samples = (float*)buffer->mAudioData;
    for(int64_t i = 0; i < frames; ++i)
    {
        float s = audio_next_sample(d);
        samples[i * 2] = s;
        samples[i * 2 + 1] = s;
    }
    buffer->mAudioDataByteSize = (UInt32)(frames * 2 * (int64_t)sizeof(float));
}

static void audioqueue_callback(void* user_data, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)user_data;
    audioqueue_fill(d, buffer);
    (void)AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

static int audio_coreaudio_open(mlang_audio_device_t* d)
{
    d->backend = 1;
    d->sample_rate = 48000.0;
    d->buffer_frames = 512;

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
    for(jack_nframes_t i = 0; i < nframes; ++i)
    {
        float s = audio_next_sample(d);
        out_l[i] = s;
        out_r[i] = s;
    }
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
    if(ports[0] && l)
        (void)p_jack_connect(d->jack_client, l, ports[0]);
    if(ports[1] && r)
        (void)p_jack_connect(d->jack_client, r, ports[1]);
    else if(ports[0] && r)
        (void)p_jack_connect(d->jack_client, r, ports[0]);
    if(p_jack_free)
        p_jack_free((void*)ports);
}
#endif

int64_t __mlang_std_audio_open_default_output(const char* client_name)
{
    mlang_audio_device_t* d = (mlang_audio_device_t*)calloc(1u, sizeof(*d));
    if(!d)
    {
        audio_set_error("std::audio allocation failed");
        return 0;
    }
    d->sample_rate = 48000.0;
    d->buffer_frames = 512;
    d->frequency_hz = 440.0;
    d->gain = 0.15;
    d->frames_left = 0;

#if defined(__APPLE__)
    (void)client_name;
    if(audio_coreaudio_open(d) != 0)
    {
        __mlang_std_audio_close((int64_t)(intptr_t)d);
        return 0;
    }
#elif defined(__linux__)
    if(audio_jack_open(d, client_name) != 0)
    {
        __mlang_std_audio_close((int64_t)(intptr_t)d);
        return 0;
    }
#else
    (void)client_name;
    free(d);
    audio_set_error("std::audio backend unsupported on this platform");
    return 0;
#endif
    audio_clear_error();
    return (int64_t)(intptr_t)d;
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
    audio_clear_error();
    return 0;
}
