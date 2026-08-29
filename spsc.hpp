#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <type_traits>

template<typename T, std::size_t N>
class SPSCQueue {
    static_assert(N >= 2, "N must be at least 2 (usable capacity is N - 1)");
    static_assert(std::has_single_bit(N), "N must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>);

    static constexpr std::size_t mask = N - 1;

    private:
        std::array<T, N> buffer{};
        alignas(64) std::atomic<std::size_t> head = 0;
        alignas(64) std::atomic<std::size_t> tail = 0;
    public:
        [[nodiscard]] bool enqueue(T val) {
            std::size_t currentTail = tail.load(std::memory_order_relaxed);
            std::size_t nextTail = (currentTail + 1) & mask;
            if (nextTail == head.load(std::memory_order_acquire))
                return false; // Queue full
            buffer[currentTail] = std::move(val);
            tail.store(nextTail, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool dequeue(T& out) {
            std::size_t currentHead = head.load(std::memory_order_relaxed);
            if (currentHead == tail.load(std::memory_order_acquire))
                return false; // Queue empty
            out = std::move(buffer[currentHead]);
            head.store((currentHead + 1) & mask, std::memory_order_release);
            return true;
        }
};
