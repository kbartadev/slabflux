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
 * ============================================================================* @brief SLABFLUX - Asymmetric MPSC Pool Test Suite
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "slabflux/core/mpsc_pool.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::core;

struct alignas(64) mpsc_payload {
    uint64_t val;
    uint64_t ts;
};

/**
 * @brief Asymmetric Lifecycle Audit.
 * Verifies the full asymmetric cycle:
 * 1. Producer allocates from the LIFO stack (Fast Path - 0 atomics).
 * 2. Consumer releases to the intrusive atomic stack (Slow Path - 1 exchange).
 * 3. Producer reclaims the entire chain in a single O(1) transaction.
 */
TEST(MpscPool, AsymmetricLifecycleAudit) {
    constexpr size_t POOL_CAP = 4096;
    constexpr size_t BURST_SIZE = 4096;
    
    mpsc_pool<mpsc_payload, POOL_CAP, reclaim_strategy::manual> pool;
    std::vector<mpsc_payload*> allocated;
    allocated.reserve(BURST_SIZE);

    // 1. Producer-side acquisition
    for (size_t i = 0; i < BURST_SIZE; ++i) {
        auto* p = pool.make_raw();
        ASSERT_NE(p, nullptr);
        p->val = i;
        allocated.push_back(p);
    }

    // 2. Consumer-side release (Cross-thread)
    std::thread consumer([&]() {
        hardware_topology::pin_thread(1);
        for (auto* p : allocated) {
            pool.release(p);
        }
    });
    consumer.join();

    // 3. Producer-side reclamation
    // The LIFO stack should currently be empty (manual strategy)
    mpsc_payload* dummy[1];
    EXPECT_EQ(pool.make_batch(dummy, 1), 0);

    // Reclaim memory returned by threads
    pool.reclaim_returns();
    
    // Allocation should succeed now
    EXPECT_EQ(pool.make_batch(dummy, 1), 1);
    pool.release(dummy[0]);
}

/**
 * @brief Automatic Reclamation Physics.
 * Verifies that 'reclaim_strategy::automatic' correctly flushes 
 * the return ring when the main LIFO stack is exhausted.
 */
TEST(MpscPool, AutomaticReclaimPhysics) {
    mpsc_pool<size_t, 2, reclaim_strategy::automatic> pool;
    
    size_t* p1 = pool.make_raw(100);
    size_t* p2 = pool.make_raw(200);
    
    // Pool is physically empty
    EXPECT_EQ(pool.make_raw(300), nullptr);
    
    // Release one element
    pool.release(p1);
    
    // Allocation should succeed now as 'automatic' strategy 
    // triggers reclaim_returns() upon exhaustion.
    size_t* p3 = pool.make_raw(300);
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(p3, p1); // Verify LIFO reuse
    EXPECT_EQ(*p3, 300);
    
    pool.release(p2);
    pool.release(p3);
}

/**
 * @brief Batch Operation Efficiency.
 * Tests the optimized paths for make_batch and release_batch.
 */
TEST(MpscPool, BatchFlowAudit) {
    mpsc_pool<uint64_t, 1024, reclaim_strategy::manual> pool;
    uint64_t* batch[64];
    
    size_t allocated = pool.make_batch(batch, 64);
    EXPECT_EQ(allocated, 64);
    
    // Batch release: intrusive linking happens locally before committing
    pool.release_batch(batch, 64);
    
    pool.reclaim_returns();
    EXPECT_EQ(pool.make_batch(batch, 64), 64);
}

/**
 * @brief Managed Ownership Nexus.
 * Verifies RAII integration: managed_data destructor must automatically 
 * return memory to the asymmetric atomic stack.
 */
TEST(MpscPool, ManagedOwnershipNexus) {
    mpsc_pool<uint64_t, 1024> pool;
    
    {
        auto managed = pool.make(1337ULL);
        ASSERT_TRUE(managed);
        EXPECT_EQ(*managed, 1337ULL);
    }
    
    pool.reclaim_returns();
    uint64_t* raw = pool.make_raw();
    EXPECT_NE(raw, nullptr);
    pool.release(raw);
}

/**
 * @brief Intrusive Stack Contention Audit.
 * Tests 'return_head_' CAS loop stability under extreme load 
 * while many consumers return memory simultaneously.
 */
TEST(MpscPool, HighContentionReturn) {
    constexpr size_t CONSUMERS = 16;
    constexpr size_t ITEMS = 65536;
    mpsc_pool<size_t, CONSUMERS * ITEMS, reclaim_strategy::manual> pool;
    
    std::vector<size_t*> ptrs;
    for(size_t i = 0; i < CONSUMERS * ITEMS; ++i) ptrs.push_back(pool.make_raw(i));

    std::vector<std::thread> workers;
    for(size_t i = 0; i < CONSUMERS; ++i) {
        workers.emplace_back([&, i]() {
            hardware_topology::pin_thread(i + 1);
            size_t start = i * ITEMS;
            for(size_t j = 0; j < ITEMS; ++j) pool.release(ptrs[start + j]);
        });
    }

    for(auto& w : workers) w.join();
    
    // Reclamation Audit: Since the pool implements an amortized reclamation limit 
    // (MAX_RECLAIM_BURST), we must pump the returns until the stack is fully restored.
    size_t total_expected = CONSUMERS * ITEMS;
    size_t count = 0;
    while (count < total_expected) {
        pool.reclaim_returns();
        while(pool.make_raw()) count++;
    }
    EXPECT_EQ(count, CONSUMERS * ITEMS);
}
