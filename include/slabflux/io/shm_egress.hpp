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

#include "slabflux/bridge/shm_bridge.hpp"
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/wire_frame_lsn.hpp" // For slabflux::core::wire_frame_lsn
#include "slabflux/core/non_temporal_writer.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include <cstring>

namespace slabflux::io {

    template <typename EventType, size_t Capacity, typename ConduitType, typename PoolType>
    class alignas(64) shm_egress {
        ConduitType& tx_conduit_;
        PoolType& mem_pool_;
        bridge::shm_bridge<EventType, Capacity> bridge_;
        slabflux::core::spsc_ring_conduit<EventType, Capacity>* wire_{nullptr};

    public:
        shm_egress(const std::string& shm_name, ipc_role role, ConduitType& tx_conduit, PoolType& pool)
            : tx_conduit_(tx_conduit), mem_pool_(pool), bridge_(shm_name, role) {
            wire_ = &bridge_.wire();
        }

        SLAB_HOT void poll() noexcept {
            EventType* batch[32];
            size_t count = tx_conduit_.pop_batch(batch, 32);

            if (SL_EXPECT_TRUE(count > 0)) {
                // Cache the wire reference to minimize indirection overhead
                auto* wire = wire_;
                
                // Amortized Reservation: Check the ring capacity ONCE
                size_t actual_reserved = wire->reserve_batch_space(count);

                if (SL_EXPECT_FALSE(actual_reserved < count)) {
                    if constexpr (requires { tx_conduit_.revert_batch(batch + actual_reserved, count - actual_reserved); }) {
                        tx_conduit_.revert_batch(batch + actual_reserved, count - actual_reserved);
                    }
                }

                // INDIRECTION HOISTING: Cache tail horizon to stabilize the pipeline
                const size_t tail_idx = wire->get_ingress_cursor() & (Capacity - 1);
                
                if (SL_EXPECT_TRUE(tail_idx + actual_reserved <= Capacity)) {
                    EventType* __restrict__ curr_dst = wire->get_reserved_slot(0);

                    for (size_t i = 0; i < actual_reserved; ++i) {
                        if (i + 1 < actual_reserved) _mm_prefetch(reinterpret_cast<const char*>(batch[i+1]), _MM_HINT_T0);
                        
                        const EventType* __restrict__ src = batch[i];

                        if constexpr (requires { src->payload_length; src->data; }) {
                            // REGISTER-HEADER FUSION: Eliminates STLF stalls on meta
                            const uint64_t meta = (static_cast<uint64_t>(src->connection_id) << 32) | src->payload_length;
                            *reinterpret_cast<uint64_t*>(curr_dst) = meta;

                            const size_t bytes = src->payload_length;
                            #if defined(__AVX512F__)
                                const __m512i* v_src = reinterpret_cast<const __m512i*>(src->data);
                                __m512i* v_dst = reinterpret_cast<__m512i*>(curr_dst->data);
                                for(size_t v = 0; v < (bytes + 63) / 64; ++v) 
                                    _mm512_storeu_si512(&v_dst[v], _mm512_loadu_si512(&v_src[v]));
                            #else
                                std::memcpy(curr_dst->data, src->data, bytes);
                            #endif
                        } else {
                            std::memcpy(curr_dst, src, sizeof(EventType));
                        }
                        curr_dst++; // Arithmetic ADD vs index IMUL
                    }
                } else {
                    // WRAP PATH: Standard scalar fallback
                    for (size_t i = 0; i < actual_reserved; ++i) {
                        EventType* dest = wire->get_reserved_slot(i);
                        if constexpr (requires { batch[i]->payload_length; batch[i]->data; }) {
                            std::memcpy(dest, batch[i], batch[i]->payload_length + offsetof(EventType, data));
                        } else {
                            std::memcpy(dest, batch[i], sizeof(EventType));
                        }
                    }
                }

                // ATOMIC CONSOLIDATION: Return buffers to the pool meta first,
                // then publish the final commitment to the bridge. This combines
                // our local bookkeeping with the consumer's visibility signal.
                if constexpr (requires { mem_pool_.release_batch_no_publish(batch, actual_reserved); }) {
                    mem_pool_.release_batch_no_publish(batch, actual_reserved);
                    mem_pool_.publish_free_tail();
                } else {
                    for (size_t j = 0; j < actual_reserved; ++j) mem_pool_.release(batch[j]);
                }

                wire->commit_n(actual_reserved);
            }
        }
    };
} // namespace slabflux::io
