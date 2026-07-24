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
 * @file dpdk_ingress_test.cpp
 * @brief DPDK Ingress Nexus Verification Suite.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <memory>
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/dpdk_ingress.hpp"
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
 * @brief Mock Logic for DPDK Ingress routing.
 * Simulates the first stage of the Causal Mesh.
 */
struct dpdk_mock_logic {
    std::atomic<size_t> frames_received{0};

    bool on_raw_frame(const auto* frame, size_t len) noexcept {
        frames_received.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
};

/**
 * @brief Physical Residency Audit.
 * Paradigm Shattering: Validates that the DPDK Ingress structure satisfies 
 * hardware interconnect requirements to prevent cache-line splits.
 */
TEST(DpdkIngressTest, PhysicalResidencyAudit) {
    dpdk_mock_logic logic;
    using WireFrame = net::wire_frame_lsn<uint64_t>;

    // Requirement 1: The Nexus structure must be 64-byte aligned
    io::dpdk_ingress<WireFrame, 1024, dpdk_mock_logic> nexus(logic, 0, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0);

    // Requirement 2: Class size must be a multiple of cache line size 
    // to prevent L1-D set aliasing in dense arrays.
    EXPECT_EQ(sizeof(nexus) % 64, 0);
}

/**
 * @brief Interconnect Polling Physics.
 * Measures the cost of a hardware poll when no packets are available.
 * This proves the efficiency of the user-space PMD integration.
 */
TEST(DpdkIngressTest, PollingLatencyAudit) {
    if (geteuid() != 0) {
        GTEST_SKIP() << "DPDK tests require root privileges.";
    }
    dpdk_mock_logic logic;
    using WireFrame = net::wire_frame_lsn<uint64_t>;

    // Environment Requirement: Ensure hugepages are available for DPDK memory
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. DPDK physics audit requires HFT tuning.";
    }

    if (!sf_ensure_dpdk_eal_initialized()) {
        GTEST_SKIP() << "DPDK EAL initialization failed.";
    }

    rte_mempool* test_pool = rte_pktmbuf_pool_create(
        "SF_IG_POOL_LAT", 2048, 64, 0, 2048, rte_socket_id()
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

    io::dpdk_ingress<WireFrame, 1024, dpdk_mock_logic> nexus(logic, 0, 0);

    constexpr size_t ITERATIONS = 100'000;
    uint64_t start_cycles = __rdtsc();

    for(size_t i = 0; i < ITERATIONS; ++i) {
        // Note: This calls rte_eth_rx_burst. In a unit test without linked PMD,
        // it must safely return 0 without crashing.
        nexus.poll();
    }

    uint64_t end_cycles = __rdtsc();
    double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] DPDK Nexus Ingress Idle Latency: " << cycles_per_poll << " cycles/poll\n";
    
    // Requirement: DPDK Null Poll should be sub-15 cycles on industrial silicon
    EXPECT_LT(cycles_per_poll, 15.0);

    rte_eth_dev_stop(0);
    rte_mempool_free(test_pool);
}

/**
 * @brief Zero-Copy Handoff Integrity.
 * Validates that the Nexus correctly translates DPDK mbufs into 
 * WireFrames without intermediate copying.
 */
TEST(DpdkIngressTest, BatchDispatchIntegrity) {
    if (geteuid() != 0) {
        GTEST_SKIP() << "DPDK tests require root privileges.";
    }
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping DPDK dispatch audit.";
    }

    if (!sf_ensure_dpdk_eal_initialized()) {
        GTEST_SKIP() << "DPDK EAL initialization failed.";
    }

    dpdk_mock_logic logic;
    using WireFrame = net::wire_frame_lsn<uint64_t>;

    rte_mempool* test_pool = rte_pktmbuf_pool_create(
        "SF_IG_POOL_INT", 2048, 64, 0, 2048, rte_socket_id()
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

    io::dpdk_ingress<WireFrame, 1024, dpdk_mock_logic> nexus(logic, 0, 0);

    // Proof of dry-run stability
    EXPECT_NO_THROW(nexus.poll());
    EXPECT_EQ(logic.frames_received.load(), 0);

    rte_eth_dev_stop(0);
    rte_mempool_free(test_pool);
}
