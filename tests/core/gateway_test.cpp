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
 * ============================================================================* @brief SLABFLUX - Gateway Ingress/Egress Audit
 */

#include <gtest/gtest.h>
#include "slabflux/core/event_gateway.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/spsc_conduit.hpp"

using namespace slabflux::core;

struct gateway_test_event {
    uint64_t payload;
};

TEST(GatewayTest, VectorizedDrainAndOwnership) {
    mpmc_pool<gateway_test_event, 1024> pool;
    spsc_conduit<gateway_test_event*, 1024> bus;
    event_gateway<mpmc_pool<gateway_test_event, 1024>, spsc_conduit<gateway_test_event*, 1024>> gateway(pool, bus);

    // 1. Publish events
    gateway.publish<gateway_test_event>(100ULL);
    gateway.publish<gateway_test_event>(200ULL);
    EXPECT_EQ(bus.occupancy(), 2);

    // 2. Vectorized Drain
    size_t processed = 0;
    gateway.drain<gateway_test_event, 16>([&](event_ref<gateway_test_event> ev) {
        processed++;
        EXPECT_TRUE(ev->payload == 100 || ev->payload == 200);
    });

    EXPECT_EQ(processed, 2);
    EXPECT_EQ(bus.occupancy(), 0);
}

TEST(GatewayTest, ManualConsumeHandoff) {
    mpmc_pool<gateway_test_event, 1024> pool;
    event_gateway<mpmc_pool<gateway_test_event, 1024>> gateway(pool);

    auto* raw = pool.make_raw(555ULL);
    bool lambda_called = false;

    gateway.consume<gateway_test_event>(raw, [&](event_ref<gateway_test_event> ref) {
        EXPECT_EQ(ref->payload, 555ULL);
        lambda_called = true;
    });

    EXPECT_TRUE(lambda_called);
    // If release didn't happen, subsequent make calls on a tiny pool would fail.
}