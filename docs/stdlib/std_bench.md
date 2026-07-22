# std::bench

Module file: `stdlib/std/bench.mla`

Google-benchmark style anti-optimization helpers:
- `do_not_optimize_i64(v)`
- `do_not_optimize_i32(v)`
- `clobber_memory()`

Typical benchmark usage:

```mla
mod std::bench;

#[test]
fn bench_vec_push_pop() -> i32 {
    let x: i64 = 123;
    do_not_optimize_i64(x);
    clobber_memory();
    return 0;
}
```

Executed via benchmark runner:
- `mlang bench tests`
- `mlang bench tests/bench_stdlib.mla --bench-iters 200000 --bench-warmup 20000`
