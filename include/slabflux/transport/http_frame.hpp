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

 *
 *
 * @file http_frame.hpp
 * @brief Aligned, Zero-Copy HTTP Frame Contract.
 */

#pragma once

#include <string_view>
#include <immintrin.h> // For _mm_pause
#include <array>
#include <cstddef>

namespace slabflux::transport {

    enum class parser_state : uint8_t {
        METHOD, URI, VERSION, 
        HEADER_KEY, HEADER_VAL, 
        BODY, CHUNK_SIZE, CHUNK_DATA,
        TRAILER,
        DONE
    };

    struct alignas(16) http_header {
        std::string_view key;
        std::string_view value;
    };

    /**
     * @brief Structured HTTP Request Frame.
     * @details Strictly aligned to 64 bytes to prevent cache-line splits 
     * during promotion to the domain logic.
     */
    struct alignas(64) http_frame {
        std::string_view method;
        std::string_view uri;
        std::string_view version;
        std::array<http_header, 32> headers;
        size_t header_count{0};
        std::string_view body;
        bool is_chunked{false};
        bool has_content_length{false};
        size_t content_length{0};
        size_t accumulated_body_size{0};
        size_t total_bytes_consumed{0};

        // Absolute Resumability State
        parser_state internal_state{parser_state::METHOD};
        size_t consumed_in_state{0};

        /** @brief Prepares the frame for a fresh request on a new connection. */
        void reset() noexcept {
            method = {}; uri = {}; version = {}; body = {};
            header_count = 0;
            is_chunked = false;
            has_content_length = false;
            content_length = 0;
            accumulated_body_size = 0;
            total_bytes_consumed = 0;
            internal_state = parser_state::METHOD;
            consumed_in_state = 0;
        }

        // Limit: Prevent DoS via oversized payloads
        static constexpr size_t MAX_BODY_SIZE = 16 * 1024 * 1024; // 16MB
        static constexpr size_t MAX_HEADERS = 32;
        
        // Token Limits: Prevent over-scanning malformed lines
        static constexpr size_t MAX_METHOD_SIZE = 16;
        static constexpr size_t MAX_URI_SIZE = 2048;
        static constexpr size_t MAX_HEADER_FIELD_SIZE = 4096;
    };

    static_assert(alignof(http_frame) == 64, "http_frame must be cache-line aligned");
    static_assert(alignof(http_header) == 16, "http_header must be 16-byte aligned");

} // namespace slabflux::transport