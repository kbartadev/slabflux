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
#include "slabflux/sys/hlc_clock.hpp"
#include "slabflux/sys/lsn_heartbeat.hpp"
#include "slabflux/sys/layout_guard.hpp"
#include "slabflux/sys/admin_interface.hpp"

using namespace slabflux::sys;

TEST(SysTest, HlcClockOrdering) {
    hlc_timestamp t1{100, 1};
    hlc_timestamp t2{100, 2};
    hlc_timestamp t3{101, 0};

    EXPECT_TRUE(t1 < t2);
    EXPECT_TRUE(t2 < t3);
    EXPECT_FALSE(t2 < t1);
}

TEST(SysTest, LsnHeartbeat) {
    lsn_heartbeat hb;
    publish_progress(hb, 100, 0xDEADBEEF);

    EXPECT_EQ(hb.current_lsn.load(), 100);
    EXPECT_EQ(hb.state_hash.load(), 0xDEADBEEF);
    EXPECT_GT(hb.last_tsc.load(), 0);
}

TEST(SysTest, LayoutGuard) {
    struct MyState {
        int a;
        double b;
    };

    uint64_t sig = layout_guard<MyState>::signature;
    EXPECT_NO_THROW(layout_guard<MyState>::verify(sig));
    EXPECT_THROW(layout_guard<MyState>::verify(sig + 1), std::runtime_error);
}

TEST(SysTest, AdminInterface) {
    admin_command cmd{admin_cmd_type::TAKE_SNAPSHOT, 12345};
    EXPECT_EQ(cmd.type, admin_cmd_type::TAKE_SNAPSHOT);
    EXPECT_EQ(cmd.payload, 12345);
}
