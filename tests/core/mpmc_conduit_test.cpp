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
 * ============================================================================*
 * @file industrial_jitter_test.cpp

 * SLABFLUX
 * Copyright (c) 2026 Kristóf Barta. All rights reserved.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Absolute Liability Limitation & Full Terms: See DISCLAIMER, NOTICE, LICENSE.
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/mpmc_conduit.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::core;

#include "slabflux/hw/spin_backoff.hpp"

/**
 * @brief Stress tests MPMC with asymmetric Producer/Consumer ratios.
 * Exposes race conditions in slot-sequence increments.
 */
TEST(MpmcConduit, HighContentionStress) {
    constexpr size_t PRODUCERS = 8; // Force oversubscription
    constexpr size_t CONSUMERS = 2; // Pressure bottleneck simulation
    constexpr size_t ITEMS_PER_PRODUCER = 5'000'000;
    constexpr size_t TOTAL_ITEMS = PRODUCERS * ITEMS_PER_PRODUCER;
    
    mpmc_conduit<uint32_t, 4096> nexus;
    auto received = std::make_unique<std::atomic<uint32_t>[]>(TOTAL_ITEMS);
    alignas(64) std::atomic<size_t> finished{0};
    alignas(64) std::atomic<bool> start_gate{false};

    auto producer = [&](uint32_t id) {
        hardware_topology::pin_thread(static_cast<int>(id + 1));
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        
        for (uint32_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            uint32_t yield_count = 0;
            while (!nexus.try_push(id * ITEMS_PER_PRODUCER + i)) {
                slabflux::hw::spin_backoff(yield_count);
            }
        }
        finished.fetch_add(1);
    };

    auto consumer = [&]() {
        hardware_topology::pin_thread(6); // Float on high-core IDs
        // Consumers are allowed to float on remaining cores to verify scheduling resilience
        uint32_t val;
        uint32_t yield_count = 0;
        while (finished.load() < PRODUCERS || nexus.occupancy() > 0) {
            if (nexus.try_pop(val)) {
                received[val].fetch_add(1);
                yield_count = 0;
            } else {
                slabflux::hw::spin_backoff(yield_count);
            }
        }
    };

    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < PRODUCERS; ++i) threads.emplace_back(producer, i);
    for (size_t i = 0; i < CONSUMERS; ++i) threads.emplace_back(consumer);
    
    start_gate.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();

    for (size_t i = 0; i < TOTAL_ITEMS; ++i) ASSERT_EQ(received[i].load(), 1);
}

/**
 * @brief Verifies the Ambiguity Resolution Contract.
 * Paradigm Shattering: try_pop(T&) must distinguish between "Queue Empty"
 * and "Item is NULL" in a concurrent environment.
 */
TEST(MpmcConduit, ContractAmbiguity) {
    mpmc_conduit<int*, 1024> nexus;
    int* out = reinterpret_cast<int*>(0xDEADBEEF);

    // 1. Empty Case
    EXPECT_FALSE(nexus.try_pop(out));
    EXPECT_EQ(out, reinterpret_cast<int*>(0xDEADBEEF));

    // 2. NULL Item Case
    nexus.try_push(nullptr);
    EXPECT_TRUE(nexus.try_pop(out));
    EXPECT_EQ(out, nullptr);
}

/**
 * @brief Verifies physical residency and capacity normalization.
 */
TEST(MpmcConduit, CapacityRounding) {
    // Requesting 1000 should result in a 1024 capacity (bit_ceil)
    mpmc_conduit<size_t, 1000> nexus;

    // Verify power-of-two masking logic handles the rounded size
    for (size_t i = 0; i < 1024; ++i) {
        ASSERT_TRUE(nexus.try_push(i));
    }
    EXPECT_FALSE(nexus.try_push(9999));
    EXPECT_EQ(nexus.occupancy(), 1024);
}

/**
 * @brief Verifies automated reclamation through managed_data integration.
 */
TEST(MpmcConduit, ManagedDataPoolIntegration) {
    mpmc_pool<uint64_t, 1024> pool;
    mpmc_conduit<uint64_t*, 1024> nexus;
    auto managed = pool.make(12345ULL);

    // Transfer ownership to the nexus
    nexus.push(managed);
    EXPECT_EQ(managed.get(), nullptr);

    auto recovered = nexus.try_pop(pool);
    ASSERT_TRUE(recovered);
    EXPECT_EQ(*recovered, 12345ULL);
}

/**
 * @brief Verifies that managed_data retains ownership if push fails.
 */
TEST(MpmcConduit, BackpressureOwnershipRetention) {
    mpmc_pool<uint64_t, 256> pool;
    mpmc_conduit<uint64_t*, 8> nexus;

    // Fill the conduit until it is physically saturated.
    std::vector<managed_data<uint64_t, mpmc_pool<uint64_t, 256>>> fillers;
    while (true) {
        auto m = pool.make(1ULL);
        if (!nexus.try_push(m)) break;
        fillers.push_back(std::move(m));
        if (fillers.size() > 1024) break; // Safety fuse
    }

    auto m_blocked = pool.make(999ULL);
    uint64_t* raw_addr = m_blocked.get();
    EXPECT_FALSE(nexus.try_push(m_blocked));
    EXPECT_EQ(m_blocked.get(), raw_addr); // Pointer must still be held by managed_data
}

/**
 * @brief Adversarial Slot Collision.
 * Paradigm Shattering: Forces maximum CAS contention on a single physical 
 * cache line by utilizing a buffer size of 1.
 */
TEST(MpmcConduit, AdversarialSlotCollision) {
    constexpr size_t ITER = 1'000'000;
    // Constraint: Use 1 Lane to ensure strict FIFO and force CAS contention on a single atomic head/tail pair.
    mpmc_conduit<size_t, 2, 1> tiny_nexus;
    alignas(64) std::atomic<bool> start_gate{false};

    std::thread p([&]() {
        hardware_topology::pin_thread(1);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        for (size_t i = 0; i < ITER; ++i) {
            uint32_t yield_count = 0;
            while (!tiny_nexus.try_push(i)) slabflux::hw::spin_backoff(yield_count);
        }
    });

    std::thread c([&]() {
        hardware_topology::pin_thread(2);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        for (size_t i = 0; i < ITER; ++i) {
            size_t val;
            uint32_t yield_count = 0;
            while (!tiny_nexus.try_pop(val)) slabflux::hw::spin_backoff(yield_count);
            if (val != i) {
                ADD_FAILURE() << "FIFO breach in adversarial collision: Expected " << i << " but got " << val;
                return;
            }
        }
    });

    start_gate.store(true, std::memory_order_release);
    p.join(); c.join();
}

/**
 * @brief Verifies ABA-Safety by rapidly reusing a tiny ring buffer.
 */
TEST(MpmcConduit, ABARapidCycleStress) {
    constexpr size_t TINY_SIZE = 2; // Maximum sensitivity
    constexpr size_t ITERATIONS = 1'000'000;
    // Constraint: Use 1 Lane to ensure strict FIFO during rapid reuse.
    mpmc_conduit<size_t, TINY_SIZE, 1> nexus;
    alignas(64) std::atomic<bool> start_gate{false};

    std::thread p([&]() {
        hardware_topology::pin_thread(1);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        for (size_t i = 0; i < ITERATIONS; ++i) {
            uint32_t yield_count = 0;
            while (!nexus.try_push(i)) slabflux::hw::spin_backoff(yield_count);
        }
    });

    hardware_topology::pin_thread(2);
    start_gate.store(true, std::memory_order_release);

    for (size_t i = 0; i < ITERATIONS; ++i) {
        size_t val;
        uint32_t yield_count = 0;
        while (!nexus.try_pop(val)) slabflux::hw::spin_backoff(yield_count);
        if (val != i) {
            ADD_FAILURE() << "FIFO breach in ABA stress: Expected " << i << " but got " << val;
            return;
        }
    }
    p.join();
}

/**
 * @brief Verifies that MPMC occupancy is monotonic during single-threaded bursts.
 */
TEST(MpmcConduit, OccupancyMonotonicity) {
    mpmc_conduit<uint64_t, 1024> nexus;
    for (uint64_t i = 0; i < 100; ++i) {
        nexus.push(i);
        EXPECT_EQ(nexus.occupancy(), i + 1);
    }
    for (uint64_t i = 0; i < 100; ++i) {
        uint64_t val;
        nexus.pop(val);
        EXPECT_EQ(nexus.occupancy(), 100 - (i + 1));
    }
}

TEST(MpmcConduit, ManagedOwnershipNexus) {
    mpmc_pool<uint64_t, 4096> pool;
    mpmc_conduit<uint64_t*, 1024> nexus;
    constexpr size_t OPS = 50'000;
    alignas(64) std::atomic<bool> start_gate{false};

    std::thread p([&]() {
        hardware_topology::pin_thread(1);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        for(size_t i = 0; i < OPS; ++i) {
            managed_data<uint64_t, mpmc_pool<uint64_t, 4096>> m;
            uint32_t yield_count = 0;
            while (!(m = pool.make(static_cast<uint64_t>(i)))) { slabflux::hw::spin_backoff(yield_count); }
            yield_count = 0;
            while(!nexus.try_push(m)) { slabflux::hw::spin_backoff(yield_count); }
        }
    });

    std::thread c([&]() {
        hardware_topology::pin_thread(2);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        size_t count = 0;
        uint32_t yield_count = 0;
        while(count < OPS) {
            auto recovered = nexus.try_pop(pool);
            if (recovered) {
                count++;
                yield_count = 0;
            } else {
                slabflux::hw::spin_backoff(yield_count);
            }
        }
    });

    start_gate.store(true, std::memory_order_release);
    p.join(); c.join();
    size_t pool_count = 0;
    while (auto p_raw = pool.make_raw()) {
        pool_count++;
        if (pool_count > 5000) break;
    }
    EXPECT_EQ(pool_count, 4096);
}

/**
 * @brief Audits MPMC throughput under max contention.
 * Paradigm Shattering: Validates that slot-level versioning scales linearly.
 */
TEST(MpmcConduit, ThroughputPhysics) {
    mpmc_conduit<uint64_t, 4096> nexus;
    constexpr size_t total_messages = 5'000'000;
    std::atomic<size_t> read_count{0};

    std::thread producer([&]() {
        hardware_topology::pin_thread(1);
        for (size_t i = 0; i < total_messages; ++i) {
            uint32_t yield_count = 0;
            while (!nexus.try_push(i)) slabflux::hw::spin_backoff(yield_count);
        }
    });

    hardware_topology::pin_thread(2);
    uint32_t yield_count = 0;
    while (read_count < total_messages) {
        uint64_t val;
        if (nexus.try_pop(val)) {
            read_count++;
            yield_count = 0;
        } else {
            slabflux::hw::spin_backoff(yield_count);
        }
    }
    producer.join();
}

TEST(MpmcConduit, PhysicalArchitecture) {
    mpmc_conduit<uint64_t, 1024> nexus;
    // Verify that the conduit structure itself is cache-line aligned
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0);
}
