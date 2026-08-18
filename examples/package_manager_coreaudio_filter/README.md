# Realtime Filter Sweeps in MLang

This example decodes a mono or stereo 16-bit PCM WAV, AIFF, or uncompressed
AIFF-C file with `std::audio`, processes it with `dsp::filter`, and streams
stereo PCM to CoreAudio. The executable sources are entirely MLang:

- `src/main_mlang.mla`: CLI, decoding, interpolation, scheduling, and PCM queue
- `src/filter_processor.mla`: filter state, ramps, and sample processing

The package uses the system `c++` command only as a linker driver for the MLang
objects and the existing `mlang_std` runtime archive. There are no C++ example
sources or CMake build steps.

## Sweep sequence

The complete source duration is divided using a 3:5:5:5:5 timing ratio:

1. dry reference
2. falling cutoff without resonance
3. rising cutoff without resonance
4. falling cutoff with increasing resonance
5. rising cutoff with resonance

Cutoff, resonance, and dry/wet transitions use sample-accurate ramps. The
default maximum resonance is `+12 dB`. One preallocated
`std::audio::PcmBlock` is reused by the producer, so playback does not grow an
MLang list or allocate a new output block.

## Build and run

From this directory:

```sh
../../build/mlang pkg run demo
```

Run the requested AIFF configuration:

```sh
../../build/mlang pkg run demo \
  --option filter=moog24 \
  --option interpolation=hermite \
  --option playback_rate=1 \
  --option dry_wet=0.8 \
  --option audio_path=~/Desktop/1995-Short.aif
```

Other examples:

```sh
../../build/mlang pkg run demo \
  --option filter=bandpass12 \
  --option max_resonance_db=6 \
  --option interpolation=linear \
  --option playback_rate=0.75 \
  --option audio_path=/path/to/input.wav
```

Supported filters are the Moog-style ladder modes `moog12` and `moog24`, plus
`lowpass12`, `lowpass24`, `highpass12`, `highpass24`, `bandpass12`, and
`bandpass24`. (`ladder12` and `ladder24` are aliases.) Supported interpolation modes are `nearest`,
`linear`, `hermite`, `cubic`, and `bicubic`. For a one-dimensional waveform,
`cubic` and `bicubic` use the same four-point Hermite/Catmull-Rom polynomial.

The playback rate must be from `0.25` to `4`, and the maximum resonance must be
from `0` to `36` dB. At `1.0x`, source positions are integers, so interpolation
modes produce the same source values.

`dry_wet` ranges from `0` (fully dry) to `1` (fully filtered). The first demo
section remains a fully dry reference. Each later section uses a short
sample-accurate crossfade to the selected mix.

## Output devices

List the integer `std::audio` output ids:

```sh
../../build/mlang pkg run list-devices
```

Select one through the package option:

```sh
../../build/mlang pkg run demo \
  --option audio_output=0 \
  --option audio_path=~/Desktop/1995-Short.aif
```

An empty `audio_output` or `-1` uses the default device.

After building, the executable can also be invoked directly:

```sh
./build/coreaudio_filter_sweeps_mlang --help
./build/coreaudio_filter_sweeps_mlang --list-devices
./build/coreaudio_filter_sweeps_mlang \
  --filter moog24 \
  --interpolation hermite \
  --playback-rate 1 \
  --dry-wet 0.8 \
  --audio-path /path/to/input.aif
```

Paths beginning with `~/` are expanded by `std::audio`. AIFF uses big-endian
PCM; uncompressed AIFF-C `NONE`, `twos`, and little-endian `sowt` are supported.
