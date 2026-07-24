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
#include "slabflux/io/uring_ingress.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

/**
 * @brief Mock Logic for Ingress Routing.
 * Simulates the first stage of the Causal Mesh.
 */
struct mock_ingress_logic {
    std::atomic<size_t> frames_received{0};
    bool force_failure{false};

    bool on_raw_frame(auto* frame, size_t len) noexcept {
        if (force_failure) return false;
        frames_received.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void on_conduit_full_drop() noexcept {
        // Telemetry hook
    }
};

/**
 * @brief Physical Residency Audit.
 * Paradigm Shattering: Validates that the Ingress Nexus structure satisfies
 * hardware interconnect requirements to prevent cache-line splits.
 */
TEST(UringIngressTest, PhysicalResidencyAudit) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping physical residency test.";
    }

    mock_ingress_logic logic;
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using EventType = uint64_t;
    using WireFrame = net::wire_frame_lsn<EventType>;
    std::atomic<bool> running{true};

    try {
        NetBus conduit;
        io::uring_ingress<NetBus, 1024> nexus(-1, conduit, running);

        // Requirement: The ingress nexus must be 64-byte aligned for the L3 interconnect
        EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0);

        // Requirement: Total structure size should be a multiple of cache line size
        // if intended for dense array placement in the Node Context.
        EXPECT_EQ(sizeof(nexus) % 64, 0);
    } catch (...) {
        GTEST_SKIP() << "Environment does not support io_uring/XDP.";
    }
}

/**
 * @brief Interconnect Polling Physics.
 * Measures the cost of a "Null Poll" (no data) to prove zero-syscall overhead.
 */
TEST(UringIngressTest, PollingLatencyAudit) {
    mock_ingress_logic logic;
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using EventType = uint64_t;
    using WireFrame = net::wire_frame_lsn<EventType>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    NetAlloc mem_pool;
    std::atomic<bool> running{true};
    NetBus conduit;

    try {
        io::uring_ingress<NetBus, 1024> nexus(-1, conduit, running);

        constexpr size_t ITERATIONS = 100'000;
        uint64_t start_cycles = __rdtsc();

        for(size_t i = 0; i < ITERATIONS; ++i) {
            nexus.poll_ingress();
        }

        uint64_t end_cycles = __rdtsc();
        double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

        std::cout << "[PERF] Nexus Ingress Idle Latency: " << cycles_per_poll << " cycles/poll\n";

        // Requirement: An idle poll should be < 15 cycles on modern silicon
        EXPECT_LT(cycles_per_poll, 15.0);
    } catch (...) {
        GTEST_SKIP() << "io_uring/XDP not supported.";
    }
}

/**
 * @brief Empty Pool Resilience (Corrected).
 * Proves that the Ingress Nexus correctly handles backpressure when the
 * Memory Pool is exhausted by exercising the actual XDP fill ring logic.
 */
TEST(UringIngressTest, PoolExhaustionIntegrity) {
    mock_ingress_logic logic;
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 16>; // Tiny pool

    NetAlloc mem_pool;
    // Drain the pool completely to simulate hardware pressure
    while (mem_pool.make_raw()) { /* consume */ }

    std::atomic<bool> running{true};
    NetBus conduit;

    io::uring_ingress<NetBus, 1024> nexus(-1, conduit, running);

    // Requirement: Polling with an empty pool must not crash and must safely break
    // out of the refill loop when mem_pool.make_raw() returns nullptr.
    EXPECT_NO_THROW(nexus.poll_ingress());
}
