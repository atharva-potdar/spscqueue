#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "spsc.hpp"

#include <gtest/gtest.h>

TEST(SPSCQueueLayout, IsStandardLayout) {
    // offsets() uses offsetof, which is only portable for standard-layout
    // types.
    static_assert(std::is_standard_layout_v<SPSCQueue<int, 1024>>);
    SUCCEED();
}

TEST(SPSCQueueLayout, WholeObjectIsCacheLineAligned) {
    using Queue = SPSCQueue<int, 1024>;
    EXPECT_EQ(alignof(Queue), Queue::cache_line_size);
    EXPECT_EQ(sizeof(Queue) % Queue::cache_line_size, std::size_t{0});
}

TEST(SPSCQueueLayout, HeadAndTailAreOnDifferentCacheLines) {
    using Queue = SPSCQueue<int, 1024>;
    constexpr auto offsets = Queue::offsets();

    EXPECT_EQ(offsets.head % Queue::cache_line_size, std::size_t{0});
    EXPECT_EQ(offsets.tail % Queue::cache_line_size, std::size_t{0});
    EXPECT_GE(offsets.tail - offsets.head, Queue::cache_line_size);
}

TEST(SPSCQueueLayout, HeadDoesNotShareALineWithTheRing) {
    using Queue = SPSCQueue<int, 1024>;
    constexpr auto offsets = Queue::offsets();
    constexpr std::size_t buffer_end = offsets.buffer + sizeof(std::array<int, 1024>);

    // head is aligned up to the next cache line, so it cannot overlap the last
    // line occupied by the ring.
    EXPECT_GE(offsets.head, buffer_end);
    EXPECT_EQ(buffer_end % Queue::cache_line_size, std::size_t{0});
}

TEST(SPSCQueueLayout, SmallestRingIsStillPadded) {
    using Queue = SPSCQueue<std::uint8_t, 2>;
    constexpr auto offsets = Queue::offsets();

    EXPECT_EQ(offsets.head % Queue::cache_line_size, std::size_t{0});
    EXPECT_GE(offsets.tail - offsets.head, Queue::cache_line_size);
}

TEST(SPSCQueueLayout, UnpaddedReferenceSharesALine) {
    // Sanity check on the control used by the benchmarks: without alignment
    // the two indices do share a cache line.
    struct Unpadded {
        std::array<int, 1024> buffer{};
        std::atomic<std::size_t> head{0};
        std::atomic<std::size_t> tail{0};
    };
    constexpr std::size_t head_offset = offsetof(Unpadded, head);
    constexpr std::size_t tail_offset = offsetof(Unpadded, tail);
    EXPECT_LT(tail_offset - head_offset, (SPSCQueue<int, 1024>::cache_line_size));
}
