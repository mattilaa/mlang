# Examples {#examples_page}

Curated MLang example programs shipped under `examples/`. The files listed
below have Doxygen headers and are grouped here so they live under their own
navigation tree instead of appearing alongside top-level stdlib documentation.

Run any example with:

```
mlang <path> -L ~/.local/lib -lmlang_std
```

(Add any extra flags the example's header documents.)

## Example pages

- @subpage uml_ui_generator — TOML-driven UML diagram/UI generator example.

## Example programs

- `examples/mlang_attributes.mla` — combining `#[test]` and `#[derive(Debug)]`.
- `examples/testing_mock_example.mla` — mock-based testing using `std::testing`.
- `examples/builder_object_json_demo.mla` — @ref builder_syntax "builder
  syntax" (Option A): every clause key and container is a full `struct`
  declaration; renders as JSON via `{:json}` / `{:#json}`.
- `examples/builder_object_field_demo.mla` — @ref builder_syntax "builder
  syntax" (Option B): clause keys declared with the compact `field Name:
  Type;` form; shop-order domain covering `str8`, `i32`, and `f64` values.
