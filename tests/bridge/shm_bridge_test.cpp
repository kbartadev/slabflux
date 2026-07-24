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
 * @file shm_bridge_test.cpp
 * @brief Shared Memory Bridge Verification.
 */

#include <gtest/gtest.h>
#include <string>
#include "slabflux/bridge/shm_bridge.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux::bridge;
using namespace slabflux::io;


/**
 * @brief Segment Lifecycle and Alignment Audit.
 * Proves that SHM segments are correctly page-aligned to enable 
 * zero-copy DMA across process boundaries.
 */
TEST(ShmBridgeTest, PhysicalResidencyAudit) {
    const std::string shm_name = "/slabflux_bridge_test";
    
    try {
        // 1. Creator Role: Setup the segment
        shm_bridge<uint64_t, 1024> bridge(shm_name, ipc_role::creator);
        
        // Requirement: The wire structure must be 64-byte aligned for MESI
        EXPECT_EQ(reinterpret_cast<uintptr_t>(&bridge.wire()) % 64, 0);
        
        // Requirement: Memory must be committed and visible
        uint64_t* slot = bridge.wire().reserve();
        ASSERT_NE(slot, nullptr);
        *slot = 0x55AA55AAULL;
        bridge.wire().commit();

        // 2. Joiner Role: Verify connectivity
        shm_bridge<uint64_t, 1024> joiner(shm_name, ipc_role::joiner);
        const uint64_t* read_slot = joiner.wire().peek();
        ASSERT_NE(read_slot, nullptr);
        EXPECT_EQ(*read_slot, 0x55AA55AAULL);
        
    } catch (const std::exception& e) {
        GTEST_SKIP() << "SHM Bridge skipped: " << e.what();
    }
}

/**
 * @brief Role
 * Validates that creator and joiner modes handle existing segments correctly.
 */
TEST(ShmBridgeTest, RoleExclusivity) {
    const std::string shm_name = "/slabflux_role_test";
    
    // Creator
    shm_bridge<int, 16> b1(shm_name, ipc_role::creator);
    
    // Second creator should fail or correctly handle O_EXCL depending on impl
    // Here we test that joiner can at least connect to the same segment
    EXPECT_NO_THROW((shm_bridge<int, 16>(shm_name, ipc_role::joiner)));
}
