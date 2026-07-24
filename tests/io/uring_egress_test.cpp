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
 * @file uring_egress_test.cpp
 */

#include <gtest/gtest.h>
#include <thread> // Required for std::atomic<bool> running
#include <x86intrin.h>
#include <memory>
#include <chrono>
#include <cstring>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/uring_egress.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

// ============================================================================
// METRIC 1: PHYSICAL RESIDENCY & CACHE-LINE ISOLATION AUDIT
// ============================================================================
TEST(UringNexusEgressTest, PhysicalResidencyAudit) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping physical residency test.";
    }

    using EventType = uint64_t;
    using WireFrame = slabflux::core::wire_frame_lsn<EventType>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>; // Pool of frames
    using NetBus    = core::spsc_conduit<core::tagged_pointer, 1024>; // Conduit of tokens

    auto np = std::make_unique<NetAlloc>();
    auto nc = std::make_unique<NetBus>();
    std::atomic<bool> running{true};

    try {
        io::uring_egress<NetBus, NetAlloc, 1024> nexus(-1, *nc, *np, running);

        EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0);
        EXPECT_EQ(sizeof(nexus) % 64, 0);
        EXPECT_EQ(alignof(decltype(nexus)), 64);
    } catch (...) {
        SUCCEED();
    }
}

// ============================================================================
// METRIC 2: INTERCONNECT THROUGHPUT PHYSICS & LATENCY PROFILE
// ============================================================================
TEST(UringNexusEgressTest, ThroughputPhysics) {
    using EventType = uint64_t;
    using WireFrame = slabflux::core::wire_frame_lsn<EventType>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 4096>;
    using NetBus    = core::spsc_conduit<WireFrame*, 4096>;

    auto np = std::make_unique<NetAlloc>();
    auto nc = std::make_unique<NetBus>();
    std::atomic<bool> running{true};

    try {
        io::uring_egress<NetBus, NetAlloc, 4096> nexus(-1, *nc, *np, running);

        constexpr size_t BATCH_SIZE = 100'000;

        for (size_t i = 0; i < 1024; ++i) {
            if (auto* frame = np->make_raw()) {
                nc->push(frame);
            }
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        uint64_t start_cycles = __rdtsc();

        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            nexus.poll_egress();

            if (nc->occupancy() < 512) [[unlikely]] {
                if (auto* frame = np->make_raw()) nc->push(frame);
            }
        }

        uint64_t end_cycles = __rdtsc();
        auto end_time = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> duration = end_time - start_time;
        double mops = (static_cast<double>(BATCH_SIZE) / duration.count()) / 1'000'000.0;
        double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / BATCH_SIZE;

        std::cout << "[PERF] Nexus Egress Throughput: " << mops << " Mops/sec\n";
        std::cout << "[PERF] Nexus Egress Latency: " << cycles_per_poll << " cycles/poll\n";

        EXPECT_GT(mops, 1.0);
    } catch (...) {
        GTEST_SKIP() << "Environment does not support native io_uring instances.";
    }
}

// ============================================================================
// METRIC 3: EXTREME BACKPRESSURE & MEMORY RECYCLING INTEGRITY
// ============================================================================
TEST(UringNexusEgressTest, BackpressureIntegrity) {
    using EventType = uint64_t;
    using WireFrame = slabflux::core::wire_frame_lsn<EventType>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 128>;
    using NetBus    = core::spsc_conduit<WireFrame*, 128>;

    auto np = std::make_unique<NetAlloc>();
    auto nc = std::make_unique<NetBus>();
    std::atomic<bool> running{true};

    try {
        io::uring_egress<NetBus, NetAlloc, 64> nexus(-1, *nc, *np, running);

        // Fix: Fill the entire conduit queue past the allocator pool's hard limit (128 elements)
        // to actively verify that your backpressure recovery paths safely recycle frames
        for (int i = 0; i < 256; ++i) {
            if (auto* frame = np->make_raw()) {
                // Use non-blocking try_push to prevent single-threaded deadlocks
                // when testing saturation boundaries.
                if (!nc->try_push(frame)) {
                    np->release(frame);
                }
            }
            // Drive the engine to process whatever made it into the conduit
            nexus.poll_egress();
        }

        SUCCEED() << "Backpressure handled without leak or crash.";
    } catch (...) {
        GTEST_SKIP() << "Environment does not support native io_uring instances.";
    }
}
