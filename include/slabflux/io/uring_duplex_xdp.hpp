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

#include <sys/socket.h>
#include <atomic>
#include <cstdint>
#include "slabflux/io/xdp_shim.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"

namespace slabflux::io {

    /**
     * @brief Unified AF_XDP Zero-Copy Duplex Engine.
     * @details Orchestrates 4 rings (Fill, Comp, RX, TX) in a single vectorized poll loop.
     * @tparam InboundConduit Conduit for reaped ingress tokens.
     * @tparam OutboundConduit Conduit for pending egress packets.
     * @tparam PoolType UMEM-backed block pool.
     */
    template <typename InboundConduit, typename OutboundConduit, typename PoolType, std::size_t RingEntries = 256>
    class alignas(64) uring_duplex_xdp {
        static_assert((RingEntries & (RingEntries - 1)) == 0, "XSK Ring Slots must be power-of-two");

    private:
        ::xsk_socket* xsk_{nullptr};
        ::xsk_ring_prod fill_ring_{};
        ::xsk_ring_cons comp_ring_{};
        ::xsk_ring_cons rx_ring_{};
        ::xsk_ring_prod tx_ring_{};

        InboundConduit&   ingress_conduit_;
        OutboundConduit&  egress_conduit_;
        PoolType&         mem_pool_;
        std::atomic<bool>& running_sentinel_;

        alignas(64) std::size_t tx_inflight_{0};
        alignas(64) uint64_t rx_total_ingressed_{0};
        char* cached_umem_base_{nullptr};
        static constexpr std::size_t BATCH_SIZE = 32;

    public:
        explicit uring_duplex_xdp(
            ::xsk_socket* xsk,
            ::xsk_ring_prod fill, ::xsk_ring_cons comp,
            ::xsk_ring_cons rx, ::xsk_ring_prod tx,
            InboundConduit& in_q, OutboundConduit& out_q,
            PoolType& pool, std::atomic<bool>& running
        ) noexcept 
        : xsk_(xsk), fill_ring_(fill), comp_ring_(comp), rx_ring_(rx), tx_ring_(tx),
          ingress_conduit_(in_q), egress_conduit_(out_q),
          mem_pool_(pool), running_sentinel_(running),
          cached_umem_base_(reinterpret_cast<char*>(mem_pool_.get_ptr(0))) {}

        /**
         * @brief Vectorized Duplex Runtime.
         * @details Executes a fused reap-poll-refill-dispatch cycle.
         */
        SLAB_HOT void poll_runtime() noexcept {
            if (SL_UNLIKELY(!xsk_)) return;

            // 1. RECLAIM TRANSMIT COMPLETIONS
            if (tx_inflight_ > 0) {
                uint32_t comp_idx = 0;
                uint32_t comp_count = xdp_shim::rx_peek(&comp_ring_, BATCH_SIZE, &comp_idx);
                if (comp_count > 0) {
                    using FramePtr = decltype(mem_pool_.make_raw());
                    FramePtr release_batch[BATCH_SIZE];
                    
                    #if defined(__GNUC__) && !defined(__clang__)
                    #pragma GCC unroll 32
                    #endif
                    for (uint32_t i = 0; i < comp_count; ++i) {
                        uint64_t addr = *xdp_shim::comp_addr(&comp_ring_, comp_idx + i);
                        release_batch[i] = reinterpret_cast<FramePtr>(cached_umem_base_ + addr);
                    }
                    
                    if constexpr (requires { mem_pool_.release_batch(release_batch, comp_count); }) {
                        mem_pool_.release_batch(release_batch, comp_count);
                    } else {
                        for (uint32_t i = 0; i < comp_count; ++i) mem_pool_.release(release_batch[i]);
                    }
                    xdp_shim::rx_release(&comp_ring_, comp_count);
                    tx_inflight_ -= comp_count;
                }
            }

            // 2. PROCESS INGRESS BURST
            uint32_t rx_idx = 0;
            uint32_t rx_count = xdp_shim::rx_peek(&rx_ring_, BATCH_SIZE, &rx_idx);
            if (rx_count > 0) {
                alignas(64) typename InboundConduit::value_type rx_tokens[BATCH_SIZE];
                alignas(64) uint64_t recycled_addrs[BATCH_SIZE];

                #if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 32
                #endif
                for (uint32_t i = 0; i < rx_count; ++i) {
                    const ::xdp_desc* desc = xdp_shim::rx_desc(&rx_ring_, rx_idx + i);
                    recycled_addrs[i] = desc->addr;
                    char* frame_ptr = cached_umem_base_ + desc->addr;
                    
                    auto* frame = reinterpret_cast<transport::raw_tcp_frame*>(frame_ptr);
                    frame->payload_length = static_cast<uint16_t>(desc->len);

                    auto packed = core::tagged_pointer::pack(0, frame);
                    if constexpr (requires { rx_tokens[i].embed_symmetry(0); }) {
                        rx_tokens[i] = typename InboundConduit::value_type(transport::raw_tcp_frame::ID, packed);
                        rx_tokens[i].embed_symmetry(static_cast<uint32_t>(rx_total_ingressed_ + i));
                    } else if constexpr (requires { rx_tokens[i].anchor_to_lightcone(0); }) {
                        rx_tokens[i] = typename InboundConduit::value_type(packed);
                        rx_tokens[i].anchor_to_lightcone(rx_total_ingressed_ + i);
                    } else {
                        rx_tokens[i] = packed;
                    }
                }
                xdp_shim::rx_release(&rx_ring_, rx_count);
                rx_total_ingressed_ += rx_count;

                // Batch push to logic pipeline
                std::size_t pushed = ingress_conduit_.push_batch(rx_tokens, rx_count);
                
                // 3. REFILL FILL RING (Recycle reaped RX buffers immediately)
                uint32_t fill_idx = 0;
                uint32_t reserved = xdp_shim::fill_reserve(&fill_ring_, rx_count, &fill_idx);
                if (reserved > 0) {
                    #if defined(__GNUC__) && !defined(__clang__)
                    #pragma GCC unroll 32
                    #endif
                    for (uint32_t i = 0; i < reserved; ++i) {
                        *xdp_shim::fill_addr(&fill_ring_, fill_idx + i) = recycled_addrs[i];
                    }
                    xdp_shim::fill_submit(&fill_ring_, reserved);
                }
            }

            // 4. PROCESS EGRESS BURST
            if (tx_inflight_ < RingEntries) {
                std::size_t tx_allowed = std::min(RingEntries - tx_inflight_, BATCH_SIZE);
                alignas(64) typename OutboundConduit::value_type tx_batch[BATCH_SIZE];
                std::size_t pulled = egress_conduit_.pop_batch(tx_batch, tx_allowed);

                if (pulled > 0) {
                    uint32_t tx_idx = 0;
                    uint32_t tx_res = xdp_shim::fill_reserve(&tx_ring_, static_cast<uint32_t>(pulled), &tx_idx);
                    
                    #if defined(__GNUC__) && !defined(__clang__)
                    #pragma GCC unroll 32
                    #endif
                    for (uint32_t i = 0; i < tx_res; ++i) {
                        void* addr = tx_batch[i].get_address();
                        auto* frame = static_cast<transport::raw_tcp_frame*>(addr);
                        
                        ::xdp_desc* desc = xdp_shim::tx_desc(&tx_ring_, tx_idx + i);
                        desc->addr = static_cast<char*>(frame->data) - cached_umem_base_;
                        desc->len  = frame->payload_length;
                    }
                    
                    xdp_shim::fill_submit(&tx_ring_, tx_res);
                    tx_inflight_ += tx_res;

                    // Revert unsubmitted items if ring saturated
                    if (tx_res < pulled) {
                        egress_conduit_.revert_batch(tx_batch + tx_res, pulled - tx_res);
                    }
                }
            }

            // 5. DOORBELLS (Amortized Kick)
            if (xdp_shim::fill_needs_wakeup(&fill_ring_) || xdp_shim::fill_needs_wakeup(&tx_ring_)) {
                xdp_shim::socket_kick(xsk_);
            }
        }
    };
}