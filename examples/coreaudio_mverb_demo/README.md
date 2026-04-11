# CoreAudio MVerb Demo

This example decodes an input file with macOS `ExtAudioFile`, runs an
MLang stereo reverb inspired by the `MVerb` parameter model, and plays the
result through the default CoreAudio output device.

Run it with:

```sh
./examples/coreaudio_mverb_demo/run_demo.sh /path/to/file.wav
./examples/coreaudio_mverb_demo/run_demo.sh /path/to/file.aif --preset=plate
./examples/coreaudio_mverb_demo/run_demo.sh /path/to/file.m4a --preset=gated --mix=0.45 --predelay-ms=22
```

Show built-in help:

```sh
./examples/coreaudio_mverb_demo/run_demo.sh --help
```

Available presets:

- `hall`
- `room`
- `plate`
- `gated`
- `ambient`

Notes:

- macOS only
- input decoding is handled by CoreAudio, so any format supported by
  `ExtAudioFile` should work
- the reverb itself is implemented in MLang, while the native bridge only
  handles decode and playback
