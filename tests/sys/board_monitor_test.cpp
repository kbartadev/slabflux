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
 * @brief SLABFLUX - Board Monitor Health Audit
 */

#include <gtest/gtest.h>
#include "slabflux/sys/board_monitor.hpp"

using namespace slabflux::sys;

TEST(BoardMonitorTest, LagDetectionAndRebirth) {
    board_monitor monitor;
    uint32_t node_id = 5;
    
    // 1. Report initial state
    monitor.report_lsn(node_id, 1000);
    EXPECT_EQ(monitor.total_rebirths(), 0);

    // 2. Evaluate within threshold (lag = 500)
    monitor.evaluate_health(node_id, 1500);
    EXPECT_EQ(monitor.total_rebirths(), 0);

    // 3. Trigger Rebirth (lag = 1001)
    monitor.evaluate_health(node_id, 2001);
    EXPECT_EQ(monitor.total_rebirths(), 1);
}

TEST(BoardMonitorTest, OutOfBoundsSafety) {
    board_monitor monitor;
    // MAX_NODES is 256
    EXPECT_NO_THROW(monitor.report_lsn(999, 1000));
    EXPECT_NO_THROW(monitor.evaluate_health(999, 2000));
    
    // Telemetry for invalid node should not affect global rebirth count
    EXPECT_EQ(monitor.total_rebirths(), 0);
}
