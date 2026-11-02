// Shared helpers for the latency and throughput harnesses.
//
// Everything here is deliberately small and dependency-free so the harnesses
// build and run without Google Benchmark.
#pragma once

#include <x86intrin.h>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace bench {

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
//
// `lfence; rdtscp; lfence` keeps the measured region bracketed: the first
// lfence stops the operation from moving before the timestamp, the second stops
// it from moving after the closing timestamp. Both cores on this machine share
// one invariant TSC, so a timestamp taken on the producer core can be
// subtracted from one taken on the consumer core.

inline std::uint64_t tsc_begin() {
    _mm_lfence();
    unsigned aux = 0;
    const std::uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

inline std::uint64_t tsc_end() {
    _mm_lfence();
    unsigned aux = 0;
    const std::uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

// Nanoseconds per TSC tick, calibrated against CLOCK_MONOTONIC once.
inline double ns_per_tick() {
    static const double value = [] {
        timespec t0{};
        timespec t1{};
        clock_gettime(CLOCK_MONOTONIC, &t0);
        const std::uint64_t c0 = __rdtsc();
        volatile std::uint64_t sink = 0;
        for (std::uint64_t i = 0; i < 50'000'000; ++i) {
            sink += i;
        }
        (void)sink;
        const std::uint64_t c1 = __rdtsc();
        clock_gettime(CLOCK_MONOTONIC, &t1);
        const double ns = static_cast<double>(t1.tv_sec - t0.tv_sec) * 1e9 +
                          static_cast<double>(t1.tv_nsec - t0.tv_nsec);
        const double ticks = static_cast<double>(c1 - c0);
        return ns / ticks;
    }();
    return value;
}

// ---------------------------------------------------------------------------
// Pinning and topology
// ---------------------------------------------------------------------------

inline bool pin_this_thread(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

inline std::vector<int> allowed_cpus() {
    cpu_set_t set;
    CPU_ZERO(&set);
    std::vector<int> out;
    if (sched_getaffinity(0, sizeof(set), &set) != 0) {
        return out;
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &set) != 0) {
            out.push_back(cpu);
        }
    }
    return out;
}

inline std::string read_sysfs(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    if (!in) {
        return {};
    }
    std::getline(in, line);
    return line;
}

// Parses the kernel's cpulist format: "0-5,8,10-11".
inline std::vector<int> parse_cpu_list(const std::string& text) {
    std::vector<int> cpus;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t comma = text.find(',', pos);
        const std::string item = text.substr(pos, comma == std::string::npos ? std::string::npos
                                                                            : comma - pos);
        const std::size_t dash = item.find('-');
        if (dash == std::string::npos) {
            if (!item.empty()) {
                cpus.push_back(std::atoi(item.c_str()));
            }
        } else {
            const int lo = std::atoi(item.substr(0, dash).c_str());
            const int hi = std::atoi(item.substr(dash + 1).c_str());
            for (int cpu = lo; cpu <= hi; ++cpu) {
                cpus.push_back(cpu);
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return cpus;
}

inline std::vector<int> sysfs_cpu_list(int cpu, const char* node) {
    const std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) +
                             "/cache/index3/";
    if (std::strcmp(node, "thread_siblings_list") == 0) {
        return parse_cpu_list(read_sysfs(
            "/sys/devices/system/cpu/cpu" + std::to_string(cpu) +
            "/topology/thread_siblings_list"));
    }
    return parse_cpu_list(read_sysfs(path + node));
}

struct Pair {
    int producer;
    int consumer;
    std::string label;
};

inline bool contains(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

// Builds the pairs we want to report: SMT siblings (same core), two cores in
// the same L3, and two cores in different L3s. Cores come from the process
// affinity mask, so `taskset -c ...` narrows the candidates.
inline std::vector<Pair> topology_pairs() {
    const std::vector<int> allowed = allowed_cpus();
    if (allowed.size() < 2) {
        return {};
    }

    const char* env_a = std::getenv("SPSC_CPU_A");
    const char* env_b = std::getenv("SPSC_CPU_B");
    if (env_a != nullptr && env_b != nullptr) {
        return {{std::atoi(env_a), std::atoi(env_b), "manual"}};
    }

    const int a = allowed.front();
    const std::vector<int> siblings = sysfs_cpu_list(a, "thread_siblings_list");
    std::vector<int> same_l3 = sysfs_cpu_list(a, "shared_cpu_list");

    std::vector<Pair> pairs;

    for (const int cpu : siblings) {
        if (cpu != a && contains(allowed, cpu)) {
            pairs.push_back({a, cpu, "SMT siblings"});
            break;
        }
    }

    for (const int cpu : same_l3) {
        if (cpu != a && !contains(siblings, cpu) && contains(allowed, cpu)) {
            pairs.push_back({a, cpu, "same L3"});
            break;
        }
    }

    for (const int cpu : allowed) {
        if (cpu != a && !contains(same_l3, cpu)) {
            pairs.push_back({a, cpu, "different L3"});
            break;
        }
    }

    // A single-CPU subset (e.g. taskset -c 2) leaves only one candidate; fall
    // back to the first allowed peer so the harness still produces a row.
    if (pairs.empty()) {
        for (const int cpu : allowed) {
            if (cpu != a) {
                pairs.push_back({a, cpu, "same L3"});
                break;
            }
        }
    }
    return pairs;
}

inline std::string cpu_name(int cpu) {
    return "cpu" + std::to_string(cpu);
}

inline std::string host_model() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string model = line.substr(colon + 1);
                const std::size_t start = model.find_first_not_of(' ');
                return start == std::string::npos ? std::string{} : model.substr(start);
            }
        }
    }
    return "unknown";
}

inline const char* scheduler_name() {
    switch (sched_getscheduler(0)) {
        case SCHED_FIFO:
            return "SCHED_FIFO";
        case SCHED_RR:
            return "SCHED_RR";
        case SCHED_OTHER:
            return "other";
        default:
            return "other";
    }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

struct Stats {
    std::size_t n = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p90 = 0.0;
    double p99 = 0.0;
    double p999 = 0.0;
    double max = 0.0;
};

inline double percentile_ticks(const std::vector<std::uint64_t>& sorted, double p) {
    const auto idx = static_cast<std::size_t>(
        p * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[idx]);
}

// `floor_ticks` is the median cost of an empty timed region measured with the
// same fence sequence; it is subtracted from every sample.
inline Stats summarize(std::vector<std::uint64_t> samples, std::uint64_t floor_ticks) {
    std::ranges::sort(samples);
    const double ns = ns_per_tick();

    auto adjusted = [&](std::uint64_t ticks) -> double {
        return (ticks > floor_ticks ? static_cast<double>(ticks - floor_ticks) : 0.0) * ns;
    };

    std::uint64_t sum = 0;
    for (const auto s : samples) {
        sum += s;
    }
    const double mean_ticks =
        static_cast<double>(sum) / static_cast<double>(samples.size()) -
        static_cast<double>(floor_ticks);

    return {
        .n = samples.size(),
        .mean = mean_ticks * ns,
        .p50 = adjusted(static_cast<std::uint64_t>(percentile_ticks(samples, 0.500))),
        .p90 = adjusted(static_cast<std::uint64_t>(percentile_ticks(samples, 0.900))),
        .p99 = adjusted(static_cast<std::uint64_t>(percentile_ticks(samples, 0.990))),
        .p999 = adjusted(static_cast<std::uint64_t>(percentile_ticks(samples, 0.999))),
        .max = static_cast<double>(samples.back()) * ns,
    };
}

inline void print_header() {
    std::printf("\n%-40s %10s %8s %8s %8s %10s %10s\n", "Benchmark", "mean", "p50",
                "p90", "p99", "p99.9", "max");
    std::printf("%-40s %10s %8s %8s %8s %10s %10s\n",
                "----------------------------------------", "---------", "-------",
                "-------", "-------", "---------", "---------");
}

inline void print_row(const std::string& name, const Stats& s) {
    std::printf("%-40s %9.2f  %7.2f  %7.2f  %7.2f  %9.2f  %9.2f\n", name.c_str(),
                s.mean, s.p50, s.p90, s.p99, s.p999, s.max);
    std::printf("RESULT_LAT name=\"%s\" n=%zu mean=%.4f p50=%.4f p90=%.4f p99=%.4f "
                "p999=%.4f max=%.4f\n",
                name.c_str(), s.n, s.mean, s.p50, s.p90, s.p99, s.p999, s.max);
}

inline void print_throughput_row(const std::string& name, double mops, double ns_per_pair,
                                 std::uint64_t items, double seconds) {
    std::printf("%-40s %9.2f Mops/s  %7.2f ns/pair  (%llu items in %.3f s)\n",
                name.c_str(), mops, ns_per_pair,
                static_cast<unsigned long long>(items), seconds);
    std::printf("RESULT_TP name=\"%s\" mops=%.4f ns_per_pair=%.4f items=%llu seconds=%.6f\n",
                name.c_str(), mops, ns_per_pair,
                static_cast<unsigned long long>(items), seconds);
}

}  // namespace bench
