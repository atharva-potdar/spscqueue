#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <type_traits>

// Cache line size used to pad the producer and consumer indices apart.
// Override with -DSPSC_CACHE_LINE_SIZE=<bytes> for targets where 64 is wrong
// (e.g. some Apple Silicon parts use 128).
#ifndef SPSC_CACHE_LINE_SIZE
#define SPSC_CACHE_LINE_SIZE 64
#endif

// Bounded, zero-allocation, lock-free single-producer / single-consumer queue.
//
// Contract:
//   * exactly one thread may call enqueue();
//   * exactly one *different* thread may call dequeue();
//   * T must be trivially copyable and default constructible.
//
// Capacity is N - 1. One slot is always left empty so that full and empty are
// distinguishable without a separate counter.
//
// Memory ordering:
//   producer: relaxed load of tail, acquire load of head, then a release store
//             of tail publishes the payload write.
//   consumer: relaxed load of head, acquire load of tail, then a release store
//             of head publishes the payload read (so the producer may reuse
//             the slot).
template<typename T, std::size_t N>
class SPSCQueue {
    static_assert(N >= 2, "N must be at least 2 (usable capacity is N - 1)");
    static_assert(std::has_single_bit(N), "N must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable (the queue performs no object construction)");
    static_assert(std::is_default_constructible_v<T>,
                  "T must be default constructible (the ring is value-initialised on construction)");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "SPSCQueue requires lock-free std::atomic<std::size_t>");

    public:
        static constexpr std::size_t cache_line_size = SPSC_CACHE_LINE_SIZE;
        static constexpr std::size_t mask = N - 1;
        static constexpr std::size_t capacity = N - 1;

        // Byte offsets of the members, for layout tests and diagnostics.
        struct Offsets {
            std::size_t buffer;
            std::size_t head;
            std::size_t tail;
        };
        static constexpr Offsets offsets() {
            return {offsetof(SPSCQueue, buffer), offsetof(SPSCQueue, head),
                    offsetof(SPSCQueue, tail)};
        }

    private:
        // The ring itself. For N = 1024 and int this is exactly 4096 bytes, so
        // the two indices below start on their own cache lines.
        std::array<T, N> buffer{};

        // Producer-owned index and consumer-owned index, each on its own cache
        // line so that the two cores never write to the same line.
        alignas(cache_line_size) std::atomic<std::size_t> head = 0;
        alignas(cache_line_size) std::atomic<std::size_t> tail = 0;

    public:
        [[nodiscard]] bool enqueue(T val) {
            const std::size_t currentTail = tail.load(std::memory_order_relaxed);
            const std::size_t nextTail = (currentTail + 1) & mask;
            // Acquire pairs with the consumer's release store of head: the
            // slot we are about to overwrite has been fully read.
            if (nextTail == head.load(std::memory_order_acquire))
                return false; // Queue full
            buffer[currentTail] = std::move(val);
            // Release publishes the payload write before the index update.
            tail.store(nextTail, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool dequeue(T& out) {
            const std::size_t currentHead = head.load(std::memory_order_relaxed);
            // Acquire pairs with the producer's release store of tail: the
            // payload for this slot is visible.
            if (currentHead == tail.load(std::memory_order_acquire))
                return false; // Queue empty
            out = std::move(buffer[currentHead]);
            // Release publishes the read so the producer may reuse the slot.
            head.store((currentHead + 1) & mask, std::memory_order_release);
            return true;
        }
};
