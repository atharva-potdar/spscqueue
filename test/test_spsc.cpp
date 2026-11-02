#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>

#include "spsc.hpp"

TEST(SPSCQueue, StartsEmpty) {
    SPSCQueue<int, 8> queue;
    int out = 0;
    EXPECT_FALSE(queue.dequeue(out));
}

TEST(SPSCQueue, SingleEnqueueDequeue) {
    SPSCQueue<int, 8> queue;
    int out = 0;

    EXPECT_TRUE(queue.enqueue(7));
    EXPECT_TRUE(queue.dequeue(out));
    EXPECT_EQ(out, 7);
    EXPECT_FALSE(queue.dequeue(out));
}

TEST(SPSCQueue, PreservesFifoOrder) {
    SPSCQueue<int, 8> queue;
    int out = 0;
    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(queue.enqueue(i));
    }
    for (int i = 0; i < 7; ++i) {
        ASSERT_TRUE(queue.dequeue(out));
        EXPECT_EQ(out, i);
    }
}

TEST(SPSCQueue, UsableCapacityIsNMinusOne) {
    SPSCQueue<int, 8> queue;
    EXPECT_EQ((SPSCQueue<int, 8>::capacity), 7u);

    int out = 0;
    for (std::size_t i = 0; i < SPSCQueue<int, 8>::capacity; ++i) {
        EXPECT_TRUE(queue.enqueue(static_cast<int>(i)));
    }
    EXPECT_FALSE(queue.enqueue(99));

    // One dequeue makes exactly one slot available.
    ASSERT_TRUE(queue.dequeue(out));
    EXPECT_EQ(out, 0);
    EXPECT_TRUE(queue.enqueue(100));
    EXPECT_FALSE(queue.enqueue(101));
}

TEST(SPSCQueue, FullQueueRejectsUntilDrained) {
    SPSCQueue<int, 2> queue;  // capacity 1
    int out = 0;

    EXPECT_TRUE(queue.enqueue(1));
    EXPECT_FALSE(queue.enqueue(2));
    ASSERT_TRUE(queue.dequeue(out));
    EXPECT_EQ(out, 1);
    EXPECT_FALSE(queue.dequeue(out));
    EXPECT_TRUE(queue.enqueue(3));
    ASSERT_TRUE(queue.dequeue(out));
    EXPECT_EQ(out, 3);
}

TEST(SPSCQueue, WrapsAroundRepeatedly) {
    SPSCQueue<int, 4> queue;  // capacity 3
    int out = 0;
    int next_enqueue = 0;
    int next_dequeue = 0;

    for (int cycle = 0; cycle < 10'000; ++cycle) {
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(queue.enqueue(next_enqueue++));
        }
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(queue.dequeue(out));
            EXPECT_EQ(out, next_dequeue++);
        }
    }
}

TEST(SPSCQueue, LargePayloadIsCopiedExactly) {
    struct Payload {
        std::uint64_t words[16];
    };

    SPSCQueue<Payload, 4> queue;
    Payload in{};
    for (std::size_t i = 0; i < 16; ++i) {
        in.words[i] = i * 0x0102030405060708ULL;
    }

    ASSERT_TRUE(queue.enqueue(in));
    Payload out{};
    ASSERT_TRUE(queue.dequeue(out));
    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(out.words[i], in.words[i]);
    }
}

TEST(SPSCQueue, CharAndBytePayloads) {
    SPSCQueue<std::uint8_t, 4> bytes;
    std::uint8_t out = 0;
    EXPECT_TRUE(bytes.enqueue(0xAB));
    EXPECT_TRUE(bytes.dequeue(out));
    EXPECT_EQ(out, 0xAB);
}

TEST(SPSCQueue, IsNotCopyableOrMovable) {
    using Q = SPSCQueue<int, 8>;
    static_assert(!std::is_copy_constructible_v<Q>);
    static_assert(!std::is_copy_assignable_v<Q>);
    static_assert(!std::is_move_constructible_v<Q>);
    static_assert(!std::is_move_assignable_v<Q>);
}

TEST(SPSCQueue, RotationOfValuesFromProducerToConsumer) {
    // A small deterministic multi-threaded smoke test; the heavier one lives in
    // test_stress.cpp.
    SPSCQueue<std::uint64_t, 16> queue;
    constexpr std::uint64_t kItems = 100'000;
    std::atomic<bool> start{false};
    std::atomic<bool> mismatch{false};

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        for (std::uint64_t i = 0; i < kItems; ++i) {
            while (!queue.enqueue(i)) {
            }
        }
    });

    std::thread consumer([&] {
        start.store(true, std::memory_order_release);
        for (std::uint64_t i = 0; i < kItems; ++i) {
            std::uint64_t value = 0;
            while (!queue.dequeue(value)) {
            }
            if (value != i) {
                mismatch.store(true, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();
    EXPECT_FALSE(mismatch.load());
}
