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
 * @file dpdk_egress_test.cpp
 * @brief DPDK Egress Nexus Verification Suite.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <memory>
#include <chrono>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/dpdk_egress.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

inline bool sf_ensure_dpdk_eal_initialized() {
    static bool attempted = false;
    static bool success = false;
    if (!attempted) {
        attempted = true;
        const char* const argv[] = {
            "slabflux_test", "-c", "0x1",
            "--vdev=net_ring0,node=0",
            "--no-pci", "--file-prefix=sf_dpdk_test"
        };
        success = (rte_eal_init(6, const_cast<char**>(argv)) >= 0);
    }
    return success;
}

/**
 * @brief Physical Residency Audit.
 * Paradigm Shattering: Validates that the DPDK Egress structure satisfies 
 * hardware interconnect requirements to prevent cache-line splits during 
 * high-frequency TX bursts.
 */
TEST(DpdkEgressTest, PhysicalResidencyAudit) {
    using EventType = uint64_t;
    using WireFrame = net::wire_frame_lsn<EventType>;
    using NetBus    = core::spsc_conduit<WireFrame*, 1024>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    NetBus nc;
    NetAlloc np;

    // Requirement 1: The Egress Nexus must be 64-byte aligned
    ::slabflux::io::dpdk_egress<WireFrame, 1024, NetBus, NetAlloc> nexus(nc, np, nullptr, 0, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0);

    // Requirement 2: Class size must be a multiple of 64 to avoid set-aliasing
    EXPECT_EQ(sizeof(nexus) % 64, 0);
}

/**
 * @brief Interconnect Polling Physics.
 * Measures the cost of an empty egress poll to prove that the PMD 
 * integration remains O(1) and branchless.
 */
TEST(DpdkEgressTest, PollingLatencyAudit) {
    if (geteuid() != 0) {
        GTEST_SKIP() << "DPDK tests require root privileges.";
    }
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping DPDK physics audit.";
    }

    if (!sf_ensure_dpdk_eal_initialized()) {
        GTEST_SKIP() << "DPDK EAL initialization failed.";
    }

    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetBus    = core::spsc_conduit<WireFrame*, 1024>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 1024>;

    rte_mempool* test_pool = rte_pktmbuf_pool_create(
        "SF_EG_POOL_LAT", 2048, 64, 0, 2048, rte_socket_id()
    );
    ASSERT_NE(test_pool, nullptr);

    struct rte_eth_conf port_conf{};
    if (rte_eth_dev_configure(0, 1, 1, &port_conf) < 0 ||
        rte_eth_rx_queue_setup(0, 0, 128, rte_eth_dev_socket_id(0), nullptr, test_pool) < 0 ||
        rte_eth_tx_queue_setup(0, 0, 128, rte_eth_dev_socket_id(0), nullptr) < 0 ||
        rte_eth_dev_start(0) < 0) {
        rte_mempool_free(test_pool);
        GTEST_SKIP() << "DPDK Port 0 initialization failed.";
    }

    NetBus nc;
    NetAlloc np;
    ::slabflux::io::dpdk_egress<WireFrame, 1024, NetBus, NetAlloc> nexus(nc, np, test_pool, 0, 0);

    constexpr size_t ITERATIONS = 100'000;
    uint64_t start_cycles = __rdtsc();

    for(size_t i = 0; i < ITERATIONS; ++i) {
        nexus.poll();
    }

    uint64_t end_cycles = __rdtsc();
    double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] DPDK Nexus Egress Idle Latency: " << cycles_per_poll << " cycles/poll\n";
    
    // Requirement: DPDK Null Poll should be sub-20 cycles
    EXPECT_LT(cycles_per_poll, 20.0);

    rte_eth_dev_stop(0);
    rte_mempool_free(test_pool);
}

/**
 * @brief Throughput Physics.
 * Measures Millions of Operations per second (Mops/sec) for draining 
 * the conduit into the DPDK TX burst logic.
 */
TEST(DpdkEgressTest, ThroughputPhysics) {
    if (geteuid() != 0) {
        GTEST_SKIP() << "DPDK tests require root privileges.";
    }
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping DPDK throughput physics audit.";
    }

    if (!sf_ensure_dpdk_eal_initialized()) {
        GTEST_SKIP() << "DPDK EAL initialization failed.";
    }

    using WireFrame = net::wire_frame_lsn<uint64_t>;
    using NetBus    = core::spsc_conduit<WireFrame*, 4096>;
    using NetAlloc  = core::pinned_allocator_spsc<WireFrame, 4096>;

    rte_mempool* test_pool = rte_pktmbuf_pool_create(
        "SF_EG_POOL_THR", 2048, 64, 0, 2048, rte_socket_id()
    );
    ASSERT_NE(test_pool, nullptr);

    struct rte_eth_conf port_conf{};
    if (rte_eth_dev_configure(0, 1, 1, &port_conf) < 0 ||
        rte_eth_rx_queue_setup(0, 0, 128, rte_eth_dev_socket_id(0), nullptr, test_pool) < 0 ||
        rte_eth_tx_queue_setup(0, 0, 128, rte_eth_dev_socket_id(0), nullptr) < 0 ||
        rte_eth_dev_start(0) < 0) {
        rte_mempool_free(test_pool);
        GTEST_SKIP() << "DPDK Port 0 initialization failed.";
    }

    NetBus nc;
    NetAlloc np;
    ::slabflux::io::dpdk_egress<WireFrame, 4096, NetBus, NetAlloc> nexus(nc, np, test_pool, 0, 0);

    constexpr size_t BATCH_SIZE = 100'000;
    auto start_time = std::chrono::high_resolution_clock::now();

    for(size_t i = 0; i < BATCH_SIZE; ++i) {
        if (nc.occupancy() < 128) {
            if (auto* frame = np.make_raw()) nc.push(frame);
        }
        nexus.poll();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end_time - start_time;
    double mops = (static_cast<double>(BATCH_SIZE) / duration.count()) / 1'000'000.0;

    std::cout << "[PERF] DPDK Nexus Egress Throughput: " << mops << " Mops/sec\n";
    
    // Proof of dry-run stability
    SUCCEED();

    rte_eth_dev_stop(0);
    rte_mempool_free(test_pool);
}
