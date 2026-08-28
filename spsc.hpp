#pragma once
#include <array>
#include <bit>

template<typename T, std::size_t N>
class SPSCQueue {
    static_assert(N >= 2, "N must be at least 2 (usable capacity is N - 1)");
    static_assert(std::has_single_bit(N), "N must be a power of 2");

    static constexpr std::size_t mask = N - 1;

    private:
        std::array<T, N> buffer{};
        std::size_t head = 0;
        std::size_t tail = 0;
    public:
        [[nodiscard]] bool enqueue(T val) {
            if (((tail + 1) & mask) == head)
                return false; // Queue full
            buffer[tail] = std::move(val);
            tail = (tail + 1) & mask;
            return true;
        }

        [[nodiscard]] bool dequeue(T& out) {
            if (head == tail)
                return false; // Queue empty
            out = std::move(buffer[head]);
            head = (head + 1) & mask;
            return true;
        }
};
