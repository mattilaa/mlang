# UML UI Generator Example

This example builds a small `mlang` CLI that reads a text definition and
renders a PNG for either:

- UML activity/control-flow diagrams
- UML-style sequence diagrams
- UML class diagrams

The renderer is implemented in C and writes PNGs directly with `stb`. No Java
runtime is used.

## Input Format

The preferred format is a TOML-like sectioned file.

Use:

- `[settings]` for diagram-wide values
- `[properties]` for default colors per element family
- `[[nodes]]`, `[[edges]]`, `[[participants]]`, `[[messages]]` for the actual graph

Activity example:

```toml
[settings]
diagram = "activity"
title = "Basic order approval flow"
title_size = 22
title_bold = true
scale = 1.0
box_radius = 8
edge_radius = 5

[properties]
action_fill = "#bfdbfe"
action_stroke = "#2563eb"
action_text = "#0f172a"
decision_fill = "#fde68a"
decision_stroke = "#d97706"
decision_text = "#111827"
start_fill = "#22c55e"
start_stroke = "#166534"
start_text = "#ffffff"
start_radius = 9
end_fill = "#c084fc"
end_stroke = "#7c3aed"
end_text = "#ffffff"
end_radius = 10
edge_color = "#334155"

[[nodes]]
id = "review"
type = "decision"
label = "Approved?"

[[edges]]
from = "review"
to = "ship"
label = "yes"
color = "#16a34a"
```

Sequence example:

```toml
[settings]
diagram = "sequence"
title = "HTTPS authentication sequence"
title_size = 22
title_bold = true
scale = 1.0
box_radius = 8
arrow_size = 12

[properties]
participant_fill = "#dbeafe"
participant_stroke = "#2563eb"
participant_text = "#0f172a"
message_color = "#334155"

[[participants]]
id = "gateway"
label = "HTTPS API Gateway"
fill = "#fde68a"
stroke = "#d97706"
text = "#111827"

[[messages]]
from = "browser"
to = "gateway"
label = "POST /login over TLS"
color = "#2563eb"
```

Class example:

```toml
[settings]
diagram = "class"
title = "Online golf store domain"
title_size = 24
title_bold = true
scale = 1.0
box_radius = 4

[properties]
class_fill = "#ffffff"
class_header_fill = "#f4f4f5"
class_stroke = "#18181b"
class_text = "#18181b"
association_color = "#3f3f46"

[[classes]]
id = "customer"
name = "Customer"
x = 40
y = 180
attributes = ["id", "name", "shippingAddress", "billingAddress"]
methods = ["+ findById(): Customer"]
header_fill = "#dbeafe"

[[classes]]
id = "order"
name = "Order"
x = 360
y = 180
attributes = ["- customer", "- salesTax", "- shipping", "- total"]
methods = ["- calculateShipping(zipCode:Int): Float"]

[[associations]]
from = "customer"
to = "order"
kind = "association"
from_multiplicity = "1"
to_multiplicity = "*"
```

## Sections

`[settings]`

- `diagram = "activity"`, `diagram = "sequence"`, or `diagram = "class"`
- `title = "Text"`
- `title_size = 22`
- `title_bold = true`
- `scale = 1.0`
- `box_radius = 8`
- `edge_radius = 5` for activity/control-flow diagrams
- `arrow_size = 12` for sequence diagrams

`[properties]`

Supported activity defaults:

- `action_fill`, `action_stroke`, `action_text`
- `action_bold`
- `decision_fill`, `decision_stroke`, `decision_text`
- `decision_bold`
- `start_fill`, `start_stroke`, `start_text`
- `start_radius`
- `start_bold`
- `end_fill`, `end_stroke`, `end_text`
- `end_radius`
- `end_bold`
- `edge_color`

Supported sequence defaults:

- `participant_fill`, `participant_stroke`, `participant_text`
- `participant_bold`
- `message_color`
- `message_bold`

Supported class defaults:

- `class_fill`, `class_stroke`, `class_text`
- `class_header_fill`
- `class_bold`
- `association_color`
- `association_bold`

## Item Tables

`[[nodes]]`

- required: `id`, `type`
- optional: `label`, `fill`, `stroke`, `text`

`[[edges]]`

- required: `from`, `to`
- optional: `label`, `color`

`[[participants]]`

- required: `id`
- optional: `label`, `fill`, `stroke`, `text`

`[[messages]]`

- required: `from`, `to`
- optional: `label`, `color`

`[[classes]]`

- required: `id`
- optional: `name`, `x`, `y`, `attributes`, `methods`, `fill`, `header_fill`, `stroke`, `text`, `bold`

`[[associations]]`

- required: `from`, `to`
- optional: `kind`, `from_multiplicity`, `to_multiplicity`, `label`, `color`, `bold`
- supported `kind` values: `"association"`, `"aggregation"`, `"composition"`, `"generalization"`, `"inheritance"`, `"realization"`, `"dependency"`

## Color Rules

- If an item defines its own `fill`, `stroke`, `text`, or `color`, that value wins.
- Otherwise the renderer uses the matching default from `[properties]`.
- If `[properties]` omits a value, the built-in example defaults are used.

## Legacy Support

The older pipe-delimited format is still accepted for now, but the sectioned
format is the intended direction.

## Build

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
```

## Run

Render the bundled control-flow sample:

```sh
../../build/mlang pkg run render-sample
```

Render the more complex multi-path control-flow sample:

```sh
../../build/mlang pkg run render-complex-sample
```

Render the HTTPS authentication sequence sample:

```sh
../../build/mlang pkg run render-sequence-sample
```

Render the bundled class diagram sample:

```sh
../../build/mlang pkg run render-class-sample
```

Or run the binary directly:

```sh
./build/uml_ui_generator samples/basic_control_flow.toml build/generated/basic_control_flow.png
./build/uml_ui_generator samples/multi_path_control_flow.toml build/generated/multi_path_control_flow.png
./build/uml_ui_generator samples/https_auth_sequence.toml build/generated/https_auth_sequence.png
./build/uml_ui_generator samples/online_golf_store_class.toml build/generated/online_golf_store_class.png
```
