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
 * @file baremetal_parser.hpp
 * @brief High-performance DFA-based HTTP Parser.
 */

#pragma once

#include "slabflux/transport/http_frame.hpp"
#include <immintrin.h>
#include <charconv>
#include <string_view>
#include "slabflux/hw/intrinsics.hpp" // For tzcnt_64
#include "slabflux/core/wire_frame_lsn.hpp" // For http_frame if it inherits from event
#include <system_error>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::transport {

    enum class parser_status {
        OK,
        INCOMPLETE,
        ERROR,
        CHUNK_READY // Zero-copy streaming state
    };

    class baremetal_parser {
    public:
        SLAB_FORCE_INLINE static bool iequals(std::string_view a, std::string_view b) noexcept {
            if (a.size() != b.size()) return false;
            const char* pa = a.data();
            const char* pb = b.data();
            for (size_t i = 0; i < a.size(); ++i) {
                unsigned char ca = pa[i], cb = pb[i];
                if (ca == cb) continue;
                ca |= (static_cast<unsigned int>(ca - 'A') <= 25) ? 0x20 : 0;
                cb |= (static_cast<unsigned int>(cb - 'A') <= 25) ? 0x20 : 0;
                if (ca != cb) return false;
            }
            return true;
        }

        /** @brief AVX-512 hardware-accelerated delimiter discovery */
        SLAB_FORCE_INLINE static size_t find_delimiter(const char* ptr, size_t len, char del) noexcept {
#if defined(__AVX512F__) && defined(__AVX512BW__)
            size_t pos = 0;
            const __m512i v_del = _mm512_set1_epi8(del);
            while (pos + 64 <= len) {
                __m512i v_input = _mm512_loadu_si512(ptr + pos);
                __mmask64 mask = _mm512_cmpeq_epi8_mask(v_input, v_del);
                if (mask) return pos + __builtin_ctzll(mask);
                pos += 64;
            }
            while (pos < len) {
                if (ptr[pos] == del) return pos;
                pos++;
            }
#else
            for (size_t i = 0; i < len; ++i) if (ptr[i] == del) return i;
#endif
            return std::string_view::npos;
        }

        SLAB_FORCE_INLINE static size_t find_two_chars(const char* ptr, size_t len, size_t start_pos, char c1, char c2, char& out_c) noexcept {
            for (size_t i = start_pos; i < len; ++i) {
                char c = ptr[i];
                if (c == c1 || c == c2) {
                    out_c = c;
                    return i;
                }
            }
            return std::string_view::npos;
        }

        [[nodiscard]] static parser_status parse(std::string_view buffer, http_frame& frame) noexcept {
            if (buffer.empty()) return parser_status::INCOMPLETE;

            // Absolute Rebase Check: If the buffer size is smaller than our previous
            // consumption, the caller is reusing the frame for a new request/stream.
            if (SL_EXPECT_FALSE(frame.total_bytes_consumed > buffer.size())) {
                frame.reset();
            }

            static const void* const dispatch[] = {
                &&L_METHOD, &&L_URI, &&L_VERSION, &&L_HEADER_KEY, &&L_HEADER_VAL,
                &&L_BODY, &&L_CHUNK_SIZE, &&L_CHUNK_DATA, &&L_TRAILER, &&L_DONE
            };

            size_t pos = frame.total_bytes_consumed;
            size_t token_start = pos - frame.consumed_in_state;

            if (frame.internal_state == parser_state::METHOD && pos == 0) {
                frame.method = {}; frame.uri = {}; frame.version = {}; frame.body = {};
                frame.header_count = 0; frame.has_content_length = false;
                frame.content_length = 0; frame.is_chunked = false; 
                frame.accumulated_body_size = 0;
            }

            goto *dispatch[(uint8_t)frame.internal_state];

L_METHOD:
            frame.internal_state = parser_state::METHOD;
            {
                while (pos < buffer.size() && (buffer[pos] == '\r' || buffer[pos] == '\n')) {
                    pos++;
                }
                token_start = pos;
                if (pos == buffer.size()) goto L_INCOMPLETE;
                size_t d = find_delimiter(buffer.data() + pos, buffer.size() - pos, ' ');
                if (d == std::string_view::npos) { 
                    pos = buffer.size(); 
                    if (SL_EXPECT_FALSE(pos - token_start > http_frame::MAX_METHOD_SIZE)) return parser_status::ERROR;
                    goto L_INCOMPLETE; 
                }
                pos += d;
                frame.method = buffer.substr(token_start, pos - token_start);
                if (SL_EXPECT_FALSE(frame.method.size() > http_frame::MAX_METHOD_SIZE)) return parser_status::ERROR;
                token_start = ++pos;
            }

L_URI:
            frame.internal_state = parser_state::URI;
            {
                size_t d = find_delimiter(buffer.data() + pos, buffer.size() - pos, ' ');
                if (d == std::string_view::npos) { 
                    pos = buffer.size(); 
                    if (SL_EXPECT_FALSE(pos - token_start > http_frame::MAX_URI_SIZE)) return parser_status::ERROR;
                    goto L_INCOMPLETE; 
                }
                pos += d;
                frame.uri = buffer.substr(token_start, pos - token_start);
                if (SL_EXPECT_FALSE(frame.uri.size() > http_frame::MAX_URI_SIZE)) return parser_status::ERROR;
                token_start = ++pos;
            }

L_VERSION:
            frame.internal_state = parser_state::VERSION;
            {
                size_t d = find_delimiter(buffer.data() + pos, buffer.size() - pos, '\r');
                if (d == std::string_view::npos) { 
                    pos = buffer.size(); 
                    if (SL_EXPECT_FALSE(pos - token_start > 32)) return parser_status::ERROR;
                    goto L_INCOMPLETE; 
                }
                pos += d;
                if (SL_EXPECT_FALSE(pos + 1 >= buffer.size())) goto L_INCOMPLETE;
                if (SL_EXPECT_FALSE(buffer[pos + 1] != '\n')) return parser_status::ERROR;
                frame.version = buffer.substr(token_start, pos - token_start);
                if (SL_EXPECT_FALSE(frame.version != "HTTP/1.1")) return parser_status::ERROR;
                pos += 2; token_start = pos;
            }

L_HEADER_KEY:
            frame.internal_state = parser_state::HEADER_KEY;
            // Start-of-Headers Reset: If this is the very first header of a request (start line just finished),
            // we ensure length-tracking flags are ready for the new stream.
            if (pos == token_start && frame.header_count == 0) {
                frame.has_content_length = false;
                frame.is_chunked = false;
                frame.accumulated_body_size = 0;
            }

            while (pos < buffer.size()) {
                if (pos == token_start) {
                    if (buffer[pos] == '\r') {
                        if (SL_EXPECT_FALSE(pos + 1 >= buffer.size())) goto L_INCOMPLETE;
                        if (SL_EXPECT_FALSE(buffer[pos + 1] != '\n')) return parser_status::ERROR;
                        pos += 2;
                        token_start = pos;
                        if (frame.is_chunked) goto L_CHUNK_SIZE;
                        if (frame.content_length > 0) goto L_BODY;
                        goto L_DONE;
                    } else if (buffer[pos] == '\n') {
                        pos += 1;
                        token_start = pos;
                        if (frame.is_chunked) goto L_CHUNK_SIZE;
                        if (frame.content_length > 0) goto L_BODY;
                        goto L_DONE;
                    }
                }

                char found_c;
                size_t d = find_two_chars(buffer.data(), buffer.size(), pos, ':', '\n', found_c);

                if (d == std::string_view::npos) {
                    size_t chunk_len = buffer.size() - token_start;
                    if (SL_EXPECT_FALSE(chunk_len > http_frame::MAX_HEADER_FIELD_SIZE)) return parser_status::ERROR;

                    for (size_t k = pos; k < buffer.size(); ++k) {
                        if (SL_EXPECT_FALSE(static_cast<unsigned char>(buffer[k] - 33) > 93)) return parser_status::ERROR;
                    }
                    pos = buffer.size();
                    goto L_INCOMPLETE;
                }

                if (SL_EXPECT_FALSE(found_c == '\n')) {
                    // Protocol Violation: Reached an unescaped line break before finding a colon.
                    return parser_status::ERROR;
                }

                size_t key_len = d - token_start;
                if (SL_EXPECT_FALSE(key_len == 0 || key_len > http_frame::MAX_HEADER_FIELD_SIZE)) return parser_status::ERROR;
                if (SL_EXPECT_FALSE(frame.header_count >= http_frame::MAX_HEADERS)) return parser_status::ERROR;

                size_t k_idx = pos;
#if defined(__AVX2__)
                if (d - pos >= 32) {
                    const __m256i v_33_256 = _mm256_set1_epi8(33);
                    const __m256i v_126_256 = _mm256_set1_epi8(126);
                    __m256i v_data1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buffer.data() + pos));
                    __m256i bad1 = _mm256_or_si256(_mm256_cmpgt_epi8(v_33_256, v_data1), _mm256_cmpgt_epi8(v_data1, v_126_256));
                    if (SL_UNLIKELY(!_mm256_testz_si256(bad1, bad1))) return parser_status::ERROR;
                    
                    __m256i v_data2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buffer.data() + d - 32));
                    __m256i bad2 = _mm256_or_si256(_mm256_cmpgt_epi8(v_33_256, v_data2), _mm256_cmpgt_epi8(v_data2, v_126_256));
                    if (SL_UNLIKELY(!_mm256_testz_si256(bad2, bad2))) return parser_status::ERROR;
                    k_idx = d;
                }
#endif
                for (size_t k = k_idx; k < d; ++k) {
                    if (SL_EXPECT_FALSE(static_cast<unsigned char>(buffer[k] - 33) > 93)) return parser_status::ERROR;
                }

                frame.headers[frame.header_count].key = buffer.substr(token_start, key_len);
                pos = d + 1;
                frame.internal_state = parser_state::HEADER_VAL;
                token_start = pos;
                goto L_HEADER_VAL;
            }
            goto L_INCOMPLETE;

L_HEADER_VAL:
            frame.internal_state = parser_state::HEADER_VAL;
            {
                size_t d = find_delimiter(buffer.data() + pos, buffer.size() - pos, '\n');
                if (d == std::string_view::npos) { 
                    pos = buffer.size(); 
                    if (SL_EXPECT_FALSE(pos - token_start > http_frame::MAX_HEADER_FIELD_SIZE)) return parser_status::ERROR;
                    goto L_INCOMPLETE; 
                }
                pos += d;
                
                std::string_view val;
                if (pos > token_start && buffer[pos - 1] == '\r') {
                    val = buffer.substr(token_start, pos - token_start - 1);
                } else {
                    val = buffer.substr(token_start, pos - token_start);
                }
                if (SL_EXPECT_FALSE(val.size() > http_frame::MAX_HEADER_FIELD_SIZE)) return parser_status::ERROR;
                
                // Boundary-agnostic OWS trimming
                while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.remove_suffix(1);
                frame.headers[frame.header_count].value = val;
                if (iequals(frame.headers[frame.header_count].key, "Content-Length")) {
                    // Security Guard: Reject dual framing (Request Smuggling Protection)
                    if (SL_EXPECT_FALSE(frame.has_content_length || frame.is_chunked)) return parser_status::ERROR;
                    auto [p, ec] = std::from_chars(val.data(), val.data() + val.size(), frame.content_length);
                    // Security Guard: Ensure complete parse to prevent smuggling (e.g. "5 smuggled")
                    if (SL_EXPECT_FALSE(ec != std::errc{} || p != val.data() + val.size() || frame.content_length > http_frame::MAX_BODY_SIZE)) return parser_status::ERROR;
                    frame.has_content_length = true;
                } else if (iequals(frame.headers[frame.header_count].key, "Transfer-Encoding")) {
                    if (SL_EXPECT_TRUE(iequals(val, "chunked"))) {
                        // Security Guard: Reject dual framing
                        if (SL_EXPECT_FALSE(frame.has_content_length || frame.is_chunked)) return parser_status::ERROR;
                        frame.is_chunked = true;
                    } else {
                        // Prevent TE.TE or TE.CL smuggling via obfuscated / multiple encodings
                        return parser_status::ERROR;
                    }
                }
                frame.header_count++; pos += 1; token_start = pos; // Advance over '\n'
                goto L_HEADER_KEY;
            }

L_BODY:
            frame.internal_state = parser_state::BODY;
            if (buffer.size() - pos < frame.content_length) goto L_INCOMPLETE;
            frame.body = buffer.substr(pos, frame.content_length);
            pos += frame.content_length;
            goto L_DONE;

L_CHUNK_SIZE:
            frame.internal_state = parser_state::CHUNK_SIZE;
            {
                size_t d = find_delimiter(buffer.data() + pos, buffer.size() - pos, '\n');
                if (d == std::string_view::npos) { pos = buffer.size(); goto L_INCOMPLETE; }
                pos += d;
                
                std::string_view hex;
                if (pos > token_start && buffer[pos - 1] == '\r') {
                    hex = buffer.substr(token_start, pos - token_start - 1);
                } else {
                    hex = buffer.substr(token_start, pos - token_start);
                }
                if (SL_EXPECT_FALSE(hex.empty())) return parser_status::ERROR;
                
                auto [p, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), frame.content_length, 16);
                if (SL_EXPECT_FALSE(ec != std::errc{} || frame.content_length > http_frame::MAX_BODY_SIZE)) return parser_status::ERROR;
                // Allow chunk extensions (;) but prevent chunk size smuggling
                if (SL_EXPECT_FALSE(p != hex.data() + hex.size() && *p != ';')) return parser_status::ERROR;

                // Guard: Prevent Chunked Accumulation DoS (Determinism Invariant)
                frame.accumulated_body_size += frame.content_length;
                if (SL_EXPECT_FALSE(frame.accumulated_body_size > http_frame::MAX_BODY_SIZE)) return parser_status::ERROR;

                pos += 1; token_start = pos; // Advance over '\n'
                if (frame.content_length == 0) goto L_TRAILER;
                goto L_CHUNK_DATA;
            }

L_CHUNK_DATA:
            frame.internal_state = parser_state::CHUNK_DATA;
            if (buffer.size() - token_start < frame.content_length + 1) goto L_INCOMPLETE;
            
            if (buffer[token_start + frame.content_length] == '\r') {
                if (buffer.size() - token_start < frame.content_length + 2) goto L_INCOMPLETE;
                if (SL_EXPECT_FALSE(buffer[token_start + frame.content_length + 1] != '\n')) return parser_status::ERROR;
                frame.body = buffer.substr(token_start, frame.content_length);
                pos = token_start + frame.content_length + 2; 
            } else if (buffer[token_start + frame.content_length] == '\n') {
                frame.body = buffer.substr(token_start, frame.content_length);
                pos = token_start + frame.content_length + 1;
            } else {
                return parser_status::ERROR;
            }
            token_start = pos;
            
            frame.internal_state = parser_state::CHUNK_SIZE; // Advance state for next entry
            frame.total_bytes_consumed = pos;
            frame.consumed_in_state = 0;
            return parser_status::CHUNK_READY; // Emit chunk natively to prevent string_view overwrite

L_TRAILER:
            frame.internal_state = parser_state::TRAILER;
            if (buffer.size() - pos < 1) goto L_INCOMPLETE;
            if (buffer[pos] == '\r') {
                if (buffer.size() - pos < 2) goto L_INCOMPLETE;
                if (SL_EXPECT_FALSE(buffer[pos+1] != '\n')) return parser_status::ERROR;
                pos += 2;
            } else if (buffer[pos] == '\n') {
                pos += 1;
            } else {
                return parser_status::ERROR;
            }
            goto L_DONE;

L_DONE:
            // Synchronization: Move to METHOD for pipelining support
            // so the next call resumes from the correct state
            frame.internal_state = parser_state::METHOD;
            frame.total_bytes_consumed = pos;
            frame.consumed_in_state = 0;
            return parser_status::OK;

L_INCOMPLETE:
            frame.total_bytes_consumed = pos;
            frame.consumed_in_state = pos - token_start;
            return parser_status::INCOMPLETE;
        }
    };
}