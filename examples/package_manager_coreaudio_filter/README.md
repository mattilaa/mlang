# CoreAudio Realtime Filter Sweeps

This macOS example loads `examples/fft_example/illusion.wav` and processes it
through a selectable 24 dB `dsp::filter` in a CoreAudio Audio Queue callback.
It plays five sections so cutoff and resonance changes are easy to compare:

1. dry reference
2. falling cutoff sweep without resonance
3. rising cutoff sweep without resonance
4. falling cutoff sweep with increasing resonance
5. rising resonant cutoff sweep

The selected filter slope is always 24 dB/octave. The `+18 dB` shown for the
last two sections is a separate resonance target, not the filter slope. The
demo output labels both values explicitly.

The five sections span the complete input sample. Their original 3:5:5:5:5
timing ratio is scaled to the WAV length, and each filter ramp ends at its
section boundary. Replacing `illusion.wav` with a longer sample therefore
extends the demo automatically without changing the source.

The sample is decoded and all Audio Queue buffers are allocated before
playback. The callback performs fixed-cost sample processing only: no memory
allocation, file I/O, logging, or locking. Cutoff and resonance targets move
sample by sample instead of changing abruptly. Filter history is retained
between sections, and a 20 ms dry/wet crossfade removes the dry-to-filtered
transition click.

From this directory, build and run the complete sequence with:

```sh
../../build/mlang pkg run demo
```

The default is the 24 dB low-pass. Select another filter in the built binary:

```sh
./build/cmake/coreaudio_filter_sweeps --filter lowpass24
./build/cmake/coreaudio_filter_sweeps --filter highpass24
./build/cmake/coreaudio_filter_sweeps --filter bandpass24
```

Pass another mono or stereo 16-bit PCM WAV file directly to the built program:

```sh
./build/cmake/coreaudio_filter_sweeps --filter highpass24 /path/to/input.wav
```

Run `./build/cmake/coreaudio_filter_sweeps --help` for the complete syntax.

Validate decoding without opening an audio device:

```sh
./build/cmake/coreaudio_filter_sweeps --validate ../../examples/fft_example/illusion.wav
```

The MLang processing entry points are in `src/filter_processor.mla`; the thin
CoreAudio and WAV boundary is in `src/main.cpp`.
