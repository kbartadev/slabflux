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
 * ============================================================================*/
#include <gtest/gtest.h>
#include "slabflux/core.hpp"
#include "slabflux/bridge/bridge_sync.hpp"
#include "slabflux/core/pool.hpp"

namespace {
    struct eb_engine_input {
        uint32_t action_mask;
        float mouse_dx;
        float mouse_dy;
    };

    // Mock business logic that the bridge's consume() method will invoke
    struct eb_mock_engine_logic {
        bool called = false;
        eb_engine_input last_input{};
        uint64_t last_lsn = 0;

        void process(const eb_engine_input& in, uint64_t lsn, const float* positions) noexcept {
            called = true;
            last_input = in;
            last_lsn = lsn;
        }
    };

    // Mock context for reserving sequence numbers (LSN)
    struct eb_mock_context {
        uint64_t next_lsn = 100;
        uint64_t reserve_next() noexcept { return next_lsn++; }
    };
}

TEST(SlabfluxBridgeTest, SpscEventBridgeTransmissionTest) {
    using namespace slabflux;
    using namespace slabflux::bridge;

    // 1. Prepare the pool for event creation
    slabflux::core::pool<eb_engine_input, 16> event_pool;

    // 2. Instantiate the SPSC Event Bridge
    spsc_event_bridge<eb_engine_input, 1024> bridge(event_pool);

    // 3. Create an event from the pool
    auto ev = event_pool.make(0xFF, 12, -8); // action_mask, mouse_dx, mouse_dy
    ASSERT_TRUE(ev);

    // 4. Send it through the bridge (Ownership transfer)
    bridge.send(ev);

    // Guarantee: ownership on the sender side is gone (null)
    EXPECT_FALSE(ev);

    // 5. Consumption on the processing (Engine) side
    eb_mock_engine_logic logic;
    eb_mock_context context;

    bridge.consume(logic, context);

    // 6. VERIFICATION: Data arrived intact on the far side of the bridge
    EXPECT_TRUE(logic.called);
    EXPECT_EQ(logic.last_lsn, 100);
    EXPECT_EQ(logic.last_input.action_mask, 0xFF);
    EXPECT_EQ(logic.last_input.mouse_dx, 12);
    EXPECT_EQ(logic.last_input.mouse_dy, -8);
}

TEST(SlabfluxBridgeTest, MpmcEventBridgeTransmissionTest) {
    using namespace slabflux;
    using namespace slabflux::bridge;

    slabflux::core::pool<eb_engine_input, 16> event_pool;
    mpmc_event_bridge<eb_engine_input, 1024> bridge(event_pool);

    auto ev = event_pool.make(0xAA, 44, 55);
    ASSERT_TRUE(ev);

    bridge.send(ev);
    EXPECT_FALSE(ev);

    eb_mock_engine_logic logic;
    eb_mock_context context;

    bridge.consume(logic, context);

    EXPECT_TRUE(logic.called);
    EXPECT_EQ(logic.last_lsn, 100);
    EXPECT_EQ(logic.last_input.action_mask, 0xAA);
}
