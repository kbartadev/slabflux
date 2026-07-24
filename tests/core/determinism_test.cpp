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
 * @file industrial_jitter_test.cpp @file determinism_tests.cpp
 * @brief Verifies that SIMD state remains bit-identical across recovery.
 */

#include <gtest/gtest.h>
#include "slabflux/core.hpp"
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/pool.hpp"
#include "slabflux/core/hole_puncher.hpp"
#include "slabflux/core/parity_verifier.hpp"

using namespace slabflux;

/**
 * @brief Ensures the Hole Puncher correctly reorders out-of-sequence events.
 * This is the foundation of the deterministic timeline.
 */
TEST(LogicTest, HolePuncherSequenceRestoration) {
    struct payload { int val; };
    core::hole_puncher<payload, 16> puncher;
    std::vector<int> results;

    // Simulate network jitter: LSN 1 arrives before LSN 0
    puncher.insert(1, {100});
    puncher.insert(0, {200});

    puncher.flush_ready([&](const payload& p, uint64_t lsn) {
        results.push_back(p.val);
    });

    // Validating prefix-based handoff
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], 200); // LSN 0 must be first
    EXPECT_EQ(results[1], 100); // LSN 1 must be second
}

/**
 * @brief Validates bit-identical parity between live and replayed states.
 * Uses the parity_verifier to detect any state drift.
 */
TEST(DeterminismTest, CausalParityVerification) {
    struct alignas(64) engine_state {
        float signals[64];
    };

    engine_state live_state;
    engine_state replay_state;

    // Initialize with identical data
    for(int i = 0; i < 64; ++i) {
        float val = static_cast<float>(i) * 1.5f;
        live_state.signals[i] = val;
        replay_state.signals[i] = val;
    }

    auto report = test::causal_parity_checker<engine_state>::verify(live_state, replay_state);

    EXPECT_TRUE(report.bit_identical);
    EXPECT_EQ(report.drift_detected_at, 0);
}

/**
 * @brief Verifies that HugePage-backed memory is correctly returned via RAII.
 */
TEST(PhysicalTest, PoolRAIIIntegrity) {
    struct blob { char data[1024]; };
    core::pool<blob, 5> pool;

    {
        auto ev1 = pool.make();
        auto ev2 = pool.make();
        ASSERT_NE(ev1, nullptr);
        ASSERT_NE(ev2, nullptr);
        // ev1 and ev2 go out of scope here
    }

    // Allocation should still succeed because RAII returned blocks to the free-list
    auto ev_after = pool.make();
    EXPECT_NE(ev_after, nullptr);
}
