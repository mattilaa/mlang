#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <jack/jack.h>

#include "../include/mlang_c_types.h"

extern int64_t __mlang_std_sync_lfqueue_try_recv(int64_t queue_handle, char* buf, int64_t capacity);

static jack_client_t* g_client = NULL;
static jack_port_t* g_out_port_l = NULL;
static jack_port_t* g_out_port_r = NULL;
static int64_t g_queue_handle = 0;
static float g_sample_rate = 48000.0f;
static float g_phase = 0.0f;
static float g_freq_hz = 440.0f;
static float g_gain = 0.15f;
static int64_t g_frames_left = 0;
static int g_active = 0;

static void handle_command(const char* msg)
{
    if(!msg || !*msg)
        return;

    if(strncmp(msg, "stop", 4) == 0)
    {
        g_active = 0;
        g_frames_left = 0;
        return;
    }

    float gain = 0.0f;
    if(sscanf(msg, "gain %f", &gain) == 1)
    {
        if(gain < 0.0f)
            gain = 0.0f;
        if(gain > 1.0f)
            gain = 1.0f;
        g_gain = gain;
        return;
    }

    float freq = 0.0f;
    int dur_ms = 0;
    if(sscanf(msg, "play %f %d", &freq, &dur_ms) == 2)
    {
        if(freq < 20.0f)
            freq = 20.0f;
        if(freq > 20000.0f)
            freq = 20000.0f;
        if(dur_ms < 1)
            dur_ms = 1;
        g_freq_hz = freq;
        g_frames_left = (int64_t)((g_sample_rate * (float)dur_ms) / 1000.0f);
        g_active = 1;
        return;
    }
}

static int jack_process_cb(jack_nframes_t nframes, void* arg)
{
    (void)arg;

    char cmd[256];
    for(int i = 0; i < 32; ++i)
    {
        int64_t n = __mlang_std_sync_lfqueue_try_recv(g_queue_handle, cmd, (int64_t)sizeof(cmd));
        if(n <= 0)
            break;
        handle_command(cmd);
    }

    jack_default_audio_sample_t* out_l =
        (jack_default_audio_sample_t*)jack_port_get_buffer(g_out_port_l, nframes);
    jack_default_audio_sample_t* out_r =
        (jack_default_audio_sample_t*)jack_port_get_buffer(g_out_port_r, nframes);
    if(!out_l || !out_r)
        return 0;

    const float two_pi = 6.2831853071795864769f;
    for(jack_nframes_t i = 0; i < nframes; ++i)
    {
        float s = 0.0f;
        if(g_active)
        {
            s = sinf(g_phase) * g_gain;
            g_phase += two_pi * g_freq_hz / g_sample_rate;
            if(g_phase >= two_pi)
                g_phase -= two_pi;
            if(g_frames_left > 0)
            {
                --g_frames_left;
                if(g_frames_left == 0)
                    g_active = 0;
            }
        }
        out_l[i] = s;
        out_r[i] = s;
    }
    return 0;
}

int32_t jack2_bridge_start(int64_t queue_handle, mlang_string client_name)
{
    if(g_client)
        return -1;
    if(queue_handle == 0)
        return -2;

    const char* name = (client_name && client_name[0]) ? client_name : "mlang_jack2_demo";
    g_client = jack_client_open(name, JackNullOption, NULL);
    if(!g_client)
        return -3;

    g_sample_rate = (float)jack_get_sample_rate(g_client);
    g_queue_handle = queue_handle;
    g_phase = 0.0f;
    g_freq_hz = 440.0f;
    g_gain = 0.15f;
    g_frames_left = 0;
    g_active = 0;

    g_out_port_l = jack_port_register(g_client, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    g_out_port_r = jack_port_register(g_client, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if(!g_out_port_l || !g_out_port_r)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_port_l = NULL;
        g_out_port_r = NULL;
        return -4;
    }

    if(jack_set_process_callback(g_client, jack_process_cb, NULL) != 0)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_port_l = NULL;
        g_out_port_r = NULL;
        return -5;
    }

    if(jack_activate(g_client) != 0)
    {
        jack_client_close(g_client);
        g_client = NULL;
        g_out_port_l = NULL;
        g_out_port_r = NULL;
        return -6;
    }

    const char** playback_ports =
        jack_get_ports(g_client, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | JackPortIsInput);
    if(playback_ports)
    {
        const char* out_l_name = jack_port_name(g_out_port_l);
        const char* out_r_name = jack_port_name(g_out_port_r);
        if(playback_ports[0] && out_l_name)
        {
            int rc = jack_connect(g_client, out_l_name, playback_ports[0]);
            fprintf(stderr, "[jack2] connect %s -> %s rc=%d\n", out_l_name, playback_ports[0], rc);
        }
        if(playback_ports[1] && out_r_name)
        {
            int rc = jack_connect(g_client, out_r_name, playback_ports[1]);
            fprintf(stderr, "[jack2] connect %s -> %s rc=%d\n", out_r_name, playback_ports[1], rc);
        }
        else if(playback_ports[0] && out_r_name)
        {
            int rc = jack_connect(g_client, out_r_name, playback_ports[0]);
            fprintf(stderr, "[jack2] connect %s -> %s rc=%d\n", out_r_name, playback_ports[0], rc);
        }
        jack_free((void*)playback_ports);
    }
    else
    {
        fprintf(stderr, "[jack2] no physical playback ports found\n");
    }

    fprintf(stderr,
            "[jack2] client started (auto-connect attempted). If silent, connect '%s:out_l/out_r' to system playback.\n",
            jack_get_client_name(g_client));
    return 0;
}

int32_t jack2_bridge_stop(void)
{
    if(!g_client)
        return 0;
    jack_deactivate(g_client);
    jack_client_close(g_client);
    g_client = NULL;
    g_out_port_l = NULL;
    g_out_port_r = NULL;
    g_queue_handle = 0;
    g_active = 0;
    g_frames_left = 0;
    return 0;
}
