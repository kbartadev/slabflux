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
 * @file       poller_edge_cases.cpp
 * @brief      Tests for edge cases in the round-robin poller.
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <gmock/gmock.h>

#include "slabflux/core.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/conduit.hpp"
#include "slabflux/bridge/round_robin_poller.hpp"

using namespace slabflux;

struct load_event {
    int source_track;
    load_event(int s) : source_track(s) {}
    load_event() = default;
};

// ============================================================================
// TEST — Round‑robin poller must prevent starvation even under asymmetric load.
// The poller MUST alternate between tracks regardless of queue depth.
// This is a fairness invariant: no track may monopolize the poller.
// ============================================================================
TEST(PollerEdgeCases, poller_prevents_starvation_under_asymmetric_load) {
    slabflux::core::pool<load_event, 100> p;
    slabflux::core::conduit<load_event*, 10> track_1;
    slabflux::core::conduit<load_event*, 10> track_2;

    slabflux::bridge::round_robin_poller<load_event, 2> poller;
    poller.bind_track(0, track_1);
    poller.bind_track(1, track_2);

    // Asymmetric load:
    // Track 1 contains 3 events, Track 2 contains only 1.
    // NOTE: In SLABFLUX, when pushing to a raw-pointer conduit, you MUST release() from managed_data!
    track_1.push(p.make(1).release());
    track_1.push(p.make(1).release());
    track_1.push(p.make(1).release());

    track_2.push(p.make(2).release());

    // EXPECTATION:
    // The poller MUST alternate between tracks:
    //   1st poll → Track 1
    //   2nd poll → Track 2 (even though Track 1 still has more)
    //   3rd poll → Track 1 again
    //
    // This proves starvation‑free fairness under uneven load.

    auto ev1 = poller.poll();
    ASSERT_TRUE(ev1) << "CRITICAL: A poller.poll() üres (null) pointert adott vissza!";
    EXPECT_EQ(ev1->source_track, 1);

    auto ev2 = poller.poll();
    ASSERT_TRUE(ev2);
    EXPECT_EQ(ev2->source_track, 2);

    auto ev3 = poller.poll();
    ASSERT_TRUE(ev3);
    EXPECT_EQ(ev3->source_track, 1);
}
