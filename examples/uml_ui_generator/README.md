# UML UI Generator Example

This example builds a small `mlang` CLI that reads a text definition and
renders a PNG for either:

- UML activity/control-flow diagrams
- UML-style sequence diagrams

Activity diagrams use:

- start: filled circle
- action: rectangle
- decision: diamond
- end: bullseye

The PNG writer and ASCII text rasterization come from the fetched open-source
[`stb`](https://github.com/nothings/stb) C headers. No Java runtime is used.

## Input Format

Use a simple pipe-delimited text file.

Activity example:

```text
scale|1.0
box_radius|8
edge_radius|5
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

- `diagram|activity` or `diagram|sequence` (optional for activity, required for sequence)
- `scale|1.0` optional global scaling factor; when omitted, `1.0` is used
- `box_radius|8` optional activity/flow node corner radius; when omitted, a mild default is used
- `edge_radius|5` optional activity/flow connector bend radius; when omitted, a mild default is used
- `title|Text`
- `node|id|type|label|fill|stroke|text`
- `edge|from|to|label|color`

Sequence example:

```text
diagram|sequence
scale|1.0
box_radius|0
edge_radius|0
title|HTTPS authentication sequence
participant|browser|Browser|#dbeafe|#2563eb|#0f172a
participant|gateway|HTTPS API Gateway|#fde68a|#d97706|#111827
participant|auth|Auth Service|#ddd6fe|#7c3aed|#111827
participant|session|Session Store|#bbf7d0|#16a34a|#052e16
message|browser|gateway|POST /login over TLS|#2563eb
message|gateway|auth|Validate credentials|#d97706
message|auth|session|Create authenticated session|#16a34a
message|session|auth|Session id + expiry|#16a34a
message|auth|gateway|JWT + session cookie|#7c3aed
message|gateway|browser|200 OK Set-Cookie auth_sid|#2563eb
```

Sequence records:

- `participant|id|label|fill|stroke|text`
- `message|from|to|label|color`

Notes:

- Lines starting with `#` are comments.
- Colors use `#RRGGBB`.
- `box_radius` and `edge_radius` are currently applied to activity/flow rendering.
- For now the layout is optimized for control-flow and activity-style diagrams.
- Back-edges such as retry loops are supported.
- Sequence diagrams render participant headers, lifelines, and message arrows.

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

Render the HTTPS authentication sequence sample:

```sh
../../build/mlang pkg run render-sequence-sample
```

Or run the binary directly with your own text file:

```sh
./build/uml_ui_generator samples/basic_control_flow.umlflow build/generated/basic_control_flow.png
./build/uml_ui_generator samples/multi_path_control_flow.umlflow build/generated/multi_path_control_flow.png
./build/uml_ui_generator samples/https_auth_sequence.umlflow build/generated/https_auth_sequence.png
./build/uml_ui_generator my_flow.umlflow build/generated/my_flow.png
```

## Scope

This now covers:

- text-driven node and edge definitions
- colorful UML-style control-flow nodes
- sequence participants, lifelines, and messages
- direct PNG output from one command
