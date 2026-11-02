// Google Benchmark cross-check.
//
// Read the multi-threaded rows as *throughput per enqueue+dequeue pair*, not as
// one-way latency. Google Benchmark reports
//   (sum of per-thread elapsed time) / (total iterations across threads),
// and with one producer and one consumer that is wall time per pair.
// bench_latency.cpp measures the actual cross-core handoff.
#include "spsc.hpp"

#include "bench_common.hpp"
#include "naive_queue.hpp"

#include <benchmark/benchmark.h>
#include <emmintrin.h>

#include <cstddef>
#include <string>

// One enqueue + one dequeue on a single thread. This is an L1-hot round trip
// through the ring, so the reported time is per *pair* of operations.
//
// No DoNotOptimize sink is needed: enqueue() writes the payload and releases
// the tail, and dequeue() releases the head, so neither call can be removed.
// Passing the payload through DoNotOptimize only adds a store/reload round trip
// to the measurement and makes clang look ~1.5 ns slower than gcc.
static void BM_SingleThreaded(benchmark::State& state) {
    SPSCQueue<int, 1024> queue;
    int value = 42;

    for (auto _ : state) {
        (void)queue.enqueue(value);
        (void)queue.dequeue(value);
    }
}
BENCHMARK(BM_SingleThreaded);

// Failure path: dequeue from an empty queue. The result must be consumed or
// the (side-effect-free) load can be hoisted out of the loop.
static void BM_EmptyDequeue(benchmark::State& state) {
    SPSCQueue<int, 1024> queue;
    int value = 0;

    for (auto _ : state) {
        bool popped = queue.dequeue(value);
        benchmark::DoNotOptimize(popped);
    }
}
BENCHMARK(BM_EmptyDequeue);

// Failure path: enqueue into a full queue.
static void BM_FullEnqueue(benchmark::State& state) {
    SPSCQueue<int, 1024> queue;
    for (int i = 0; i < 1023; ++i) {
        if (!queue.enqueue(i)) {
            state.SkipWithError("setup failed to fill the queue");
            return;
        }
    }

    for (auto _ : state) {
        bool pushed = queue.enqueue(7);
        benchmark::DoNotOptimize(pushed);
    }
}
BENCHMARK(BM_FullEnqueue);

static SPSCQueue<int, 1024> g_padded;
static NaiveSPSCQueue<int, 1024> g_unpadded;

namespace {

// Captured before any benchmark pins a thread. A thread that calls
// pthread_setaffinity_np keeps that mask after the benchmark loop, so it must
// be restored or later benchmarks only see one core.
struct OriginalAffinity {
    cpu_set_t mask;
    OriginalAffinity() {
        CPU_ZERO(&mask);
        if (sched_getaffinity(0, sizeof(mask), &mask) != 0) {
            CPU_ZERO(&mask);
        }
    }
};

const OriginalAffinity& original_affinity() {
    static const OriginalAffinity saved;
    return saved;
}

void restore_affinity() {
    if (pthread_setaffinity_np(pthread_self(), sizeof(original_affinity().mask),
                               &original_affinity().mask) != 0) {
        std::fprintf(stderr, "failed to restore thread affinity\n");
    }
}

// Resolved once, before any pinning, so both the padded and unpadded
// benchmarks use the same pair of cores.
const bench::Pair& benchmark_pair() {
    static const bench::Pair pair = [] {
        const auto pairs = bench::topology_pairs();
        for (const auto& candidate : pairs) {
            if (candidate.label == "same L3") {
                return candidate;
            }
        }
        return pairs.empty() ? bench::Pair{0, 0, "unpinned"} : pairs.front();
    }();
    return pair;
}

}  // namespace

template<typename Queue>
static void run_cross_core(benchmark::State& state, Queue& queue) {
    // Google Benchmark has no affinity API, so pin explicitly. Pinning removes
    // the run-to-run variance from the scheduler moving the two threads between
    // SMT siblings, the same L3, and different L3s.
    const bench::Pair& pair = benchmark_pair();
    // Initialise the saved mask before pinning: the first call must see the
    // process's original affinity, not the single core we are about to pin to.
    (void)original_affinity();
    const int cpu = state.thread_index() == 0 ? pair.producer : pair.consumer;
    const bool pinned = pair.label != "unpinned";
    if (pinned && !bench::pin_this_thread(cpu)) {
        state.SkipWithError("failed to pin benchmark thread");
        return;
    }
    state.SetLabel(pair.label);

    if (state.thread_index() == 0) {
        int value = 42;
        for (auto _ : state) {
            while (!queue.enqueue(value)) {
                _mm_pause();
            }
        }
    } else {
        int value = 0;
        for (auto _ : state) {
            while (!queue.dequeue(value)) {
                _mm_pause();
            }
        }
    }

    if (pinned) {
        restore_affinity();
    }
}

// Producer and consumer on two threads. The printed time is wall time per
// enqueue+dequeue pair at steady state; it is not one-way latency.
static void BM_MultiThreaded(benchmark::State& state) {
    run_cross_core(state, g_padded);
}
BENCHMARK(BM_MultiThreaded)->Threads(2)->UseRealTime();

// Same logic with head/tail sharing a cache line. The gap against the row above
// is the false-sharing cost.
static void BM_MultiThreadedUnpadded(benchmark::State& state) {
    run_cross_core(state, g_unpadded);
}
BENCHMARK(BM_MultiThreadedUnpadded)->Threads(2)->UseRealTime();

BENCHMARK_MAIN();
