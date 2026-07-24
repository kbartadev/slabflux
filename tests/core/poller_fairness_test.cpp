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
 * ============================================================================* @file poller_fairness_test.cpp
 * @brief Verification of starvation-free round-robin polling.
 */

#include <gtest/gtest.h>
#include "slabflux/core.hpp"

using namespace slabflux;

TEST(PollerTest, StarvationResistance) {
    pool<int, 128> mem_pool;
    conduit<int*, 64> track_a;
    conduit<int*, 64> track_b;

    round_robin_poller<int, 2> poller;
    poller.bind_track(0, track_a);
    poller.bind_track(1, track_b);

    // Track A is "chatty" (2 events), Track B has 1 event
    track_a.push(mem_pool.make(100).release());
    track_a.push(mem_pool.make(101).release());
    track_b.push(mem_pool.make(200).release());

    // First poll should take from Track A (start index 0)
    auto ev1 = poller.poll();
    ASSERT_TRUE(ev1);
    EXPECT_EQ(*ev1, 100);

    // Second poll MUST take from Track B, even though Track A still has data
    auto ev2 = poller.poll();
    ASSERT_TRUE(ev2);
    EXPECT_EQ(*ev2, 200);

    // Third poll comes back to Track A
    auto ev3 = poller.poll();
    ASSERT_TRUE(ev3);
    EXPECT_EQ(*ev3, 101);

    // Cleanup
    mem_pool.release(ev1);
    mem_pool.release(ev2);
    mem_pool.release(ev3);
}
