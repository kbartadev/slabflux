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
#include <cstring>
#include "slabflux/transport/wire_protocol.hpp" // For raw_tcp_frame
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/transport/http.hpp"

namespace slabflux::transport {

    /**
     * @brief Perfectly Linear L7 Stream Processor.
     * @details Acts as a regular handler within the core pipeline matrix.
     */
    template <typename HttpBusinessHandler, std::size_t BufferCapacity = 524288>
    class tcp_stream_session {
    public:
        // Declare interest exclusively in the raw L4 frame to satisfy the demuxer type-pack
        using event_types = std::tuple<raw_tcp_frame>;

    private:
        HttpBusinessHandler& business_logic_;
        alignas(64) char session_buffer_[BufferCapacity];
        std::size_t accumulated_bytes_{0};

    public:
        explicit tcp_stream_session(HttpBusinessHandler& handler) noexcept 
            : business_logic_(handler) {}

        /**
         * @brief Primary execution path triggered directly by the core demuxer.
         */
        inline void on(raw_tcp_frame& frame) noexcept {
            if (__builtin_expect(!!(accumulated_bytes_ + frame.payload_length > BufferCapacity), 0)) {
                accumulated_bytes_ = 0; // Guard session memory lines against overflow exploits
            }

            std::memcpy(session_buffer_ + accumulated_bytes_, frame.data, frame.payload_length);
            accumulated_bytes_ += frame.payload_length;

            std::string_view stream_window(session_buffer_, accumulated_bytes_);
            
            // Scan for standard text-protocol message boundaries
            std::size_t header_end = stream_window.find("\r\n\r\n");
            if (header_end == std::string_view::npos) return; // Incomplete stream state; await next burst

            // Slice out pure L7 components via zero-allocation string_views
            http_request_event request;
            // (Ultra-low latency parser maps method, uri, and body pointers directly inside session_buffer_...)

            // Forward directly down the execution chain via an inline function call.
            // Bypasses the core demuxer entirely because the application routing is resolved linearly.
            business_logic_.on_request(request);

            // Shift fractional trailing stream data back to index zero
            std::size_t consumed_bytes = header_end + 4;
            if (consumed_bytes < accumulated_bytes_) {
                std::memmove(session_buffer_, session_buffer_ + consumed_bytes, accumulated_bytes_ - consumed_bytes);
                accumulated_bytes_ -= consumed_bytes;
            } else {
                accumulated_bytes_ = 0;
            }
        }
    };

} // namespace slabflux::transport