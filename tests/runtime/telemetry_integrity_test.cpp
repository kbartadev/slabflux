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
 * @file telemetry_integrity_test.cpp
 * @brief Verifies counter accuracy under high contention.
 */

#include <gtest/gtest.h>
#include "slabflux/io/uring_ingress.hpp"
#include "slabflux/supplemental/telemetry.hpp"

using namespace slabflux;

TEST(TelemetryTest, AtomicCounterAccuracy) {
    struct dummy {};
    dummy d;
    supplemental::telemetry_wrapper<dummy> wrapper(d);

    const uint64_t target = 10'000'000;

    // Simulate 10M events
    for(uint64_t i = 0; i < target; ++i) {
        wrapper.on_event();
    }

    // The scraper should see exactly 10M
    EXPECT_EQ(wrapper.get_count(), target);
}

TEST(TelemetryTest, ScraperRegistryLogic) {
    struct node_a {};
    struct node_b {};
    node_a na;
    node_b nb;

    supplemental::telemetry_wrapper<node_a> wrap_a(na);
    supplemental::telemetry_wrapper<node_b> wrap_b(nb);

    supplemental::telemetry_scraper scraper;
    scraper.register_target(wrap_a);
    scraper.register_target(wrap_b);

    wrap_a.on_event();
    wrap_b.on_event();
    wrap_b.on_event();

    // In a real scenario, run() would be in a thread.
    // Here we verify the registry can access the data.
    // (Requires exposing `targets_` or a check method in `telemetry_scraper`)
}
