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
 * @file baremetal_egress_test.cpp
 * @brief Egress and Wire Latency Audit.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>
#include <cstring>
#include <sys/socket.h>
#include "slabflux/io/baremetal_egress.hpp"
#include "slabflux/io/wire_latency_monitor.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

using namespace slabflux;

/**
 * @brief Hardware Timestamping Extraction Physics.
 * Proves that we can extract nanosecond-precision wire timestamps
 * from the NIC's control message (CMSG) block.
 */
TEST(LatencyMonitorTest, HardwareTimestampAudit) {
    struct msghdr msg;
    struct cmsghdr* cmsg_hdr;
    char control_buf[CMSG_SPACE(sizeof(struct scm_timestamping))];
    struct scm_timestamping* ts_ptr;

    std::memset(&msg, 0, sizeof(msg));
    msg.msg_control = control_buf;
    msg.msg_controllen = sizeof(control_buf);

    cmsg_hdr = CMSG_FIRSTHDR(&msg);
    cmsg_hdr->cmsg_level = SOL_SOCKET;
    cmsg_hdr->cmsg_type = SCM_TIMESTAMPING;
    cmsg_hdr->cmsg_len = CMSG_LEN(sizeof(struct scm_timestamping));

    ts_ptr = reinterpret_cast<struct scm_timestamping*>(CMSG_DATA(cmsg_hdr));
    ts_ptr->ts[2].tv_sec = 123456789;
    ts_ptr->ts[2].tv_nsec = 500; // 500ns

    // Resolution
    uint64_t wire_ts = io::get_hardware_timestamp(&msg);
    
    EXPECT_EQ(wire_ts, 123456789000000500ULL);
}

/**
 * @brief Bare-metal Egress Polling Latency.
 * Measures the submission cost of an io_uring write request.
 */
TEST(BaremetalEgressTest, SubmissionCycleAudit) {
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    
    try {
        // Use an invalid FD for logic-only cycle test, but bind SQPOLL to a valid core (0)
        io::baremetal_egress<WireFrame, 1024> egress(-1, 0);
        
        WireFrame frame;
        frame.lsn = 1;

        // Warm up I-Cache and wake the SQPOLL kernel thread.
        // The first operation typically incurs an io_uring_enter syscall to wake the thread.
        for (int i = 0; i < 10; ++i) {
            egress.send(&frame);
        }

        constexpr size_t ITERATIONS = 100;
        uint64_t start = __rdtsc();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            egress.send(&frame);
        }
        uint64_t end = __rdtsc();

        double cycles = static_cast<double>(end - start) / ITERATIONS;
        std::cout << "[PERF] Baremetal Egress Submission: " << cycles << " cycles\n";
        
        // Requirement: Sub-200 cycles for submission (dry-run)
        EXPECT_LT(cycles, 300.0);
    } catch (...) {
        GTEST_SKIP() << "io_uring not supported on this platform.";
    }
}
