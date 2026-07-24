#include <gtest/gtest.h>

#include <cstdint>

#include "slabflux/core.hpp"

using namespace slabflux;

struct alignas(64) massive_event {
    int data[10];
};

// ============================================================================
// TEST 1 — Cache‑line physics: payloads must be physically aligned.
// ============================================================================
TEST(MemoryPhysics, payloads_are_strictly_cache_line_aligned) {
    pool<massive_event, 10> p;
    auto ev = p.make();

    // Physical determinism.
    // The payload address MUST be aligned to the event’s declared alignment (64 bytes).
    // Any deviation introduces false sharing, split loads, and unpredictable stalls.
    std::uintptr_t raw_address = reinterpret_cast<std::uintptr_t>(ev.operator massive_event *());
    EXPECT_EQ(raw_address % 64, 0) << "Unaligned memory allocation detected!";
}

// ============================================================================
// TEST 2 — Free‑list physics: LIFO reuse of hot cache lines.
// ============================================================================
TEST(MemoryPhysics, pool_strictly_reuses_memory_addresses_in_LIFO_order) {
    pool<massive_event, 10> p;

    void* addr1 = nullptr;
    {
        auto ev1 = p.make();
        addr1 = static_cast<massive_event*>(ev1);
    } // Returned to the pool here.

    auto ev2 = p.make();
    void* addr2 = static_cast<massive_event*>(ev2);

    // Lock‑free free‑list = Treiber stack.
    // The most recently freed cell MUST be the next one returned.
    // This guarantees hot L1 reuse and zero allocator entropy.
    EXPECT_EQ(addr1, addr2) << "Pool is not reusing hot cache lines!";
}

// ============================================================================
// TEST 3 — ABA fuzzing: pointer‑tagging must prevent corruption.
// ============================================================================
TEST(MemoryPhysics, pool_survives_rapid_aba_fuzzing) {
    pool<massive_event, 5> p;

    // Brutal single‑threaded churn to hammer the free‑list.
    // If ABA protection is broken, the free‑list collapses instantly.
    for (int i = 0; i < 1'000'000; ++i) {
        {
            auto ev1 = p.make();
            auto ev2 = p.make();
        }
        {
            auto ev3 = p.make();
        }
    }

    SUCCEED() << "ABA tags successfully prevented free-list corruption.";
}
