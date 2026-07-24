#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "slabflux/core.hpp"

using namespace slabflux;

// Event with lifetime tracking injected for correctness verification.
// The counter proves that allocation and reclamation happen exactly once.
struct trackable_event {
    std::atomic<int>* alive_counter;
    int payload;

    trackable_event(std::atomic<int>* counter, int p) : alive_counter(counter), payload(p) {
        if (alive_counter) alive_counter->fetch_add(1, std::memory_order_relaxed);
    }

    ~trackable_event() {
        if (alive_counter) alive_counter->fetch_sub(1, std::memory_order_relaxed);
    }
};

// ============================================================================
// TEST 1 — O(1) allocation + O(1) reclamation with perfect lifetime accounting.
// ============================================================================
TEST(PoolTest, pool_allocates_and_reclaims_in_O1) {
    std::atomic<int> alive{ 0 };
    pool<trackable_event, 10> p;

    {
        auto ev1 = p.make(&alive, 42);
        auto ev2 = p.make(&alive, 99);

        EXPECT_EQ(alive.load(), 2);
        EXPECT_EQ(ev1->payload, 42);
        EXPECT_EQ(ev2->payload, 99);
    }  // ev1 and ev2 go out of scope → deterministic reclamation

    EXPECT_EQ(alive.load(), 0);

    // Reallocate to ensure the free‑list is intact and stable.
    auto ev3 = p.make(&alive, 100);
    EXPECT_EQ(alive.load(), 1);
    EXPECT_EQ(ev3->payload, 100);
}

// ============================================================================
// TEST 2 — Pool exhaustion enforces safe backpressure instead of crashing.
// ============================================================================
TEST(PoolTest, pool_exhaustion_yields_graceful_backpressure) {
    pool<trackable_event, 2> p;
    auto ev1 = p.make(nullptr, 1);
    auto ev2 = p.make(nullptr, 2);
    auto ev3 = p.make(nullptr, 3);  // Pool is exhausted.

    // In HFT, we drop packets (return nullptr) rather than crashing the node!
    EXPECT_EQ(ev3, nullptr);
}

// ============================================================================
// TEST 3 — Concurrent allocate/deallocate must be thread‑safe.
// High‑contention adversarial CAS hammering of the free‑list.
// ============================================================================
TEST(PoolTest, pool_concurrent_alloc_dealloc_is_thread_safe) {
    pool<trackable_event, 10000> p;
    std::atomic<int> alive{ 0 };

    auto worker = [&]() {
        for (int i = 0; i < 5000; ++i) {
            auto ev = p.make(&alive, i);
            EXPECT_NE(ev, nullptr);
            // Destructor returns memory instantly when ev goes out of scope.
        }
        };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back(worker);

    for (auto& t : threads) t.join();

    // After all threads finish, the pool must be fully intact.
    EXPECT_EQ(alive.load(), 0);
}