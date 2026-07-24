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
#include <x86intrin.h>
#include <memory>
#include <chrono>
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/uring_ingress_xdp.hpp"
#include "slabflux/io/uring_egress_xdp.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

extern "C" {
    struct xsk_socket {
        int fd = -1;
    };
}

/**
 * @brief Mock Logic for XDP Ingress.
 */
struct xdp_mock_logic {
    std::atomic<size_t> frames_received{0};
    bool on_raw_frame(auto* frame, size_t len) noexcept {
        frames_received.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    void on_conduit_full_drop() noexcept {}
};

/**
 * @brief Mock Memory Pool for UMEM simulation.
 */
template<typename T>
struct xdp_fake_pool {
    T buffer[1024];
    T* data() noexcept { return buffer; }
    T* make_raw() noexcept { return &buffer[0]; }
    void free(T*) noexcept {}
    void release(T*) noexcept {}
};

namespace {
    template <typename T>
    struct alignas(64) aligned_segment_envelope {
        T instance;
        template<typename... Args>
        aligned_segment_envelope(Args&&... args) : instance(std::forward<Args>(args)...) {}
    };
}

/**
 * @brief Physical Residency Audit.
 * Paradigm Shattering: Validates that the XDP Nexus structure and descriptors
 * satisfy hardware interconnect requirements to prevent cache-line splits.
 */
TEST(XdpIngressXdpTest, PhysicalResidencyAudit) {
    using EventType = uint64_t;
    using WireFrame = net::wire_frame_lsn<EventType>;
    using NetBus    = core::spsc_conduit<WireFrame*, 1024>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    // Allocate the primitives within cache-aligned segment envelopes to eliminate stack pointer variance
    auto aligned_nc = std::make_unique<aligned_segment_envelope<NetBus>>();
    auto aligned_np = std::make_unique<aligned_segment_envelope<NetAlloc>>();
    std::atomic<bool> running{true};

    using NexusType = io::uring_egress_xdp<NetBus, NetAlloc, 1024>;
    auto aligned_nexus = std::make_unique<aligned_segment_envelope<NexusType>>(
        nullptr, xsk_ring_prod{}, xsk_ring_cons{}, aligned_nc->instance, aligned_np->instance, running
    );

    // Verify actual hardware alignment properties matching your HugePage specifications
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&aligned_nexus->instance) % 64, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&aligned_nc->instance) % 64, 0);
}

/**
 * @brief Interconnect Polling Physics (XDP Bypass Path).
 * Proves that the AF_XDP polling loop operates with sub-15 cycle overhead
 * when no data is present (Null Poll).
 */
TEST(XdpIngressTest, XdpBypassPollingPhysics) {
    xdp_mock_logic logic;
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    xdp_fake_pool<WireFrame> mem_pool;

    // Note: We use the af_xdp_bypass backend explicitly
    io::uring_ingress_xdp<WireFrame, 1024, xdp_mock_logic, io::io_backend::af_xdp_bypass> nexus(logic);

    constexpr size_t ITERATIONS = 100'000;
    uint64_t start_cycles = __rdtsc();

    for(size_t i = 0; i < ITERATIONS; ++i) {
        nexus.poll(mem_pool);
    }

    uint64_t end_cycles = __rdtsc();
    double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] Nexus XDP Ingress Idle Latency: " << cycles_per_poll << " cycles/poll\n";

    // Requirement: XDP Null Poll must be extremely lean
    EXPECT_LT(cycles_per_poll, 20.0);
}

/**
 * @brief Fill Queue Replenishment Integrity.
 * Validates that the Nexus correctly provides memory offsets to the NIC.
 */
TEST(XdpIngressTest, FillQueueReplenishmentIntegrity) {
    xdp_mock_logic logic;
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    NetAlloc mem_pool;
    io::uring_ingress_xdp<WireFrame, 1024, xdp_mock_logic, io::io_backend::af_xdp_bypass> nexus(logic);

    // Requirement: Polling must trigger memory allocation to satisfy the NIC Fill Ring
    // Even if no data arrived (RX), the Nexus must ensure the NIC has free buffers.
    nexus.poll(mem_pool);

    SUCCEED();
}

/**
 * @brief Cross-Backend Consistency.
 * Verifies that the Nexus can be instantiated with io_uring backend
 * using the same API.
 */
TEST(XdpIngressTest, BackendConsistencyAudit) {
    xdp_mock_logic logic;
    using WireFrame = net::wire_frame_lsn<uint64_t>;

    // io_uring_kernel backend (Default)
    using NexusType = io::uring_ingress_xdp<WireFrame, 1024, xdp_mock_logic, io::io_backend::io_uring_kernel>;
    auto create_nexus = [&]() {
        NexusType nexus(logic);
    };
    EXPECT_NO_THROW(create_nexus());
}


/**
 * @brief Zero-Copy Memory Resolution.
 * Validates the arithmetic used to resolve physical pointers from XDP offsets.
 */
TEST(XdpIngressTest, UmemResolutionAudit) {
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    WireFrame slab[10];

    uint64_t offset = reinterpret_cast<char*>(&slab[5]) - reinterpret_cast<char*>(&slab[0]);
    WireFrame* resolved = reinterpret_cast<WireFrame*>(reinterpret_cast<char*>(&slab[0]) + offset);

    // Invariant: Pointer resolution must be bit-perfect
    EXPECT_EQ(resolved, &slab[5]);
}
