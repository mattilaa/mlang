# Multilanguage Static Audio Build

This example shows one `mlang pkg` project building a single binary from three
languages in separate tasks:

- `src/main.mla` is compiled by `mlang` into `build/obj/main_mlang.o`
- `src/miniaudio_player.c` is compiled by `cc` into
  `build/lib/libminiaudio_player.a`
- `src/audiofile_reader.cpp` is compiled by `c++` into
  `build/lib/libaudiofile_reader.a`

The final `link-demo` task then links those objects and static archives into
`build/multilanguage_audio_demo`. The final link also pulls in the repository
build of `libmlang_std.a` so the MLang object can resolve its runtime support
symbols. At runtime:

- the MLang entrypoint coordinates the workflow
- a task rewrites `samples/bassdrum.WAV` into `build/generated/bassdrum_audiofile.wav`
  so `AudioFile` sees a plain PCM WAV layout
- the C++ bridge uses `AudioFile` to inspect that normalized copy
- the C bridge uses `miniaudio` to play the original sample

Fetched source dependencies:

- `https://github.com/mackron/miniaudio`
- `https://github.com/adamstark/AudioFile`

The manifest fetches them as GitHub `tar.gz` source archives so the example is
fast to download but still clearly tied to those upstream repositories.

## Build

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
```

That runs the phased task graph and produces:

```text
build/obj/main_mlang.o
build/lib/libminiaudio_player.a
build/lib/libaudiofile_reader.a
build/multilanguage_audio_demo
```

## Run

```sh
../../build/mlang pkg run play-sample
```

Or run the already linked binary directly:

```sh
./build/multilanguage_audio_demo
```

## Notes

- `pkg build` works here without a `[package].entry` because the manifest is
  task-driven and the final executable is produced by `link-demo`.
- `pkg run play-sample` automatically fetches dependencies if the workspace is
  clean, then waits for both `link-demo` and `prepare-audiofile-sample` before
  launching the binary.
- The example keeps all compilation in explicit tasks so the different
  languages and build phases are visible in `mlang.toml`.
