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
#include <chrono>
#include "slabflux/core/spsc_ring_pool.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux::core;

TEST(SpscRingPoolTest, ZeroContentionLifecycle) {
    // Requirement: Logic must be verifiable even without hardware-backed residency.
    // Fallback in mpsc_hybrid_pool.hpp now enables this to run on standard RAM.
    spsc_ring_pool<uint64_t, 16> pool;
    
    // Allocation on Core A
    uint64_t* p1 = pool.make_raw(123ULL);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(*p1, 123ULL);
    
    // Reclamation on Core B (Simulated here in same thread)
    pool.release(p1);
    
    // Visibility verification: spsc_ring_conduit internals must refresh.
    // Note: spsc_ring_pool utilizes a FIFO spsc_ring_conduit. With a 16-slot pool, 
    // the next allocation returns the next available physical slot (slot 1), 
    // while the released slot (slot 0) moves to the back of the queue.
    uint64_t* p2 = pool.make_raw(456ULL);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p2), reinterpret_cast<uintptr_t>(p1) + 64);
}

/**
 * @brief Local Throughput Performance.
 * Measures the O(1) allocation/release velocity on a single core.
 */
TEST(SpscRingPoolTest, ThroughputPerformance) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured.";
    }

    constexpr size_t OPS = 10'000'000;
    spsc_ring_pool<uint64_t, 8192> pool;
    
    auto start = std::chrono::high_resolution_clock::now();
    for(size_t i=0; i<OPS; ++i) {
        uint64_t* p = pool.make_raw(i);
        pool.release(p);
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double mops = (static_cast<double>(OPS) / (ms / 1000.0)) / 1'000'000.0;
    std::cout << "[PERF] Ring Pool Local Throughput: " << mops << " Mops/sec\n";
    EXPECT_GT(mops, 40.0); // Logic: >40M allocs/sec on industrial silicon
}

/**
 * @brief Physical Architecture Integrity.
 */
TEST(SpscRingPoolTest, PhysicalResidencyAudit) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping physical residency audit.";
    }

    spsc_ring_pool<uint64_t, 1024> pool;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&pool) % 64, 0);
    
    uint64_t* p = pool.make_raw();
    ASSERT_NE(p, nullptr);
    // Requirement: Payload pointers must be 64-byte aligned for zero-copy DMA
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 64, 0);
    pool.release(p);
}

TEST(SpscRingPoolTest, ManagedHandleOwnership) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping managed ownership logic audit.";
    }

    spsc_ring_pool<int, 8> pool;
    
    {
        auto managed = pool.make(777);
        ASSERT_TRUE(managed);
        EXPECT_EQ(*managed, 777);
    } // RAII return
    
    EXPECT_NE(pool.make_raw(), nullptr);
}
