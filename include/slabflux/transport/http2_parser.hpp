/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file http2_parser.hpp
 * @brief HTTP/2 Binary Protocol Parser with Universal Event Hydration.
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>
#include <string>
#include "slabflux/transport/hpack.hpp"

namespace slabflux::transport {

    /**
     * @brief Zero-allocation oriented HTTP/2 Protocol State Machine.
     * @details Transcodes binary HTTP/2 multiplexed streams natively into 
     * the exact same Event/Frame structures used by the HTTP/1.1 parsers.
     * This allows 100% reuse of existing Application Routers and Parsed Event types!
     */
    class http2_parser {
        hpack_decoder hpack_;
        bool preface_received_{false};
        std::string body_accumulator_; // Reassembles fragmented DATA frames
        
        bool expecting_continuation_{false};
        uint32_t continuation_stream_id_{0};
        bool end_stream_pending_{false};
        std::string header_accumulator_; // Reassembles fragmented HEADERS/CONTINUATION frames

    public:
        enum class status { OK, INCOMPLETE, ERROR };

        http2_parser() = default;

        void reset() noexcept {
            preface_received_ = false;
            body_accumulator_.clear();
            expecting_continuation_ = false;
            continuation_stream_id_ = 0;
            end_stream_pending_ = false;
            header_accumulator_.clear();
        }

        /**
         * @brief Parses an HTTP/2 stream and hydrates ANY existing HTTP/1.1 event structure.
         * @tparam HttpEvent The target event type (e.g., http_request_event, http_frame).
         */
        template <typename HttpEvent>
        status parse(std::string_view& stream, HttpEvent& out_ev) {
            if (!preface_received_) {
                constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
                if (stream.size() < preface.size()) return status::INCOMPLETE;
                if (!stream.starts_with(preface)) return status::ERROR;
                preface_received_ = true;
                stream.remove_prefix(preface.size());
            }

            while (stream.size() >= 9) {
                const uint8_t* ptr = reinterpret_cast<const uint8_t*>(stream.data());
                uint32_t length = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];
                uint8_t type = ptr[3];
                uint8_t flags = ptr[4];
                uint32_t stream_id = ((ptr[5] & 0x7F) << 24) | (ptr[6] << 16) | (ptr[7] << 8) | ptr[8];

                if (stream.size() < 9u + length) return status::INCOMPLETE;

                std::string_view payload = stream.substr(9, length);
                
                // RFC 7540: CONTINUATION frames MUST immediately follow an incomplete HEADERS frame
                if (expecting_continuation_ && type != 0x09) return status::ERROR; 

                if (type == 1) { // HEADERS (0x01)
                    if (stream_id == 0) return status::ERROR; // Stream 0 is reserved for connection control
                    
                    std::vector<hpack_header> headers;
                    std::string_view hpack_data = payload;
                    if (flags & 0x08) { // PADDED
                        if (hpack_data.empty()) return status::ERROR;
                        uint8_t pad_len = hpack_data[0];
                        if (hpack_data.size() < 1u + pad_len) return status::ERROR;
                        hpack_data = hpack_data.substr(1, hpack_data.size() - 1 - pad_len);
                    }
                    if (flags & 0x20) { // PRIORITY
                        if (hpack_data.size() < 5) return status::ERROR;
                        hpack_data.remove_prefix(5);
                    }

                    if (flags & 0x01) end_stream_pending_ = true; // END_STREAM

                    if (flags & 0x04) { // END_HEADERS
                        // Hermetic Hydration: Fully reset the target event to prevent state leakage from reused frames
                        if constexpr (requires { out_ev.reset(); }) {
                            out_ev.reset();
                        } else {
                            out_ev.method = {}; out_ev.uri = {}; out_ev.header_count = 0; out_ev.body = {};
                            if constexpr (requires { out_ev.has_content_length = false; }) {
                                out_ev.has_content_length = false; out_ev.is_chunked = false; out_ev.content_length = 0;
                            }
                        }
                        if constexpr (requires { out_ev.version = std::string_view{}; }) {
                            out_ev.version = "HTTP/2.0";
                        }
                        body_accumulator_.clear(); // Reset reassembly buffer for the new stream

                        if (!hpack_.decode(hpack_data, headers)) return status::ERROR;

                        for (const auto& h : headers) {
                            if (h.name == ":method") { out_ev.method = h.value; }
                            else if (h.name == ":path") { out_ev.uri = h.value; }
                            else if (h.name == ":authority") {
                                out_ev.headers[out_ev.header_count].key = "Host";
                                out_ev.headers[out_ev.header_count].value = h.value;
                                out_ev.header_count++;
                            }
                            else if (!h.name.empty() && h.name[0] != ':') {
                                out_ev.headers[out_ev.header_count].key = h.name;
                                out_ev.headers[out_ev.header_count].value = h.value;
                                out_ev.header_count++;
                            }
                        }
                        
                        if (end_stream_pending_) {
                            // Finalize existing state before returning
                            out_ev.body = body_accumulator_;
                            if constexpr (requires { out_ev.content_length = body_accumulator_.size(); }) {
                                out_ev.content_length = body_accumulator_.size();
                            }
                            if constexpr (requires { out_ev.has_content_length = true; }) {
                                out_ev.has_content_length = true;
                                out_ev.is_chunked = false;
                            }
                            if constexpr (requires { out_ev.keep_alive = true; }) {
                                out_ev.keep_alive = true;
                            }
                            body_accumulator_.clear();
                            end_stream_pending_ = false;
                            stream.remove_prefix(9 + length);
                            return status::OK; 
                        }
                    } else {
                        expecting_continuation_ = true;
                        continuation_stream_id_ = stream_id;
                        header_accumulator_.assign(hpack_data.data(), hpack_data.size());
                    }
                } else if (type == 0x09) { // CONTINUATION (0x09)
                    if (stream_id != continuation_stream_id_) return status::ERROR;
                    
                    header_accumulator_.append(payload.data(), payload.size());
                    if (flags & 0x04) { // END_HEADERS
                        if constexpr (requires { out_ev.reset(); }) {
                            out_ev.reset();
                        } else {
                            out_ev.method = {}; out_ev.uri = {}; out_ev.header_count = 0; out_ev.body = {};
                            if constexpr (requires { out_ev.has_content_length = false; }) {
                                out_ev.has_content_length = false; out_ev.is_chunked = false; out_ev.content_length = 0;
                            }
                        }
                        if constexpr (requires { out_ev.version = std::string_view{}; }) {
                            out_ev.version = "HTTP/2.0";
                        }
                        body_accumulator_.clear();
                        expecting_continuation_ = false;
                        std::vector<hpack_header> headers;
                        if (!hpack_.decode(header_accumulator_, headers)) return status::ERROR;
                        
                        for (const auto& h : headers) {
                            if (h.name == ":method") { out_ev.method = h.value; }
                            else if (h.name == ":path") { out_ev.uri = h.value; }
                            else if (h.name == ":authority") {
                                out_ev.headers[out_ev.header_count].key = "Host";
                                out_ev.headers[out_ev.header_count].value = h.value;
                                out_ev.header_count++;
                            }
                            else if (!h.name.empty() && h.name[0] != ':') {
                                out_ev.headers[out_ev.header_count].key = h.name;
                                out_ev.headers[out_ev.header_count].value = h.value;
                                out_ev.header_count++;
                            }
                        }
                        header_accumulator_.clear();
                        
                        if (end_stream_pending_) {
                            // This frame marks the end of the stream.
                            // Finalize the HttpEvent and return OK.
                            out_ev.body = body_accumulator_;
                            if constexpr (requires { out_ev.content_length = body_accumulator_.size(); }) {
                                out_ev.content_length = body_accumulator_.size();
                            }
                            if constexpr (requires { out_ev.keep_alive = true; }) {
                                out_ev.keep_alive = true; // HTTP/2 connections are persistent by nature.
                            }
                            // Note: body_accumulator_ is cleared at the start of the next request in the HEADERS handler
                            // to ensure out_ev.body remains valid until the next request cycle begins.
                            end_stream_pending_ = false;
                            stream.remove_prefix(9 + length);
                            return status::OK; // Successfully parsed a complete HTTP/2 stream.
                        }
                    }
                } else if (type == 0) { // DATA (0x00)
                    if (stream_id == 0) return status::ERROR;
                    std::string_view data_payload = payload;
                    if (flags & 0x08) { // PADDED
                        if (data_payload.empty()) return status::ERROR;
                        uint8_t pad_len = data_payload[0];
                        if (data_payload.size() < 1u + pad_len) return status::ERROR;
                        data_payload = data_payload.substr(1, data_payload.size() - 1 - pad_len);
                    }
                    body_accumulator_.append(data_payload);
                    
                    if (flags & 0x01) { // END_STREAM
                        out_ev.body = body_accumulator_;
                        if constexpr (requires { out_ev.content_length = body_accumulator_.size(); }) {
                            out_ev.content_length = body_accumulator_.size();
                        }
                        if constexpr (requires { out_ev.keep_alive = true; }) {
                            out_ev.keep_alive = true;
                        }
                        end_stream_pending_ = false;
                        stream.remove_prefix(9 + length);
                        return status::OK; 
                    }
                } else if (type == 0x03 || type == 0x07) { // RST_STREAM or GOAWAY
                    return status::ERROR;
                } else {
                    // Control Frames: Validate RFC 7540 structural requirements then gracefully skip
                    if (type == 0x04 && (stream_id != 0 || length % 6 != 0)) return status::ERROR; // SETTINGS
                    if (type == 0x06 && (stream_id != 0 || length != 8)) return status::ERROR; // PING
                    if (type == 0x08 && length != 4) return status::ERROR; // WINDOW_UPDATE
                    if (type == 0x02 && (stream_id == 0 || length != 5)) return status::ERROR; // PRIORITY
                }

                stream.remove_prefix(9 + length);
            }
            return status::INCOMPLETE;
        }
    };

} // namespace slabflux::transport