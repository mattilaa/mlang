# std::log::json

Module file: `stdlib/std/log/json.mla`

Structured logging with heterogeneous generic parameter packs. Build named
fields with `kv`, nest them with `json_object`, and pass any number of fields
to a record builder or output helper.

```mla
mod std::log::json;
use std::log::json::*;

fn main() -> i32 {
    let written: result<i32, str8> = log_json_pretty("INFO", "request completed",
        kv("request", json_object(kv("method", "GET"), kv("status", i32(200)))),
        kv("client", json_object(kv("id", i64(42)), kv("authenticated", true)))
    );
    if written.is_err() { return 1; }
    return 0;
}
```

```json
{
    "level": "INFO",
    "message": "request completed",
    "context": {
        "request": {
            "method": "GET",
            "status": 200
        },
        "client": {
            "id": 42,
            "authenticated": true
        }
    }
}
```

| Function | Result |
| --- | --- |
| `kv(key: str8, value: T)` | `Field<T>` retaining the concrete value type |
| `json_object(fields...)` | `JsonObject` containing the serialized object in `.text` |
| `json_record(level, message, fields...)` | Compact JSON as `str8` |
| `json_record_pretty(level, message, fields...)` | Indented JSON as `result<str8, str8>` |
| `log_json(level, message, fields...)` | Write compact JSON and a newline to stdout |
| `log_json_pretty(level, message, fields...)` | Write indented JSON and a newline; return `Ok(0)` or an error |

`json_record` always emits `level`, `message`, and a `context` object. Empty
packs produce `{}`. Field order follows argument order, and argument
expressions run exactly once from left to right. Each `json_object` expands
its pack with a comma fold; record and output helpers forward `fields...`.

Keys and string values use the compiler's JSON string escaping. Integers and
booleans remain JSON numbers and booleans, and nested `JsonObject` values are
embedded as objects. Scalar formatting uses `format!("{:json}", value)`;
floating-point values currently use six decimal places and must be finite.
Use `json_object` to construct nested objects rather than manually assigning
raw text. Pretty formatting parses the record with `std::json` and returns
an error for invalid JSON, including malformed manually supplied object text.

These helpers write directly to stdout without timestamp prefixes or
`std::log` file forwarding. Compact output is one JSON document per line;
pretty output spans multiple lines. Use `json_record` or
`json_record_pretty` when choosing another output destination.

Run the [demo](../../examples/variadic_json_logging.mla), which prints the same
record in both styles:

```sh
build/mlang examples/variadic_json_logging.mla -o /tmp/variadic_json_logging
/tmp/variadic_json_logging
build/mlang --tests tests/std_log_json_tests.mla
```
