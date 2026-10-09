# std::stop_token

Module file: `stdlib/std/stop_token.mla`

Cooperative, one-shot cancellation state inspired by C++20 `std::stop_source`
and `std::stop_token`. A source requests cancellation; workers poll retained
tokens and decide how to exit. Requesting stop is idempotent and the first
successful request returns `true`.

```mlang
mod std::stop_token;
use std::stop_token::StopSource;
use std::stop_token::StopToken;

fn main() -> i32 {
    let created: result<StopSource, str8> = StopSource::new();
    if created.is_err() { return 1; }
    let source: StopSource = created.unwrap();
    let token_result: result<StopToken, str8> = source.token();
    if token_result.is_err() { source.close(); return 1; }
    let token: StopToken = token_result.unwrap();

    let requested: result<bool, str8> = source.request_stop();
    let visible: result<bool, str8> = token.stop_requested();
    source.close();
    let remains_visible: result<bool, str8> = token.stop_requested();
    token.close();
    if requested.is_err() || visible.is_err() || remains_visible.is_err() { return 1; }
    return requested.unwrap() && visible.unwrap() && remains_visible.unwrap() ? 0 : 1;
}
```

`StopSource::token()` and both `clone()` methods retain the shared state. A
token therefore remains valid after its source is closed. Every successful
`new`, `token`, and `clone` call creates one owned reference; call the matching
`close()` exactly once for each owner after its last use. Do not make untracked
by-value copies of an owning `StopSource` or `StopToken`. When passing a token
to a worker through `thread::spawn`, keep an owning token alive until the
worker has joined, or explicitly clone one for the worker and close it there.
`StopToken::stop_possible()` becomes false after the last source owner closes,
even though the retained token can still read whether a stop was requested.

Cancellation is cooperative: the token does not interrupt a blocked operation
or terminate a thread. Poll `stop_requested()` at suitable points and arrange
separate wakeups for workers blocked on I/O or synchronization. The request
uses release/acquire ordering so data published before `request_stop()` is
visible to a worker after it observes the request; unrelated concurrent writes
still require their own synchronization.
