#include "spsc.hpp"
#include <benchmark/benchmark.h>

static void BM_EnqueueDequeue(benchmark::State& state) {
    SPSCQueue<int, 1024> q;
    int val = 42;

    for (auto _ : state) {
        q.enqueue(val);
        q.dequeue(val);
        benchmark::DoNotOptimize(val);
    }
}
BENCHMARK(BM_EnqueueDequeue);

BENCHMARK_MAIN();
