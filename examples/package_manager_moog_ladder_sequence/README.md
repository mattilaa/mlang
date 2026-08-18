# Deep Moog Ladder Sequence

This realtime `std::audio` demo alternates square and sawtooth bass notes and
processes them with `dsp::filter::MoogLadder`. Every note has an amplitude ADSR
and a separate filter ADSR. A continuous triangle ramp moves the filter
envelope's maximum cutoff across the 16-step phrase, producing a smooth, deep
sweep without zipper noise.

The default `deep` variant uses the compensated 24 dB/octave ladder. The
compensated passband retains bass weight as resonance increases.

The `tb303` option switches to an original D-centered 16-step acid pattern
with rests, accents, constant-time pitch slides, a sharper filter contour, and
the 18 dB ladder output. Accents increase amplitude, filter-envelope depth,
and resonance together. This voice also enables the DSP library's asymmetric
analog-style distortion by default.

## Run

From this directory:

```sh
../../build/mlang pkg run demo
```

Run the old-school acid variant at the 95 BPM pace associated with “Acid
Bites.” The pattern captures that sparse, biting character but is an original
sequence, not a transcription:

```sh
../../build/mlang pkg run demo \
  --option variant=tb303 \
  --option bpm=95 \
  --option resonance_db=25 \
  --option cutoff_low=60 \
  --option cutoff_high=7200
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

Set analog distortion explicitly for either voice. A value of `-1` selects
the variant default (`0 dB`/bypassed for `deep`, `14 dB` for `tb303`):

```sh
../../build/mlang pkg run demo \
  --option variant=tb303 \
  --option distortion_drive_db=20
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
  --variant tb303 --bpm 95 --resonance-db 25 \
  --cutoff-low 50 --cutoff-high 7000 \
  --attack-ms 8 --decay-ms 80 --sustain 0.55 --release-ms 70 \
  --distortion-drive-db 16
```

The generated oscillators are intentionally raw square and saw waves so the
ladder receives strong harmonics. Keep the output level conservative when
raising resonance because the resonant peak can be loud.
