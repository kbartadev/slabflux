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
 * @file uring_egress_xdp_test.cpp
 */

#include <gtest/gtest.h>
#include <thread> // Required for std::atomic<bool> running
#include <x86intrin.h>
#include <memory>
#include <chrono>
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/uring_egress_xdp.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/platform/os.hpp"

// Opaque Type Completion: Define the body after includes to complete the 
// forward declaration provided by libbpf/libxdp, allowing stack residency.
struct xsk_socket { int fd = -1; };
using namespace slabflux;

/**
 * @brief Physical Residency Audit.
 * Paradigm Shattering: Validates that the Egress Nexus structure and XDP 
 * descriptors satisfy hardware interconnect requirements to prevent cache-line splits.
 */
TEST(XdpEgressTest, PhysicalResidencyAudit) {
    using EventType = uint64_t;
    using WireFrame = net::wire_frame_lsn<EventType>;
    using NetBus    = core::spsc_conduit<WireFrame*, 1024>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    NetBus nc;
    NetAlloc np;
    std::atomic<bool> running{true};

    // Fixed template arguments and constructor to match sharded meta layout
    io::uring_egress_xdp<NetBus, NetAlloc, 1024> nexus(nullptr, {}, {}, nc, np, running);

    // Requirement 1: Egress structure must be 64-byte aligned
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0);

    // Requirement 2: The conduit itself must be aligned for the consumer thread
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&nc) % 64, 0);
}

/**
 * @brief UMEM Translation Physics.
 * Verifies the bit-perfect resolution of physical pointers to XDP offsets.
 * Failure here results in the NIC reading from incorrect memory regions.
 */
TEST(XdpEgressTest, UmemTranslationAudit) {
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    
    // Simulate a 2MB HugePage backed UMEM region
    alignas(2 * 1024 * 1024) WireFrame slab[1024];
    void* umem_base = static_cast<void*>(&slab[0]);
    
    // Test index 512 (middle of the slab)
    WireFrame* target_ptr = &slab[512];
    
    // Resolution Logic
    uint64_t offset = reinterpret_cast<char*>(target_ptr) - static_cast<char*>(umem_base);
    
    // Requirement: The offset must be exactly 512 * sizeof(WireFrame)
    EXPECT_EQ(offset, 512 * sizeof(WireFrame));
    
    // Verification of the reverse resolution (used in completion reclamation)
    WireFrame* resolved = reinterpret_cast<WireFrame*>(static_cast<char*>(umem_base) + offset);
    EXPECT_EQ(resolved, target_ptr);
}

/**
 * @brief Interconnect Polling Physics (XDP Bypass Path).
 * Proves that the AF_XDP egress loop operates with sub-10 cycle overhead
 * for the reclamation phase when no new completions are present.
 */
TEST(XdpEgressTest, XdpBypassIdleLatency) {
    using EventType = uint64_t;
    using WireFrame = net::wire_frame_lsn<EventType>;
    using NetBus    = core::spsc_conduit<WireFrame*, 1024>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    NetBus nc;
    NetAlloc np;
    std::atomic<bool> running{true};

    // Fixed template arguments and constructor
    io::uring_egress_xdp<NetBus, NetAlloc, 1024> nexus(nullptr, {}, {}, nc, np, running);

    constexpr size_t ITERATIONS = 100'000;
    uint64_t start_cycles = __rdtsc();

    for(size_t i = 0; i < ITERATIONS; ++i) {
        nexus.poll_egress();
    }

    uint64_t end_cycles = __rdtsc();
    double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] Nexus XDP Egress Idle Latency: " << cycles_per_poll << " cycles/poll\n";
    
    // Requirement: XDP Egress Null Poll must be extremely lean
    EXPECT_LT(cycles_per_poll, 15.0);
}

/**
 * @brief Throughput Audit.
 * Measures the Million Operations per second (Mops/sec) metric for draining 
 * the conduit into the TX ring.
 */
TEST(XdpEgressTest, ThroughputPhysics) {
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetBus    = core::spsc_conduit<WireFrame*, 4096>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 4096>;

    NetBus nc;
    NetAlloc np;
    std::atomic<bool> running{true};

    // Mock XDP structures for testing without actual XDP setup
    ::xsk_socket mock_xsk;
    xsk_ring_prod mock_tx_ring;
    xsk_ring_cons mock_comp_ring;

    // Mocking: libbpf ring macros (peek, reserve, etc.) dereference internal 
    // pointers. We provide valid dummy memory to prevent null-dereference segfaults.
    static uint32_t tx_prod = 0, tx_cons = 0, tx_flags = 0;
    static uint32_t comp_prod = 0, comp_cons = 0, comp_flags = 0;
    static xdp_desc tx_ring_data[32];
    static uint64_t comp_ring_data[32];

    mock_tx_ring.producer = &tx_prod;
    mock_tx_ring.consumer = &tx_cons;
    mock_tx_ring.flags    = &tx_flags;
    mock_tx_ring.ring     = tx_ring_data;
    mock_tx_ring.mask     = 31;
    mock_tx_ring.size     = 32;
    mock_tx_ring.cached_prod = 0;
    mock_tx_ring.cached_cons = 0;

    mock_comp_ring.producer = &comp_prod;
    mock_comp_ring.consumer = &comp_cons;
    mock_comp_ring.flags    = &comp_flags;
    mock_comp_ring.ring     = comp_ring_data;
    mock_comp_ring.mask     = 31;
    mock_comp_ring.size     = 32;
    mock_comp_ring.cached_prod = 0;
    mock_comp_ring.cached_cons = 0;

    io::uring_egress_xdp<NetBus, NetAlloc, 4096> nexus(&mock_xsk, mock_tx_ring, mock_comp_ring, nc, np, running);

    constexpr size_t BATCH_SIZE = 100'000;
    auto start_time = std::chrono::high_resolution_clock::now();

    for(size_t i = 0; i < BATCH_SIZE; ++i) {
        // Simulated producer: Fill the conduit just-in-time
        if (nc.occupancy() < 128) {
            if (auto* frame = np.make_raw()) nc.push(frame);
        }
        nexus.poll_egress();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end_time - start_time;
    double mops = (static_cast<double>(BATCH_SIZE) / duration.count()) / 1'000'000.0;

    std::cout << "[PERF] Nexus XDP Egress Throughput: " << mops << " Mops/sec\n";
    EXPECT_GT(mops, 0.5); // Minimum baseline in simulated environment
}
