# DSP Library

Module file: `modules/dsp/mod.mla`

Digital signal processing namespace root.

Submodules:
- [dsp::convolution](convolution.md)
- [dsp::filter](filter.md)
- [dsp::distortion](distortion.md)
- [dsp::envelope](envelope.md)
- [dsp::delay](delay.md)
- [dsp::fft](fft.md)
- [dsp::limiter](limiter.md)
- [dsp::multimode](multimode.md)
- [dsp::reverb](reverb.md)
- [dsp::reverb2](reverb2.md)
- [dsp::stutter](stutter.md)

Import specific DSP modules such as `dsp::convolution`, `dsp::filter`,
`dsp::distortion`, `dsp::envelope`, `dsp::delay`, `dsp::fft`, `dsp::limiter`, `dsp::multimode`, `dsp::reverb`,
`dsp::reverb2`, and `dsp::stutter` in application code. This library is installed and discovered
separately from the `std` namespace.

The compiler searches the repository's `modules/` directory in source builds,
then user and system module locations. Set `MLANG_MODULE_PATH` to use a custom
module root.
