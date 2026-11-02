// Verifies that enqueue/dequeue do not allocate. This is a standalone
// executable (not a Google Test) because it replaces the global allocator.
//
// Under AddressSanitizer the allocator interceptor owns operator new, so the
// test reports a skip instead of producing a false signal.
#include "spsc.hpp"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(__SANITIZE_ADDRESS__)
#define SPSC_HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SPSC_HAS_ASAN 1
#endif
#endif

#ifndef SPSC_HAS_ASAN
#define SPSC_HAS_ASAN 0
#endif

#if !SPSC_HAS_ASAN

namespace {
std::atomic<long> g_allocations{0};
std::atomic<long> g_deallocations{0};
}  // namespace

void* operator new(std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept {
    if (p != nullptr) {
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

void operator delete[](void* p) noexcept {
    if (p != nullptr) {
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    if (p != nullptr) {
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    if (p != nullptr) {
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

int main() {
    SPSCQueue<int, 1024> queue;
    int value = 0;
    const long before = g_allocations.load(std::memory_order_relaxed);

    constexpr int kItems = 10'000'000;
    for (int i = 0; i < kItems; ++i) {
        while (!queue.enqueue(i)) {
        }
        while (!queue.dequeue(value)) {
        }
    }

    const long after = g_allocations.load(std::memory_order_relaxed);
    const long allocations = after - before;

    std::printf("allocations during %d enqueue+dequeue pairs: %ld\n", kItems,
                allocations);
    std::printf("sizeof(queue)=%zu alignof=%zu capacity=%zu\n", sizeof(queue),
                alignof(decltype(queue)), SPSCQueue<int, 1024>::capacity);

    if (allocations != 0) {
        std::printf("FAIL: queue allocated on the hot path\n");
        return 1;
    }
    std::printf("PASS: no allocations on the hot path\n");
    return 0;
}

#else

int main() {
    std::printf("SKIP: allocation counting is not meaningful under ASan\n");
    return 0;
}

#endif
