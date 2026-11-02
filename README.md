# SPSCQueue

A bounded, zero-allocation, lock-free single-producer / single-consumer ring
buffer in C++20.

```cpp
#include "spsc.hpp"

SPSCQueue<int, 1024> queue;   // capacity 1023

// producer thread
while (!queue.enqueue(value)) { /* full */ }

// consumer thread
int out;
while (!queue.dequeue(out)) { /* empty */ }
```

## Design

- `N` must be a power of two (`std::has_single_bit`), enforced at compile time;
  indexing uses `& (N - 1)` instead of `%`.
- Usable capacity is `N - 1`: one slot is reserved so full and empty are
  distinguishable without a separate counter.
- The ring is a `std::array<T, N>`, so there is no allocation after
  construction. `T` must be trivially copyable and default constructible.
- `head` and `tail` are `std::atomic<std::size_t>` with explicit
  acquire/release ordering, and each sits on its own cache line (`alignas(64)`
  by default, `-DSPSC_CACHE_LINE_SIZE=` to override).
- `static_assert(std::atomic<std::size_t>::is_always_lock_free)` makes the
  lock-free requirement a build error rather than a runtime surprise.
- Single producer and single consumer only. Exactly one thread may call
  `enqueue()`, and exactly one different thread may call `dequeue()`.

## Performance

Full method, raw results, and sensitivity tables: [`BENCHMARKS.md`](BENCHMARKS.md).

Measured on an AMD Ryzen 5 4600H (Zen 2), clang 23.1.4, `-O3 -march=native`,
median of 5 pinned runs:

| Measurement | Result |
|---|---:|
| single-thread enqueue+dequeue pair (L1-hot) | **3.25 ns/pair** (~1.6 ns/op) |
| cross-core throughput, same CCX | **9.20 ns/pair** (~109 M pairs/s) |
| cross-core throughput, different CCX | **14.33 ns/pair** (~70 M pairs/s) |
| one-way handoff latency, same CCX | **100 ns p50**, 270 ns p99.9 |
| one-way handoff latency, different CCX | **230 ns p50**, 740 ns p99.9 |
| padded vs unpadded, same-CCX throughput | **9.20 vs 22.35 ns/pair** |

The cross-core number is throughput, not latency: the ring decouples the two
cores, so the producer can be many items ahead. Latency is measured separately
with a capacity-1 ring and a TSC harness.

## Build

```sh
just init          # cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
just build
```

Dependencies: CMake >= 3.24, a C++20 compiler, and GoogleTest. The Google
Benchmark cross-check is fetched with `FetchContent`; disable it with
`-DSPSC_BUILD_GBENCH=OFF` for an offline build.

## Test

```sh
just test          # Release
just test-asan     # ASan + UBSan
just test-tsan     # ThreadSanitizer
```

Tests cover FIFO order, empty/full, capacity `N-1`, capacity 1, wrap-around,
large payloads, cache-line layout, a two-thread sequence stress across four
capacities, and zero allocations on the hot path.

## Benchmark

```sh
just bench                                   # latency + throughput, one run
BUILD=build/release REPS=5 bench/run_reps.sh # pinned, 5 repetitions, summary.md
```

The harnesses pin producer/consumer threads themselves, subtract the measured
empty-region cost, and report p50/p90/p99/p99.9 rather than a bare mean. Use
`SPSC_CPU_A` / `SPSC_CPU_B` to force a specific core pair, or `taskset -c ...`
to restrict the candidates.
