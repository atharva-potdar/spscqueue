// Deliberately unpadded reference queue, used only to quantify the effect of
// the cache-line separation in SPSCQueue.
//
// The logic is identical to SPSCQueue; the only difference is that head and
// tail are not aligned, so for small payloads they share a cache line and every
// handoff bounces the line between the two cores.
#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <type_traits>

template<typename T, std::size_t N>
class NaiveSPSCQueue {
    static_assert(N >= 2, "N must be at least 2");
    static_assert(std::has_single_bit(N), "N must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_default_constructible_v<T>);

    public:
        static constexpr std::size_t mask = N - 1;
        static constexpr std::size_t capacity = N - 1;

    private:
        std::array<T, N> buffer{};
        std::atomic<std::size_t> head = 0;  // same cache line as tail
        std::atomic<std::size_t> tail = 0;

    public:
        [[nodiscard]] bool enqueue(T val) {
            const std::size_t currentTail = tail.load(std::memory_order_relaxed);
            const std::size_t nextTail = (currentTail + 1) & mask;
            if (nextTail == head.load(std::memory_order_acquire))
                return false;
            buffer[currentTail] = std::move(val);
            tail.store(nextTail, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool dequeue(T& out) {
            const std::size_t currentHead = head.load(std::memory_order_relaxed);
            if (currentHead == tail.load(std::memory_order_acquire))
                return false;
            out = std::move(buffer[currentHead]);
            head.store((currentHead + 1) & mask, std::memory_order_release);
            return true;
        }
};
