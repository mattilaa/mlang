# MLang Libraries

This tree contains libraries installed separately from the `std` namespace.

- `dsp`: filters, analog-style distortion, parameter ramps, interpolation, convolution, and FFT

Source-tree builds discover this directory automatically. Installed compilers
search the corresponding `share/mlang/modules` directory. Set
`MLANG_MODULE_PATH` when using a module tree from another location.
