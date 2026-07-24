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
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    template <typename EventType, size_t Capacity, typename PipelineLogic>
    class alignas(64) shm_ingress {
        bridge::shm_bridge<EventType, Capacity> bridge_;
        PipelineLogic& logic_;
        slabflux::core::spsc_ring_conduit<EventType, Capacity>* wire_{nullptr};
        alignas(64) uint64_t full_drop_count_{0};

    public:
        shm_ingress(const std::string& shm_name, ipc_role role, PipelineLogic& logic)
            : bridge_(shm_name, role), logic_(logic) {
            wire_ = &bridge_.wire();
        }

        /**
         * @brief Zero-Copy, Zero-Syscall IPC Ingestion.
         */
        SLAB_HOT void poll() noexcept {
            // Cache the wire reference to eliminate multi-layer indirection in the hot loop
            auto* wire = wire_;
            const size_t available = wire->available_to_peek();
            if (SL_EXPECT_FALSE(available == 0)) return;

            const size_t burst_count = (available < 32) ? available : 32;

            // INDIRECTION HOISTING: Cache cursors once to eliminate the modularity tax
            const size_t head_idx = wire->get_egress_cursor() & (Capacity - 1);
            
            if (SL_EXPECT_TRUE(head_idx + burst_count <= Capacity)) {
                // HIGH-VELOCITY LINEAR PATH: Pointer increments replace IMUL-heavy indexing
                const EventType* __restrict__ curr_ev = wire->get_peek_slot(0);

                if constexpr (requires { logic_.on_vector_batch(curr_ev, burst_count); }) {
                    if (SL_EXPECT_FALSE(!logic_.on_vector_batch(curr_ev, burst_count))) {
                        full_drop_count_ += burst_count;
                    }
                } else {
                    #pragma GCC unroll 32
                    for (std::size_t i = 0; i < burst_count; ++i) {
                        if (i + 3 < burst_count) [[likely]] 
                            _mm_prefetch(reinterpret_cast<const char*>(curr_ev + 3), _MM_HINT_T0);

                        std::size_t len = sizeof(EventType);
                        if constexpr (requires { curr_ev->payload_length; }) {
                            len = static_cast<std::size_t>(curr_ev->payload_length) + 8;
                        }

                        if (SL_EXPECT_FALSE(!logic_.on_raw_frame(*curr_ev, len))) {
                            full_drop_count_++;
                        }
                        curr_ev++; // Linear ADD vs index IMUL
                    }
                }
            } else {
                // WRAP PATH: Standard scalar fallback
                for (std::size_t i = 0; i < burst_count; ++i) {
                    const EventType* ev = wire->get_peek_slot(i);
                    size_t len = sizeof(EventType);
                    if constexpr (requires { ev->payload_length; }) len = ev->payload_length + 8;

                    if (SL_EXPECT_FALSE(!logic_.on_raw_frame(*ev, len))) full_drop_count_++;
                }
            }

            // Signal completion to the remote producer only once per burst
            wire->consume_n(burst_count);
        }
    };
} // namespace slabflux::io
