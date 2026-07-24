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
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/mpsc_pool.hpp"
#include "slabflux/hw/spin_backoff.hpp"

using namespace slabflux::core;

TEST(PoolIntegrity, MpmcAbaStress) {
    constexpr size_t POOL_CAP = 1024;
    mpmc_pool<size_t, POOL_CAP> pool;
    auto worker = [&]() {
        uint32_t yield_count = 0;
        for (size_t i = 0; i < 1'000'000; ++i) {
            size_t* ptr = pool.make_raw(i);
            if (ptr) { ASSERT_EQ(*ptr, i); pool.release(ptr); yield_count = 0; }
            else slabflux::hw::spin_backoff(yield_count);
        }
    };
    std::vector<std::thread> workers;
    for (size_t i = 0; i < 8; ++i) workers.emplace_back(worker);
    for (auto& w : workers) w.join();
    size_t count = 0;
    while (pool.make_raw()) count++;
    EXPECT_EQ(count, POOL_CAP);
}

TEST(PoolIntegrity, MpmcChainLinkAmortization) {
    mpmc_pool<uint64_t, 1024> pool;
    uint64_t* batch[16];
    for (int i = 0; i < 16; ++i) batch[i] = pool.make_raw(static_cast<uint64_t>(i));
    pool.release_batch(batch, 16);
    size_t count = 0;
    while (pool.make_raw()) count++;
    EXPECT_EQ(count, 1024);
}

TEST(PoolIntegrity, MpscSequenceBarrierRace) {
    mpsc_pool<uint64_t, 4096, reclaim_strategy::manual> pool;
    std::vector<uint64_t*> allocated;
    for(int i = 0; i < 1000; ++i) allocated.push_back(pool.make_raw(static_cast<uint64_t>(i)));
    std::thread t1([&]() { for(int i = 0; i < 500; ++i) pool.release(allocated[i]); });
    std::thread t2([&]() { for(int i = 500; i < 1000; ++i) pool.release(allocated[i]); });
    t1.join(); t2.join();
    pool.reclaim_returns();
    size_t count = 0;
    while (pool.make_raw()) count++;
    EXPECT_EQ(count, 4096);
}

TEST(PoolIntegrity, PhysicalAlignment) {
    mpmc_pool<uint64_t, 64> pool;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(pool.get_raw_ptr()) % 4096, 0);
}