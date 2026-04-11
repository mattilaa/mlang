#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../include/mlang_c_types.h"

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

static char g_last_error[512];
static int64_t g_last_sample_rate = 0;
static const float k_sample_scale = 1048576.0f;

static void set_error(const char* msg)
{
    if(!msg)
    {
        g_last_error[0] = '\0';
        return;
    }
    snprintf(g_last_error, sizeof(g_last_error), "%s", msg);
}

static void set_osstatus_error(const char* prefix, OSStatus status)
{
    snprintf(g_last_error, sizeof(g_last_error), "%s (OSStatus=%d)", prefix, (int)status);
}

static void* checked_calloc(size_t count, size_t size)
{
    void* p = calloc(count, size);
    if(!p)
        set_error("out of memory");
    return p;
}

static void cf_release_if_needed(CFTypeRef obj)
{
    if(obj)
        CFRelease(obj);
}

mlang_string coreaudio_last_error(void)
{
    return g_last_error;
}

mlang_float coreaudio_parse_f32(mlang_string s)
{
    if(!s || !s[0])
        return 0.0f;
    return strtof(s, NULL);
}

mlang_float coreaudio_i64_to_f32(int64_t v)
{
    return (mlang_float)v;
}

int64_t coreaudio_f32_to_i64(mlang_float v)
{
    if(v <= 0.0f)
        return 0;
    return (int64_t)llroundf(v);
}

int64_t coreaudio_decoded_sample_rate(void)
{
    return g_last_sample_rate;
}

mlang_float coreaudio_sample_i64_to_f32(int64_t v)
{
    return ((mlang_float)v) / k_sample_scale;
}

int64_t coreaudio_sample_f32_to_i64(mlang_float v)
{
    if(v > 1.0f)
        v = 1.0f;
    else if(v < -1.0f)
        v = -1.0f;
    return (int64_t)llroundf(v * k_sample_scale);
}

static float soft_clip(float x)
{
    float ax = fabsf(x);
    return x / (1.0f + (0.45f * ax));
}

static int64_t tap_frames(float base_ms, float size, int64_t sample_rate)
{
    float shaped_ms = base_ms * (0.45f + (size * 1.15f));
    int64_t frames = coreaudio_f32_to_i64((shaped_ms * coreaudio_i64_to_f32(sample_rate)) / 1000.0f);
    return frames < 1 ? 1 : frames;
}

static float gate_gain(float level)
{
    float opened = (level - 0.015f) * 32.0f;
    if(opened < 0.0f) return 0.0f;
    if(opened > 1.0f) return 1.0f;
    return opened;
}

static float sample_stereo_i64(const int64_t* samples, int64_t sample_count, int64_t frame_index, int64_t channel)
{
    if(!samples || frame_index < 0)
        return 0.0f;
    int64_t idx = frame_index * 2 + channel;
    if(idx < 0 || idx >= sample_count)
        return 0.0f;
    return coreaudio_sample_i64_to_f32(samples[idx]);
}

mlang_list_t coreaudio_process_reverb_i64(mlang_list_t samples, int64_t sample_rate,
                                          float mix, float early_mix, float size,
                                          float decay, float damping, float bandwidth,
                                          float density, float predelay_ms,
                                          float gain, int32_t gate_mode)
{
    mlang_list_t out;
    out.size = 0;
    out.data = NULL;

    if(!samples.data || samples.size <= 1 || (samples.size & 1) != 0 || sample_rate <= 0)
    {
        set_error("invalid input to reverb processor");
        return out;
    }

    int64_t frames = samples.size / 2;
    int64_t* history = (int64_t*)checked_calloc((size_t)samples.size, sizeof(int64_t));
    int64_t* output = (int64_t*)checked_calloc((size_t)samples.size, sizeof(int64_t));
    if(!history || !output)
    {
        free(history);
        free(output);
        return out;
    }

    int64_t predelay = tap_frames(predelay_ms, 1.0f, sample_rate);
    int64_t e1 = tap_frames(6.7f, size, sample_rate);
    int64_t e2 = tap_frames(11.9f, size, sample_rate);
    int64_t e3 = tap_frames(19.3f, size, sample_rate);
    int64_t e4 = tap_frames(31.1f, size, sample_rate);
    int64_t l1 = tap_frames(33.0f, size, sample_rate);
    int64_t l2 = tap_frames(41.0f, size, sample_rate);
    int64_t l3 = tap_frames(47.0f, size, sample_rate);
    int64_t l4 = tap_frames(59.0f, size, sample_rate);
    int64_t l5 = tap_frames(71.0f, size, sample_rate);
    int64_t l6 = tap_frames(89.0f, size, sample_rate);

    float bw_coef = 0.04f + (bandwidth * 0.50f);
    float damp_coef = 0.02f + ((1.0f - damping) * 0.18f);
    float decay_gain = 0.18f + (decay * 0.67f);
    float dense_gain = 0.06f + (density * 0.26f);

    float bw_l = 0.0f;
    float bw_r = 0.0f;
    float damp_l = 0.0f;
    float damp_r = 0.0f;
    float gate_env = 0.0f;

    int64_t sample_count = samples.size;
    int64_t* in = (int64_t*)samples.data;
    for(int64_t i = 0; i < frames; ++i)
    {
        float dry_l = sample_stereo_i64(in, sample_count, i, 0) * gain;
        float dry_r = sample_stereo_i64(in, sample_count, i, 1) * gain;
        float pre_l_src = sample_stereo_i64(in, sample_count, i - predelay, 0) * gain;
        float pre_r_src = sample_stereo_i64(in, sample_count, i - predelay, 1) * gain;

        bw_l = bw_l + (bw_coef * (pre_l_src - bw_l));
        bw_r = bw_r + (bw_coef * (pre_r_src - bw_r));

        float early_l =
            (0.46f * sample_stereo_i64(in, sample_count, i - predelay - e1, 0)) +
            (0.23f * sample_stereo_i64(in, sample_count, i - predelay - e2, 1)) +
            (0.17f * sample_stereo_i64(in, sample_count, i - predelay - e3, 0)) +
            (0.09f * sample_stereo_i64(in, sample_count, i - predelay - e4, 1));

        float early_r =
            (0.46f * sample_stereo_i64(in, sample_count, i - predelay - e1, 1)) +
            (0.23f * sample_stereo_i64(in, sample_count, i - predelay - e2, 0)) +
            (0.17f * sample_stereo_i64(in, sample_count, i - predelay - e3, 1)) +
            (0.09f * sample_stereo_i64(in, sample_count, i - predelay - e4, 0));

        float fb_l =
            (0.31f * sample_stereo_i64(history, sample_count, i - l1, 0)) +
            (0.24f * sample_stereo_i64(history, sample_count, i - l2, 1)) -
            (0.18f * sample_stereo_i64(history, sample_count, i - l3, 0)) +
            (0.13f * sample_stereo_i64(history, sample_count, i - l4, 1));

        float fb_r =
            (0.31f * sample_stereo_i64(history, sample_count, i - l1, 1)) +
            (0.24f * sample_stereo_i64(history, sample_count, i - l2, 0)) -
            (0.18f * sample_stereo_i64(history, sample_count, i - l3, 1)) +
            (0.13f * sample_stereo_i64(history, sample_count, i - l4, 0));

        float dense_l =
            dense_gain *
            ((0.50f * sample_stereo_i64(history, sample_count, i - l5, 0)) +
             (0.36f * sample_stereo_i64(history, sample_count, i - l6, 1)));

        float dense_r =
            dense_gain *
            ((0.50f * sample_stereo_i64(history, sample_count, i - l5, 1)) +
             (0.36f * sample_stereo_i64(history, sample_count, i - l6, 0)));

        float tail_in_l = (0.16f * bw_l) + (0.11f * early_l) + (decay_gain * (fb_l + dense_l));
        float tail_in_r = (0.16f * bw_r) + (0.11f * early_r) + (decay_gain * (fb_r + dense_r));

        damp_l = damp_l + (damp_coef * (tail_in_l - damp_l));
        damp_r = damp_r + (damp_coef * (tail_in_r - damp_r));

        float source_level = fabsf(bw_l) > fabsf(bw_r) ? fabsf(bw_l) : fabsf(bw_r);
        gate_env = (gate_env * 0.996f) + (source_level * 0.08f);

        float late_l = damp_l;
        float late_r = damp_r;
        if(gate_mode != 0)
        {
            float gate = gate_gain(gate_env);
            late_l *= gate;
            late_r *= gate;
        }

        float wet_l = (early_mix * early_l) + ((1.0f - early_mix) * late_l);
        float wet_r = (early_mix * early_r) + ((1.0f - early_mix) * late_r);

        history[i * 2] = coreaudio_sample_f32_to_i64(soft_clip(late_l));
        history[i * 2 + 1] = coreaudio_sample_f32_to_i64(soft_clip(late_r));

        output[i * 2] = coreaudio_sample_f32_to_i64(soft_clip((dry_l * (1.0f - mix)) + (wet_l * mix)));
        output[i * 2 + 1] = coreaudio_sample_f32_to_i64(soft_clip((dry_r * (1.0f - mix)) + (wet_r * mix)));
    }

    free(history);
    out.size = samples.size;
    out.data = output;
    return out;
}

mlang_list_t coreaudio_decode_file_i64(mlang_string path)
{
    mlang_list_t out;
    out.size = 0;
    out.data = NULL;

    set_error("");
    g_last_sample_rate = 0;

    if(!path || !path[0])
    {
        set_error("empty input path");
        return out;
    }

    CFStringRef path_cf = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
    if(!path_cf)
    {
        set_error("failed to create CFString for input path");
        return out;
    }

    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, path_cf, kCFURLPOSIXPathStyle, false);
    cf_release_if_needed(path_cf);
    if(!url)
    {
        set_error("failed to create CFURL for input path");
        return out;
    }

    ExtAudioFileRef file = NULL;
    OSStatus st = ExtAudioFileOpenURL(url, &file);
    cf_release_if_needed(url);
    if(st != noErr || !file)
    {
        set_osstatus_error("ExtAudioFileOpenURL failed", st);
        return out;
    }

    AudioStreamBasicDescription file_fmt;
    UInt32 file_fmt_size = sizeof(file_fmt);
    st = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &file_fmt_size, &file_fmt);
    if(st != noErr)
    {
        set_osstatus_error("failed to query file format", st);
        ExtAudioFileDispose(file);
        return out;
    }

    SInt64 frame_count = 0;
    UInt32 frame_count_size = sizeof(frame_count);
    st = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &frame_count_size, &frame_count);
    if(st != noErr || frame_count <= 0)
    {
        set_osstatus_error("failed to query file length", st);
        ExtAudioFileDispose(file);
        return out;
    }

    AudioStreamBasicDescription client_fmt;
    memset(&client_fmt, 0, sizeof(client_fmt));
    client_fmt.mSampleRate = file_fmt.mSampleRate;
    client_fmt.mFormatID = kAudioFormatLinearPCM;
    client_fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    client_fmt.mBitsPerChannel = 32;
    client_fmt.mChannelsPerFrame = 2;
    client_fmt.mFramesPerPacket = 1;
    client_fmt.mBytesPerFrame = sizeof(float) * client_fmt.mChannelsPerFrame;
    client_fmt.mBytesPerPacket = client_fmt.mBytesPerFrame;

    st = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat,
                                 sizeof(client_fmt), &client_fmt);
    if(st != noErr)
    {
        set_osstatus_error("failed to set decode format", st);
        ExtAudioFileDispose(file);
        return out;
    }

    size_t sample_count = (size_t)frame_count * 2u;
    float* data = (float*)checked_calloc(sample_count, sizeof(float));
    if(!data)
    {
        ExtAudioFileDispose(file);
        return out;
    }

    int64_t frames_read_total = 0;
    while(frames_read_total < frame_count)
    {
        UInt32 frames_to_read = (UInt32)((frame_count - frames_read_total) > 4096 ? 4096 : (frame_count - frames_read_total));

        AudioBufferList buf;
        buf.mNumberBuffers = 1;
        buf.mBuffers[0].mNumberChannels = 2;
        buf.mBuffers[0].mDataByteSize = frames_to_read * (UInt32)client_fmt.mBytesPerFrame;
        buf.mBuffers[0].mData = data + (frames_read_total * 2);

        st = ExtAudioFileRead(file, &frames_to_read, &buf);
        if(st != noErr)
        {
            set_osstatus_error("ExtAudioFileRead failed", st);
            free(data);
            ExtAudioFileDispose(file);
            return out;
        }
        if(frames_to_read == 0)
            break;
        frames_read_total += (int64_t)frames_to_read;
    }

    ExtAudioFileDispose(file);
    g_last_sample_rate = (int64_t)client_fmt.mSampleRate;
    int64_t* fixed = (int64_t*)checked_calloc((size_t)(frames_read_total * 2), sizeof(int64_t));
    if(!fixed)
    {
        free(data);
        return out;
    }
    for(int64_t i = 0; i < frames_read_total * 2; ++i)
        fixed[i] = coreaudio_sample_f32_to_i64(data[i]);
    free(data);

    out.size = frames_read_total * 2;
    out.data = fixed;
    return out;
}

typedef struct
{
    const float* samples;
    int64_t total_frames;
    int64_t cursor;
    int done;
} player_state_t;

static OSStatus render_cb(void* in_ref_con,
                          AudioUnitRenderActionFlags* io_action_flags,
                          const AudioTimeStamp* in_time_stamp,
                          UInt32 in_bus_number,
                          UInt32 in_number_frames,
                          AudioBufferList* io_data)
{
    (void)io_action_flags;
    (void)in_time_stamp;
    (void)in_bus_number;

    player_state_t* state = (player_state_t*)in_ref_con;
    if(!state || !io_data)
        return noErr;

    for(UInt32 b = 0; b < io_data->mNumberBuffers; ++b)
        memset(io_data->mBuffers[b].mData, 0, io_data->mBuffers[b].mDataByteSize);

    for(UInt32 frame = 0; frame < in_number_frames; ++frame)
    {
        if(state->cursor >= state->total_frames)
        {
            state->done = 1;
            break;
        }

        float left = state->samples[state->cursor * 2];
        float right = state->samples[state->cursor * 2 + 1];

        if(io_data->mNumberBuffers == 1)
        {
            float* interleaved = (float*)io_data->mBuffers[0].mData;
            interleaved[frame * 2] = left;
            interleaved[frame * 2 + 1] = right;
        }
        else if(io_data->mNumberBuffers >= 2)
        {
            float* out_l = (float*)io_data->mBuffers[0].mData;
            float* out_r = (float*)io_data->mBuffers[1].mData;
            out_l[frame] = left;
            out_r[frame] = right;
        }

        state->cursor += 1;
    }

    return noErr;
}

int32_t coreaudio_play_interleaved_i64(mlang_list_t samples, int64_t sample_rate)
{
    set_error("");

    if(!samples.data || samples.size <= 1)
    {
        set_error("no audio samples to play");
        return -1;
    }
    if((samples.size & 1) != 0)
    {
        set_error("interleaved stereo buffer must contain an even number of samples");
        return -1;
    }
    if(sample_rate <= 0)
    {
        set_error("invalid sample rate");
        return -1;
    }

    AudioComponentDescription desc;
    memset(&desc, 0, sizeof(desc));
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if(!comp)
    {
        set_error("failed to locate DefaultOutput AudioUnit");
        return -1;
    }

    AudioUnit unit = NULL;
    OSStatus st = AudioComponentInstanceNew(comp, &unit);
    if(st != noErr || !unit)
    {
        set_osstatus_error("AudioComponentInstanceNew failed", st);
        return -1;
    }

    float* float_samples = (float*)checked_calloc((size_t)samples.size, sizeof(float));
    if(!float_samples)
        return -1;
    int64_t* src = (int64_t*)samples.data;
    for(int64_t i = 0; i < samples.size; ++i)
        float_samples[i] = coreaudio_sample_i64_to_f32(src[i]);

    player_state_t state;
    state.samples = (const float*)float_samples;
    state.total_frames = samples.size / 2;
    state.cursor = 0;
    state.done = 0;

    AURenderCallbackStruct cb;
    cb.inputProc = render_cb;
    cb.inputProcRefCon = &state;
    st = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    if(st != noErr)
    {
        set_osstatus_error("failed to install render callback", st);
        free(float_samples);
        AudioComponentInstanceDispose(unit);
        return -1;
    }

    AudioStreamBasicDescription fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.mSampleRate = (Float64)sample_rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBitsPerChannel = 32;
    fmt.mChannelsPerFrame = 2;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = sizeof(float) * 2;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame;

    st = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
    if(st != noErr)
    {
        set_osstatus_error("failed to set playback stream format", st);
        free(float_samples);
        AudioComponentInstanceDispose(unit);
        return -1;
    }

    st = AudioUnitInitialize(unit);
    if(st != noErr)
    {
        set_osstatus_error("AudioUnitInitialize failed", st);
        free(float_samples);
        AudioComponentInstanceDispose(unit);
        return -1;
    }

    st = AudioOutputUnitStart(unit);
    if(st != noErr)
    {
        set_osstatus_error("AudioOutputUnitStart failed", st);
        free(float_samples);
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        return -1;
    }

    while(!state.done)
        usleep(10000);

    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    free(float_samples);
    return 0;
}
