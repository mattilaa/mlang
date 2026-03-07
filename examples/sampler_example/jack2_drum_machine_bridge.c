#include <errno.h>
#include <jack/jack.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mlang_c_types.h"

extern int __mlang_std_sync_lfqueue_send(int64_t queue_handle, const char* s);
extern int64_t __mlang_std_sync_lfqueue_try_recv(int64_t queue_handle, char* buf, int64_t capacity);

#define MAX_TRACKS 64
#define MAX_TRACK_NAME 32
#define MAX_VOICES 64

typedef struct
{
    float* data;
    int frames;
    int sample_rate;
} Sample;

typedef struct
{
    int active;
    const Sample* sample;
    float pos;
    float step;
    float gain;
} Voice;

typedef struct
{
    char name[MAX_TRACK_NAME];
    Sample sample;
    float gain;
} Track;

static jack_client_t* g_client = NULL;
static jack_port_t* g_out_l = NULL;
static jack_port_t* g_out_r = NULL;
static int64_t g_cmd_queue_handle = 0;
static int64_t g_event_queue_handle = 0;
static float g_step_frames = 0.0f;
static float g_step_counter = 0.0f;
static Track g_tracks[MAX_TRACKS];
static int g_track_count = 0;
static Voice g_voices[MAX_VOICES];

static uint32_t read_u32_le(const uint8_t* p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_u16_le(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static void trim_inplace(char* s)
{
    if(!s)
        return;

    char* p = s;
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    if(p != s)
        memmove(s, p, strlen(p) + 1);

    size_t n = strlen(s);
    while(n > 0)
    {
        char c = s[n - 1];
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            s[n - 1] = '\0';
            --n;
        }
        else
            break;
    }
}

static int load_wav_pcm16_mono(const char* path, Sample* out)
{
    uint8_t header[12];
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_size = 0;
    long data_offset = 0;

    FILE* f = fopen(path, "rb");
    if(!f)
    {
        fprintf(stderr, "[drum] failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    if(fread(header, 1, sizeof(header), f) != sizeof(header))
    {
        fclose(f);
        return -2;
    }
    if(memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
    {
        fclose(f);
        return -3;
    }

    int has_fmt = 0;
    int has_data = 0;
    while(!has_data)
    {
        uint8_t chunk_hdr[8];
        if(fread(chunk_hdr, 1, sizeof(chunk_hdr), f) != sizeof(chunk_hdr))
            break;

        uint32_t chunk_size = read_u32_le(chunk_hdr + 4);
        if(memcmp(chunk_hdr, "fmt ", 4) == 0)
        {
            uint8_t* fmt = (uint8_t*)malloc(chunk_size);
            if(!fmt)
            {
                fclose(f);
                return -4;
            }
            if(fread(fmt, 1, chunk_size, f) != chunk_size)
            {
                free(fmt);
                fclose(f);
                return -5;
            }
            if(chunk_size < 16)
            {
                free(fmt);
                fclose(f);
                return -6;
            }
            uint16_t format = read_u16_le(fmt + 0);
            channels = read_u16_le(fmt + 2);
            sample_rate = read_u32_le(fmt + 4);
            bits_per_sample = read_u16_le(fmt + 14);
            free(fmt);
            if(format != 1)
            {
                fclose(f);
                return -7;
            }
            has_fmt = 1;
        }
        else if(memcmp(chunk_hdr, "data", 4) == 0)
        {
            data_offset = ftell(f);
            data_size = chunk_size;
            if(fseek(f, (long)chunk_size, SEEK_CUR) != 0)
            {
                fclose(f);
                return -8;
            }
            has_data = 1;
        }
        else
        {
            if(fseek(f, (long)chunk_size, SEEK_CUR) != 0)
            {
                fclose(f);
                return -9;
            }
        }

        if((chunk_size & 1u) != 0)
            (void)fseek(f, 1, SEEK_CUR);
    }

    if(!has_fmt || !has_data || bits_per_sample != 16 || channels == 0 || sample_rate == 0)
    {
        fclose(f);
        return -10;
    }

    if(fseek(f, data_offset, SEEK_SET) != 0)
    {
        fclose(f);
        return -11;
    }

    int total_samples = (int)(data_size / 2u);
    int frames = total_samples / (int)channels;
    if(frames <= 0)
    {
        fclose(f);
        return -12;
    }

    int16_t* pcm = (int16_t*)malloc((size_t)data_size);
    float* mono = (float*)calloc((size_t)frames, sizeof(float));
    if(!pcm || !mono)
    {
        free(pcm);
        free(mono);
        fclose(f);
        return -13;
    }

    if(fread(pcm, 1, data_size, f) != data_size)
    {
        free(pcm);
        free(mono);
        fclose(f);
        return -14;
    }
    fclose(f);

    for(int i = 0; i < frames; ++i)
    {
        float sum = 0.0f;
        for(int ch = 0; ch < (int)channels; ++ch)
            sum += (float)pcm[i * (int)channels + ch] / 32768.0f;
        mono[i] = sum / (float)channels;
    }

    free(pcm);
    out->data = mono;
    out->frames = frames;
    out->sample_rate = (int)sample_rate;
    return 0;
}

static void free_sample(Sample* s)
{
    if(s->data)
        free(s->data);
    s->data = NULL;
    s->frames = 0;
    s->sample_rate = 0;
}

static void cleanup_samples(void)
{
    for(int i = 0; i < g_track_count; ++i)
        free_sample(&g_tracks[i].sample);
    g_track_count = 0;
}

static void join_path(char* dst, size_t dst_size, const char* dir, const char* file)
{
    size_t n = strlen(dir);
    if(n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\'))
        snprintf(dst, dst_size, "%s%s", dir, file);
    else
        snprintf(dst, dst_size, "%s/%s", dir, file);
}

static int load_samples_from_schema(const char* schema_path, const char* sample_dir)
{
    FILE* f = fopen(schema_path, "r");
    if(!f)
    {
        fprintf(stderr, "[drum] failed to open schema %s: %s\n", schema_path, strerror(errno));
        return -1;
    }

    int in_samples = 0;
    char line[1024];
    while(fgets(line, sizeof(line), f))
    {
        trim_inplace(line);
        if(line[0] == '\0' || line[0] == '#')
            continue;

        if(line[0] == '[')
        {
            in_samples = (strcmp(line, "[samples]") == 0) ? 1 : 0;
            continue;
        }

        if(!in_samples)
            continue;

        char* colon = strchr(line, ':');
        if(!colon)
            continue;

        *colon = '\0';
        char* name = line;
        char* file = colon + 1;
        trim_inplace(name);
        trim_inplace(file);
        if(name[0] == '\0' || file[0] == '\0')
            continue;

        if(g_track_count >= MAX_TRACKS)
            break;

        Track* t = &g_tracks[g_track_count];
        memset(t, 0, sizeof(*t));
        snprintf(t->name, sizeof(t->name), "%s", name);
        t->gain = 1.0f;

        char wav_path[1024];
        join_path(wav_path, sizeof(wav_path), sample_dir, file);
        if(load_wav_pcm16_mono(wav_path, &t->sample) != 0)
        {
            fclose(f);
            return -2;
        }
        g_track_count += 1;
    }

    fclose(f);
    return g_track_count > 0 ? 0 : -3;
}

static void trigger_sample(int track_idx, float jack_sr)
{
    const Track* tr = &g_tracks[track_idx];
    const Sample* s = &tr->sample;
    if(!s->data || s->frames <= 0 || s->sample_rate <= 0)
        return;

    int slot = -1;
    for(int i = 0; i < MAX_VOICES; ++i)
    {
        if(!g_voices[i].active)
        {
            slot = i;
            break;
        }
    }
    if(slot < 0)
        slot = 0;

    g_voices[slot].active = 1;
    g_voices[slot].sample = s;
    g_voices[slot].pos = 0.0f;
    g_voices[slot].step = (float)s->sample_rate / jack_sr;
    g_voices[slot].gain = tr->gain;
}

static void handle_command(const char* cmd, float jack_sr)
{
    if(!cmd || !*cmd)
        return;

    for(int i = 0; i < g_track_count; ++i)
    {
        if(strcmp(cmd, g_tracks[i].name) == 0)
        {
            trigger_sample(i, jack_sr);
            return;
        }
    }
}

static int jack_process_cb(jack_nframes_t nframes, void* arg)
{
    (void)arg;

    jack_default_audio_sample_t* out_l =
        (jack_default_audio_sample_t*)jack_port_get_buffer(g_out_l, nframes);
    jack_default_audio_sample_t* out_r =
        (jack_default_audio_sample_t*)jack_port_get_buffer(g_out_r, nframes);
    if(!out_l || !out_r)
        return 0;

    const float jack_sr = (float)jack_get_sample_rate(g_client);

    char cmd[128];
    for(int i = 0; i < 64; ++i)
    {
        int64_t n = __mlang_std_sync_lfqueue_try_recv(g_cmd_queue_handle, cmd, (int64_t)sizeof(cmd));
        if(n <= 0)
            break;
        handle_command(cmd, jack_sr);
    }

    g_step_counter += (float)nframes;
    while(g_step_counter >= g_step_frames)
    {
        g_step_counter -= g_step_frames;
        (void)__mlang_std_sync_lfqueue_send(g_event_queue_handle, "tick");
    }

    for(jack_nframes_t i = 0; i < nframes; ++i)
    {
        float mix = 0.0f;
        for(int v = 0; v < MAX_VOICES; ++v)
        {
            Voice* voice = &g_voices[v];
            if(!voice->active || !voice->sample)
                continue;

            int idx = (int)voice->pos;
            if(idx >= voice->sample->frames)
            {
                voice->active = 0;
                continue;
            }

            mix += voice->sample->data[idx] * voice->gain;
            voice->pos += voice->step;
            if((int)voice->pos >= voice->sample->frames)
                voice->active = 0;
        }

        if(mix > 1.0f)
            mix = 1.0f;
        if(mix < -1.0f)
            mix = -1.0f;

        out_l[i] = mix;
        out_r[i] = mix;
    }

    return 0;
}

int32_t jack2_sampler_start(int64_t cmd_queue_handle,
                            int64_t event_queue_handle,
                            mlang_string schema_path,
                            mlang_string sample_dir,
                            mlang_string client_name,
                            int32_t bpm,
                            int32_t steps_per_beat)
{
    if(g_client)
        return -1;
    if(cmd_queue_handle == 0 || event_queue_handle == 0 || !schema_path || !schema_path[0] || !sample_dir || !sample_dir[0])
        return -2;
    if(bpm <= 0)
        bpm = 130;
    if(steps_per_beat <= 0)
        steps_per_beat = 4;

    memset(g_tracks, 0, sizeof(g_tracks));
    memset(g_voices, 0, sizeof(g_voices));
    g_track_count = 0;

    if(load_samples_from_schema(schema_path, sample_dir) != 0)
    {
        cleanup_samples();
        return -10;
    }

    const char* name = (client_name && client_name[0]) ? client_name : "mlang_drum_machine";
    g_client = jack_client_open(name, JackNullOption, NULL);
    if(!g_client)
    {
        cleanup_samples();
        fprintf(stderr, "[drum] jack_client_open failed (is jackd running?)\n");
        return -40;
    }

    g_cmd_queue_handle = cmd_queue_handle;
    g_event_queue_handle = event_queue_handle;
    g_step_counter = 0.0f;
    g_out_l = jack_port_register(g_client, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    g_out_r = jack_port_register(g_client, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if(!g_out_l || !g_out_r)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_l = NULL;
        g_out_r = NULL;
        cleanup_samples();
        return -41;
    }

    if(jack_set_process_callback(g_client, jack_process_cb, NULL) != 0)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_l = NULL;
        g_out_r = NULL;
        cleanup_samples();
        return -42;
    }

    g_step_frames = ((float)jack_get_sample_rate(g_client) * 60.0f) / ((float)bpm * (float)steps_per_beat);
    if(g_step_frames < 1.0f)
        g_step_frames = 1.0f;

    if(jack_activate(g_client) != 0)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_l = NULL;
        g_out_r = NULL;
        cleanup_samples();
        return -43;
    }

    const char** playback_ports =
        jack_get_ports(g_client, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | JackPortIsInput);
    if(playback_ports)
    {
        const char* l = jack_port_name(g_out_l);
        const char* r = jack_port_name(g_out_r);
        if(playback_ports[0] && l)
            (void)jack_connect(g_client, l, playback_ports[0]);
        if(playback_ports[1] && r)
            (void)jack_connect(g_client, r, playback_ports[1]);
        else if(playback_ports[0] && r)
            (void)jack_connect(g_client, r, playback_ports[0]);
        jack_free((void*)playback_ports);
    }

    fprintf(stderr,
            "[drum] sampler started: tracks=%d tick@%dbpm/%dspb\n",
            g_track_count,
            bpm,
            steps_per_beat);
    return 0;
}

int32_t jack2_sampler_stop(void)
{
    if(!g_client)
        return 0;

    jack_deactivate(g_client);
    jack_client_close(g_client);
    g_client = NULL;
    g_out_l = NULL;
    g_out_r = NULL;
    g_cmd_queue_handle = 0;
    g_event_queue_handle = 0;
    g_step_frames = 0.0f;
    g_step_counter = 0.0f;
    cleanup_samples();
    memset(g_voices, 0, sizeof(g_voices));
    return 0;
}
