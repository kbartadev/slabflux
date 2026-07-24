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
#include <string_view>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp" // For SLAB_FORCE_INLINE
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp" // Changed from egress_envelope.hpp

namespace slabflux::transport {

    /**
     * @brief Zero-Allocation Outbound L7 Stream Fragmentation Policy.
     * @details Slices large application frames lineally into hardware-aligned MTU packets.
     * @tparam ConduitType The outbound network ring buffer conduit (SPSC, MPMC, or Vector block).
     * @tparam PoolType The dynamic pool supplying physical memory for outbound wire envelopes.
     */
    template <typename ConduitType, typename PoolType>
    class tcp_stream_fragmenter {
    private:
        ConduitType& egress_conduit_;
        PoolType& pool_;

        static constexpr bool IsVectorConduit = requires(ConduitType c, core::tagged_pointer* b, std::size_t s) {
            c.push_batch(b, s);
        };

    public:
        explicit tcp_stream_fragmenter(ConduitType& conduit, PoolType& pool) noexcept 
            : egress_conduit_(conduit), pool_(pool) {}

        /**
         * @brief Linearly fragments an arbitrary memory view and streams slices straight into the ring.
         * @param connection_id The target network socket descriptor tracking context.
         * @param type_id The L7 type identifier for remote machine demuxer matching.
         * @param large_payload The continuous raw data view block to slice apart (HTML, JSON, binary structures).
         */
        inline void fragment_and_push(uint32_t connection_id, uint16_t type_id, std::string_view large_payload) noexcept {
            const char* data_ptr = large_payload.data();
            std::size_t bytes_remaining = large_payload.size();
            
            // Local stack tracking structures for high-speed vectorized burst flushing
            alignas(64) core::tagged_pointer token_batch[16];
            std::size_t batch_idx = 0;

            while (bytes_remaining > 0) {
                std::size_t slice_size = (bytes_remaining > 1460) ? 1460 : bytes_remaining;

                auto* packet = pool_.make_raw();
                while (SL_EXPECT_FALSE(!packet)) {
                    // Hardware backpressure: flush any existing tokens to the conduit 
                    // to make room in the mempool before spin-waiting.
                    if (batch_idx > 0) {
                        flush_to_conduit(token_batch, batch_idx);
                        batch_idx = 0;
                    }
                    #if defined(__x86_64__) || defined(_M_X64)
                    _mm_pause();
                    #endif
                    packet = pool_.make_raw();
                }

                packet->header.connection_id = connection_id;
                packet->header.type_id = type_id;
                packet->header.frame_length = static_cast<uint32_t>(slice_size);
                packet->header.is_last = (bytes_remaining == slice_size) ? 1 : 0;

                std::memcpy(packet->payload, data_ptr, slice_size);

                token_batch[batch_idx] = core::tagged_pointer::pack(raw_egress_packet::ID, packet);

                data_ptr += slice_size;
                bytes_remaining -= slice_size;
                batch_idx++;

                // Flush vector run immediately if the local batch stack hits capacity
                if (batch_idx == 16) {
                    flush_to_conduit(token_batch, batch_idx);
                    batch_idx = 0;
                }
            }

            // Sweep out any trailing fractional packet allocations left on the stack frame
            if (batch_idx > 0) {
                flush_to_conduit(token_batch, batch_idx);
            }
        }

    private:
        /**
         * @brief Inline hardware injector logic matching your baseline system laws.
         */
        SLAB_FORCE_INLINE void flush_to_conduit(core::tagged_pointer* tokens, std::size_t count) noexcept {
            if constexpr (IsVectorConduit) {
                std::size_t pushed = 0;
                while (pushed < count) {
                    pushed += egress_conduit_.push_batch(tokens + pushed, count - pushed);
                    if (pushed < count) {
                        #if defined(__x86_64__) || defined(_M_X64)
                        _mm_pause();
                        #endif
                    }
                }
            } else {
                for (std::size_t i = 0; i < count; ++i) {
                    while (!egress_conduit_.try_push(tokens[i])) {
                        #if defined(__x86_64__) || defined(_M_X64)
                        _mm_pause();
                        #endif
                    }
                }
            }
        }
    };

} // namespace slabflux::transport