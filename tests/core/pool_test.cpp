/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * SOURCE-AVAILABLE CODEBASE
 *
 * This source file is distributed under the conditions of the SLABFLUX 
 * SOURCE-AVAILABLE AND ECOSYSTEM LICENSE (the "License").
 *
 * ----------------------------------------------------------------------------
 * CRITICAL WARNING
 * ----------------------------------------------------------------------------
 * This module may execute outside standard OS mediation layers. Incorrect 
 * integration, misconfiguration, or unsafe deployment can result in:
 *
 *   • irreversible data corruption
 *   • kernel instability or panics
 *   • NIC or PCIe bus desynchronization
 *   • undefined hardware state transitions
 *   • permanent loss of system integrity
 *
 * Use only in controlled environments with full understanding of the 
 * architectural constraints and hardware implications.
 *
 * ----------------------------------------------------------------------------
 * USAGE GUIDELINES
 * ----------------------------------------------------------------------------
 * Execution, integration, and deployment by developers is permitted strictly 
 * subject to the conditional grants and structural limitations defined within 
 * the License. Please refer to the License for full terms regarding corporate 
 * deployment and replication.
 *
 * ----------------------------------------------------------------------------
 * LIMITATION OF LIABILITY
 * ----------------------------------------------------------------------------
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL THE AUTHOR OR 
 * COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, 
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 * DISCLAIMER OF WARRANTY
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * See accompanying LICENSE and NOTICE files for the integrated terms of use.
 * ============================================================================*/
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
    // Use 1 Lane to ensure strict capacity enforcement for exhaustion tests.
    mpmc_pool<trackable_event, 2, 1> p;
    auto ev1 = p.make(nullptr, 1);
    auto ev2 = p.make(nullptr, 2);
    auto ev3 = p.make(nullptr, 3);  // Pool is exhausted.

    // We drop packets (return nullptr) rather than crashing the node!
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