# Deep Moog Ladder Sequence

This realtime `std::audio` demo alternates square and sawtooth bass notes and
processes them with `dsp::filter::MoogLadder`. Every note has an amplitude ADSR
and a separate filter ADSR. A continuous triangle ramp moves the filter
envelope's maximum cutoff across the 16-step phrase, producing a smooth, deep
sweep without zipper noise.

The default filter is the compensated 24 dB/octave ladder. The compensated
passband retains bass weight as resonance increases.

## Run

From this directory:

```sh
../../build/mlang pkg run demo
```

Change the ladder and envelope from package options:

```sh
../../build/mlang pkg run demo \
  --option slope=12 \
  --option resonance_db=24 \
  --option cutoff_low=45 \
  --option cutoff_high=9000 \
  --option attack_ms=12 \
  --option decay_ms=110 \
  --option sustain=0.48 \
  --option release_ms=90
```

Select an output device:

```sh
../../build/mlang pkg run list-devices
../../build/mlang pkg run demo --option audio_output=0
```

After building, the executable also exposes ordinary command-line options:

```sh
./build/moog_ladder_sequence --help
./build/moog_ladder_sequence \
  --slope 24 --bpm 108 --resonance-db 21 \
  --cutoff-low 50 --cutoff-high 7000 \
  --attack-ms 8 --decay-ms 80 --sustain 0.55 --release-ms 70
```

The generated oscillators are intentionally raw square and saw waves so the
ladder receives strong harmonics. Keep the output level conservative when
raising resonance because the resonant peak can be loud.
