#include <math.h>
#include <stdint.h>
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

static int audio_coreaudio_open(mlang_audio_device_t* d, int64_t device_id)
{
    d->backend = 1;
    d->sample_rate = 48000.0;
    d->buffer_frames = 512;
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

int64_t __mlang_std_audio_open_output_device(int64_t device_id, const char* client_name)
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
    d->device_id = device_id;

#if defined(__APPLE__)
    (void)client_name;
    if(device_id < 0)
        d->device_id = coreaudio_default_output_index();
    if(audio_coreaudio_open(d, d->device_id) != 0)
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
    free(d);
    audio_set_error("std::audio backend unsupported on this platform");
    return 0;
#endif
    audio_clear_error();
    return (int64_t)(intptr_t)d;
}

int64_t __mlang_std_audio_open_default_output(const char* client_name)
{
    return __mlang_std_audio_open_output_device(-1, client_name);
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
