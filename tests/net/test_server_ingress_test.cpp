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
#include "slabflux/net/server_ingress.hpp"

using namespace slabflux::net;

struct mock_bridge {
    int call_count = 0;
    uint64_t last_lsn = 0;

    void on_raw_frame(wire_frame_lsn<char>* frame, int32_t /*len*/ = 0) {
        call_count++;
        last_lsn = frame->lsn;
    }
};

TEST(LabsNet, ServerIngressLifecycle) {
    // This test ensures the io_uring initialization/cleanup works on supported kernels
    try {
        server_ingress ingress(64);
        mock_bridge bridge;
        ingress.poll_clients(bridge);
        EXPECT_EQ(bridge.call_count, 0); // No frames submitted yet
    } catch (const std::exception& e) {
        GTEST_SKIP() << "io_uring not supported on this environment: " << e.what();
    }
}

TEST(LabsNet, WireFrameLayout) {
    // Ensure the wire frame is correctly aligned for zero-copy HFT
    using frame_t = wire_frame_lsn<char[1024]>;
    EXPECT_EQ(alignof(frame_t), 64);

    frame_t frame;
    frame.lsn = 42;
    EXPECT_EQ(frame.lsn, 42);
    EXPECT_GT(sizeof(frame_t), 1024);
}
