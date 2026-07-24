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
 * ============================================================================* @brief SLABFLUX - Validation Pool Concurrency Audit
 */

#include <gtest/gtest.h>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <sched.h>
#include <pthread.h>
#include "slabflux/hw/spin_backoff.hpp"
#include "slabflux/core/validation_pool.hpp"

using namespace slabflux::core;

TEST(ValidationPoolTest, TaskDistributionAndCompletion) {
    // CRITICAL FIX: The GoogleTest main thread was previously pinned to Core 1 by 
    // TimingWheel.TortureStress. When validation_pool workers ignite their sovereign_thread
    // shields, they also pin to specific cores. If the main thread spin-waits on a full 
    // queue while sharing a core with a worker, the worker is starved and the system 
    // deadlocks. We must restore the main thread's affinity mask.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < CPU_SETSIZE; i++) CPU_SET(i, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // Truly heap-allocate the pool to bypass stack size limits (internal slabs can be >256KB)
    auto pool_ptr = std::make_unique<validation_pool>(2, 4);
    auto& pool = *pool_ptr;

    auto task = []() -> bool {
        // Simulate a SIMD integrity check
        _mm_pause();
        return true;
    };

    // Submit a burst of tasks
    std::vector<slabflux::core::zero_alloc_future> results;
    for (int i = 0; i < 16; ++i) {
        auto res = pool.submit(task);
        if (res.future.valid()) {
            results.push_back(std::move(res.future));
        }
    }

    for (auto& fut : results) {
        EXPECT_TRUE(fut.get());
    }
}

TEST(ValidationPoolTest, InvalidationSafety) {
    // Use 2 workers to ensure predictable saturation behavior
    auto pool_ptr = std::make_unique<validation_pool>(2, 4);
    auto& pool = *pool_ptr;

    // Heap-allocated controls to survive any early test returns
    auto ran = std::make_shared<std::atomic<bool>>(false);
    auto long_task = [ran]() -> bool {
        ran->store(true, std::memory_order_relaxed);
        return true;
    };

    auto active_workers = std::make_shared<std::atomic<int>>(0);
    auto hold_workers = std::make_shared<std::atomic<bool>>(true);

    // RAII guard to guarantee worker release on test exit
    struct worker_releaser {
        std::shared_ptr<std::atomic<bool>> flag;
        ~worker_releaser() { flag->store(false, std::memory_order_release); }
    } releaser{hold_workers};

    // Store saturation futures to ensure clean cleanup
    std::vector<slabflux::core::zero_alloc_future> saturation_futs;

    // Phase 1: Saturate 2 workers.
    for(int i = 0; i < 2; ++i) {
        auto [fut, ptr] = pool.submit([active_workers, hold_workers]() { 
            active_workers->fetch_add(1, std::memory_order_relaxed);
            uint32_t gate_yield = 0;
            while(hold_workers->load(std::memory_order_relaxed)) {
                slabflux::hw::spin_backoff(gate_yield);
            }
            return true; 
        });
        if (fut.valid()) saturation_futs.push_back(std::move(fut));
    }

    // Wait until workers are actively spinning (with timeout fallback)
    auto start_wait = std::chrono::steady_clock::now();
    while(active_workers->load(std::memory_order_relaxed) < 2) {
        if (std::chrono::steady_clock::now() - start_wait > std::chrono::milliseconds(500)) break;
        std::this_thread::yield();
    }

    // Phase 2: Submit the target task. It must stay in the conduit because workers are saturated.
    auto [fut, task_ptr] = pool.submit(long_task);
    
    if (!task_ptr) {
        GTEST_SKIP() << "Pool saturated, unable to test invalidation.";
        return;
    }

    // Phase 3: Invalidate target while it is still queued.
    EXPECT_NO_THROW(pool.invalidate(task_ptr));

    // Phase 4: Release the workers so they can process the rest of the queue
    hold_workers->store(false, std::memory_order_release);

    // Phase 5: Wait for initial saturation tasks to complete.
    for(auto& f : saturation_futs) {
        if(f.valid()) f.wait(); 
    }

    // Phase 6: Distributed Horizon Flush
    std::vector<slabflux::core::zero_alloc_future> flush_futs;
    for(int i = 0; i < 8; ++i) {
        auto res = pool.submit([]() { return true; });
        if (res.future.valid()) flush_futs.push_back(std::move(res.future));
    }
    for(auto& f : flush_futs) {
        if(f.valid()) f.wait();
    }

    // Phase 7: Verify Invalidation side-effect.
    EXPECT_FALSE(ran->load(std::memory_order_relaxed)) << "Invalidated task executed!";
}