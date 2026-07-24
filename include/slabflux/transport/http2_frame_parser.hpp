/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file http2_frame_parser.hpp
 * @brief High-performance, zero-allocation HTTP/2.0 Binary Frame Parser.
 */

#pragma once

#include <cstdint>
#include <string_view>
#include "slabflux/core/endian.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::transport {

    enum class http2_frame_type : uint8_t {
        DATA          = 0x0,
        HEADERS       = 0x1,
        PRIORITY      = 0x2,
        RST_STREAM    = 0x3,
        SETTINGS      = 0x4,
        PUSH_PROMISE  = 0x5,
        PING          = 0x6,
        GOAWAY        = 0x7,
        WINDOW_UPDATE = 0x8,
        CONTINUATION  = 0x9
    };

    namespace http2_flags {
        constexpr uint8_t ACK = 0x1;
        constexpr uint8_t END_STREAM = 0x1;
        constexpr uint8_t END_HEADERS = 0x4;
        constexpr uint8_t PADDED = 0x8;
        constexpr uint8_t PRIORITY = 0x20;
    }

    // 9-Octet Binary Frame Header
    struct http2_frame_header {
        uint32_t length;
        http2_frame_type type;
        uint8_t flags;
        uint32_t stream_id;
    };

    class http2_frame_parser {
    public:
        /**
         * @brief Parses the 9-byte HTTP/2 frame header.
         * @return True on success, false if the buffer is incomplete.
         */
        SLAB_HOT static bool parse_header(std::string_view buffer, http2_frame_header& out_header) noexcept {
            if (SL_UNLIKELY(buffer.size() < 9)) return false;

            const auto* ptr = reinterpret_cast<const uint8_t*>(buffer.data());

            // Length is a 24-bit integer
            out_header.length = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];
            out_header.type = static_cast<http2_frame_type>(ptr[3]);
            out_header.flags = ptr[4];
            
            // Stream ID is a 31-bit integer (MSB is reserved)
            out_header.stream_id = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(ptr + 5)) & 0x7FFFFFFF;

            return true;
        }
    };

} // namespace slabflux::transport