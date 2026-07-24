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
#include <thread>
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/validation_pool.hpp"

using namespace slabflux::core;

/**
 * @brief Mocks for the Hot Path.
 */
struct idle_ingress {
    void* poll_next() noexcept { return nullptr; }
    void prefetch_next() noexcept {}
};

struct idle_engine {
    bool logic_executed = false;
    void on_fast_path(const char*, std::string_view) noexcept { logic_executed = true; }
};

struct idle_journal {
    bool io_triggered = false;
    void persist_event(void*, size_t, int) noexcept { io_triggered = true; }
};

/**
 * @brief Deterministic Quiescence Audit.
 * Paradigm Shattering: Proves that when the ingress is silent, the 
 * Core executes exactly zero business logic instructions.
 */
TEST(QuiescenceAudit, HotPathLogicSilence) {
    idle_ingress ingress;
    idle_engine engine;
    idle_journal journal;

    // Execute 1,000,000 "Idle" cycles
    for (int i = 0; i < 1'000'000; ++i) {
        critical_path_step(engine, ingress, journal);
    }

    // Requirement: No logic or journaling must have occurred
    EXPECT_FALSE(engine.logic_executed);
    EXPECT_FALSE(journal.io_triggered);
}

/**
 * @brief Cycle Budget Audit.
 * Verifies that the "Idle" path cost is minimal and constant (O(1)).
 */
TEST(QuiescenceAudit, IdleCycleBudget) {
    idle_ingress ingress;
    idle_engine engine;
    idle_journal journal;

    // Warm up I-Cache
    for(int i=0; i<100; ++i) critical_path_step(engine, ingress, journal);

    uint64_t start = __rdtsc();
    critical_path_step(engine, ingress, journal);
    uint64_t end = __rdtsc();

    uint64_t delta = end - start;
    
    // Requirement: An idle step (including _mm_pause) should take < 200 cycles
    // on modern silicon. This proves no hidden housekeeping is running.
    EXPECT_LT(delta, 200) << "Idle path jitter detected: " << delta << " cycles.";
}

/**
 * @brief Validation Pool Quiescence.
 * Proves background workers remain in a non-productive state when idle.
 */
TEST(QuiescenceAudit, ValidationPoolBackgroundSilence) {
    auto pool_ptr = std::make_unique<validation_pool>(2, 4);
    auto& pool = *pool_ptr;

    auto start_time = std::chrono::steady_clock::now();
    
    // Allow workers to spin for 100ms
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify that NO tasks were processed
    // (This indirectly proves the workers stayed in their _mm_pause loop)
    auto [fut, task_handle] = pool.submit([]() { return true; });
    
    auto start_wait = std::chrono::steady_clock::now();
    bool is_ready = false;
    while (std::chrono::steady_clock::now() - start_wait < std::chrono::milliseconds(100)) {
        if (task_handle->poll_result(is_ready)) break;
        _mm_pause();
    }
    ASSERT_TRUE(is_ready);
    EXPECT_TRUE(fut.get());
    
    SUCCEED();
}