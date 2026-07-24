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

#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <atomic>

#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    template <typename EventType, size_t Capacity, typename ConduitType, typename PoolType>
    class alignas(64) dpdk_egress {
    private:
        ConduitType& tx_conduit_;
        PoolType&    mem_pool_;
        rte_mempool* mbuf_pool_;
        uint16_t     port_id_;
        uint16_t     queue_id_;

        static void slab_free_callback(void* addr, void* opaque) noexcept {
            // The opaque pointer now points to the slot_meta, not the pool directly
            auto* sm_ptr = static_cast<slot_meta*>(opaque);
            
            // Return the base address of the slab slot to the pool
            sm_ptr->pool->release(static_cast<EventType*>(addr));
        }

        /** @brief Sovereignty Wrapper: Ensures per-slot meta for extbuf. */
        struct slot_meta {
            rte_mbuf_ext_shared_info shinfo;
            PoolType* pool;
            // Add a refcount to track if this specific slot is in flight
            // This is crucial for ensuring the shinfo is not reused while active
            std::atomic<uint16_t> ref_count{0}; 
        };
        std::unique_ptr<slot_meta[]> slot_meta_;

    public:
        explicit dpdk_egress(ConduitType& tx_conduit, PoolType& pool, rte_mempool* mbuf_pool, uint16_t port, uint16_t queue) noexcept
        : tx_conduit_(tx_conduit), mem_pool_(pool), mbuf_pool_(mbuf_pool), port_id_(port), queue_id_(queue)
        {
            // Hardening: Pre-allocate meta to match NIC ring bounds
            slot_meta_ = std::make_unique<slot_meta[]>(Capacity);
            for (size_t i = 0; i < Capacity; ++i) {
                slot_meta_[i].shinfo.free_cb = slab_free_callback;
                slot_meta_[i].shinfo.fcb_opaque = static_cast<void*>(&slot_meta_[i]);
                slot_meta_[i].pool = &mem_pool_;
                // Initialize refcount to 0, it will be incremented when attached
                rte_mbuf_ext_refcnt_set(&slot_meta_[i].shinfo, 0);
            }
        }

        ~dpdk_egress() noexcept = default;

        /**
         * @brief True zero‑copy DPDK egress hot path.
         * @details Orchestrates vectorized burst transmission. Eliminates 
         * skeleton allocation-then-attach overhead via direct buffer attachment.
         */
        SLAB_HOT void poll() noexcept {
            if (SL_EXPECT_FALSE(!mbuf_pool_)) return;

            alignas(64) EventType* batch[32];
            const std::size_t count = tx_conduit_.pop_batch(batch, 32);
            if (SL_EXPECT_TRUE(count == 0)) return;

            rte_mbuf* tx_pkts[32];
            if (SL_EXPECT_FALSE(rte_pktmbuf_alloc_bulk(mbuf_pool_, tx_pkts, static_cast<unsigned>(count)) != 0)) {
                // Hardware Satiation: Revert batch to conduit to maintain FIFO integrity
                if constexpr (requires { tx_conduit_.revert_batch(batch, count); }) {
                    tx_conduit_.revert_batch(batch, count);
                }
                return;
            }

            uint16_t valid_count = 0;
            for (size_t i = 0; i < count; ++i) {
                EventType* raw_packet = batch[i];
                if (SL_EXPECT_FALSE(!raw_packet)) {
                    rte_pktmbuf_free(tx_pkts[i]);
                    continue;
                }

                void* slot_addr = static_cast<void*>(raw_packet);
                void* data_ptr = slot_addr;
                uint32_t payload_len = sizeof(EventType);

                // Microarchitectural Optimization: If the event is a standard Wire Protocol frame,
                // we attach only the payload and use the internal length meta.
                if constexpr (requires { raw_packet->data; raw_packet->payload_length; }) {
                    data_ptr = raw_packet->data;
                    payload_len = raw_packet->payload_length;
                }

                uint16_t data_offset = static_cast<uint16_t>(reinterpret_cast<char*>(data_ptr) - reinterpret_cast<char*>(slot_addr));

                // Re-derive the slot meta index from the pool topology
                uint32_t pool_idx = mem_pool_.get_index(raw_packet);
                // Use pool_idx directly, assuming Capacity is the pool's capacity
                auto& sm = slot_meta_[pool_idx % Capacity]; 
                // Increment refcount for this specific slot's shinfo
                rte_mbuf_ext_refcnt_update(&sm.shinfo, 1);

                // Structural Fusion: Attach hardware descriptor directly to Slab residency.
                rte_pktmbuf_attach_extbuf(
                    tx_pkts[i],
                    slot_addr,
                    reinterpret_cast<rte_iova_t>(slot_addr),
                    sizeof(EventType),
                    &sm.shinfo
                );

                tx_pkts[i]->data_off = data_offset;
                tx_pkts[i]->data_len = static_cast<uint16_t>(payload_len);
                tx_pkts[i]->pkt_len  = static_cast<uint32_t>(payload_len);
                tx_pkts[valid_count++] = tx_pkts[i];
            }

            const uint16_t sent = rte_eth_tx_burst(port_id_, queue_id_, tx_pkts, valid_count);
            if (SL_EXPECT_FALSE(sent < valid_count)) {
                for (uint16_t i = sent; i < valid_count; ++i) rte_pktmbuf_free(tx_pkts[i]);
            }
        }
    };
}
