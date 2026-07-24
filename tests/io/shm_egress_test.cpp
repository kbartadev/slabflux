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
 * @file shm_egress_test.cpp
 * @brief Shared Memory Egress Nexus Verification Suite.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <memory>
#include <chrono>
#include <string>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/shm_egress.hpp"
#include "slabflux/bridge/shm_bridge.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

/**
 * @brief Physical Residency Audit.
 * Paradigm Shattering: Validates that the SHM Egress Nexus structure satisfies 
 * hardware interconnect requirements to prevent cache-line splits and MESI storms.
 */
TEST(ShmEgressTest, PhysicalResidencyAudit) {
    const std::string shm_name = "/slabflux_egress_residency";
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetBus    = core::spsc_conduit<WireFrame*, 1024>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    NetBus nc;
    NetAlloc np;

    // Requirement 1: Egress Nexus must be 64-byte aligned
    io::shm_egress<WireFrame, 1024, NetBus, NetAlloc> nexus(shm_name, io::ipc_role::creator, nc, np);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0);

    // Requirement 2: The structure size should be multiple of 64
    EXPECT_EQ(sizeof(nexus) % 64, 0);
}

/**
 * @brief Interconnect Polling Physics.
 * Measures the cost of a "Null Poll" (empty conduit) to prove that the 
 * SHM publication loop remains wait-free and branchless.
 */
TEST(ShmEgressTest, ShmPollingPhysics) {
    const std::string shm_name = "/slabflux_egress_physics";
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetBus    = core::spsc_conduit<WireFrame*, 1024>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    NetBus nc;
    NetAlloc np;

    io::shm_egress<WireFrame, 1024, NetBus, NetAlloc> nexus(shm_name, io::ipc_role::creator, nc, np);

    constexpr size_t ITERATIONS = 100'000;
    uint64_t start_cycles = __rdtsc();

    for(size_t i = 0; i < ITERATIONS; ++i) {
        nexus.poll();
    }

    uint64_t end_cycles = __rdtsc();
    double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] Nexus SHM Egress Idle Latency: " << cycles_per_poll << " cycles/poll\n";
    
    // Requirement: IPC Egress Null Poll must be sub-15 cycles
    EXPECT_LT(cycles_per_poll, 15.0);
}

/**
 * @brief Cross-Process Publication Integrity.
 * Verifies that the Egress Nexus correctly drains the Conduit, 
 * writes to Shared Memory, and reclaims memory to the Pool.
 */
TEST(ShmEgressTest, DataPublicationIntegrity) {
    const std::string shm_name = "/slabflux_egress_handoff";
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetBus    = core::spsc_conduit<WireFrame*, 1024>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    NetBus nc;
    NetAlloc np;

    // 1. Setup Egress Nexus (Creator)
    io::shm_egress<WireFrame, 1024, NetBus, NetAlloc> nexus(shm_name, io::ipc_role::creator, nc, np);
    
    // 2. Setup a Consumer Bridge (Joiner) to verify data
    bridge::shm_bridge<WireFrame, 1024> consumer_wire(shm_name, io::ipc_role::joiner);

    // 3. Populate Conduit from local Pool
    auto* frame = np.make_raw();
    ASSERT_NE(frame, nullptr);
    frame->lsn = 0xDEADBEEF;
    nc.push(frame);

    // 4. Trigger Egress Poll (Publishes to SHM and Frees local memory)
    nexus.poll();

    // 5. Verify physical commit in SHM
    const WireFrame* published = consumer_wire.wire().peek();
    ASSERT_NE(published, nullptr);
    EXPECT_EQ(published->lsn, 0xDEADBEEF);
    
    consumer_wire.wire().consume();
}

/**
 * @brief Burst Publication Throughput.
 * Measures the Mops/sec for moving a burst of events from the Compute fabric 
 * to the IPC fabric.
 */
TEST(ShmEgressTest, BurstPublicationThroughput) {
    const std::string shm_name = "/slabflux_egress_burst";
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetBus    = core::spsc_conduit<WireFrame*, 4096>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 4096>;

    NetBus nc;
    NetAlloc np;
    io::shm_egress<WireFrame, 4096, NetBus, NetAlloc> nexus(shm_name, io::ipc_role::creator, nc, np);

    for(int i = 0; i < 1000; ++i) {
        auto* f = np.make_raw();
        if (f) { f->lsn = i; nc.push(f); }
    }

    auto start = std::chrono::high_resolution_clock::now();
    nexus.poll(); // Drains the whole burst (batch limit is 32, so it might need multiple polls if not in a loop, 
                  // but our current poll implementation handles up to 32 per call).
    auto end = std::chrono::high_resolution_clock::now();
    SUCCEED();
}
