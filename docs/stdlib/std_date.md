# std::date

Module file: `stdlib/std/date.mla`

### Types
- `date_time`
- `utc_offset`
- `time_zone`

### API
- `now() -> date_time`
- `unix_now() -> i64`
- `utc() -> utc_offset`
- `offset_seconds(seconds: i32) -> utc_offset`
- `offset_hours_minutes(hours: i32, minutes: i32) -> utc_offset`
- `local_offset() -> utc_offset`
- `local_offset_at(timestamp: i64) -> utc_offset`
- `from_unix(timestamp: i64, offset: utc_offset) -> date_time`
- `from_unix_utc(timestamp: i64) -> date_time`
- `from_unix_local(timestamp: i64) -> date_time`
- `load_timezone(name: str8) -> result<time_zone, str8>`
- `from_unix_tz(timestamp: i64, zone: time_zone) -> date_time`
- `to_unix(dt: date_time, offset: utc_offset) -> i64`
- `to_unix_utc(dt: date_time) -> i64`
- `to_unix_tz(dt: date_time, zone: time_zone) -> i64`
- `timezone_offset_at(zone: time_zone, timestamp: i64) -> utc_offset`
- `format_iso8601(dt: date_time) -> str8`
- `format_iso8601_offset(dt: date_time, offset: utc_offset) -> str8`
- `format_offset(offset: utc_offset) -> str8`
- `format_date(dt: date_time) -> str8`
- `format_time(dt: date_time) -> str8`

`std::date` supports Unix UTC timestamps in whole seconds. Timezone support is
provided as fixed UTC offsets (`utc_offset`), system-local conversions, and named
system timezones (`time_zone`). Unix-like platforms load IANA/zoneinfo names such
as `Europe/Helsinki` from the OS timezone database. Windows uses Windows system
timezone IDs such as `FLE Standard Time`.

Example:

```mla
mod std::date;
use std::date::*;

let utc_dt: date_time = from_unix_utc(0);
let helsinki: utc_offset = offset_hours_minutes(2, 0);
let local_dt: date_time = from_unix(0, helsinki);

let ts: i64 = to_unix(local_dt, helsinki);
let s: str8 = format_iso8601_offset(local_dt, helsinki);

let zone_r: result<time_zone, str8> = load_timezone("Europe/Helsinki");
if !zone_r.is_err() {
    let zone: time_zone = zone_r.unwrap();
    let zoned: date_time = from_unix_tz(1704067200, zone);
    let zone_offset: utc_offset = timezone_offset_at(zone, 1704067200);
    let zoned_text: str8 = format_iso8601_offset(zoned, zone_offset);
}
```
