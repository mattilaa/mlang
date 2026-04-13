# UML UI Generator Example

This example builds a small `mlang` CLI that reads a text flow definition and
renders a PNG using UML activity-style shapes:

- start: filled circle
- action: rounded rectangle
- decision: diamond
- end: bullseye

The PNG writer and ASCII text rasterization come from the fetched open-source
[`stb`](https://github.com/nothings/stb) C headers. No Java runtime is used.

## Input Format

Use a simple pipe-delimited text file:

```text
title|Basic order approval flow
node|start|start|Start|#22c55e|#166534|#ffffff
node|capture|action|Capture order|#bfdbfe|#2563eb|#0f172a
node|review|decision|Approved?|#fde68a|#d97706|#111827
node|fix|action|Revise request|#fecaca|#dc2626|#111827
node|ship|action|Ship order|#bbf7d0|#16a34a|#052e16
node|done|end|Done|#c084fc|#7c3aed|#ffffff
edge|start|capture||#334155
edge|capture|review||#334155
edge|review|ship|yes|#16a34a
edge|review|fix|no|#dc2626
edge|fix|capture|retry|#2563eb
edge|ship|done||#334155
```

Records:

- `title|Text`
- `node|id|type|label|fill|stroke|text`
- `edge|from|to|label|color`

Notes:

- Lines starting with `#` are comments.
- Colors use `#RRGGBB`.
- For now the layout is optimized for control-flow and activity-style diagrams.
- Back-edges such as retry loops are supported.

## Build

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
```

That fetches `stb`, builds the MLang wrapper plus the C renderer, and links:

```text
build/uml_ui_generator
```

## Run

Render the bundled colorful sample:

```sh
../../build/mlang pkg run render-sample
```

Render the more complex multi-path sample:

```sh
../../build/mlang pkg run render-complex-sample
```

Or run the binary directly with your own text file:

```sh
./build/uml_ui_generator samples/basic_control_flow.umlflow build/generated/basic_control_flow.png
./build/uml_ui_generator samples/multi_path_control_flow.umlflow build/generated/multi_path_control_flow.png
./build/uml_ui_generator my_flow.umlflow build/generated/my_flow.png
```

## Scope

This is the starting point for control-flow and activity-style charts. Sequence
diagram support can be added later by extending the input grammar and shape
set, but the current example already covers:

- text-driven node and edge definitions
- colorful UML-style control-flow nodes
- direct PNG output from one command
