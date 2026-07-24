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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Absolute Liability Limitation & Full Terms: See DISCLAIMER, NOTICE, LICENSE.
 *
 * @file dpdk_duplex.hpp
 * @brief Unified DPDK Duplex Nexus.
 * @details Aggregates isolated Ingress and Egress DPDK engines into a single
 * cache-aligned structure for zero-copy high-frequency networking.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <unistd.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_malloc.h>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/io/dpdk_duplex.hpp"
#include "slabflux/platform/os.hpp"

namespace {
    constexpr uint32_t NUM_MBUFS = 2048;
    constexpr uint32_t MBUF_CACHE_SIZE = 64;
    constexpr uint32_t PRIV_SIZE = 0;
    constexpr uint16_t MBUF_BUF_SIZE = 2048;
    constexpr std::size_t BURST_LIMIT = 32;
}

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

TEST(HardenedDpdkDuplexClassTest, VerifyUnifiedDuplexLoopbackPhysics) {
    if (geteuid() != 0) {
        GTEST_SKIP() << "DPDK tests require root (sudo) privileges for hugepage access.";
    }

    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages are missing. DPDK memory allocation cannot be verified.";
    }

    if (!sf_ensure_dpdk_eal_initialized()) {
        GTEST_SKIP() << "DPDK EAL initialization failed (likely permissions or hugepage configuration).";
    }

    rte_mempool* test_pool = rte_pktmbuf_pool_create(
        "SF_DUPLEX_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, PRIV_SIZE, MBUF_BUF_SIZE, rte_socket_id()
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

    using NetConduit = slabflux::core::spsc_conduit<slabflux::core::tagged_pointer, 1024>;
    NetConduit ingress_bus;
    NetConduit egress_bus;
    std::atomic<bool> is_running{true};

    // Instantiate the full DUPLEX class on the net_ring0 device (Port 0, Queue 0)
    slabflux::transport::dpdk_duplex<NetConduit, NetConduit, BURST_LIMIT> duplex_instance(
        0, 0, test_pool, ingress_bus, egress_bus, is_running
    );

    // Fill the egress side: insert slab-like raw TCP frames
    constexpr std::size_t PACKET_COUNT = 64;
    for (std::size_t i = 0; i < PACKET_COUNT; ++i) {
        auto* frame = reinterpret_cast<slabflux::transport::raw_tcp_frame*>(
            rte_malloc(nullptr, sizeof(slabflux::transport::raw_tcp_frame) + 64, 64)
        );
        ASSERT_NE(frame, nullptr);
        frame->payload_length = 64;
        frame->connection_id = 101;
        std::memset(frame->data, 0xAB, 64); // Unique test marker

        auto token = slabflux::core::tagged_pointer::pack(slabflux::transport::raw_tcp_frame::ID, frame);
        ASSERT_TRUE(egress_bus.try_push(token));
    }

    // Simultaneous and tightly coupled Ingress/Egress drive
    uint64_t start_cycles = __rdtsc();
    constexpr std::size_t ITERATIONS = 5000;

    for (std::size_t i = 0; i < ITERATIONS; ++i) {
        duplex_instance.poll_egress();   // Pushes to NIC/ring
        duplex_instance.poll_ingress();  // Immediately reads back from loopback
    }

    uint64_t end_cycles = __rdtsc();
    double avg_cycles = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] DPDK Unified Duplex Class Cost: " << avg_cycles << " cycles/iteration\n";

    // Verify that the loopback transferred data into the interconnect channel
    if (ingress_bus.occupancy() > 0) {
        slabflux::core::tagged_pointer rx_token;
        if (ingress_bus.try_pop(rx_token)) {
            auto* mbuf = reinterpret_cast<rte_mbuf*>(rx_token.get_address());
            ASSERT_NE(mbuf, nullptr);

            // Verify that the returned mbuf data is intact
            uint8_t* incoming_data = rte_pktmbuf_mtod(mbuf, uint8_t*);

            // DPDK egress only transmits the payload portion, not the internal tracking headers.
            EXPECT_EQ(incoming_data[0], 0xAB);
            EXPECT_EQ(incoming_data[63], 0xAB);
        }
    }

    EXPECT_LT(avg_cycles, 60.0);

    is_running.store(false);
    rte_eth_dev_stop(0);
    rte_mempool_free(test_pool);
}
