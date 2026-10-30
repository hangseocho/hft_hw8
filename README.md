# HW8 — Sliding-Window Event Counter (`RollingCounter`)

`rolling_counter.hpp` counts events with `ts > now - window_ns`.

## Design

- **Ring buffer** of `uint64_t` timestamps with power-of-two capacity, so slot = `index & mask` (no modulo).
- `head_` / `tail_` are monotonically increasing logical indices; `count = tail_ - head_`.
- **Lazy expiry:** `count(now)` pops from the front while `ts <= now - window_ns`. If `now < window_ns`, nothing is expired (avoids unsigned underflow).
- **Growth:** when full, capacity doubles and the live range is copied in order (two `memcpy`s). The buffer never shrinks, so steady state is allocation-free.

## Complexity

Each timestamp is pushed once and popped at most once, so the total work of all expiry loops is bounded by the number of `add` calls. Doubling growth costs O(1) amortized per `add`. Therefore `add` and `count` are **amortized O(1)**; no call rescans the window.

Assumptions: `count()` is called with a non-decreasing clock (as specified), and `add()` timestamps are non-decreasing.

## Correctness

- Edge cases (boundary `ts == now - window` is expired, `now < window`, duplicate timestamps).
- 200 randomized traces × 5,000 interleaved ops checked against a naive reference, with a small initial capacity (16) to exercise buffer growth.
- Clean under AddressSanitizer + UndefinedBehaviorSanitizer.

## Performance

Machine: Apple Silicon MacBook, Apple clang, `-O2 -std=c++17`.
Workload: interleaved `add(ts)` / `count(ts)`, window = 1 ms, exponential inter-arrival gaps (mean 37 ns ≈ 27k live events) plus 0.1% idle gaps of 2 ms.

| Metric | batch = 256 | batch = 64 |
|---|---|---|
| ns/op (mean) | **0.64** | 0.68 |
| p50 | **0.49** | 0.66 |
| p99 | **3.58** | 6.52 |
| p99.9 | **6.51** | 17.58 |
| max | 181.64 | 375.66 |

### Methodology

A single op (~1 ns) is far below the macOS clock resolution (~41.7 ns per tick), so ops are timed in **batches** and per-op latency = batch time / batch size. Percentiles are taken over 1,000,000 batch samples after 100,000 warm-up batches; the event stream is pre-generated so RNG cost is not timed. Timer overhead is measured and subtracted (it rounds to 0 on macOS because two back-to-back reads usually fall in the same tick). Larger batches give finer resolution but average out spikes, so both batch sizes are reported.

### Tail latency

The tail comes from lazy expiry: after a 2 ms idle gap, the next `count()` expires the entire window (~27k events) in one call. This is the expected cost profile of amortized O(1) — the work is deferred, not avoided — and the mean stays below 1 ns/op.

## Files

- `rolling_counter.hpp` — implementation (graded by CI)
- `bench/test_bench.cpp` — correctness tests + throughput benchmark
- `bench/latency_bench.cpp` — ns/op + p50/p99/p99.9 benchmark (`./lb [batch]`)
