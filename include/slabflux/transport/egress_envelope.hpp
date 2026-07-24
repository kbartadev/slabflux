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
#include <array>
#include "slabflux/transport/session_context.hpp"
#include "slabflux/transport/baremetal_parser.hpp"

namespace slabflux::transport {

    /**
     * @brief Stateless L4/L5 Stream Boundary Defragmentation Engine.
     * @details Fully integrates with core::demuxer fold expressions. Agnostic to underlying network I/O runtimes.
     * @tparam ProtocolParser The static hardware-accelerated DFA parser class (e.g., baremetal_parser).
     * @tparam ProtocolState The tracking state container required by the parser target (e.g., http_frame).
     * @tparam NextHandler The immediate downstream business step in the processing matrix.
     * @tparam InboundEnvelope The structural network envelope dispatched by the demuxer core.
     */
    template <typename ProtocolParser, typename ProtocolState, typename NextHandler, typename InboundEnvelope>
    class tcp_stream_defragmenter {
    public:
        // Expose interest to satisfy the compile-time demuxer tuple registration
        using event_types = std::tuple<InboundEnvelope>;

    private:
        NextHandler& next_layer_;

        // Statically allocate the storage registry directly on the thread's local execution stack
        static inline thread_local std::array<session_context<ProtocolState, 524288>, 1024> local_registry_{};

    public:
        explicit tcp_stream_defragmenter(NextHandler& next_layer) noexcept 
            : next_layer_(next_layer) {}

        /**
         * @brief Primary execution entry point invoked directly by the core demuxer.
         * @return false always to comply with standard non-forwarding pipeline filters.
         */
        inline bool on(InboundEnvelope& frame) noexcept {
            const char* incoming_data = nullptr;
            std::size_t incoming_length = 0;
            std::size_t connection_id = 0;

            // Compile-time trait extraction maps any network interface architecture with zero overhead
            if constexpr (requires { frame.data; frame.payload_length; frame.connection_id; }) {
                // Route A: Standard binary wire wrapper framing format (io_uring / socket buffers)
                incoming_data = frame.data;
                incoming_length = frame.payload_length;
                connection_id = frame.connection_id;
            } else if constexpr (requires { frame.payload(); frame.size(); frame.session_index(); }) {
                // Route B: Zero-copy kernel-bypass descriptor format (DPDK mbufs / AF_XDP frames)
                incoming_data = reinterpret_cast<const char*>(frame.payload());
                incoming_length = frame.size();
                connection_id = frame.session_index();
            } else {
                // Route C: Structural fallback for raw memory descriptors passing through conduits
                static_assert(std::is_pointer_v<decltype(frame.ptr())>, "InboundEnvelope must provide a contiguous memory pointer");
                incoming_data = reinterpret_cast<const char*>(frame.ptr());
                incoming_length = frame.length();
                connection_id = frame.id();
            }

            // O(1) direct slot index calculation pinned to this thread's private memory map
            auto& ctx = local_registry_[connection_id % 1024];

            // Memory Guard & Line Wrap Rotation Check
            if (__builtin_expect(!!(ctx.read_offset + ctx.accumulated_bytes + incoming_length > 524288), 0)) {
                if (ctx.accumulated_bytes > 0 && ctx.read_offset > 0) {
                    std::memmove(ctx.buffer, ctx.buffer + ctx.read_offset, ctx.accumulated_bytes);
                } else {
                    ctx.accumulated_bytes = 0;
                }
                ctx.read_offset = 0;
                
                ctx.clear();

                if (__builtin_expect(!!(ctx.accumulated_bytes + incoming_length > 524288), 0)) {
                    ctx.accumulated_bytes = 0;
                    return false;
                }
            }

            // Direct append to the current active write frontier
            std::memcpy(ctx.buffer + ctx.read_offset + ctx.accumulated_bytes, incoming_data, incoming_length);
            ctx.accumulated_bytes += incoming_length;

            // Zero-copy extraction crawl loop
            while (ctx.accumulated_bytes > 0) {
                std::string_view stream_window(ctx.buffer + ctx.read_offset, ctx.accumulated_bytes);

                // Run your incremental state machine directly against the thread-isolated context block
                parser_status status = ProtocolParser::parse(stream_window, ctx.protocol_state);

                if (status == parser_status::OK) {
                    // Forward directly into the next business handler's execution signature
                    if constexpr (requires { next_layer_.on(ctx.protocol_state); }) {
                        if constexpr (std::is_same_v<decltype(next_layer_.on(ctx.protocol_state)), bool>) {
                            static_cast<void>(next_layer_.on(ctx.protocol_state));
                        } else {
                            next_layer_.on(ctx.protocol_state);
                        }
                    } else {
                        next_layer_.on_request(ctx.protocol_state);
                    }

                    std::size_t consumed = ctx.protocol_state.total_bytes_consumed;
                    
                    ctx.read_offset += consumed;
                    ctx.accumulated_bytes -= consumed;

                    ctx.protocol_state.reset();
                } 
                else if (status == parser_status::INCOMPLETE) {
                    break;
                } 
                else if (status == parser_status::ERROR) {
                    ctx.clear();
                    break;
                }
            }

            if (ctx.accumulated_bytes == 0) {
                ctx.read_offset = 0;
            }

            return false;
        }
    };

} // namespace slabflux::transport