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
#include <tuple>
#include <string_view>
#include <type_traits> // For std::is_pointer_v
#include <cstring>
#include "slabflux/core/wire_frame_lsn.hpp" // For InboundEnvelope if it's a wire_frame_lsn
#include "slabflux/transport/session_context.hpp"
#include "slabflux/transport/baremetal_parser.hpp"

namespace slabflux::transport {

    /**
     * @brief Pure Stateless Layer-4/5 Stream Execution Policy.
     * @details Completely decoupled from specific L7 structures. Works with any protocol parser.
     * @tparam ProtocolParser The static hardware-accelerated DFA parser class (e.g., baremetal_parser).
     * @tparam ProtocolState The tracking state container matching the parser (e.g., http_frame).
     * @tparam BusinessHandler Downstream domain engine handling complete frames.
     * @tparam InboundEnvelope The structural event layout dispatched by the demuxer core.
     */
    template <typename ProtocolParser, typename ProtocolState, typename BusinessHandler, typename InboundEnvelope, std::size_t BufferCapacity = 524288>
    class tcp_stream_session {
    public:
        using event_types = std::tuple<InboundEnvelope>;

    private:
        BusinessHandler& business_logic_;

    public:
        explicit tcp_stream_session(BusinessHandler& handler) noexcept 
            : business_logic_(handler) {}

        /** @brief Linear Stream Reassembly. */
        SLAB_FORCE_INLINE void process_stream(session_context<ProtocolState, BufferCapacity>& ctx, InboundEnvelope& frame) noexcept {
            const char* incoming_data = nullptr;
            std::size_t incoming_length = 0;

            // Extract buffer maps at compile-time via template constraint checks
            if constexpr (requires { frame.data; frame.payload_length; }) {
                incoming_data = frame.data;
                incoming_length = frame.payload_length;
            } else if constexpr (requires { frame.payload(); frame.size(); }) {
                incoming_data = reinterpret_cast<const char*>(frame.payload());
                incoming_length = frame.size();
            } else {
                static_assert(std::is_pointer_v<decltype(frame.ptr())>, "InboundEnvelope must expose a contiguous memory pointer");
                incoming_data = reinterpret_cast<const char*>(frame.ptr());
                incoming_length = frame.length();
            }

            // 1. BOUNDARY STABILIZATION (Matrix Realignment)
            if (SL_EXPECT_FALSE(ctx.read_offset + ctx.accumulated_bytes + incoming_length > BufferCapacity)) {
                stabilize_matrix_boundary(ctx);
                if (SL_EXPECT_FALSE(ctx.accumulated_bytes + incoming_length > BufferCapacity)) {
                    ctx.accumulated_bytes = 0;
                    return;
                }
            }

            // 2. SIGNAL INGESTION
            std::memcpy(ctx.buffer + ctx.read_offset + ctx.accumulated_bytes, incoming_data, incoming_length);
            ctx.accumulated_bytes += incoming_length;

            // 3. TRANSITION CRAWL
            while (ctx.accumulated_bytes > 0) {
                if (!apply_transition_step(ctx)) {
                    // Chaos Guard: Rejection for zombie fragments.
                    // Rejects data if a truncated line consumes > 40% of the session buffer.
                    if (SL_EXPECT_FALSE(ctx.accumulated_bytes > (BufferCapacity * 2 / 5))) {
                        ctx.clear();
                    }
                    break;
                }
            }

            if (ctx.accumulated_bytes == 0) {
                ctx.read_offset = 0;
            }
        }

    private:
        /** @brief Matrix Realignment: Compresses the linear buffer to reclaim memory space. */
        SLAB_FORCE_INLINE void stabilize_matrix_boundary(session_context<ProtocolState, BufferCapacity>& ctx) noexcept {
            if (ctx.accumulated_bytes > 0 && ctx.read_offset > 0) {
                std::memmove(ctx.buffer, ctx.buffer + ctx.read_offset, ctx.accumulated_bytes);
            } else {
                ctx.accumulated_bytes = 0;
            }
            ctx.read_offset = 0;
            // RESTORE: reset_protocol_state is MANDATORY here. Any pointers or string_views 
            // inside the protocol_state pointing into ctx.buffer are invalidated by 
            // the memmove. Resetting ensures the parser starts fresh on the shifted data 
            // rather than accessing dangling pointers.
            reset_protocol_state(ctx.protocol_state);
        }

        /** @brief Executes a single state transition against the Protocol Parser. */
        SLAB_FORCE_INLINE bool apply_transition_step(session_context<ProtocolState, BufferCapacity>& ctx) noexcept {
            std::string_view window(ctx.buffer + ctx.read_offset, ctx.accumulated_bytes);
            parser_status status = ProtocolParser::parse(window, ctx.protocol_state);

            if (status == parser_status::OK) {
                business_logic_.on_request(ctx.protocol_state);
                const std::size_t consumed = ctx.protocol_state.total_bytes_consumed;
                ctx.read_offset += consumed;
                ctx.accumulated_bytes -= consumed;
                reset_protocol_state(ctx.protocol_state);
                return true;
            }
            
            if (status == parser_status::ERROR) ctx.clear();
            return false;
        }

        SLAB_FORCE_INLINE void reset_protocol_state(ProtocolState& state) noexcept {
            if constexpr (requires(ProtocolState s) { s.reset(); }) state.reset();
            else state = ProtocolState{};
        }
    };

} // namespace slabflux::transport