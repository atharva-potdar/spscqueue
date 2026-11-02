// Cross-core streaming throughput harness.
//
// One producer and one consumer run on pinned cores. The producer enqueues
// `items` values as fast as the ring allows; the consumer dequeues them and
// stamps the start/end of its own loop. Throughput is therefore
// items / (consumer last-dequeue - consumer first-dequeue), which includes any
// time the consumer spends waiting for the producer.
//
// This is *throughput*, not latency. The ring decouples the two cores, so the
// producer can be several items ahead; see bench_latency.cpp for the one-way
// handoff time of a single item.
#include "bench_common.hpp"
#include "naive_queue.hpp"
#include "spsc.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace {

struct Payload8 {
    std::uint64_t v[1];
};
struct Payload64 {
    std::uint64_t v[8];
};
struct Payload256 {
    std::uint64_t v[32];
};

struct Result {
    double seconds;
    double mops;
    double ns_per_pair;
    std::uint64_t sink;
};

template<typename Queue, typename Payload>
Result stream(int producer_cpu, int consumer_cpu, std::size_t items) {
    // Heap-allocated: a 65,536-entry ring of 256-byte payloads is 16 MiB
    // and would overflow the default stack.
    auto queue = std::make_unique<Queue>();
    alignas(64) std::atomic<int> stage{0};
    alignas(64) std::atomic<std::uint64_t> start_tsc{0};
    alignas(64) std::atomic<std::uint64_t> stop_tsc{0};
    alignas(64) std::atomic<std::uint64_t> sink{0};

    std::thread consumer([&] {
        if (!bench::pin_this_thread(consumer_cpu)) {
            std::fprintf(stderr, "failed to pin consumer to cpu%d\n", consumer_cpu);
            std::exit(1);
        }
        stage.store(1, std::memory_order_release);
        while (stage.load(std::memory_order_acquire) != 2) {
        }
        const std::uint64_t begin = bench::tsc_begin();
        std::uint64_t sum = 0;
        Payload out{};
        for (std::size_t i = 0; i < items; ++i) {
            while (!queue->dequeue(out)) {
            }
            sum += out.v[0];
        }
        const std::uint64_t end = bench::tsc_end();
        start_tsc.store(begin, std::memory_order_relaxed);
        stop_tsc.store(end, std::memory_order_relaxed);
        sink.store(sum, std::memory_order_relaxed);
    });

    std::thread producer([&] {
        if (!bench::pin_this_thread(producer_cpu)) {
            std::fprintf(stderr, "failed to pin producer to cpu%d\n", producer_cpu);
            std::exit(1);
        }
        while (stage.load(std::memory_order_acquire) != 1) {
        }
        stage.store(2, std::memory_order_release);
        Payload value{};
        for (std::size_t i = 0; i < items; ++i) {
            value.v[0] = i;
            while (!queue->enqueue(value)) {
            }
        }
    });

    producer.join();
    consumer.join();

    const double ticks = static_cast<double>(stop_tsc.load(std::memory_order_relaxed) -
                                             start_tsc.load(std::memory_order_relaxed));
    const double seconds = ticks * bench::ns_per_tick() / 1e9;
    return {
        .seconds = seconds,
        .mops = static_cast<double>(items) / seconds / 1e6,
        .ns_per_pair = seconds * 1e9 / static_cast<double>(items),
        .sink = sink.load(std::memory_order_relaxed),
    };
}

template<typename Queue, typename Payload>
void run_row(const std::string& label, int producer_cpu, int consumer_cpu,
             std::size_t items) {
    const Result r = stream<Queue, Payload>(producer_cpu, consumer_cpu, items);
    if (r.sink == 0) {
        std::fprintf(stderr, "throughput invariant failed for %s\n", label.c_str());
        std::exit(1);
    }
    bench::print_throughput_row(label, r.mops, r.ns_per_pair, items, r.seconds);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    const auto cpus = bench::allowed_cpus();
    const auto pairs = bench::topology_pairs();

    std::printf("SPSCQueue throughput harness\n");
    std::printf("cpu: %s\n", bench::host_model().c_str());
    std::printf("scheduling: %s\n", bench::scheduler_name());
    std::printf("TSC->ns factor: %.9f\n", bench::ns_per_tick());
    std::printf("allowed cpus:");
    for (const int cpu : cpus) {
        std::printf(" %d", cpu);
    }
    std::printf("\n");
    std::printf("ENV ns_per_tick=%.9f\n", bench::ns_per_tick());

    if (pairs.empty()) {
        std::fprintf(stderr, "no usable cpu pair (affinity has fewer than 2 cpus)\n");
        return 1;
    }

    constexpr std::size_t kItems = 20'000'000;
    constexpr std::size_t kItemsLarge = 5'000'000;

    std::printf("\n%-58s %12s %14s\n", "Benchmark", "throughput", "cost");
    std::printf("%-58s %12s %14s\n",
                "----------------------------------------------------------",
                "------------", "--------------");

    for (const auto& pair : pairs) {
        const std::string where = pair.label + ", cpu" + std::to_string(pair.producer) +
                                  "->cpu" + std::to_string(pair.consumer);

        // Ring-size sensitivity with an 8-byte payload.
        run_row<SPSCQueue<Payload8, 1024>, Payload8>(
            "padded ring 1,024, 8 B (" + where + ")", pair.producer, pair.consumer,
            kItems);
        run_row<SPSCQueue<Payload8, 65536>, Payload8>(
            "padded ring 65,536, 8 B (" + where + ")", pair.producer, pair.consumer,
            kItems);
        run_row<SPSCQueue<Payload8, 262144>, Payload8>(
            "padded ring 262,144, 8 B (" + where + ")", pair.producer, pair.consumer,
            kItems);

        // Payload-size sensitivity with a 65,536-entry ring.
        run_row<SPSCQueue<Payload64, 65536>, Payload64>(
            "padded ring 65,536, 64 B (" + where + ")", pair.producer, pair.consumer,
            kItemsLarge);
        run_row<SPSCQueue<Payload256, 65536>, Payload256>(
            "padded ring 65,536, 256 B (" + where + ")", pair.producer, pair.consumer,
            kItemsLarge);

        // Reference: identical logic with head/tail on the same cache line.
        run_row<NaiveSPSCQueue<Payload8, 65536>, Payload8>(
            "unpadded ring 65,536, 8 B (" + where + ")", pair.producer, pair.consumer,
            kItems);
    }

    std::printf("\nNote: cost is wall time per enqueue+dequeue pair at steady state; "
                "it is not one-way latency.\n");
    return 0;
}
