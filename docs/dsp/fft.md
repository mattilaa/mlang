# dsp::fft

Module file: `modules/dsp/fft.mla`

Radix-2 FFT over interleaved complex arrays:
`[re0, im0, re1, im1, ...]`.

### API
- `is_power_of_two(n: i64) -> bool`
- `forward(data: list<i64>) -> list<i64>` (empty list on invalid input)
- `inverse(data: list<i64>) -> list<i64>` (empty list on invalid input)
- `forward_f32(data: list<f32>) -> list<f32>` (empty list on invalid input)
- `inverse_f32(data: list<f32>) -> list<f32>` (empty list on invalid input)
- `forward_f64(data: list<f64>) -> list<f64>` (empty list on invalid input)
- `inverse_f64(data: list<f64>) -> list<f64>` (empty list on invalid input)
