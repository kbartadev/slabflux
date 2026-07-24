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
 * @file matrix_nexus.hpp
 * @brief DPDK Poll Mode Driver (PMD) Ingress/Egress Loop.
 */

#pragma once

#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    template <typename Gateway, typename EgressConduit>
    class alignas(core::CACHE_LINE_SIZE) matrix_nexus {
        Gateway& gateway_;
        EgressConduit& egress_conduit_;
        uint16_t port_id_;
        uint16_t queue_id_;

        uint64_t last_temporal_tick_{0};
        uint64_t tsc_hz_{1};
        alignas(core::CACHE_LINE_SIZE) std::atomic<uint64_t> heartbeat_tsc_{0};

    public:
        matrix_nexus(Gateway& gateway, EgressConduit& egress, uint16_t port_id = 0, uint16_t queue_id = 0)
            : gateway_(gateway), egress_conduit_(egress), port_id_(port_id), queue_id_(queue_id) {
            
            // Capture CPU clock frequency for accurate temporal sweeps
            tsc_hz_ = rte_get_timer_hz();
            if (SL_EXPECT_FALSE(tsc_hz_ == 0)) tsc_hz_ = 3000000000ULL; // Fallback to 3GHz
        }

        SLAB_FORCE_INLINE uint64_t get_heartbeat() const noexcept {
            return heartbeat_tsc_.load(std::memory_order_relaxed);
        }

        /**
         * @brief Natively polls the DPDK PMD to evaluate RX and TX bursts.
         * @details Replaces generic AF_XDP/io_uring interrupts with a dedicated 
         * 100% core-pinned busy-spin loop.
         */
        SLAB_HOT void poll_and_execute() noexcept {
            uint64_t current_tsc = rte_rdtsc();
            heartbeat_tsc_.store(current_tsc, std::memory_order_relaxed);
            uint64_t current_time_ms = (current_tsc * 1000) / tsc_hz_;

            // ====================================================================
            // 1. Hardware PMD Ingress (RX Burst)
            // ====================================================================
            struct rte_mbuf* rx_mbufs[32];
            uint16_t nb_rx = rte_eth_rx_burst(port_id_, queue_id_, rx_mbufs, 32);

            for (uint16_t i = 0; i < nb_rx; ++i) {
                struct rte_mbuf* mbuf = rx_mbufs[i];
                
                // Software Pipelining: Hide memory access latency for the next buffer
                if (i + 1 < nb_rx) {
                    rte_prefetch0(rte_pktmbuf_mtod(rx_mbufs[i + 1], void*));
                }

                // Hardware Checksum Offload Verification (RX CKSUM)
                // Instantly discard frames mathematically corrupted over the physical fiber
                if (SL_EXPECT_FALSE(mbuf->ol_flags & (RTE_MBUF_F_RX_IP_CKSUM_BAD | RTE_MBUF_F_RX_L4_CKSUM_BAD))) {
                    rte_pktmbuf_free(mbuf);
                    continue;
                }

                const char* data = rte_pktmbuf_mtod(mbuf, const char*);
                size_t length = rte_pktmbuf_pkt_len(mbuf);

                // Inject directly into the gateway.
                // The gateway evaluates the raw frame natively, and will safely 
                // recycle the mbuf back to the mempool if it isn't swallowed by an OOO matrix.
                gateway_.on_raw_frame(data, length, current_time_ms, mbuf);
            }

            // ====================================================================
            // 2. Amortized Temporal Sweeping (~1ms precision)
            // ====================================================================
            if (SL_EXPECT_FALSE(current_time_ms > last_temporal_tick_)) {
                gateway_.poll_temporal(current_time_ms);
                last_temporal_tick_ = current_time_ms;
            }

            // ====================================================================
            // 3. Hardware PMD Egress (TX Burst)
            // ====================================================================
            size_t available = egress_conduit_.available_to_peek();
            if (SL_EXPECT_FALSE(available > 0)) {
                struct rte_mbuf* tx_mbufs[32];
                uint16_t to_send = static_cast<uint16_t>(available > 32 ? 32 : available);

                for (uint16_t i = 0; i < to_send; ++i) {
                    auto* slot = egress_conduit_.get_peek_slot(i);
                    struct rte_mbuf* mbuf = slot->mbuf;
                    
                    // Zero-Cost Hardware Checksum Offloading (Tx CKSUM)
                    // Defers IP and TCP checksum calculations directly to the physical NIC
                    mbuf->l2_len = 14; // Standard Ethernet II 
                    mbuf->l3_len = 20; // Standard IPv4
                    mbuf->ol_flags |= RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_TCP_CKSUM;
                    
                    tx_mbufs[i] = mbuf;
                }

                // Blast the constructed mbufs directly to the physical wire
                uint16_t nb_tx = rte_eth_tx_burst(port_id_, queue_id_, tx_mbufs, to_send);

                // Commit the successful dispatch back to the lock-free conduit
                egress_conduit_.consume_n(nb_tx);

                // Handle transient hardware backpressure
                if (SL_EXPECT_FALSE(nb_tx < to_send)) {
                    // The NIC ring is full. We drop the remaining mbufs from the software ring
                    // to prevent head-of-line blocking, and recycle them to the mempool.
                    // Standard TCP will retransmit via RTO/tx_unacked_ring if heavily dropped.
                    for (uint16_t i = nb_tx; i < to_send; ++i) {
                        rte_pktmbuf_free(tx_mbufs[i]);
                    }
                    egress_conduit_.consume_n(to_send - nb_tx);
                }
            }
        }
    };

} // namespace slabflux::net