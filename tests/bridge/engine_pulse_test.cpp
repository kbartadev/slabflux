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
/* tests/bridge/engine_pulse.cpp */
#include <gtest/gtest.h>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/bridge/engine_pulse.hpp"

// Mock state processing handler that satisfies the fast-path engine layout contracts
struct mock_logic_processor {
    void process(const int& data, uint64_t lsn, float* tracking_positions) noexcept {
        (void)data;
        (void)lsn;
        (void)tracking_positions;
    }
};

TEST(EnginePulseTest, PulseInjectionMonotonicity) {
    using namespace slabflux::bridge;

    spsc_data_bridge<int, 128> data_bridge;
    mock_logic_processor business_logic;

    // Fix: Instantiate a persistent clock context within the test framework block scope
    pulse_execution_context live_clock_ctx;

    float output_buffer[256]{0.0f};
    uint64_t last_lsn = 0;
    uint64_t current_lsn = 0;

    // 1. Capture absolute baseline tracking fields before iteration pass (Expect 0)
    data_bridge.try_read_wide(output_buffer, last_lsn);

    // 2. Transmit entry frame across the lock-free conduit ring channel
    bool send_ok = data_bridge.send(1337);
    ASSERT_TRUE(send_ok);

    // 3. Force execution step by passing BOTH parameters to prevent local stack stubs from hijacking the LSN
    data_bridge.consume(business_logic, live_clock_ctx);

    // 4. Extract updated runtime structural tracking metrics
    bool read_ok = data_bridge.try_read_wide(output_buffer, current_lsn);
    ASSERT_TRUE(read_ok);

    // 5. Hard physical validation assertions: tracking index MUST advance monotonically
    EXPECT_GT(current_lsn, last_lsn);
    EXPECT_EQ(current_lsn, 1u);
}
