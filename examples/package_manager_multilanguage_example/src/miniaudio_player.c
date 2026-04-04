#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>

int ml_audio_play_blocking(const char* path)
{
    ma_engine engine;
    ma_sound sound;
    ma_result result;

    result = ma_engine_init(NULL, &engine);
    if(result != MA_SUCCESS)
    {
        fprintf(stderr,
                "[c] miniaudio engine init failed for %s (code %d)\n",
                path,
                (int)result);
        return 1;
    }

    result = ma_sound_init_from_file(&engine, path, 0, NULL, NULL, &sound);
    if(result != MA_SUCCESS)
    {
        fprintf(stderr,
                "[c] miniaudio failed to open %s (code %d)\n",
                path,
                (int)result);
        ma_engine_uninit(&engine);
        return 1;
    }

    printf("[c] Playing %s with miniaudio...\n", path);
    result = ma_sound_start(&sound);
    if(result != MA_SUCCESS)
    {
        fprintf(stderr,
                "[c] miniaudio failed to start playback for %s (code %d)\n",
                path,
                (int)result);
        ma_sound_uninit(&sound);
        ma_engine_uninit(&engine);
        return 1;
    }

    while(ma_sound_is_playing(&sound))
        ma_sleep(100);

    ma_sound_uninit(&sound);
    ma_engine_uninit(&engine);
    printf("[c] Playback complete.\n");
    return 0;
}
