#include <errno.h>
#include <inttypes.h>
#include <jack/jack.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mlang_c_types.h"

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

static mlang_list_t empty_list(void)
{
    mlang_list_t out;
    out.size = 0;
    out.data = NULL;
    return out;
}

typedef struct
{
    float* left;
    float* right;
    int frames;
    int sample_rate;
} stereo_wav_t;

static jack_client_t* g_client = NULL;
static jack_port_t* g_out_l = NULL;
static jack_port_t* g_out_r = NULL;
static stereo_wav_t g_wav = {0};
static double g_pos = 0.0;
static double g_step = 1.0;
static int g_running = 0;
static int g_sr = 48000;

#define RING_CAP 16384
static float g_ring_l[RING_CAP];
static float g_ring_r[RING_CAP];
static uint32_t g_ring_write = 0;

static void clear_wav(void)
{
    free(g_wav.left);
    free(g_wav.right);
    g_wav.left = NULL;
    g_wav.right = NULL;
    g_wav.frames = 0;
    g_wav.sample_rate = 0;
    g_pos = 0.0;
    g_step = 1.0;
    g_running = 0;
}

static uint32_t read_u32_le(const uint8_t* p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t read_u16_le(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static int load_wav_pcm16(const char* path, stereo_wav_t* out)
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
        fprintf(stderr, "[fftviz] open failed: %s (%s)\n", path, strerror(errno));
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

    if(!has_fmt || !has_data || bits_per_sample != 16 || channels == 0 ||
       channels > 2 || sample_rate == 0)
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
    float* left = (float*)calloc((size_t)frames, sizeof(float));
    float* right = (float*)calloc((size_t)frames, sizeof(float));
    if(!pcm || !left || !right)
    {
        free(pcm);
        free(left);
        free(right);
        fclose(f);
        return -13;
    }

    if(fread(pcm, 1, data_size, f) != data_size)
    {
        free(pcm);
        free(left);
        free(right);
        fclose(f);
        return -14;
    }
    fclose(f);

    for(int i = 0; i < frames; ++i)
    {
        if(channels == 1)
        {
            float s = (float)pcm[i] / 32768.0f;
            left[i] = s;
            right[i] = s;
        }
        else
        {
            left[i] = (float)pcm[2 * i] / 32768.0f;
            right[i] = (float)pcm[2 * i + 1] / 32768.0f;
        }
    }

    free(pcm);
    out->left = left;
    out->right = right;
    out->frames = frames;
    out->sample_rate = (int)sample_rate;
    return 0;
}

static float sample_linear(const float* data, int frames, double pos)
{
    if(!data || frames <= 0 || pos < 0.0)
        return 0.0f;
    int i0 = (int)pos;
    if(i0 >= frames)
        return 0.0f;
    int i1 = i0 + 1;
    if(i1 >= frames)
        i1 = i0;
    double t = pos - (double)i0;
    double a = (double)data[i0];
    double b = (double)data[i1];
    return (float)(a + (b - a) * t);
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

    for(jack_nframes_t i = 0; i < nframes; ++i)
    {
        float l = 0.0f;
        float r = 0.0f;
        if(g_running && g_pos < (double)g_wav.frames)
        {
            l = sample_linear(g_wav.left, g_wav.frames, g_pos);
            r = sample_linear(g_wav.right, g_wav.frames, g_pos);
            g_pos += g_step;
            if(g_pos >= (double)g_wav.frames)
                g_running = 0;
        }
        out_l[i] = l;
        out_r[i] = r;
        g_ring_l[g_ring_write & (RING_CAP - 1u)] = l;
        g_ring_r[g_ring_write & (RING_CAP - 1u)] = r;
        g_ring_write += 1u;
    }
    return 0;
}

static void try_connect_outputs(void)
{
    if(!g_client || !g_out_l || !g_out_r)
        return;

    const char* out_l_name = jack_port_name(g_out_l);
    const char* out_r_name = jack_port_name(g_out_r);
    if(!out_l_name || !out_r_name)
        return;

    const char* force_l = getenv("FFTVIZ_OUT_L");
    const char* force_r = getenv("FFTVIZ_OUT_R");
    if(force_l && force_l[0] && force_r && force_r[0])
    {
        int rc0 = jack_connect(g_client, out_l_name, force_l);
        int rc1 = jack_connect(g_client, out_r_name, force_r);
        fprintf(stderr,
                "[fftviz] forced connect L:%s -> %s rc=%d\n",
                out_l_name, force_l, rc0);
        fprintf(stderr,
                "[fftviz] forced connect R:%s -> %s rc=%d\n",
                out_r_name, force_r, rc1);
        return;
    }

    const char* prefix = getenv("FFTVIZ_OUT_PREFIX");

    // First preference: physical playback inputs.
    const char** playback_ports = jack_get_ports(
        g_client, NULL, JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsPhysical | JackPortIsInput);

    if(!playback_ports || !playback_ports[0])
    {
        // Fallback: any JACK input ports (PipeWire/bridged setups often omit
        // JackPortIsPhysical).
        if(playback_ports)
            jack_free((void*)playback_ports);
        playback_ports = jack_get_ports(
            g_client, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    }

    if(playback_ports && playback_ports[0])
    {
        const char* tgt_l = playback_ports[0];
        const char* tgt_r = playback_ports[1] ? playback_ports[1] : playback_ports[0];

        // Prefer common system playback pairs first (CoreAudio/JACK bridges).
        const char* sys_p1 = NULL;
        const char* sys_p2 = NULL;
        const char* sys_p3 = NULL;
        const char* sys_p4 = NULL;
        const char* pb_first = NULL;
        const char* pb_second = NULL;
        for(int i = 0; playback_ports[i]; ++i)
        {
            const char* p = playback_ports[i];
            if(strcmp(p, "system:playback_1") == 0)
                sys_p1 = p;
            else if(strcmp(p, "system:playback_2") == 0)
                sys_p2 = p;
            else if(strcmp(p, "system:playback_3") == 0)
                sys_p3 = p;
            else if(strcmp(p, "system:playback_4") == 0)
                sys_p4 = p;

            if(strstr(p, "playback"))
            {
                if(!pb_first)
                    pb_first = p;
                else if(!pb_second)
                    pb_second = p;
            }
        }

        if(sys_p1 && sys_p2)
        {
            tgt_l = sys_p1;
            tgt_r = sys_p2;
        }
        else if(sys_p3 && sys_p4)
        {
            tgt_l = sys_p3;
            tgt_r = sys_p4;
        }
        else if(pb_first)
        {
            tgt_l = pb_first;
            tgt_r = pb_second ? pb_second : pb_first;
        }

        if(prefix && prefix[0])
        {
            const char* first = NULL;
            const char* second = NULL;
            for(int i = 0; playback_ports[i]; ++i)
            {
                if(strstr(playback_ports[i], prefix))
                {
                    if(!first)
                        first = playback_ports[i];
                    else if(!second)
                    {
                        second = playback_ports[i];
                        break;
                    }
                }
            }
            if(first)
            {
                tgt_l = first;
                tgt_r = second ? second : first;
            }
        }

        int rc0 = jack_connect(g_client, out_l_name, tgt_l);
        int rc1 = jack_connect(g_client, out_r_name, tgt_r);

        // If the preferred pair failed, try system:playback_3/4 as fallback.
        if((rc0 != 0 || rc1 != 0) && sys_p3 && sys_p4 &&
           (strcmp(tgt_l, sys_p3) != 0 || strcmp(tgt_r, sys_p4) != 0))
        {
            int frc0 = jack_connect(g_client, out_l_name, sys_p3);
            int frc1 = jack_connect(g_client, out_r_name, sys_p4);
            fprintf(stderr,
                    "[fftviz] fallback connect L:%s -> %s rc=%d\n",
                    out_l_name, sys_p3, frc0);
            fprintf(stderr,
                    "[fftviz] fallback connect R:%s -> %s rc=%d\n",
                    out_r_name, sys_p4, frc1);
            if(frc0 == 0 || frc1 == 0)
            {
                rc0 = frc0;
                rc1 = frc1;
                tgt_l = sys_p3;
                tgt_r = sys_p4;
            }
        }

        fprintf(stderr,
                "[fftviz] connect L:%s -> %s rc=%d\n",
                out_l_name, tgt_l, rc0);
        fprintf(stderr,
                "[fftviz] connect R:%s -> %s rc=%d\n",
                out_r_name, tgt_r, rc1);
    }
    else
    {
        fprintf(stderr,
                "[fftviz] no JACK input playback ports found; connect manually.\n");
    }

    if(playback_ports)
        jack_free((void*)playback_ports);
}

int32_t jack2_fftviz_start(mlang_string wav_path, mlang_string client_name)
{
    if(g_client)
        return -1;
    if(!wav_path || !wav_path[0])
        return -2;

    clear_wav();
    int rc = load_wav_pcm16(wav_path, &g_wav);
    if(rc != 0)
        return -10 + rc;

    const char* name =
        (client_name && client_name[0]) ? client_name : "mlang_fft_viz";
    g_client = jack_client_open(name, JackNullOption, NULL);
    if(!g_client)
    {
        clear_wav();
        return -3;
    }

    g_sr = (int)jack_get_sample_rate(g_client);
    g_step = (double)g_wav.sample_rate / (double)g_sr;
    g_pos = 0.0;
    g_running = 1;
    g_ring_write = 0;
    memset(g_ring_l, 0, sizeof(g_ring_l));
    memset(g_ring_r, 0, sizeof(g_ring_r));

    g_out_l = jack_port_register(g_client, "out_l", JACK_DEFAULT_AUDIO_TYPE,
                                 JackPortIsOutput, 0);
    g_out_r = jack_port_register(g_client, "out_r", JACK_DEFAULT_AUDIO_TYPE,
                                 JackPortIsOutput, 0);
    if(!g_out_l || !g_out_r)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_l = NULL;
        g_out_r = NULL;
        clear_wav();
        return -4;
    }

    if(jack_set_process_callback(g_client, jack_process_cb, NULL) != 0)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_l = NULL;
        g_out_r = NULL;
        clear_wav();
        return -5;
    }

    if(jack_activate(g_client) != 0)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_l = NULL;
        g_out_r = NULL;
        clear_wav();
        return -6;
    }

    try_connect_outputs();
    fprintf(stderr,
            "[fftviz] started client '%s' (wav_sr=%d jack_sr=%d)\n",
            jack_get_client_name(g_client), g_wav.sample_rate, g_sr);
    return 0;
}

int32_t jack2_fftviz_stop(void)
{
    if(g_client)
    {
        jack_deactivate(g_client);
        jack_client_close(g_client);
    }
    g_client = NULL;
    g_out_l = NULL;
    g_out_r = NULL;
    clear_wav();
    return 0;
}

int32_t jack2_fftviz_running(void)
{
    return g_running ? 1 : 0;
}

int64_t jack2_fftviz_sample_rate(void)
{
    return (int64_t)g_sr;
}

int64_t jack2_fftviz_wav_sample_rate(void)
{
    return (int64_t)g_wav.sample_rate;
}

int64_t jack2_fftviz_total_frames(void)
{
    return (int64_t)g_wav.frames;
}

int64_t jack2_fftviz_current_frame(void)
{
    return (int64_t)g_pos;
}

int64_t jack2_fftviz_seek_rel_frames(int64_t delta)
{
    if(g_wav.frames <= 0)
        return 0;

    double p = g_pos + (double)delta;
    if(p < 0.0)
        p = 0.0;
    if(p >= (double)g_wav.frames)
        p = (double)(g_wav.frames - 1);

    g_pos = p;
    // Resume playback on seek so scrubbing near the tail keeps rendering/audio.
    g_running = 1;
    return (int64_t)g_pos;
}

static int parse_timecode_seconds(const char* s, int64_t* out_sec)
{
    if(!s || !s[0] || !out_sec)
        return -1;

    char tmp[64];
    size_t n = strlen(s);
    if(n >= sizeof(tmp))
        n = sizeof(tmp) - 1;
    memcpy(tmp, s, n);
    tmp[n] = '\0';

    // Normalize "hh::mm::ss" to "hh:mm:ss".
    char norm[64];
    size_t j = 0;
    for(size_t i = 0; i < n && j + 1 < sizeof(norm); ++i)
    {
        if(tmp[i] == ':' && (i + 1 < n) && tmp[i + 1] == ':')
        {
            norm[j++] = ':';
            ++i;
        }
        else
        {
            norm[j++] = tmp[i];
        }
    }
    norm[j] = '\0';

    int64_t a = 0, b = 0, c = 0;
    int parts = sscanf(norm, "%" SCNd64 ":%" SCNd64 ":%" SCNd64, &a, &b, &c);
    if(parts == 3)
    {
        if(a < 0 || b < 0 || c < 0 || b > 59 || c > 59)
            return -2;
        *out_sec = a * 3600 + b * 60 + c;
        return 0;
    }
    parts = sscanf(norm, "%" SCNd64 ":%" SCNd64, &a, &b);
    if(parts == 2)
    {
        if(a < 0 || b < 0 || b > 59)
            return -3;
        *out_sec = a * 60 + b;
        return 0;
    }
    parts = sscanf(norm, "%" SCNd64, &a);
    if(parts == 1 && a >= 0)
    {
        *out_sec = a;
        return 0;
    }
    return -4;
}

int64_t jack2_fftviz_seek_timecode(mlang_string time_spec)
{
    if(g_wav.frames <= 0 || g_wav.sample_rate <= 0)
        return -1;
    int64_t sec = 0;
    if(parse_timecode_seconds(time_spec, &sec) != 0)
        return -2;

    int64_t frame = sec * (int64_t)g_wav.sample_rate;
    if(frame < 0)
        frame = 0;
    if(frame >= (int64_t)g_wav.frames)
        frame = (int64_t)g_wav.frames - 1;
    g_pos = (double)frame;
    g_running = 1;
    return (int64_t)g_pos;
}

mlang_list_t jack2_fftviz_snapshot_i64(int32_t channel, int32_t window)
{
    if(window <= 0)
        return empty_list();
    if(window > RING_CAP)
        window = RING_CAP;

    int64_t out_n = (int64_t)window * 2;
    int64_t* out = (int64_t*)malloc(sizeof(int64_t) * (size_t)out_n);
    if(!out)
        return empty_list();

    const float* src = (channel == 0) ? g_ring_l : g_ring_r;
    uint32_t w = g_ring_write;
    uint32_t start = w - (uint32_t)window;

    // Remove DC offset to avoid dominant low-frequency bias in the plot.
    double mean = 0.0;
    for(int32_t i = 0; i < window; ++i)
    {
        mean += (double)src[(start + (uint32_t)i) & (RING_CAP - 1u)];
    }
    mean /= (double)window;

    const double two_pi = 6.28318530717958647692;
    const double denom = (window > 1) ? (double)(window - 1) : 1.0;
    for(int32_t i = 0; i < window; ++i)
    {
        double s = (double)src[(start + (uint32_t)i) & (RING_CAP - 1u)] - mean;
        // Hann window reduces spectral leakage and stabilizes bar shape.
        double win = 0.5 - 0.5 * cos(two_pi * (double)i / denom);
        int64_t v = (int64_t)llround(s * win * 32767.0);
        out[2 * i] = v;
        out[2 * i + 1] = 0;
    }

    mlang_list_t list;
    list.size = out_n;
    list.data = out;
    return list;
}

mlang_string jack2_fftviz_block_ansi_i64(int64_t color_idx)
{
    static char buf[64];
    int idx = (int)color_idx;
    if(idx < 0)
        idx = 0;
    if(idx > 255)
        idx = 255;
    snprintf(buf, sizeof(buf), "\x1b[48;5;%dm\x1b[38;5;%dm█", idx, idx);
    return buf;
}

mlang_string jack2_fftviz_bg_ansi_i64(int64_t bg_idx)
{
    static char buf[32];
    int idx = (int)bg_idx;
    if(idx < 0)
        idx = 0;
    if(idx > 255)
        idx = 255;
    snprintf(buf, sizeof(buf), "\x1b[48;5;%dm", idx);
    return buf;
}
