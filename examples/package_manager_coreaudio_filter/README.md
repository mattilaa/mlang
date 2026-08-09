# CoreAudio Realtime Filter Sweeps

This macOS example loads `examples/fft_example/illusion.wav` and processes it
through `dsp::filter` in a CoreAudio Audio Queue callback. It plays five
sections so cutoff and resonance changes are easy to compare:

1. dry reference
2. closing cutoff sweep without resonance
3. opening cutoff sweep without resonance
4. closing cutoff sweep with 18 dB resonance
5. opening cutoff sweep with 18 dB resonance

The five sections span the complete input sample. Their original 3:5:5:5:5
timing ratio is scaled to the WAV length, and each filter ramp ends at its
section boundary. Replacing `illusion.wav` with a longer sample therefore
extends the demo automatically without changing the source.

The sample is decoded and all Audio Queue buffers are allocated before
playback. The callback performs fixed-cost sample processing only: no memory
allocation, file I/O, logging, or locking. Cutoff and resonance targets move
sample by sample instead of changing abruptly.

From this directory, build and run the complete sequence with:

```sh
../../build/mlang pkg run demo
```

Pass another mono or stereo 16-bit PCM WAV file directly to the built program:

```sh
./build/cmake/coreaudio_filter_sweeps /path/to/input.wav
```

Validate decoding without opening an audio device:

```sh
./build/cmake/coreaudio_filter_sweeps --validate ../../examples/fft_example/illusion.wav
```

The MLang processing entry points are in `src/filter_processor.mla`; the thin
CoreAudio and WAV boundary is in `src/main.cpp`.
