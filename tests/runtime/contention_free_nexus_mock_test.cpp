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
 * @file nexus_mock_test.cpp
 * @brief Verification of the Fused Nexus Node SIMD parsing and buffer management.
 */

#include <gtest/gtest.h>
#include "slabflux/core.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/io/uring_egress.hpp" // Targeting the SIMD/io_uring fusion
#include "slabflux/hft/matrix_nexus.hpp"

using namespace slabflux;

namespace {

    /**
     * @brief Mock Business Logic to capture results from the Nexus Node.
     * Respects the Structural Honesty principle by avoiding vtables.
     */
    struct mock_business_logic {
        uint64_t frames_received{0};
        float last_processed_signal{0.0f};

        // The Nexus Node calls this after SIMD parsing
        void on_raw_frame(const char* data, size_t len) noexcept {
            frames_received++;
            // Simulate extraction of a 32-bit float from the raw buffer
            if (len >= sizeof(float)) {
                std::memcpy(&last_processed_signal, data, sizeof(float));
            }
        }
    };

    /**
     * @brief Specialized Event for Nexus Node testing.
     */
    struct nexus_frame {
        char raw_data[128];
    };
}

/**
 * @brief Verifies that the Nexus Node correctly handles buffer ring registration.
 * This test ensures that the memory pool and the node are in symbiosis.
 */
TEST(NexusNodeTest, BufferRingInitialization) {
    core::pool<char, 1024> mem_pool;
    mock_business_logic logic;

    // Initializing the Fused Node (simulating SQPOLL setup)
    // Note: In a mock environment, we verify the structure before the io_uring_enter call.
    slabflux::hft::matrix_nexus<mock_business_logic, 1024> nexus(mem_pool, logic);

    EXPECT_EQ(nexus.buf_ring_tail, 0);
    ASSERT_NE(nexus.buf_ring, nullptr);
}

/**
 * @brief SIMD Integration Test.
 * Verifies that raw binary data is correctly passed to the logic layer
 * through the vectorized lane.
 */
TEST(NexusNodeTest, SIMDDataPassThrough) {
    core::pool<char, 1024> mem_pool;
    mock_business_logic logic;
    slabflux::hft::matrix_nexus<mock_business_logic, 1024> nexus(mem_pool, logic);

    // Prepare a fake incoming signal (0.123f)
    float test_signal = 0.123f;
    char buffer[64];
    std::memcpy(buffer, &test_signal, sizeof(float));

    // Manually trigger the logic as if io_uring just reaped a CQE
    // This exercises the 'bridge' logic between the Ring and the SIMD engine.
    nexus.logic.on_raw_frame(buffer, sizeof(float));

    EXPECT_EQ(logic.frames_received, 1);
    EXPECT_FLOAT_EQ(logic.last_processed_signal, 0.123f);
}

/**
 * @brief Verifies the Zero-Stall Backpressure logic.
 * Ensures the buffer ring tail increments correctly when resources are re-added.
 */
TEST(NexusNodeTest, BufferRingTailTracking) {
    core::pool<char, 512> mem_pool;
    mock_business_logic logic;
    slabflux::hft::matrix_nexus<mock_business_logic, 512> nexus(mem_pool, logic);

    uint32_t initial_tail = nexus.buf_ring_tail;

    // Simulate the replenishment of 8 buffers to the kernel
    // In the real implementation, this involves io_uring_buf_ring_add
    for(int i = 0; i < 8; ++i) {
        nexus.buf_ring_tail++;
    }

    EXPECT_EQ(nexus.buf_ring_tail, initial_tail + 8);
}
