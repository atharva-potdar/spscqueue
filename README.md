v0.1 performance stats:

```
./build/spscqueue
2026-08-28T21:46:10+05:30
Running ./build/spscqueue
Run on (12 X 2993.41 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x6)
  L1 Instruction 32 KiB (x6)
  L2 Unified 512 KiB (x6)
  L3 Unified 4096 KiB (x2)
Load Average: 1.19, 1.14, 1.03
------------------------------------------------------------
Benchmark                  Time             CPU   Iterations
------------------------------------------------------------
BM_EnqueueDequeue       3.03 ns         3.02 ns    230526415
```

v0.1.1 performance stats:

Same as earlier, nothing performance-relevant was changed

v0.2 performance stats:

```
2026-08-29T20:19:10+05:30
Running ./build/spscqueue
Run on (12 X 2937.37 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x6)
  L1 Instruction 32 KiB (x6)
  L2 Unified 512 KiB (x6)
  L3 Unified 4096 KiB (x2)
Load Average: 0.32, 0.34, 0.40
-------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations
-------------------------------------------------------------------------------
BM_SingleThreaded                          3.25 ns         3.23 ns    217041069
BM_MultiThreaded/real_time/threads:2       7.90 ns         7.87 ns     78233692
```
