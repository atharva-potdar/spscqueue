# Benchmark summary (5 repetitions)

Directory: `bench/results/2026-11-02`

## Environment

- cpu: AMD Ryzen 5 4600H with Radeon Graphics
- scheduling: other
- TSC->ns factor: 0.333969434
- empty-region cost: 90 ticks (30.06 ns)
- allowed cpus: 2 3 4 10
- method: lfence+rdtscp on both ends, median empty-region cost subtracted per sample
- ns_per_tick=0.333969434 floor_ticks=90 floor_ns=30.057

## Latency (nanoseconds, median across repetitions)

| Benchmark | mean | p50 | p99 | p99.9 | p99.9 min..max |
|---|---:|---:|---:|---:|---:|
| noop (measurement floor) | 31.63 | 30.06 | 40.08 | 40.08 | 40.08..40.08 |
| one-way handoff (SMT siblings, cpu2->cpu3) | 33.57 | 30.06 | 50.10 | 50.10 | 50.10..60.11 |
| round trip (SMT siblings, cpu2->cpu3) | 65.75 | 70.13 | 80.16 | 100.19 | 90.17..170.33 |
| one-way handoff (same L3, cpu2->cpu4) | 105.48 | 100.19 | 140.27 | 270.52 | 230.44..280.54 |
| round trip (same L3, cpu2->cpu4) | 184.26 | 180.34 | 230.44 | 390.74 | 360.69..430.83 |
| one-way handoff (different L3, cpu2->cpu10) | 256.40 | 230.44 | 521.01 | 741.44 | 681.30..781.49 |
| round trip (different L3, cpu2->cpu10) | 434.90 | 410.78 | 741.42 | 1122.15 | 1072.04..1142.18 |

## Throughput (enqueue+dequeue pairs, median across repetitions)

| Benchmark | Mops/s | min..max | ns/pair |
|---|---:|---:|---:|
| padded ring 1,024, 8 B (SMT siblings, cpu2->cpu3) | 77.44 | 77.11..77.71 | 12.91 |
| padded ring 65,536, 8 B (SMT siblings, cpu2->cpu3) | 83.68 | 83.42..83.86 | 11.95 |
| padded ring 262,144, 8 B (SMT siblings, cpu2->cpu3) | 84.53 | 84.09..84.78 | 11.83 |
| padded ring 65,536, 64 B (SMT siblings, cpu2->cpu3) | 93.31 | 91.55..94.53 | 10.72 |
| padded ring 65,536, 256 B (SMT siblings, cpu2->cpu3) | 29.38 | 29.23..29.58 | 34.04 |
| unpadded ring 65,536, 8 B (SMT siblings, cpu2->cpu3) | 97.87 | 97.58..97.99 | 10.22 |
| padded ring 1,024, 8 B (same L3, cpu2->cpu4) | 108.23 | 100.73..109.30 | 9.24 |
| padded ring 65,536, 8 B (same L3, cpu2->cpu4) | 108.70 | 100.51..109.97 | 9.20 |
| padded ring 262,144, 8 B (same L3, cpu2->cpu4) | 110.19 | 101.11..111.18 | 9.07 |
| padded ring 65,536, 64 B (same L3, cpu2->cpu4) | 54.25 | 51.34..55.02 | 18.43 |
| padded ring 65,536, 256 B (same L3, cpu2->cpu4) | 17.89 | 17.40..17.97 | 55.89 |
| unpadded ring 65,536, 8 B (same L3, cpu2->cpu4) | 44.74 | 44.58..44.94 | 22.35 |
| padded ring 1,024, 8 B (different L3, cpu2->cpu10) | 68.18 | 64.89..68.45 | 14.67 |
| padded ring 65,536, 8 B (different L3, cpu2->cpu10) | 69.76 | 66.00..70.30 | 14.33 |
| padded ring 262,144, 8 B (different L3, cpu2->cpu10) | 69.40 | 65.96..69.78 | 14.41 |
| padded ring 65,536, 64 B (different L3, cpu2->cpu10) | 26.10 | 25.28..26.78 | 38.31 |
| padded ring 65,536, 256 B (different L3, cpu2->cpu10) | 9.58 | 9.52..9.94 | 104.37 |
| unpadded ring 65,536, 8 B (different L3, cpu2->cpu10) | 58.37 | 58.17..58.50 | 17.13 |
