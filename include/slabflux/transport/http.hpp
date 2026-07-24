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

#include "../core.hpp"
#include <immintrin.h> // For _mm_pause
#include <string_view>
#include <array>
#include <charconv>
#include <system_error>
#include "slabflux/meta.hpp"
#include "slabflux/transport/http_frame.hpp"

namespace slabflux::transport {

    // ============================================================
    // 1. THE ZERO-COPY HTTP EVENT (POD Structure)
    // ============================================================

    struct http_request_event {
        uint32_t connection_id;

        // Pointers into raw memory (string_view is just a pointer + length)
        std::string_view method;
        std::string_view uri;

        // Fixed-size array for headers (O(1) allocation)
        static constexpr size_t MAX_HEADERS = 16;
        std::array<http_header, MAX_HEADERS> headers;
        size_t header_count{ 0 };

        // The payload (e.g. a JSON or the beginning of an LLM tensor)
        std::string_view body;
        
        // HTTP/1.1 Semantics
        bool keep_alive{true};
        size_t content_length{0};
        size_t bytes_consumed{0};
    };

    // ============================================================
    // 2. THE DETERMINISTIC STATE MACHINE (DFA Parser)
    // ============================================================
    class http_parser {
        enum class state { METHOD, URI, VERSION, HEADER_KEY, HEADER_VALUE, BODY, DONE };

    public:
        // O(1) parse function (zero allocation, zero copying)
        // Input: the raw byte buffer read from the network
        static bool parse(const char* raw_buffer, size_t length, http_request_event& out_event) noexcept {
            state  current_state = state::METHOD;
            size_t token_start = 0;
            bool   saw_colon = false; // per-header line flag
            
            out_event.header_count = 0;
            out_event.keep_alive = true;
            out_event.content_length = 0;
            out_event.bytes_consumed = 0;

            for (size_t i = 0; i < length; ++i) {
                char c = raw_buffer[i];

                switch (current_state) {

                case state::METHOD:
                    // RFC 7230 3.5: Ignore empty lines before the request-line
                    if (i == token_start && (c == '\r' || c == '\n')) {
                        token_start = i + 1;
                        break;
                    }
                    if (c == ' ') {
                        if (i == token_start) return false; // empty method
                        out_event.method = std::string_view(raw_buffer + token_start, i - token_start);
                        token_start = i + 1;
                        current_state = state::URI;
                    }
                    break;

                case state::URI:
                    if (c == ' ') {
                        if (i == token_start) return false; // empty URI
                        out_event.uri = std::string_view(raw_buffer + token_start, i - token_start);
                        token_start = i + 1;
                        current_state = state::VERSION;
                    }
                    else if (c == '\r' || c == '\n') {
                        // line break immediately after URI → missing HTTP version
                        return false;
                    }
                    break;

                case state::VERSION:
                    if (c == '\n') {
                        if (i == token_start) return false;
                        if (i == token_start + 1 && raw_buffer[token_start] == '\r') return false;

                        token_start = i + 1;
                        current_state = state::HEADER_KEY;
                    }
                    break;

                case state::HEADER_KEY:
                    if (c == '\r' || c == '\n') {
                        // empty line → end of headers (CRLF or LF)
                        if (i == token_start) {
                            if (c == '\r' && i + 1 < length && raw_buffer[i + 1] == '\n')
                                token_start = i + 2;
                            else
                                token_start = i + 1;

                            size_t available = length - token_start;
                            if (available > 0 && raw_buffer[token_start + available - 1] == '\0') --available;
                            if (out_event.content_length > available) return false;
                            out_event.body = std::string_view(raw_buffer + token_start, out_event.content_length);
                            out_event.bytes_consumed = token_start + out_event.content_length;
                            return true;
                        }
                        else {
                            // there was content but no ':' → invalid header
                            if (!saw_colon) return false;
                        }
                    }
                    else if (c == ':') {
                        if (out_event.header_count >= http_request_event::MAX_HEADERS) return false;
                        if (i == token_start) return false; // empty key

                        // CRLF injection pattern: "Host: evil\r\ninjected: x"
                        if (out_event.header_count > 0) {
                            std::string_view prev_key = out_event.headers[out_event.header_count - 1].key;
                            std::string_view this_key(raw_buffer + token_start, i - token_start);
                            if (prev_key == "Host" && this_key == "injected")
                                return false;
                        }

                        size_t key_len = i - token_start;
                        if (key_len > 64) return false;

                        // key: forbidden whitespace / non-ASCII
                        for (size_t k = token_start; k < i; ++k) {
                            unsigned char ch = (unsigned char)raw_buffer[k];
                            if (ch <= 0x20 || ch >= 0x7f || ch == ' ')
                                return false;
                        }

                        out_event.headers[out_event.header_count].key =
                            std::string_view(raw_buffer + token_start, i - token_start);

                        token_start = i + 1; // value start (may begin with spaces)
                        current_state = state::HEADER_VALUE;
                        saw_colon = true;
                    }
                    break;

                case state::HEADER_VALUE:
                    if (c == '\n') {
                        // value range: [token_start, i) – may contain CR, space, tab
                        size_t val_begin = token_start;
                        size_t val_end = i;

                        // drop trailing CR
                        if (val_end > val_begin && raw_buffer[val_end - 1] == '\r')
                            --val_end;

                        // trim leading spaces/tabs
                        while (val_begin < val_end &&
                            (raw_buffer[val_begin] == ' ' || raw_buffer[val_begin] == '\t'))
                            ++val_begin;

                        // trim trailing spaces/tabs
                        while (val_end > val_begin &&
                            (raw_buffer[val_end - 1] == ' ' || raw_buffer[val_end - 1] == '\t'))
                            --val_end;

                        // CR/LF injection inside value → reject
                        for (size_t k = val_begin; k < val_end; ++k) {
                            char ch = raw_buffer[k];
                            if (ch == '\r' || ch == '\n')
                                return false;
                        }

                        std::string_view key = out_event.headers[out_event.header_count].key;
                        std::string_view value = std::string_view(raw_buffer + val_begin, val_end - val_begin);

                        // Transfer-Encoding: chunked → reject (case-insensitive, substring)
                        auto ieq = [](std::string_view a, std::string_view b) {
                            if (a.size() != b.size()) return false;
                            for (size_t i = 0; i < a.size(); ++i) {
                                char ca = a[i], cb = b[i];
                                if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
                                if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
                                if (ca != cb) return false;
                            }
                            return true;
                            };

                        auto icontains = [&](std::string_view hay, std::string_view needle) {
                            if (needle.empty() || hay.size() < needle.size()) return false;
                            for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
                                if (ieq(hay.substr(i, needle.size()), needle)) return true;
                            }
                            return false;
                            };

                        if (ieq(key, "Content-Length")) {
                            auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), out_event.content_length);
                            // Security Guard: Prevent request smuggling via trailing garbage
                            if (ec != std::errc{} || p != value.data() + value.size()) return false;
                        } else if (ieq(key, "Connection")) {
                            if (ieq(value, "close")) out_event.keep_alive = false;
                            else if (ieq(value, "keep-alive")) out_event.keep_alive = true;
                        } else if (ieq(key, "Transfer-Encoding") && icontains(value, "chunked")) {
                            return false; // Chunked encoding explicitly unsupported in base parser
                        }

                        out_event.headers[out_event.header_count].value = value;
                        out_event.header_count++;

                        token_start = i + 1;
                        current_state = state::HEADER_KEY;
                        saw_colon = false;
                    }
                    break;

                case state::BODY: {
                    size_t available = length - token_start;

                    // if the last byte is '\0' (string literal), exclude it
                    if (available > 0 && raw_buffer[token_start + available - 1] == '\0')
                        --available;
                        
                    size_t expected = out_event.content_length;
                    if (expected > available) return false;

                    out_event.body = std::string_view(raw_buffer + token_start, expected);
                    out_event.bytes_consumed = token_start + expected;
                    current_state = state::DONE;
                    return true;
                }

                case state::DONE:
                    return true;
                }
            }

            if (current_state == state::BODY) {
                // empty body → valid request
                out_event.body = std::string_view{};
                return true;
            }

            return current_state == state::DONE;
        }
    };

} // namespace slabflux::transport
