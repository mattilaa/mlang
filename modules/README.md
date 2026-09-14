# MLang Libraries

This tree contains libraries installed separately from the `std` namespace.

- `dsp`: filters, delay, analog-style distortion, parameter ramps, interpolation, convolution, and FFT
- `tui`: terminal cell compositing, nested layouts, box panels, and layered menus

Source-tree builds discover this directory automatically. Installed compilers
search the corresponding `share/mlang/modules` directory. Set
`MLANG_MODULE_PATH` when using a module tree from another location.
