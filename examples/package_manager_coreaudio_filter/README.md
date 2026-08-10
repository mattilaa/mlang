# CoreAudio Realtime Filter Sweeps

This macOS example loads `examples/fft_example/illusion.wav` and processes it
through a selectable 12 or 24 dB/octave `dsp::filter` in a CoreAudio Audio
Queue callback. It plays five sections so cutoff and resonance changes are
easy to compare:

1. dry reference
2. falling cutoff sweep without resonance
3. rising cutoff sweep without resonance
4. falling cutoff sweep with increasing resonance
5. rising resonant cutoff sweep

Filter slope and resonance are independent. The realtime resonance peak
defaults to `+12 dB`; choose another peak with `--max-resonance-db`. For 24 dB
filters, resonance is distributed across the two cascaded stages. The limit is
applied before the per-frame ramp is calculated, so resonance moves
continuously without a sudden parameter change. Peaks above the default can
clip strongly and may require reducing source or output gain.

The five sections span the complete input sample. Their original 3:5:5:5:5
timing ratio is scaled to the audio-file length, and each filter ramp ends at
its section boundary. Replacing `illusion.wav` with a longer sample therefore
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

The default is the 24 dB low-pass. Select 12 or 24 dB/octave filters in the
built binary:

```sh
./build/cmake/coreaudio_filter_sweeps --filter lowpass12
./build/cmake/coreaudio_filter_sweeps --filter highpass12
./build/cmake/coreaudio_filter_sweeps --filter bandpass12
./build/cmake/coreaudio_filter_sweeps --filter lowpass24
./build/cmake/coreaudio_filter_sweeps --filter highpass24
./build/cmake/coreaudio_filter_sweeps --filter bandpass24
```

Set a different resonance peak; `+12 dB` remains the default:

```sh
./build/cmake/coreaudio_filter_sweeps --filter lowpass12 --max-resonance-db 6
./build/cmake/coreaudio_filter_sweeps --filter bandpass24 --max-resonance-db 9.5
```

## Interpolation filters

Fractional playback supports these sampler interpolation filters:

- `nearest`: closest sample, lowest CPU, highest imaging/distortion
- `linear`: two-point blend with low CPU usage
- `hermite`: four-point third-order Hermite with smooth derivatives and a
  strong quality/CPU balance for audio
- `cubic`: four-point Catmull-Rom cubic; mathematically the same polynomial as
  this Hermite implementation
- `bicubic`: a selectable one-dimensional waveform reduction to cubic

`hermite` is the default. True bicubic interpolation needs a two-dimensional
4x4 neighborhood. A waveform has only one time axis, so the realtime bicubic
mode reduces to the equivalent four-point cubic result without allocating a
4x4 list in the callback. The full 2D `bicubic_interpolate_f32` API remains
available for DSP tables.

Use a fractional playback rate to hear interpolation differences:

```sh
./build/cmake/coreaudio_filter_sweeps \
  --filter lowpass12 \
  --interpolation hermite \
  --playback-rate 0.75 \
  ~/Desktop/1995-Short.aif
```

The startup interface prints the selected audio filter, interpolation filter,
playback rate, and maximum resonance peak. At `1.0x`, source positions are
integers, so interpolation methods produce the same sample values.
The five-step schedule is not dumped at startup. Each step's time range,
filter, and resonance target are printed only when playback enters that step.

Pass another mono or stereo 16-bit PCM WAV, AIFF, or AIFF-C file directly to
the built program:

```sh
./build/cmake/coreaudio_filter_sweeps --filter highpass24 /path/to/input.wav
./build/cmake/coreaudio_filter_sweeps --filter bandpass24 /path/to/input.aif
```

Run `./build/cmake/coreaudio_filter_sweeps --help` for the complete syntax.

## Output device selection

List available CoreAudio output devices:

```sh
./build/cmake/coreaudio_filter_sweeps --list-devices
./build/cmake/coreaudio_filter_sweeps --list-devices --verbose
```

Select an output by its full name, a case-insensitive name substring, or the
UID shown by `--list-devices`:

```sh
./build/cmake/coreaudio_filter_sweeps \
  --filter bandpass24 \
  --output-device "BlackHole 2ch" \
  /path/to/input.aif
```

The package task exposes the same settings:

```sh
../../build/mlang pkg run list-devices
../../build/mlang pkg run list-devices-verbose
../../build/mlang pkg run demo \
  --option filter=bandpass24 \
  --option max_resonance_db=9 \
  --option interpolation=hermite \
  --option playback_rate=0.75 \
  --option audio_output=BlackHole \
  --option audio_path=/path/to/input.aif
```

Without `--output-device`, CoreAudio uses the current system default output.

Validate decoding without opening an audio device:

```sh
./build/cmake/coreaudio_filter_sweeps --validate ../../examples/fft_example/illusion.wav
./build/cmake/coreaudio_filter_sweeps --validate /path/to/input.aif
```

AIFF uses big-endian PCM. For AIFF-C, the uncompressed `NONE`, `twos`, and
little-endian `sowt` encodings are supported.

Paths beginning with `~/` are expanded by the binary. This also applies to
`--option audio_path=~/...` values passed through the package task.

The MLang processing entry points are in `src/filter_processor.mla`; the thin
CoreAudio and WAV boundary is in `src/main.cpp`.
