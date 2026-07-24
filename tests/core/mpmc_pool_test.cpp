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
#include <set>
#include <numa.h>
#include <numaif.h>
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/core/thread_context.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/hw/intrinsics.hpp"
#include "slabflux/hw/spin_backoff.hpp"

using namespace slabflux::core;

struct alignas(64) test_payload {
    uint64_t id;
    uint64_t magic;
};

/**
 * @brief Sharded Allocation Physics.
 * Verifies that the pool can serve parallel allocation requests
 * from different hardware threads without any observable global lock
 * (or lack thereof).
 */
TEST(MpmcPool, ShardedThroughputPhysics) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping physical throughput audit.";
    }

    constexpr size_t THREADS = 8;
    constexpr size_t OPS_PER_THREAD = 1'000'000;
    constexpr size_t TOTAL_OPS = THREADS * OPS_PER_THREAD;

    mpmc_pool<test_payload, TOTAL_OPS, THREADS> pool;
    std::atomic<size_t> success_count{0};
    std::atomic<bool> start_gate{false};
    std::atomic<uint32_t> ready{0};

    auto worker = [&](uint32_t id) {
        hardware_topology::pin_thread(id);
        slabflux::core::thread_context::worker_id = id;
        
        ready.fetch_add(1, std::memory_order_release);
        while(!start_gate.load(std::memory_order_acquire)) { _mm_pause(); }

        std::vector<test_payload*> allocated;
        allocated.reserve(OPS_PER_THREAD);

        for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
            if (auto* ptr = pool.make_raw(i, 0xACEULL)) {
                allocated.push_back(ptr);
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        for (auto* ptr : allocated) {
            pool.release(ptr);
        }
    };

    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < THREADS; ++i) threads.emplace_back(worker, i);

    while (ready.load(std::memory_order_acquire) < THREADS) { _mm_pause(); }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t start_cycles = __rdtsc();
    start_gate.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    uint64_t end_cycles = __rdtsc();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    double mops = (static_cast<double>(TOTAL_OPS) / (duration_ms / 1000.0)) / 1'000'000.0;
    double cycles_per_op = static_cast<double>(end_cycles - start_cycles) / TOTAL_OPS;

    std::cout << "[PERF] MPMC Pool Contended Throughput: " << mops << " Mops/sec\n";
    std::cout << "[PERF] MPMC Pool Latency: " << cycles_per_op << " cycles/alloc\n";

    EXPECT_EQ(success_count.load(), TOTAL_OPS);
    // Requirement: Must maintain high throughput even under RFO storms
    EXPECT_GT(mops, 5.0); 
}

/**
 * @brief Physical Architecture Integrity.
 * Paradigm Shattering: Validates that the sharded pool structure and 
 * internal meta are aligned to prevent MESI protocol thrashing.
 */
TEST(MpmcPool, PhysicalResidencyAudit) {
    // Sharded pool: Metadata is distributed to prevent false sharing
    mpmc_pool<uint64_t, 1024, 4> pool;
    
    // Requirement 1: Structure must be cache-line aligned for the interconnect
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&pool) % 64, 0);
    
    // Requirement 2: Base memory address must be 2MB (HugePage) aligned
    if (slabflux::os::has_hugepage_support()) {
        EXPECT_EQ(reinterpret_cast<uintptr_t>(pool.get_raw_ptr()) % (2 * 1024 * 1024), 0);
    }

    uint64_t* p = pool.make_raw();
    ASSERT_NE(p, nullptr);
    
    // Requirement 3: Allocated pointers must be 64-byte aligned for zero-copy DMA
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 64, 0);
    
    pool.release(p);
}

/**
 * @brief NUMA Locality Audit.
 * Verifies that the pool's hugepage-backed slab is physically resident 
 * on the same NUMA node as the ignition thread, minimizing L3/interconnect lag.
 */
TEST(MpmcPool, NumaLocalityAudit) {
    if (!slabflux::os::has_hugepage_support() || numa_available() < 0) {
        GTEST_SKIP() << "NUMA or HugePages not available.";
    }

    mpmc_pool<uint64_t, 1024, 4> pool;
    void* ptr = pool.get_raw_ptr();

    int status[1];
    void* pages[1] = { ptr };
    int nodes[1] = { -1 };

    // Requirement: move_pages with null nodes array returns the current location
    if (move_pages(0, 1, pages, nullptr, nodes, MPOL_MF_MOVE) == 0) {
        int current_cpu = hardware_topology::get_current_cpu();
        int expected_node = numa_node_of_cpu(current_cpu);
        EXPECT_EQ(nodes[0], expected_node) << "Physical residency mismatch! Slab allocated on remote NUMA node.";
    }
}

/**
 * @brief Lane Contention Resilience.
 * Proves that sharding effectively reduces CAS contention by comparing 
 * distributed lanes vs forced lane collisions.
 */
TEST(MpmcPool, LaneContentionResilience) {
    constexpr size_t TOTAL_OPS = 1'000'000;
    constexpr size_t THREADS = 2; // Reduced to 2 to prevent CI vCPU preemption noise
    
    // Case A: distributed (Natural sharding)
    mpmc_pool<uint64_t, TOTAL_OPS, THREADS> distributed_pool;
    std::atomic<bool> start_gate_A{false};
    std::atomic<uint32_t> ready_A{0};
    std::atomic<uint32_t> finished_A{0};
    
    std::vector<std::thread> threads_A;
    for(int i=0; i<THREADS; ++i) {
        threads_A.emplace_back([&, i]() {
            hardware_topology::pin_thread(i);
            slabflux::core::thread_context::worker_id = i;
            
            ready_A.fetch_add(1, std::memory_order_release);
            while(!start_gate_A.load(std::memory_order_acquire)) { _mm_pause(); }
            
            for(size_t j=0; j<TOTAL_OPS/THREADS; ++j) {
                auto* p = distributed_pool.make_raw(j);
                if (p) distributed_pool.release(p);
            }
            finished_A.fetch_add(1, std::memory_order_release);
        });
    }
    
    while (ready_A.load(std::memory_order_acquire) < THREADS) { _mm_pause(); }
    start_gate_A.store(true, std::memory_order_release);
    uint64_t start_A = __rdtsc();
    while (finished_A.load(std::memory_order_acquire) < THREADS) { _mm_pause(); }
    uint64_t distributed_cycles = __rdtsc() - start_A;
    for(auto& t : threads_A) t.join();

    // Case B: Forced collision (Single Lane)
    mpmc_pool<uint64_t, TOTAL_OPS, 1> collision_pool;
    std::atomic<bool> start_gate_B{false};
    std::atomic<uint32_t> ready_B{0};
    std::atomic<uint32_t> finished_B{0};
    
    std::vector<std::thread> threads_B;
    for(int i=0; i<THREADS; ++i) {
        threads_B.emplace_back([&, i]() {
            hardware_topology::pin_thread(i);
            slabflux::core::thread_context::worker_id = i;
            
            ready_B.fetch_add(1, std::memory_order_release);
            while(!start_gate_B.load(std::memory_order_acquire)) { _mm_pause(); }
            
            for(size_t j=0; j<TOTAL_OPS/THREADS; ++j) {
                auto* p = collision_pool.make_raw(j);
                if (p) collision_pool.release(p);
            }
            finished_B.fetch_add(1, std::memory_order_release);
        });
    }
    
    while (ready_B.load(std::memory_order_acquire) < THREADS) { _mm_pause(); }
    start_gate_B.store(true, std::memory_order_release);
    uint64_t start_B = __rdtsc();
    while (finished_B.load(std::memory_order_acquire) < THREADS) { _mm_pause(); }
    uint64_t collision_cycles = __rdtsc() - start_B;
    for(auto& t : threads_B) t.join();

    std::cout << "[PHYSICS] Sharded cycles: " << distributed_cycles << ", Contended cycles: " << collision_cycles << "\n";
    // Sharding should be significantly faster under 4-core contention
    EXPECT_LT(distributed_cycles, collision_cycles);
}

/**
 * @brief Shard Exhaustion and Stealing Logic.
 * Verifies that when a thread exhausts its own lane, the pool can
 * transparently steal free slots from neighboring shards.
 */
TEST(MpmcPool, ShardStealingLogic) {
    // 2 lanes, total of 4 elements (2 per lane)
    mpmc_pool<size_t, 4, 2> pool;

    // 1. Allocate all elements from a single thread
    // This forces the stealing logic, since the thread only has 2 local slots.
    size_t* p1 = pool.make_raw(101); // Lane 0
    size_t* p2 = pool.make_raw(102); // Lane 0
    size_t* p3 = pool.make_raw(103); // Lane 1 (Stealing!)
    size_t* p4 = pool.make_raw(104); // Lane 1 (Stealing!)

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);
    ASSERT_NE(p4, nullptr);

    // The pool must now be completely empty
    EXPECT_EQ(pool.make_raw(105), nullptr);

    pool.release(p1); pool.release(p2); pool.release(p3); pool.release(p4);
}

/**
 * @brief Batch Flow Efficiency.
 * Tests the atomic transactions of make_batch and release_batch.
 */
TEST(MpmcPool, VectorizedBatchFlow) {
    mpmc_pool<uint64_t, 1024, 4> pool;
    uint64_t* batch[64];

    // Bulk acquisition
    size_t allocated = pool.make_batch(batch, 64);
    EXPECT_EQ(allocated, 64);

    for(size_t i = 0; i < 64; ++i) {
        *batch[i] = 0xDEADBEEF0000 + i;
    }

    // Bulk release
    pool.release_batch(batch, 64);

    // Verify we can re-acquire them
    size_t second_alloc = pool.make_batch(batch, 64);
    EXPECT_EQ(second_alloc, 64);
    pool.release_batch(batch, 64);
}

/**
 * @brief ABA Protection Stress Audit.
 * Tests whether the 64-bit versioned head prevents freelist corruption
 * under extremely fast allocation–release cycles.
 */
TEST(MpmcPool, AbaProtectionAudit) {
    // Very small pool to maximize collisions and reuse
    mpmc_pool<uint64_t, 2, 1> tiny_pool;

    auto worker = [&]() {
        for (size_t i = 0; i < 500'000; ++i) {
            auto* p = tiny_pool.make_raw(i);
            if (p) {
                tiny_pool.release(p);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    // If ABA protection is broken, the pool either deadlocks here
    // or returns nullptr while not actually empty.
    auto* p1 = tiny_pool.make_raw(1);
    auto* p2 = tiny_pool.make_raw(2);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(tiny_pool.make_raw(3), nullptr);
}

/**
 * @brief Managed Data Integration.
 * Verifies RAII-based managed_data integration.
 */
TEST(MpmcPool, ManagedOwnershipNexus) {
    mpmc_pool<uint64_t, 1024, 4> pool;

    {
        auto managed = pool.make(42ULL);
        ASSERT_TRUE(managed);
        EXPECT_EQ(*managed, 42ULL);
        // Automatically invokes deleter_fn at scope exit
    }

    // Verify reclamation
    uint64_t* raw[1024];
    size_t acquired = 0;
    while (acquired < 1024) {
        size_t n = pool.make_batch(raw + acquired, 1024 - acquired);
        if (n == 0) break;
        acquired += n;
    }
    EXPECT_EQ(acquired, 1024);
    pool.release_batch(raw, 1024);
}
