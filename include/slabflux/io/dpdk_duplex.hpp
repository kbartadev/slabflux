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

#pragma once

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#include <concepts>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <rte_prefetch.h>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/conduit.hpp" // For Conduit concept
#include "slabflux/core/pool.hpp"    // For MemoryPool concept
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"

namespace slabflux::transport {

    /**
     * @brief Hardware Burst Acquisition Invariant.
     * @details Defines the structural contract for high-throughput NIC burst 
     * reception to eliminate standard library overhead.
     */
    template <typename FrameT>
    concept HardwareBurstSource = requires(uint16_t p, uint16_t q, FrameT** batch, uint16_t max) {
        { rte_eth_rx_burst(p, q, batch, max) } -> std::convertible_to<uint16_t>;
    };

    /**
     * @brief Memory Lifecycle Policy.
     * @details Replaces standard DPDK callback structures with a 
     * template-driven resource reclamation strategy.
     */
    template <typename PoolType, typename T>
    struct reclamation_policy {
        static void execute(void* addr, void* opaque) noexcept {
            // Bit-perfect return to the Slab Matrix
            auto* pool = static_cast<PoolType*>(opaque);
            pool->release(static_cast<T*>(addr));
        }
    };

    template <typename InboundConduit, typename OutboundConduit, std::size_t MaxBurstSize = 32, typename ReclamationPool = void>
    requires core::Conduit<InboundConduit, typename InboundConduit::value_type> && 
             core::Conduit<OutboundConduit, typename OutboundConduit::value_type>
    class alignas(64) dpdk_duplex {
        static_assert(MaxBurstSize > 0 && (MaxBurstSize <= 64), "Burst size must match cache and SIMD bounds");

    private:
        uint16_t            port_id_{0};
        uint16_t            queue_id_{0};
        rte_mempool*        mbuf_pool_{nullptr};
        InboundConduit&     ingress_conduit_;
        OutboundConduit&    egress_conduit_;
        using InboundValueTypePod = typename InboundConduit::value_type_pod;
        ReclamationPool*    mem_pool_{nullptr};
        std::atomic<bool>&  running_sentinel_;
        alignas(64) uint64_t rx_total_ingressed_{0};

        // External buffer free callback structure for Zero-Copy
        rte_mbuf_ext_shared_info shinfo_;

    public:
        explicit dpdk_duplex(
            uint16_t port,
            uint16_t queue,
            rte_mempool* mbuf_pool,
            InboundConduit& in_conduit,
            OutboundConduit& out_conduit,
            std::atomic<bool>& running,
            ReclamationPool* pool = nullptr
        ) noexcept
        : port_id_(port)
        , queue_id_(queue)
        , mbuf_pool_(mbuf_pool)
        , ingress_conduit_(in_conduit)
        , egress_conduit_(out_conduit)
        , mem_pool_(pool)
        , running_sentinel_(running)
        {
            if constexpr (!std::is_same_v<ReclamationPool, void>) {
                if (mem_pool_) {
                    using RecPolicy = reclamation_policy<ReclamationPool, InboundValueTypePod>;
                    shinfo_.free_cb = RecPolicy::execute;
                    shinfo_.fcb_opaque = static_cast<void*>(mem_pool_);
                    rte_mbuf_ext_refcnt_update(&shinfo_, 1);
                }
            }
        }

        ~dpdk_duplex() noexcept = default;
        dpdk_duplex(const dpdk_duplex&) = delete;
        dpdk_duplex& operator=(const dpdk_duplex&) = delete;

        /**
         * @brief Hardware-Abstracted Burst Concept Receiver.
         * @details Replaces standard DPDK RX loops with a vectorized burst 
         * acquisition engine. Decouples hardware descriptors from the Matrix 
         * in 512-bit register bursts.
         */
        SLAB_HOT void poll_ingress() noexcept {
            // 1. Physical Burst Acquisition: Replaces manual boilerplate 
            // with a concept-verified acquisition trace.
            alignas(64) rte_mbuf* hardware_batch[MaxBurstSize];
            const uint16_t rx_count = rte_eth_rx_burst(port_id_, queue_id_, hardware_batch, static_cast<uint16_t>(MaxBurstSize));

            if (SL_EXPECT_TRUE(rx_count == 0)) return;

            // 2. Hardware Prefetch Pipeline: Prime the L1-D for upcoming protocol parsing.
            for (uint16_t i = 0; i < rx_count; ++i) {
                rte_prefetch0(rte_pktmbuf_mtod(hardware_batch[i], void*));
            }

            alignas(64) typename InboundConduit::value_type token_batch[MaxBurstSize]; // Use conduit's actual value_type

            // 3. Physical Token Rematerialization: Vectorized pointer-to-token transcoding.
            if constexpr (std::is_same_v<typename InboundConduit::value_type, core::tagged_pointer>) {
                uint16_t i = 0;
#if defined(__AVX512F__)
                const __m512i v_tags = _mm512_set1_epi64(static_cast<int64_t>(transport::raw_tcp_frame::ID) << 48);
                for (; i + 8 <= rx_count; i += 8) {
                    __m512i v_mbufs = _mm512_loadu_si512(&hardware_batch[i]);
                    // pack: (ID << 48) | (ptr & 0x0000FFFFFFFFFFFF)
                    __m512i v_tokens = _mm512_or_si512(v_tags, _mm512_and_si512(v_mbufs, _mm512_set1_epi64(0x0000FFFFFFFFFFFF)));
                    _mm512_storeu_si512(&token_batch[i], v_tokens);
                }
#endif
                for (; i < rx_count; ++i) {
                    token_batch[i] = core::tagged_pointer::pack(transport::raw_tcp_frame::ID, hardware_batch[i]);
                }
            } else {
                for (uint16_t i = 0; i < rx_count; ++i) {
                    auto packed = core::tagged_pointer::pack(transport::raw_tcp_frame::ID, hardware_batch[i]);
                    if constexpr (requires { token_batch[i].embed_symmetry(0); }) {
                        token_batch[i] = typename InboundConduit::value_type(transport::raw_tcp_frame::ID, packed);
                        token_batch[i].embed_symmetry(static_cast<uint32_t>(rx_total_ingressed_ + i));
                    } else if constexpr (requires { token_batch[i].anchor_to_lightcone(0); }) {
                        token_batch[i] = typename InboundConduit::value_type(packed);
                        token_batch[i].anchor_to_lightcone(rx_total_ingressed_ + i);
                    } else {
                        token_batch[i] = static_cast<typename InboundConduit::value_type>(hardware_batch[i]);
                    }
                }
            }
            rx_total_ingressed_ += rx_count;

            std::size_t pushed = ingress_conduit_.push_batch(token_batch, static_cast<std::size_t>(rx_count));
            if (SL_EXPECT_FALSE(pushed < rx_count)) {
                for (std::size_t i = pushed; i < rx_count; ++i) rte_pktmbuf_free(hardware_batch[i]);
            }
        }

        /**
         * @brief Fused Egress Orchestrator.
         * @details Eliminates standard skeleton allocation-then-attach loops.
         * Utilizes an optimized Fused Lifecycle Strategy to bind Slab memory 
         * to hardware rings without intermediate buffer management.
         */
        SLAB_HOT void poll_egress() noexcept {
            if (SL_EXPECT_FALSE(!mbuf_pool_)) return;

            alignas(64) typename OutboundConduit::value_type batch[MaxBurstSize];
            const std::size_t count = egress_conduit_.pop_batch(batch, static_cast<std::size_t>(MaxBurstSize));
            if (SL_EXPECT_TRUE(count == 0)) return;

            rte_mbuf* tx_pkts[MaxBurstSize];
            if (SL_EXPECT_FALSE(rte_pktmbuf_alloc_bulk(mbuf_pool_, tx_pkts, static_cast<unsigned>(count)) != 0)) {
                egress_conduit_.revert_batch(batch, static_cast<std::size_t>(count));
                return;
            }

            uint16_t valid_mbufs = 0;
            for (std::size_t i = 0; i < count; ++i) {
                auto* raw_packet = reinterpret_cast<transport::raw_tcp_frame*>(batch[i].get_address());
                if (SL_EXPECT_FALSE(!raw_packet)) continue;

                // Structural Fusion: Attach descriptor directly to the Slab row
                rte_pktmbuf_attach_extbuf(tx_pkts[i], raw_packet->data, reinterpret_cast<size_t>(raw_packet->data), 
                                          raw_packet->payload_length, &shinfo_);

                tx_pkts[i]->data_off = 0;
                tx_pkts[i]->data_len = static_cast<uint16_t>(raw_packet->payload_length);
                tx_pkts[i]->pkt_len  = static_cast<uint32_t>(raw_packet->payload_length);
                tx_pkts[valid_mbufs++] = tx_pkts[i];
            }

            const uint16_t sent = rte_eth_tx_burst(port_id_, queue_id_, tx_pkts, valid_mbufs);
            if (SL_EXPECT_FALSE(sent < valid_mbufs)) {
                for (uint16_t i = sent; i < valid_mbufs; ++i) rte_pktmbuf_free(tx_pkts[i]);
            }
        }
    };
}
