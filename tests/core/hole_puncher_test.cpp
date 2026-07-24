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
 * ============================================================================* @brief SLABFLUX - Hole Puncher Sequence Restoration
 */

#include <gtest/gtest.h>
#include "slabflux/core/hole_puncher.hpp"
#include <vector>

using namespace slabflux::core;

TEST(HolePuncherTest, GapFillingAndFlush) {
    hole_puncher<int, 64> puncher;
    std::vector<int> results;

    // Arrival order: 1, 3, 0, 2
    puncher.insert(1, 101);
    puncher.insert(3, 103);
    
    // Nothing should be ready yet (waiting for 0)
    puncher.flush_ready([&](int val, uint64_t) { results.push_back(val); });
    EXPECT_TRUE(results.empty());

    // Fill the hole at 0
    puncher.insert(0, 100);
    puncher.flush_ready([&](int val, uint64_t) { results.push_back(val); });
    
    // Should flush 0 and 1, but stop at 2 (missing)
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], 100);
    EXPECT_EQ(results[1], 101);

    // Fill remaining
    puncher.insert(2, 102);
    puncher.flush_ready([&](int val, uint64_t) { results.push_back(val); });
    
    ASSERT_EQ(results.size(), 4);
    EXPECT_EQ(results[2], 102);
    EXPECT_EQ(results[3], 103);
}

TEST(HolePuncherTest, RejectOutOfBounds) {
    hole_puncher<int, 16> puncher;

    // 1. Verify window ceiling (Capacity 16, start 0 -> valid range [0, 15])
    EXPECT_TRUE(puncher.insert(5, 50));
    EXPECT_FALSE(puncher.insert(20, 200)); // Too far ahead

    // 2. Advance the committed sequence to move the window floor
    puncher.insert(0, 100);
    puncher.flush_ready([](int, uint64_t) {}); // Consumes LSN 0, next expected is 1

    EXPECT_FALSE(puncher.insert(0, 0));  // Already "passed" (behind floor)
}