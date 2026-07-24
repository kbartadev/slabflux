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

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <charconv>
#include <system_error>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::transport {

    /**
     * @brief Zero-Allocation HTTP/1.1 Producer.
     * @details Operates directly on raw, cache-aligned memory boundaries (e.g., NIC DMA rings).
     * Performs $O(1)$ branch-minimal string appends to eliminate dynamically allocated
     * formatting overhead.
     */
    class http_producer {
    private:
        char* SLAB_RESTRICT buffer_;
        size_t capacity_;
        size_t cursor_{0};

        /**
         * @brief Branch-predicted capacity enforcer.
         */
        SLAB_FORCE_INLINE bool ensure_capacity(size_t required_bytes) const noexcept {
            return SL_EXPECT_TRUE(required_bytes <= capacity_ - cursor_);
        }

    public:
        /**
         * @brief Bind the producer to an outbound physical buffer.
         */
        explicit constexpr http_producer(char* SLAB_RESTRICT outbound_buffer, size_t max_capacity) noexcept
            : buffer_(outbound_buffer), capacity_(max_capacity) {}

        SLAB_FORCE_INLINE void reset() noexcept { cursor_ = 0; }
        SLAB_FORCE_INLINE size_t bytes_written() const noexcept { return cursor_; }
        SLAB_FORCE_INLINE std::string_view view() const noexcept { return {buffer_, cursor_}; }

        // ============================================================
        // REQUEST GENERATION
        // ============================================================

        SLAB_FORCE_INLINE bool start_request(std::string_view method, std::string_view uri) noexcept {
            // CRLF Injection Guard: Prevent HTTP Request Smuggling
            for (char c : method) if (SL_EXPECT_FALSE(c == '\r' || c == '\n')) return false;
            for (char c : uri) if (SL_EXPECT_FALSE(c == '\r' || c == '\n')) return false;

            // Pre-calculate exact bounds: method + ' ' + uri + ' ' + "HTTP/1.1\r\n"
            size_t req_len = method.size() + uri.size() + 12;
            if (SL_EXPECT_FALSE(req_len < method.size())) return false; // Overflow guard
            if (SL_EXPECT_FALSE(!ensure_capacity(req_len))) return false;
            
            std::memcpy(buffer_ + cursor_, method.data(), method.size());
            cursor_ += method.size();
            buffer_[cursor_++] = ' ';
            
            std::memcpy(buffer_ + cursor_, uri.data(), uri.size());
            cursor_ += uri.size();
            
            std::memcpy(buffer_ + cursor_, " HTTP/1.1\r\n", 11);
            cursor_ += 11;
            return true;
        }

        // ============================================================
        // RESPONSE GENERATION
        // ============================================================

        SLAB_FORCE_INLINE bool start_response(uint16_t status_code, std::string_view status_text) noexcept {
            // CRLF Injection Guard: Prevent HTTP Response Splitting
            for (char c : status_text) if (SL_EXPECT_FALSE(c == '\r' || c == '\n')) return false;

            // Maximum bounds: "HTTP/1.1 " + status(3) + " " + status_text + "\r\n"
            size_t req_len = status_text.size() + 15;
            if (SL_EXPECT_FALSE(req_len < status_text.size())) return false; // Overflow guard
            if (SL_EXPECT_FALSE(!ensure_capacity(req_len))) return false;
            
            std::memcpy(buffer_ + cursor_, "HTTP/1.1 ", 9);
            cursor_ += 9;
            
            auto [p, ec] = std::to_chars(buffer_ + cursor_, buffer_ + capacity_, status_code);
            if (SL_EXPECT_FALSE(ec != std::errc{})) return false;
            cursor_ = p - buffer_;
            
            buffer_[cursor_++] = ' ';
            std::memcpy(buffer_ + cursor_, status_text.data(), status_text.size());
            cursor_ += status_text.size();
            
            buffer_[cursor_++] = '\r';
            buffer_[cursor_++] = '\n';
            return true;
        }

        // ============================================================
        // HEADER INJECTION
        // ============================================================

        SLAB_FORCE_INLINE bool add_header(std::string_view key, std::string_view value) noexcept {
            // CRLF Injection Guard: Prevent HTTP Response Splitting
            for (char c : key) if (SL_EXPECT_FALSE(c == '\r' || c == '\n')) return false;
            for (char c : value) if (SL_EXPECT_FALSE(c == '\r' || c == '\n')) return false;

            size_t req_len = key.size() + value.size() + 4;
            if (SL_EXPECT_FALSE(req_len < key.size())) return false; // Overflow guard
            if (SL_EXPECT_FALSE(!ensure_capacity(req_len))) return false;
            
            std::memcpy(buffer_ + cursor_, key.data(), key.size());
            cursor_ += key.size();
            
            buffer_[cursor_++] = ':';
            buffer_[cursor_++] = ' ';
            
            std::memcpy(buffer_ + cursor_, value.data(), value.size());
            cursor_ += value.size();
            
            buffer_[cursor_++] = '\r';
            buffer_[cursor_++] = '\n';
            return true;
        }

        SLAB_FORCE_INLINE bool add_content_length(size_t length) noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(16 + 20 + 2))) return false;
            std::memcpy(buffer_ + cursor_, "Content-Length: ", 16);
            cursor_ += 16;
            auto [p, ec] = std::to_chars(buffer_ + cursor_, buffer_ + capacity_ - 2, length);
            if (SL_EXPECT_FALSE(ec != std::errc{})) return false;
            cursor_ = p - buffer_;
            buffer_[cursor_++] = '\r';
            buffer_[cursor_++] = '\n';
            return true;
        }
        
        SLAB_FORCE_INLINE bool add_keep_alive(bool keep_alive) noexcept {
            if (keep_alive) {
                if (SL_EXPECT_FALSE(!ensure_capacity(24))) return false;
                std::memcpy(buffer_ + cursor_, "Connection: keep-alive\r\n", 24);
                cursor_ += 24;
            } else {
                if (SL_EXPECT_FALSE(!ensure_capacity(19))) return false;
                std::memcpy(buffer_ + cursor_, "Connection: close\r\n", 19);
                cursor_ += 19;
            }
            return true;
        }

        SLAB_FORCE_INLINE bool add_chunked_encoding() noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(28))) return false;
            std::memcpy(buffer_ + cursor_, "Transfer-Encoding: chunked\r\n", 28);
            cursor_ += 28;
            return true;
        }

        SLAB_FORCE_INLINE bool end_headers() noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(2))) return false;
            buffer_[cursor_++] = '\r';
            buffer_[cursor_++] = '\n';
            return true;
        }

        // ============================================================
        // PAYLOAD INJECTION
        // ============================================================

        SLAB_FORCE_INLINE bool append_body(std::string_view body) noexcept {
            if (body.empty()) return true;
            if (SL_EXPECT_FALSE(!ensure_capacity(body.size()))) return false;
            std::memcpy(buffer_ + cursor_, body.data(), body.size());
            cursor_ += body.size();
            return true;
        }

        SLAB_FORCE_INLINE bool append_chunk(std::string_view chunk) noexcept {
            size_t req_len = chunk.size() + 20;
            if (SL_EXPECT_FALSE(req_len < chunk.size())) return false; // Overflow guard
            if (SL_EXPECT_FALSE(!ensure_capacity(req_len))) return false;
            auto [p, ec] = std::to_chars(buffer_ + cursor_, buffer_ + capacity_, chunk.size(), 16);
            if (SL_EXPECT_FALSE(ec != std::errc{})) return false;
            cursor_ = p - buffer_;
            buffer_[cursor_++] = '\r';
            buffer_[cursor_++] = '\n';
            std::memcpy(buffer_ + cursor_, chunk.data(), chunk.size());
            cursor_ += chunk.size();
            buffer_[cursor_++] = '\r';
            buffer_[cursor_++] = '\n';
            return true;
        }

        SLAB_FORCE_INLINE bool end_chunked_stream() noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(5))) return false;
            std::memcpy(buffer_ + cursor_, "0\r\n\r\n", 5);
            cursor_ += 5;
            return true;
        }
    };

    /**
     * @brief Zero-Allocation HTTP/2 HPACK Producer.
     * @details Generates binary HTTP/2 multiplexed responses natively without heap allocation.
     */
    class http2_producer {
    private:
        char* SLAB_RESTRICT buffer_;
        size_t capacity_;
        size_t cursor_{0};
        size_t frame_start_{0};
        uint32_t stream_id_{0};

        SLAB_FORCE_INLINE bool ensure_capacity(size_t required_bytes) const noexcept {
            return SL_EXPECT_TRUE(required_bytes <= capacity_ - cursor_);
        }

        void encode_hpack_int(size_t value, uint8_t prefix_bits, uint8_t prefix_mask) noexcept {
            uint8_t mask = (1 << prefix_bits) - 1;
            if (value < mask) {
                buffer_[cursor_++] = prefix_mask | static_cast<uint8_t>(value);
            } else {
                buffer_[cursor_++] = prefix_mask | mask;
                value -= mask;
                while (value >= 128) {
                    buffer_[cursor_++] = (value & 0x7F) | 0x80;
                    value >>= 7;
                }
                buffer_[cursor_++] = static_cast<uint8_t>(value);
            }
        }

        void encode_hpack_string(std::string_view str) noexcept {
            encode_hpack_int(str.size(), 7, 0x00); // 0 = No Huffman for outbound payload brevity
            std::memcpy(buffer_ + cursor_, str.data(), str.size());
            cursor_ += str.size();
        }

    public:
        explicit constexpr http2_producer(char* SLAB_RESTRICT outbound_buffer, size_t max_capacity) noexcept
            : buffer_(outbound_buffer), capacity_(max_capacity) {}

        SLAB_FORCE_INLINE void reset() noexcept { cursor_ = 0; stream_id_ = 0; }
        SLAB_FORCE_INLINE size_t bytes_written() const noexcept { return cursor_; }
        SLAB_FORCE_INLINE std::string_view view() const noexcept { return {buffer_, cursor_}; }

        bool begin_headers(uint32_t stream_id) noexcept {
            if (!ensure_capacity(9)) return false;
            stream_id_ = stream_id;
            frame_start_ = cursor_;
            cursor_ += 9;
            return true;
        }

        bool add_status(uint16_t status_code) noexcept {
            if (!ensure_capacity(16)) return false;
            if (status_code == 200) { buffer_[cursor_++] = 0x88; } // Indexed 8
            else if (status_code == 204) { buffer_[cursor_++] = 0x89; } // Indexed 9
            else {
                buffer_[cursor_++] = 0x08; // Literal without indexing, :status is index 8
                char s_str[3];
                auto [p, ec] = std::to_chars(s_str, s_str + 3, status_code);
                encode_hpack_string(std::string_view(s_str, p - s_str));
            }
            return true;
        }

        bool add_header(std::string_view key, std::string_view value) noexcept {
            if (!ensure_capacity(key.size() + value.size() + 10)) return false;
            buffer_[cursor_++] = 0x00; // Literal without indexing, new name
            encode_hpack_string(key);
            encode_hpack_string(value);
            return true;
        }

        bool end_headers(bool end_stream = false) noexcept {
            uint32_t len = cursor_ - frame_start_ - 9;
            buffer_[frame_start_] = (len >> 16) & 0xFF;
            buffer_[frame_start_ + 1] = (len >> 8) & 0xFF;
            buffer_[frame_start_ + 2] = len & 0xFF;
            buffer_[frame_start_ + 3] = 0x01; // HEADERS
            buffer_[frame_start_ + 4] = 0x04 | (end_stream ? 0x01 : 0x00);
            buffer_[frame_start_ + 5] = (stream_id_ >> 24) & 0x7F;
            buffer_[frame_start_ + 6] = (stream_id_ >> 16) & 0xFF;
            buffer_[frame_start_ + 7] = (stream_id_ >> 8) & 0xFF;
            buffer_[frame_start_ + 8] = stream_id_ & 0xFF;
            return true;
        }

        bool add_data(std::string_view data, bool end_stream = true) noexcept {
            if (!ensure_capacity(9 + data.size())) return false;
            uint32_t len = data.size();
            buffer_[cursor_++] = (len >> 16) & 0xFF;
            buffer_[cursor_++] = (len >> 8) & 0xFF;
            buffer_[cursor_++] = len & 0xFF;
            buffer_[cursor_++] = 0x00; // DATA
            buffer_[cursor_++] = end_stream ? 0x01 : 0x00;
            buffer_[cursor_++] = (stream_id_ >> 24) & 0x7F;
            buffer_[cursor_++] = (stream_id_ >> 16) & 0xFF;
            buffer_[cursor_++] = (stream_id_ >> 8) & 0xFF;
            buffer_[cursor_++] = stream_id_ & 0xFF;
            std::memcpy(buffer_ + cursor_, data.data(), len);
            cursor_ += len;
            return true;
        }

        bool add_settings_ack() noexcept {
            if (!ensure_capacity(9)) return false;
            buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; // Length 0
            buffer_[cursor_++] = 0x04; // SETTINGS
            buffer_[cursor_++] = 0x01; // ACK flag
            buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; // Stream 0
            return true;
        }

        bool add_ping_ack(const char opaque_data[8]) noexcept {
            if (!ensure_capacity(17)) return false;
            buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 8; // Length 8
            buffer_[cursor_++] = 0x06; // PING
            buffer_[cursor_++] = 0x01; // ACK flag
            buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; // Stream 0
            std::memcpy(buffer_ + cursor_, opaque_data, 8);
            cursor_ += 8;
            return true;
        }

        bool add_rst_stream(uint32_t stream_id, uint32_t error_code) noexcept {
            if (!ensure_capacity(13)) return false;
            buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 4; // Length 4
            buffer_[cursor_++] = 0x03; // RST_STREAM
            buffer_[cursor_++] = 0x00; // Flags 0
            buffer_[cursor_++] = (stream_id >> 24) & 0x7F;
            buffer_[cursor_++] = (stream_id >> 16) & 0xFF;
            buffer_[cursor_++] = (stream_id >> 8) & 0xFF;
            buffer_[cursor_++] = stream_id & 0xFF;
            buffer_[cursor_++] = (error_code >> 24) & 0xFF;
            buffer_[cursor_++] = (error_code >> 16) & 0xFF;
            buffer_[cursor_++] = (error_code >> 8) & 0xFF;
            buffer_[cursor_++] = error_code & 0xFF;
            return true;
        }
        
        bool add_goaway(uint32_t last_stream_id, uint32_t error_code) noexcept {
            if (!ensure_capacity(17)) return false;
            buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 8; // Length 8
            buffer_[cursor_++] = 0x07; // GOAWAY
            buffer_[cursor_++] = 0x00; // Flags
            buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; // Stream 0
            buffer_[cursor_++] = (last_stream_id >> 24) & 0x7F;
            buffer_[cursor_++] = (last_stream_id >> 16) & 0xFF;
            buffer_[cursor_++] = (last_stream_id >> 8) & 0xFF;
            buffer_[cursor_++] = last_stream_id & 0xFF;
            buffer_[cursor_++] = (error_code >> 24) & 0xFF;
            buffer_[cursor_++] = (error_code >> 16) & 0xFF;
            buffer_[cursor_++] = (error_code >> 8) & 0xFF;
            buffer_[cursor_++] = error_code & 0xFF;
            return true;
        }

        bool add_window_update(uint32_t stream_id, uint32_t increment) noexcept {
            if (!ensure_capacity(13)) return false;
            buffer_[cursor_++] = 0; buffer_[cursor_++] = 0; buffer_[cursor_++] = 4; // Length 4
            buffer_[cursor_++] = 0x08; // WINDOW_UPDATE
            buffer_[cursor_++] = 0x00; // Flags 0
            buffer_[cursor_++] = (stream_id >> 24) & 0x7F;
            buffer_[cursor_++] = (stream_id >> 16) & 0xFF;
            buffer_[cursor_++] = (stream_id >> 8) & 0xFF;
            buffer_[cursor_++] = stream_id & 0xFF;
            buffer_[cursor_++] = (increment >> 24) & 0x7F; // Reserved bit is 0
            buffer_[cursor_++] = (increment >> 16) & 0xFF;
            buffer_[cursor_++] = (increment >> 8) & 0xFF;
            buffer_[cursor_++] = increment & 0xFF;
            return true;
        }
    };

} // namespace slabflux::transport