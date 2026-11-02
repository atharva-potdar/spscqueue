# Benchmarks

Measured on 2026-11-02 from the working tree at commit `e5fa391` plus the
benchmark/test changes in this tree. Raw runs and the generated summary are in
`bench/results/2026-11-02/`.

## Environment

| Item | Value |
|---|---|
| CPU | AMD Ryzen 5 4600H, 6 cores / 12 threads (Zen 2) |
| L1d / L2 | 32 KiB / 512 KiB per core |
| L3 | 4 MiB per CCX (2 CCXs, 4 MiB visible per CCX) |
| TSC | 0.333969 ns per tick (~2.994 GHz) |
| Governor | `performance` |
| Scheduling | ordinary `other` policy; no `SCHED_FIFO` |
| Compiler | clang 23.1.4 (cross-checked with gcc 16.2.1) |
| Flags | `-O3 -march=native -Wall -Wextra -Wpedantic -Wconversion -Werror` |
| Pinning | `taskset -c 2,3,4,10`; harness pins producer on cpu2 |
| Repetitions | 5, median reported |
| Load average | ~1.3–1.6 during the run |

The machine was not isolated. The producer/consumer pairs are:

| Label | Cores | Relationship |
|---|---|---|
| SMT siblings | cpu2 → cpu3 | same physical core, shared L1/L2 |
| same L3 | cpu2 → cpu4 | different cores, same CCX |
| different L3 | cpu2 → cpu10 | different CCX |

## Reproduce

```sh
# Release build (clang, as measured)
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build/release

# 5 pinned repetitions; writes raw output and summary.md
BUILD=build/release CPU=2,3,4,10 REPS=5 bench/run_reps.sh

# Individual harnesses
taskset -c 2,3,4,10 ./build/release/bench_latency
taskset -c 2,3,4,10 ./build/release/bench_throughput
taskset -c 2,3,4,10 ./build/release/spscqueue --benchmark_min_time=0.2s
```

## How to read the numbers

Three different things are being measured, and they must not be conflated:

| Quantity | What it is | Where |
|---|---|---|
| **pair time** | one enqueue + one dequeue, L1-hot, single thread | `BM_SingleThreaded`, ~3.25 ns/pair (~1.6 ns/op) |
| **throughput per pair** | steady-state rate of a producer/consumer pair; the ring is pipelined, so several items are in flight | `bench_throughput`, ~9.2 ns/pair same L3 |
| **one-way latency** | producer timestamp → consumer timestamp for one item through a capacity-1 ring | `bench_latency`, ~100 ns p50 same L3 |

Google Benchmark's `Threads(2)` row is the middle quantity, not latency:
it reports `(sum of per-thread elapsed time) / (total iterations)`, which for
one producer and one consumer is wall time per pair.

## Latency

Method: `lfence; rdtscp; lfence` on both ends, 100,000 samples per row after
10,000 warmup iterations, median empty-region cost subtracted per sample. The
ring has capacity 1 (`SPSCQueue<T, 2>`) and an ack queue spaces the samples, so
each sample is a real handoff rather than a pipelined throughput measurement.

| Benchmark | mean | p50 | p99 | p99.9 | p99.9 min..max |
|---|---:|---:|---:|---:|---:|
| noop (measurement floor) | 31.63 | 30.06 | 40.08 | 40.08 | 40.08..40.08 |
| one-way, SMT siblings (2→3) | 33.57 | 30.06 | 50.10 | 50.10 | 50.10..60.11 |
| round trip, SMT siblings (2→3) | 65.75 | 70.13 | 80.16 | 100.19 | 90.17..170.33 |
| one-way, same L3 (2→4) | 105.48 | 100.19 | 140.27 | 270.52 | 230.44..280.54 |
| round trip, same L3 (2→4) | 184.26 | 180.34 | 230.44 | 390.74 | 360.69..430.83 |
| one-way, different L3 (2→10) | 256.40 | 230.44 | 521.01 | 741.44 | 681.30..781.49 |
| round trip, different L3 (2→10) | 434.90 | 410.78 | 741.42 | 1122.15 | 1072.04..1142.18 |

The empty-region cost is 90 ticks (30.06 ns) and the observed samples are
quantised in ~10 ns steps, so the SMT one-way row is at the measurement floor:
those handoffs are faster than this harness can resolve. `max` was dropped from
the table because it is a scheduler/interrupt outlier (tens to hundreds of
microseconds); p99.9 is the reportable tail.

Key readings:

- **Same-CCX one-way handoff: ~100 ns p50, ~270 ns p99.9.**
- **Cross-CCX one-way handoff: ~230 ns p50, ~740 ns p99.9.**
- A round trip is roughly 1.8–2× the one-way number, as expected.
- The dominant cost is the cache-coherence transfer between the two cores, not
  the ring arithmetic; the queue's contribution is small next to the ~100 ns
  line-transfer time.

## Throughput

Method: one producer and one consumer on pinned cores, 20M items (5M for
64/256-byte payloads). The consumer timestamps its own first and last dequeue;
the result therefore includes any time it waits for the producer. The producer
can be up to `N-1` items ahead, which is why this is throughput and not
latency.

| Benchmark | Mops/s (median) | min..max | ns/pair |
|---|---:|---:|---:|
| padded ring 1,024, 8 B (SMT) | 77.44 | 77.11..77.71 | 12.91 |
| padded ring 65,536, 8 B (SMT) | 83.68 | 83.42..83.86 | 11.95 |
| padded ring 65,536, 8 B (same L3) | 108.70 | 100.51..109.97 | 9.20 |
| padded ring 65,536, 8 B (different L3) | 69.76 | 66.00..70.30 | 14.33 |
| unpadded ring 65,536, 8 B (SMT) | 97.87 | 97.58..97.99 | 10.22 |
| unpadded ring 65,536, 8 B (same L3) | 44.74 | 44.58..44.94 | 22.35 |
| unpadded ring 65,536, 8 B (different L3) | 58.37 | 58.17..58.50 | 17.13 |

### Ring-size and payload sensitivity

Same L3 pair, 8-byte payload: 1,024 → 108.23 Mops/s; 65,536 → 108.70;
262,144 → 110.19. The ring is large enough that the indices, not the buffer,
determine the rate.

Same L3 pair, 65,536-entry ring: 8 B → 108.70 Mops/s; 64 B → 54.25; 256 B →
17.89. Larger payloads cost more because the payload cache line must move
between the two cores for every item.

### False sharing

The unpadded control is the same queue logic with `head`/`tail` adjacent in one
cache line (`bench/naive_queue.hpp`).

| Pair | padded | unpadded | effect |
|---|---:|---:|---|
| same L3 (2→4) | 9.20 ns/pair | 22.35 ns/pair | **2.4× faster when padded** |
| different L3 (2→10) | 14.33 ns/pair | 17.13 ns/pair | 1.2× faster when padded |
| SMT siblings (2→3) | 11.95 ns/pair | 10.22 ns/pair | no penalty; unpadded slightly faster |

On separate physical cores the padding is worth ~2.4×. On SMT siblings the two
threads share one L1, so there is no inter-core line transfer and the unpadded
version can even win (the two indices fit in one line). This is why the
padding result must be quoted with its topology.

## Google Benchmark cross-check

`build/release/spscqueue`, every row pinned to the same-L3 pair:

| Benchmark | Median |
|---|---:|
| `BM_SingleThreaded` (pair) | 3.27 ns |
| `BM_EmptyDequeue` | 0.339 ns |
| `BM_FullEnqueue` | 0.691 ns |
| `BM_MultiThreaded` (padded) | 8.29 ns/pair |
| `BM_MultiThreadedUnpadded` | 21.4 ns/pair |

The padded/unpadded gap agrees with the custom throughput harness (8.29 vs 21.4
here; 9.20 vs 22.35 there). The single-threaded row is identical in the gcc and
clang builds (3.26 ns) and matches the original v0.2 reading.

`BM_MultiThreaded` is pinned explicitly because Google Benchmark has no affinity
API. Without pinning its result ranged from 7.96 to 16.0 ns/pair across runs as
the scheduler placed the two threads on SMT siblings, the same CCX, or different
CCXs.

## Claims, corrected

- **"3.25 ns single-threaded."** Reproduced: 3.25–3.27 ns median, CV < 0.6%.
  It is **one enqueue + one dequeue pair** in one thread, L1-hot, i.e. ~1.6 ns
  per operation. Say "pair".
- **"7.90 ns cross-core latency."** Not a latency. 7.90 ns is an amortised
  **throughput** figure (≈127 M pairs/s) and was a best-case sample; the pinned
  median on the same L3 is 8.29 ns/pair and the unpinned spread was
  7.96–16.0 ns/pair. The measured **one-way latency** is ~100 ns p50 /
  ~270 ns p99.9 on the same CCX and ~230 ns p50 / ~740 ns p99.9 across CCX.
- **"Eliminated cache-line false sharing."** True on separate physical cores:
  9.20 vs 22.35 ns/pair (2.4×). Not observable on SMT siblings, where the
  threads share L1 and the unpadded queue is slightly faster. The alignment is a
  hard-coded 64 bytes, which is correct on this CPU but not on parts with 128-byte
  lines (`-DSPSC_CACHE_LINE_SIZE=128` overrides it).
- **"Bounded, zero-allocation, lock-free."** Bounded to `N-1` items; zero
  allocations over 10M pairs, verified by an allocator-counting test; lock-free
  enforced with `static_assert(std::atomic<std::size_t>::is_always_lock_free)`.
- **"C++20."** Now enforced: `CMAKE_CXX_EXTENSIONS OFF`, `CMAKE_CXX_STANDARD 20`.
  The previous build was actually `gnu++26`.

## Test coverage

`ctest` runs 4 targets (21 GoogleTest cases plus a standalone allocation test)
in Release, ASan+UBSan, and TSan configurations:

- FIFO order, empty/full, capacity `N-1`, capacity-1 ring, 10,000 wraparounds,
  large and byte payloads, non-copyable/non-movable.
- Layout: object alignment, head/tail on separate cache lines, head not sharing
  a line with the ring, smallest ring still padded, and an unpadded control that
  is shown to share a line.
- Two-thread sequence stress across capacities 2, 4, 1024, and 65,536: every
  item must arrive exactly once, in order, with the correct value.
- Zero allocations on the hot path (standalone executable with a counting
  allocator).
