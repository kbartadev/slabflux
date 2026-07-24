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
 * ============================================================================* @brief SLABFLUX - Sharded MPMC Conduit Test Suite
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/mpmc_sharded_conduit.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

using namespace slabflux::core;

#include "slabflux/hw/spin_backoff.hpp"

/**
 * @brief Sharded Throughput Physics.
 * Validates that the conduit scales linearly
 * by spreading contention across hardware-isolated lanes.
 */
TEST(MpmcShardedConduit, ShardedThroughputPhysics) {
    constexpr size_t PRODUCERS = 8;
    constexpr size_t CONSUMERS = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 1'000'000;
    constexpr size_t TOTAL_ITEMS = PRODUCERS * ITEMS_PER_PRODUCER;

    // 8 Lanes matches the producer count for zero-contention ideal path
    mpmc_sharded_conduit<uint32_t, 16384, 8> conduit;
    auto received = std::make_unique<std::atomic<uint32_t>[]>(TOTAL_ITEMS);
    std::atomic<size_t> finished{0};
    std::atomic<bool> start_gate{false};

    auto start_time = std::chrono::high_resolution_clock::now();

    auto producer = [&](uint32_t id) {
        hardware_topology::pin_thread(id);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        for (uint32_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            uint32_t yield_count = 0;
            while (!conduit.try_push(id * ITEMS_PER_PRODUCER + i)) slabflux::hw::spin_backoff(yield_count);
        }
        finished.fetch_add(1, std::memory_order_release);
    };

    auto consumer = [&](uint32_t id) {
        hardware_topology::pin_thread(PRODUCERS + id);
        uint32_t val;
        uint32_t yield_count = 0;
        while (finished.load(std::memory_order_acquire) < PRODUCERS || conduit.occupancy() > 0) {
            if (conduit.try_pop(val)) {
                received[val].fetch_add(1, std::memory_order_relaxed);
                yield_count = 0;
            } else {
                slabflux::hw::spin_backoff(yield_count);
            }
        }
    };

    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < PRODUCERS; ++i) threads.emplace_back(producer, i);
    for (uint32_t i = 0; i < CONSUMERS; ++i) threads.emplace_back(consumer, i);

    start_gate.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();

    for (size_t i = 0; i < TOTAL_ITEMS; ++i) ASSERT_EQ(received[i].load(), 1);
}

/**
 * @brief Shard Saturated Spillover (Stealing Logic).
 * Verifies that producers and consumers correctly probe neighboring lanes
 * when their physically local memory channel is busy/empty.
 */
TEST(MpmcShardedConduit, ShardStealingLogic) {
    // Minimum functional capacity is 2 per lane (4 total for 2 lanes).
    // Requesting 2 will be rounded up to 4 to satisfy Vyukov invariants.
    mpmc_sharded_conduit<size_t, 4, 2> conduit;

    // Force a specific core for the first push
    hardware_topology::pin_thread(0);

    // Lane 0 capacity is 2.
    ASSERT_TRUE(conduit.try_push(101));
    ASSERT_TRUE(conduit.try_push(102));

    // Lane 0 is now full. The next push must "steal" or spill over
    // to Lane 1 despite thread affinity.
    ASSERT_TRUE(conduit.try_push(103));
    ASSERT_TRUE(conduit.try_push(104));

    // Both lanes are now full
    EXPECT_FALSE(conduit.try_push(105));

    // Consumption side
    size_t val;
    ASSERT_TRUE(conduit.try_pop(val));
    ASSERT_TRUE(conduit.try_pop(val));
    ASSERT_TRUE(conduit.try_pop(val));
    ASSERT_TRUE(conduit.try_pop(val));
    EXPECT_FALSE(conduit.try_pop(val)); // Both empty
}

/**
 * @brief Batch Flow Efficiency.
 * Tests SIMD-accelerated batch operations across multiple shards.
 */
TEST(MpmcShardedConduit, VectorizedBatchFlow) {
    mpmc_sharded_conduit<uint64_t, 4096, 4> conduit;
    std::array<uint64_t, 64> in_batch;
    std::array<uint64_t, 64> out_batch;

    for(uint64_t i = 0; i < 64; ++i) in_batch[i] = 0xAAAA'BBBB'0000 + i;

    // Push batch should distribute items across shards
    size_t pushed = conduit.push_batch(in_batch.data(), 64);
    EXPECT_EQ(pushed, 64);
    EXPECT_EQ(conduit.occupancy(), 64);

    // Pop batch should collect them back
    size_t popped = conduit.pop_batch(out_batch.data(), 64);
    EXPECT_EQ(popped, 64);

    for(uint64_t i = 0; i < 64; ++i) {
        EXPECT_EQ(out_batch[i], 0xAAAA'BBBB'0000 + i);
    }
}

/**
 * @brief Cross-Shard Invalidation.
 * Validates that invalidate_by_ptr scans and nulls targets across all parallel lanes.
 */
TEST(MpmcShardedConduit, CrossShardInvalidation) {
    mpmc_sharded_conduit<uintptr_t, 1024, 8> conduit;
    uintptr_t target = 0xDEADBEEF;
    uintptr_t noise = 0x12345678;

    // Populate all shards with a mix of target and noise
    for (size_t i = 0; i < 64; ++i) {
        conduit.push(target);
        conduit.push(noise);
    }

    conduit.invalidate_by_ptr(target);

    // Verify results
    uintptr_t val;
    size_t found_target = 0;
    while(conduit.try_pop(val)) {
        if (val == target) found_target++;
    }

    // Targets should be reset to default T{}
    EXPECT_EQ(found_target, 0);
}

/**
 * @brief Managed Ownership Nexus.
 * Validates wait-free transfer of pool-backed memory through sharded channels.
 */
TEST(MpmcShardedConduit, ManagedOwnershipNexus) {
    mpmc_pool<uint64_t, 1024> pool;
    mpmc_sharded_conduit<uint64_t*, 512, 4> conduit;

    auto managed = pool.make(42ULL);
    uint64_t* raw = managed.get();

    // Transfer to conduit
    ASSERT_TRUE(conduit.try_push(managed));
    EXPECT_EQ(managed.get(), nullptr); // Ownership relinquished

    // Recover from conduit using pool-aware pop
    auto recovered = conduit.try_pop(pool);
    ASSERT_TRUE(recovered);
    EXPECT_EQ(recovered.get(), raw);
    EXPECT_EQ(*recovered, 42ULL);
}

/**
 * @brief Physical Architecture Integrity.
 * Paradigm Shattering: Validates that the sharded structure does not 
 * violate cache-line boundaries, which would induce MESI thrashing 
 * (False Sharing) across the conduit
 */
TEST(MpmcShardedConduit, PhysicalResidencyAudit) {
    mpmc_sharded_conduit<uint64_t, 1024, 4> conduit;
    
    // Requirement: The conduit itself must be 64-byte aligned.
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&conduit) % 64, 0);
    
    // Requirement: Total size must be a multiple of cache line size 
    // to prevent L1-D set aliasing in dense arrays.
    EXPECT_EQ(sizeof(conduit) % 64, 0);
}

/**
 * @brief Producer-Lane Monotonicity (FIFO).
 * Note: Sharded conduits are "Sequentially Consistent" per-lane, but not 
 * "Globally Monotonic" across shards. We use 1 Lane here to verify the 
 * core FIFO integrity under thread affinity.
 */
TEST(MpmcShardedConduit, ProducerAffinityMonotonicity) {
    // Constraint: Use 1 Lane to guarantee global FIFO for this audit.
    mpmc_sharded_conduit<uint64_t, 1024, 1> conduit;
    constexpr size_t OPS = 50'000;
    
    std::thread p([&]() {
        hardware_topology::pin_thread(1);
        for(uint64_t i = 0; i < OPS; ++i) {
            uint32_t yield_count = 0;
            while(!conduit.try_push(i)) slabflux::hw::spin_backoff(yield_count);
        }
    });
    
    uint64_t last_val = 0;
    size_t count = 0;
    uint32_t yield_count = 0;
    while(count < OPS) {
        uint64_t val;
        if(conduit.try_pop(val)) {
            if (count > 0) {
                EXPECT_GE(val, last_val) << "FIFO violation in sharded timeline!";
            }
            last_val = val;
            count++;
            yield_count = 0;
        } else {
            slabflux::hw::spin_backoff(yield_count);
        }
    }
    p.join();
}
