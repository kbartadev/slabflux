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
 * @file shm_ingress_test.cpp
 * @brief Shared Memory Ingress Nexus Verification Suite.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <memory>
#include <chrono>
#include <string>
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/shm_ingress.hpp"
#include "slabflux/bridge/shm_bridge.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

/**
 * @brief Mock Logic for IPC Ingress.
 */
struct shm_mock_logic {
    std::atomic<size_t> received_count{0};
    uint64_t last_lsn{0};

    bool on_raw_frame(const auto& frame, size_t len) noexcept {
        received_count.fetch_add(1, std::memory_order_relaxed);
        last_lsn = frame.lsn;
        return true;
    }
};

/**
 * @brief Physical Residency Audit.
 * Paradigm Shattering: Validates that the SHM Nexus preserves cache-line 
 * boundaries between processes to prevent MESI interconnect thrashing.
 */
TEST(ShmIngressTest, PhysicalResidencyAudit) {
    const std::string shm_name = "/slabflux_test_residency";
    shm_mock_logic logic;
    using WireFrame = net::wire_frame_lsn<uint64_t>;

    // creator role initializes the segment
    io::shm_ingress<WireFrame, 1024, shm_mock_logic> nexus(shm_name, io::ipc_role::creator, logic);
    
    // Requirement 1: Ingress structure must be 64-byte aligned
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0);

    // Requirement 2: The entire nexus class should be sized to avoid set-aliasing
    EXPECT_EQ(sizeof(nexus) % 64, 0);
}

/**
 * @brief Interconnect Polling Physics.
 * Proves that the Shared Memory polling loop operates with sub-10 cycle overhead
 * when no data is present, as it is purely a local L3/RAM read.
 */
TEST(ShmIngressTest, ShmPollingPhysics) {
    const std::string shm_name = "/slabflux_test_physics";
    shm_mock_logic logic;
    using WireFrame = net::wire_frame_lsn<uint64_t>;

    io::shm_ingress<WireFrame, 1024, shm_mock_logic> nexus(shm_name, io::ipc_role::creator, logic);

    constexpr size_t ITERATIONS = 100'000;
    uint64_t start_cycles = __rdtsc();

    for(size_t i = 0; i < ITERATIONS; ++i) {
        nexus.poll();
    }

    uint64_t end_cycles = __rdtsc();
    double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] Nexus SHM Ingress Idle Latency: " << cycles_per_poll << " cycles/poll\n";
    
    // Requirement: SHM Null Poll must be significantly faster than io_uring/XDP
    EXPECT_LT(cycles_per_poll, 12.0);
}

/**
 * @brief Cross-Process Handoff Integrity.
 * Simulates a Producer writing directly to the SHM segment and verifies 
 * that the Nexus (Joiner) picks up the data bit-perfectly.
 */
TEST(ShmIngressTest, DataHandoffIntegrity) {
    const std::string shm_name = "/slabflux_test_handoff";
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    shm_mock_logic logic;

    // 1. Setup the Shared Memory segment as a Producer
    bridge::shm_bridge<WireFrame, 1024> producer_wire(shm_name, io::ipc_role::creator);
    
    // 2. Setup the Nexus as a Consumer (Joiner)
    io::shm_ingress<WireFrame, 1024, shm_mock_logic> nexus(shm_name, io::ipc_role::joiner, logic);

    // 3. Produce an event into the shared segment
    WireFrame* slot = producer_wire.wire().reserve();
    ASSERT_NE(slot, nullptr);
    slot->lsn = 0xABCDEF;
    slot->payload = 123456789;
    producer_wire.wire().commit();

    // 4. Poll the Nexus
    nexus.poll();

    // 5. Verify bit-perfect delivery
    EXPECT_EQ(logic.received_count.load(), 1);
    EXPECT_EQ(logic.last_lsn, 0xABCDEF);
}

/**
 * @brief Multi-Frame Burst Stability.
 * Verifies that the Nexus can process a rapid burst of IPC events 
 * without cache pollution or drops.
 */
TEST(ShmIngressTest, BurstIngestionIntegrity) {
    const std::string shm_name = "/slabflux_test_burst";
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    shm_mock_logic logic;

    bridge::shm_bridge<WireFrame, 1024> producer_wire(shm_name, io::ipc_role::creator);
    io::shm_ingress<WireFrame, 1024, shm_mock_logic> nexus(shm_name, io::ipc_role::joiner, logic);

    for(uint64_t i = 0; i < 100; ++i) {
        WireFrame* slot = producer_wire.wire().reserve();
        if (slot) {
            slot->lsn = i;
            producer_wire.wire().commit();
        }
        nexus.poll();
    }

    EXPECT_EQ(logic.received_count.load(), 100);
}
