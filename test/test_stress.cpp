// Multi-threaded sequence stress tests.
//
// The producer enqueues 0, 1, 2, ...; the consumer checks that it dequeues
// exactly that sequence. Any dropped, duplicated, reordered, or corrupted item
// fails the test. Run this under ThreadSanitizer and AddressSanitizer as well
// as Release (see the justfile recipes).
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include "spsc.hpp"

namespace {

std::size_t stress_items() {
    const char* env = std::getenv("SPSC_STRESS_ITEMS");
    if (env == nullptr) {
        return 1'000'000;
    }
    const auto value = std::strtoull(env, nullptr, 10);
    return value == 0 ? 1'000'000 : static_cast<std::size_t>(value);
}

template<std::size_t N>
void run_sequence_test(std::size_t items) {
    SPSCQueue<std::uint64_t, N> queue;
    std::atomic<int> stage{0};
    std::atomic<bool> mismatch{false};
    std::atomic<std::uint64_t> consumed{0};

    std::thread producer([&] {
        stage.store(1, std::memory_order_release);
        while (stage.load(std::memory_order_acquire) != 2) {
        }
        for (std::uint64_t i = 0; i < items; ++i) {
            while (!queue.enqueue(i)) {
            }
        }
    });

    std::thread consumer([&] {
        while (stage.load(std::memory_order_acquire) != 1) {
        }
        stage.store(2, std::memory_order_release);
        for (std::uint64_t i = 0; i < items; ++i) {
            std::uint64_t value = 0;
            while (!queue.dequeue(value)) {
            }
            if (value != i) {
                mismatch.store(true, std::memory_order_relaxed);
            }
            consumed.store(i + 1, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();

    EXPECT_FALSE(mismatch.load()) << "item corrupted, dropped, duplicated, or reordered";
    EXPECT_EQ(consumed.load(), items) << "consumer did not receive every item";
}

}  // namespace

TEST(Stress, SequenceCapacity1) {
    run_sequence_test<2>(stress_items());
}

TEST(Stress, SequenceCapacity3) {
    run_sequence_test<4>(stress_items());
}

TEST(Stress, SequenceCapacity1023) {
    run_sequence_test<1024>(stress_items());
}

TEST(Stress, SequenceCapacity65535) {
    run_sequence_test<65536>(stress_items());
}

TEST(Stress, RepeatedShortRuns) {
    // Fresh queues, short runs: catches state that only breaks at startup or
    // when the ring wraps immediately.
    const std::size_t items = stress_items() / 100;
    for (int i = 0; i < 100; ++i) {
        run_sequence_test<4>(items);
    }
}
