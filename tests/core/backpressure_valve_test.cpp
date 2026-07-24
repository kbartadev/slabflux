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
 */

#include <gtest/gtest.h>
#include "slabflux/core/backpressure_valve.hpp"

using namespace slabflux::core;

/**
 * @brief Verifies standard threshold boundaries.
 */
TEST(BackpressureValve, ThresholdBoundaries) {
    const uint64_t threshold = 1000;
    backpressure_valve valve(threshold);

    // Initial state
    EXPECT_FALSE(valve.is_stalled());

    // 1. Below threshold (Lag 999)
    valve.update(1000, 1);
    EXPECT_FALSE(valve.is_stalled());

    // 2. Exactly at threshold (Lag 1000)
    valve.update(1000, 0);
    EXPECT_TRUE(valve.is_stalled());

    // 3. Above threshold (Lag 1001)
    valve.update(2001, 1000);
    EXPECT_TRUE(valve.is_stalled());

    // 4. Recovery (Lag 500)
    valve.update(2001, 1501);
    EXPECT_FALSE(valve.is_stalled());
}

/**
 * @brief Verifies that the valve handles uint64_t wrap-around correctly.
 * LSNs are monotonic but eventually wrap. The distance-based logic 
 * (current - committed) must remain valid.
 */
TEST(BackpressureValve, SerialNumberArithmeticWrap) {
    backpressure_valve valve(100);

    // Start near the end of uint64_t range
    uint64_t committed = 0xFFFFFFFFFFFFFFF0; 
    uint64_t current   = 0xFFFFFFFFFFFFFFFA; // Lag 10

    valve.update(current, committed);
    EXPECT_FALSE(valve.is_stalled());

    // Current wraps around to 5
    // (5 - 0xFFFFFFFFFFFFFFF0) should result in 21
    current = 5;
    valve.update(current, committed);
    EXPECT_FALSE(valve.is_stalled());

    // Advance current further to trigger stall across the wrap boundary
    current = 95; // Lag is now 105 (relative to 0xFFFFFFFFFFFFFFF0)
    valve.update(current, committed);
    EXPECT_TRUE(valve.is_stalled());

    // Move committed past the wrap point to recover
    committed = 10; // Lag is now 85
    valve.update(current, committed);
    EXPECT_FALSE(valve.is_stalled());
}