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
#include <sys/socket.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>
#include <cstring>
#include "slabflux/io/wire_latency_monitor.hpp"

using namespace slabflux::io;

/**
 * @brief Hardware Timestamping Extraction Physics.
 * Proves that we can accurately extract hardware-level nanoseconds 
 * from NIC control messages.
 */
TEST(LatencyMonitorTest, HardwareTimestampExtraction) {
    struct msghdr msg;
    struct cmsghdr* cmsg;
    char control[CMSG_SPACE(sizeof(struct scm_timestamping))];
    struct scm_timestamping* ts_ptr;

    memset(&msg, 0, sizeof(msg));
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_TIMESTAMPING;
    cmsg->cmsg_len = CMSG_LEN(sizeof(struct scm_timestamping));

    // Simulate NIC providing hardware timestamp in ts[2]
    ts_ptr = reinterpret_cast<struct scm_timestamping*>(CMSG_DATA(cmsg));
    ts_ptr->ts[2].tv_sec = 1672531200; // Epoch
    ts_ptr->ts[2].tv_nsec = 500;

    // Logic Verification
    uint64_t wire_ns = get_hardware_timestamp(&msg);
    EXPECT_EQ(wire_ns, 1672531200000000500ULL);
}

/**
 * @brief Null Message Safety.
 */
TEST(LatencyMonitorTest, NullGuardAudit) {
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    
    // Requirement: Must return 0 instead of crashing on empty control blocks
    EXPECT_EQ(get_hardware_timestamp(&msg), 0);
}
