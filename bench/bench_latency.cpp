// Cross-core latency harness.
//
// Measures, with the TSC and per-sample empty-region subtraction:
//   * noop           - the measurement floor (empty timed region)
//   * one-way        - producer timestamp -> consumer timestamp for one item
//   * round trip     - producer -> consumer -> producer through two queues
//
// The queue under test has capacity 1 (`SPSCQueue<T, 2>`), so the producer
// cannot run ahead: every sample is a real cross-core handoff, not a pipelined
// throughput measurement. An ack queue spaces the samples out.
//
// Threads are pinned according to bench::topology_pairs(), which reads
// /sys/devices/system/cpu/*/topology and .../cache/index3/shared_cpu_list.
// Use `taskset -c <list>` to restrict which cores are considered, or
// SPSC_CPU_A / SPSC_CPU_B to force one pair.
#include "bench_common.hpp"
#include "spsc.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Sample {
    std::vector<std::uint64_t> ticks;
};

// One-way: the producer stamps a value and enqueues it; the consumer stamps
// immediately after dequeue. The ack queue prevents the producer from
// overwriting the single usable slot before the consumer has read it.
Sample one_way(int producer_cpu, int consumer_cpu, std::size_t samples) {
    SPSCQueue<std::uint64_t, 2> forward;
    SPSCQueue<std::uint8_t, 2> ack;
    std::atomic<int> stage{0};
    Sample result;
    result.ticks.reserve(samples);

    constexpr std::size_t kWarmup = 10'000;

    std::thread consumer([&] {
        if (!bench::pin_this_thread(consumer_cpu)) {
            std::fprintf(stderr, "failed to pin consumer to cpu%d\n", consumer_cpu);
            std::exit(1);
        }
        stage.store(1, std::memory_order_release);
        while (stage.load(std::memory_order_acquire) != 2) {
        }
        for (std::size_t i = 0; i < samples + kWarmup; ++i) {
            std::uint64_t sent = 0;
            while (!forward.dequeue(sent)) {
            }
            const std::uint64_t received = bench::tsc_end();
            if (i >= kWarmup) {
                result.ticks.push_back(received - sent);
            }
            while (!ack.enqueue(0)) {
            }
        }
    });

    std::thread producer([&] {
        if (!bench::pin_this_thread(producer_cpu)) {
            std::fprintf(stderr, "failed to pin producer to cpu%d\n", producer_cpu);
            std::exit(1);
        }
        while (stage.load(std::memory_order_acquire) != 1) {
        }
        stage.store(2, std::memory_order_release);
        for (std::size_t i = 0; i < samples + kWarmup; ++i) {
            const std::uint64_t sent = bench::tsc_begin();
            while (!forward.enqueue(sent)) {
            }
            std::uint8_t ignored = 0;
            while (!ack.dequeue(ignored)) {
            }
        }
    });

    producer.join();
    consumer.join();
    return result;
}

// Round trip: producer -> consumer -> producer. This is the number people
// usually mean when they say "ping-pong latency", and it is roughly twice the
// one-way handoff.
Sample round_trip(int producer_cpu, int consumer_cpu, std::size_t samples) {
    SPSCQueue<std::uint64_t, 2> forward;
    SPSCQueue<std::uint64_t, 2> backward;
    std::atomic<int> stage{0};
    Sample result;
    result.ticks.reserve(samples);

    constexpr std::size_t kWarmup = 10'000;

    std::thread consumer([&] {
        if (!bench::pin_this_thread(consumer_cpu)) {
            std::fprintf(stderr, "failed to pin consumer to cpu%d\n", consumer_cpu);
            std::exit(1);
        }
        stage.store(1, std::memory_order_release);
        while (stage.load(std::memory_order_acquire) != 2) {
        }
        for (;;) {
            std::uint64_t value = 0;
            while (!forward.dequeue(value)) {
                if (stage.load(std::memory_order_acquire) == 3) {
                    return;
                }
            }
            while (!backward.enqueue(value)) {
            }
        }
    });

    std::thread producer([&] {
        if (!bench::pin_this_thread(producer_cpu)) {
            std::fprintf(stderr, "failed to pin producer to cpu%d\n", producer_cpu);
            std::exit(1);
        }
        while (stage.load(std::memory_order_acquire) != 1) {
        }
        stage.store(2, std::memory_order_release);
        for (std::size_t i = 0; i < samples + kWarmup; ++i) {
            const std::uint64_t start = bench::tsc_begin();
            while (!forward.enqueue(start)) {
            }
            std::uint64_t echoed = 0;
            while (!backward.dequeue(echoed)) {
            }
            const std::uint64_t stop = bench::tsc_end();
            if (i >= kWarmup) {
                result.ticks.push_back(stop - start);
            }
        }
        stage.store(3, std::memory_order_release);
    });

    producer.join();
    consumer.join();
    return result;
}

std::uint64_t measurement_floor() {
    std::vector<std::uint64_t> ticks;
    ticks.reserve(100'000);
    for (int i = 0; i < 100'000; ++i) {
        const std::uint64_t start = bench::tsc_begin();
        const std::uint64_t stop = bench::tsc_end();
        ticks.push_back(stop - start);
    }
    std::ranges::sort(ticks);
    return ticks[ticks.size() / 2];
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    const auto cpus = bench::allowed_cpus();
    const auto pairs = bench::topology_pairs();
    const std::uint64_t floor_ticks = measurement_floor();
    const double ns = bench::ns_per_tick();

    std::printf("SPSCQueue latency harness\n");
    std::printf("cpu: %s\n", bench::host_model().c_str());
    std::printf("scheduling: %s\n", bench::scheduler_name());
    std::printf("TSC->ns factor: %.9f\n", ns);
    std::printf("empty-region cost: %llu ticks (%.2f ns)\n",
                static_cast<unsigned long long>(floor_ticks),
                static_cast<double>(floor_ticks) * ns);
    std::printf("allowed cpus:");
    for (const int cpu : cpus) {
        std::printf(" %d", cpu);
    }
    std::printf("\n");
    std::printf("ENV ns_per_tick=%.9f floor_ticks=%llu floor_ns=%.3f\n", ns,
                static_cast<unsigned long long>(floor_ticks),
                static_cast<double>(floor_ticks) * ns);
    std::printf("method: lfence+rdtscp on both ends, median empty-region cost "
                "subtracted per sample\n");

    constexpr std::size_t kSamples = 100'000;

    // Raw floor, no subtraction: this is the quantisation limit of the TSC
    // sequence on this machine.
    {
        std::vector<std::uint64_t> ticks;
        ticks.reserve(100'000);
        for (int i = 0; i < 100'000; ++i) {
            const std::uint64_t start = bench::tsc_begin();
            const std::uint64_t stop = bench::tsc_end();
            ticks.push_back(stop - start);
        }
        bench::print_row("noop (measurement floor)", bench::summarize(ticks, 0));
    }

    if (pairs.empty()) {
        std::fprintf(stderr, "no usable cpu pair (affinity has fewer than 2 cpus)\n");
        return 1;
    }

    bench::print_header();
    for (const auto& pair : pairs) {
        {
            const Sample s = one_way(pair.producer, pair.consumer, kSamples);
            const std::string name = "one-way handoff (" + pair.label + ", cpu" +
                                     std::to_string(pair.producer) + "->cpu" +
                                     std::to_string(pair.consumer) + ")";
            bench::print_row(name, bench::summarize(s.ticks, floor_ticks));
        }
        {
            const Sample s = round_trip(pair.producer, pair.consumer, kSamples);
            const std::string name = "round trip (" + pair.label + ", cpu" +
                                     std::to_string(pair.producer) + "->cpu" +
                                     std::to_string(pair.consumer) + ")";
            bench::print_row(name, bench::summarize(s.ticks, floor_ticks));
        }
    }

    std::printf("\nmax is a single-sample scheduler/interrupt outlier; p99.9 is the "
                "reportable tail.\n");
    return 0;
}
