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
#include "slabflux/core/spsc_pool.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux::core;

struct alignas(64) spsc_test_payload {
    uint64_t data[8];
};

#include "slabflux/hw/spin_backoff.hpp"

/**
 * @brief Single-Producer Single-Consumer Physics.
 * Verifies that the pool can serve the 1:1 model at maximum bandwidth, 
 * with zero cache-line collisions and without any atomic loop.
 */
TEST(SpscPool, SingleProducerSingleConsumerPhysics) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping physical residency test.";
    }

    constexpr size_t OPS = 10'000'000;
    spsc_pool<uint64_t, 8192> pool;
    spsc_conduit<uint64_t*, 8192> bridge;

    std::atomic<bool> start_gate{false};
    std::atomic<size_t> consumed{0};

    auto start_time = std::chrono::high_resolution_clock::now();

    // Allocator Thread (Producer)
    std::thread producer([&]() {
        hardware_topology::pin_thread(1);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);

        for (size_t i = 0; i < OPS; ++i) {
            uint64_t* p = nullptr;
            uint32_t yield_count = 0;
            // Wait until a free slot is available (Shadow Pointer Acquisition)
            while (!(p = pool.make_raw())) { slabflux::hw::spin_backoff(yield_count); }
            *p = i;
            // Forward to the Consumer
            yield_count = 0;
            while (!bridge.try_push(p)) { slabflux::hw::spin_backoff(yield_count); }
        }
    });

    // Freer Thread (Consumer)
    hardware_topology::pin_thread(2);
    start_gate.store(true, std::memory_order_release);

    for (size_t i = 0; i < OPS; ++i) {
        uint64_t* p = nullptr;
        uint32_t yield_count = 0;
        while (!bridge.try_pop(p)) { slabflux::hw::spin_backoff(yield_count); }
        if (SL_EXPECT_FALSE(*p != i)) {
            ADD_FAILURE() << "FIFO parity breach at index " << i;
            break;
        }

        // Release from the thread-local storage
        pool.release(p);
        consumed++;
    }

    producer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    double mops = (static_cast<double>(OPS) / (duration_ms / 1000.0)) / 1'000'000.0;
    
    std::cout << "[PERF] SPSC Pool Round-trip: " << mops << " Mops/sec\n";

    EXPECT_EQ(consumed.load(), OPS);
    EXPECT_GT(mops, 15.0); // Requirement: High-speed handoff
}

/**
 * @brief Physical Architecture Integrity.
 * Paradigm Shattering: Validates that the pool structure is cache-line 
 * aligned and isolation constants are respected to prevent False Sharing.
 */
TEST(SpscPool, PhysicalResidencyAudit) {
    spsc_pool<uint64_t, 1024> pool;
    
    // Requirement: Structure must be aligned for the memory controller
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&pool) % 64, 0);
    
    uint64_t* p = pool.make_raw();
    ASSERT_NE(p, nullptr);
    // Requirement: Allocated slots must be cache-line aligned for zero-copy DMA
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 64, 0);
    pool.release(p);
}

/**
 * @brief Vectorized Batch Flow.
 * Verifies the AVX-512-based transactions of make_batch and release_batch.
 */
TEST(SpscPool, VectorizedBatchFlow) {
    spsc_pool<uint64_t, 1024> pool;
    uint64_t* batch[64];

    // Bulk acquisition
    size_t allocated = pool.make_batch(batch, 64);
    EXPECT_EQ(allocated, 64);

    // Bulk release
    pool.release_batch(batch, 64);

    // The SPSC mechanism guarantees immediate visibility
    EXPECT_EQ(pool.make_batch(batch, 64), 64);
    pool.release_batch(batch, 64);
}

/**
 * @brief SIMD Recovery Invalidation Logic.
 * Verifies that a specific pointer can be removed from the free list
 * during journal replay, preventing reuse of corrupted memory blocks.
 */
TEST(SpscPool, InvalidationLogic) {
    spsc_pool<uint64_t, 16> pool;

    // Drain the pool
    uint64_t* ptrs[16];
    pool.make_batch(ptrs, 16);

    // Selected element for invalidation
    uint64_t* target = ptrs[5];

    // Return everything to the pool
    pool.release_batch(ptrs, 16);

    // SIMD scan and nulling inside the free list
    pool.invalidate(target);

    // Re-allocation: the pool must return nullptr for the destroyed slot
    size_t valid_allocs = 0;
    for(int i = 0; i < 16; ++i) {
        if (pool.make_raw() != nullptr) valid_allocs++;
    }

    EXPECT_EQ(valid_allocs, 15);
}

/**
 * @brief Managed Ownership RAII Integration.
 */
TEST(SpscPool, ManagedOwnershipNexus) {
    spsc_pool<uint64_t, 1024> pool;

    {
        auto managed = pool.make(1337ULL);
        ASSERT_TRUE(managed);
        EXPECT_EQ(*managed, 1337ULL); 
        // SCOPE EXIT: Automatically returned to the ring
    }

    // Reclamation verification
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
