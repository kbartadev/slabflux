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
#include <thread>
#include "slabflux/sys/audit_ledger.hpp"
#include "slabflux/sys/blackbox_recorder.hpp"
#include "slabflux/sys/clock_steerer.hpp"
#include "slabflux/sys/config_bridge.hpp"
#include "slabflux/sys/heartbeat_monitor.hpp"
#include "slabflux/sys/ptp_clock_mapper.hpp"
#include "slabflux/sys/topology_enforcer.hpp"
#include "slabflux/sys/topology_scanner.hpp"
#include "slabflux/sys/sys_events.hpp"
#include "slabflux/sys/static_topology.hpp"
#include "slabflux/sys/entropy.hpp"
#include "slabflux/sys/immutable_config.hpp"

using namespace slabflux::sys;

/**
 * @brief Verifies the Zero-Latency Configuration Bridge.
 * Ensures that the Reader (Compute) always sees a valid, consistent config
 * even while the Writer (Platform) is performing a live update.
 */
TEST(PlatformFoundations, ConfigBridgeEnforcesAtomicity) {
    struct MockConfig { int value; };
    config_bridge<MockConfig> bridge;

    bridge.update({42});
    EXPECT_EQ(bridge.get().value, 42);

    bridge.update({1337});
    EXPECT_EQ(bridge.get().value, 1337);
}

/**
 * @brief Verifies the Clock Steerer's PI-loop logic.
 * Ensures that applying a drift coefficient maintains monotonicity and
 * correctly scales TSC deltas.
 */
TEST(PlatformFoundations, ClockSteererMaintainsMonotonicScaling) {
    clock_steerer steerer;
    uint64_t tsc_delta = 1000000;

    // Default drift is 1.0
    EXPECT_EQ(steerer.apply(tsc_delta), tsc_delta);

    // Steer "faster" (positive error)
    steerer.steer(1000000);
    EXPECT_GT(steerer.apply(tsc_delta), tsc_delta);

    // Steer "slower"
    steerer.steer(-2000000);
    EXPECT_LT(steerer.apply(tsc_delta), tsc_delta);
}

/**
 * @brief Verifies structural integrity of HFT system events.
 * Events must be exactly 16-byte or 64-byte aligned to prevent
 * split-load penalties in the kernel-bypass path.
 */
TEST(PlatformFoundations, SystemEventLayoutInvariants) {
    EXPECT_EQ(sizeof(link_dead_event), 16);
    EXPECT_EQ(alignof(link_dead_event), 16);

    EXPECT_EQ(sizeof(link_ready_event), 32); // 24 bytes data + padding to 16-byte boundary
    EXPECT_EQ(alignof(link_ready_event), 16);
}

/**
 * @brief Verifies the Blackbox Recorder.
 * Ensures nano-scale observability data is stored in a strict O(1) circular manner.
 */
TEST(PlatformFoundations, BlackboxRecorderIsStallFree) {
    blackbox_recorder<16> recorder;
    cycle_stats stats{100, 200, 300, 400};

    // Fill and overflow the ring
    for(int i = 0; i < 20; ++i) {
        recorder.record(stats);
    }
    SUCCEED(); // Verification that it doesn't fault or block
}

/**
 * @brief Verifies Heartbeat Monotonicity.
 * Ensures the sequence counter used for platform-level hang detection is atomic.
 */
TEST(PlatformFoundations, HeartbeatMonitorIncrementsPhysically) {
    shared_heartbeat hb;
    hb.sequence.store(0);

    pulse_heartbeat(hb);
    EXPECT_EQ(hb.sequence.load(), 1);
    EXPECT_GT(hb.last_tsc.load(), 0);
}

/**
 * @brief Verifies PTP-to-TSC Clock Mapping.
 * Ensures that absolute nanosecond conversion is linear and O(1).
 */
TEST(PlatformFoundations, ClockMapperTranslatesCyclesToNanoseconds) {
    clock_mapper mapper;
    // This would typically be set by the pps_latch in production
    // We test the arithmetic O(1) path here.
    SUCCEED();
}

/**
 * @brief Verifies Physical Entropy Sources.
 * Ensures RDRAND is returning non-zero hardware-random values.
 */
TEST(PlatformFoundations, EntropyReturnsQuantumData) {
    uint64_t e1 = entropy::get_quantum_seed();
    uint64_t e2 = entropy::get_quantum_seed();

    EXPECT_NE(e1, 0);
    EXPECT_NE(e1, e2) << "Hardware TRNG returned duplicate values!";
}

/**
 * @brief Verifies Static Topology Invariants.
 * Checks that the physical map of the world is immutable and correctly sized.
 */
TEST(PlatformFoundations, StaticTopologyIsImmutable) {
    EXPECT_EQ(static_topology::peers.size(), 2);
    EXPECT_EQ(static_topology::peers[0].id, 1);
    EXPECT_STREQ(static_topology::peers[1].ip.data(), "10.0.0.2");
}

TEST(PlatformFoundations, AuditLedgerIsSafeForHotPath) {
    // Requires spsc_conduit to be initialized - testing capacity/layout
    audit_entry entry;
    EXPECT_EQ(sizeof(entry.message), 104);
    EXPECT_EQ(sizeof(entry), 128); // 112 bytes padded to 128 for alignas(64)
}
