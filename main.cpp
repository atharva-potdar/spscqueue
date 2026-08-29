#include "spsc.hpp"
#include <benchmark/benchmark.h>
#include <emmintrin.h>

static void BM_SingleThreaded(benchmark::State& state) {
    SPSCQueue<int, 1024> q;
    int val = 42;

    for (auto _ : state) {
        (void) q.enqueue(val);
        (void) q.dequeue(val);
        benchmark::DoNotOptimize(val);
    }
}
BENCHMARK(BM_SingleThreaded);

static SPSCQueue<int, 1024> global_q;

static void BM_MultiThreaded(benchmark::State& state) {
    if (state.thread_index() == 0) {
        int val = 42;
        for (auto _ : state) {
            while (!global_q.enqueue(val)) {
                _mm_pause();
            }
        }
    } else {
        int val = 0;
        for (auto _ : state) {
            while (!global_q.dequeue(val)) {
                _mm_pause();
            }
        }
    }
}
BENCHMARK(BM_MultiThreaded)->Threads(2)->UseRealTime();

BENCHMARK_MAIN();
