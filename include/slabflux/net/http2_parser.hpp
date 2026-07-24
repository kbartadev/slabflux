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

    public:
        enum class status { OK, INCOMPLETE, ERROR };

        http2_parser() = default;

        void reset() noexcept {
            preface_received_ = false;
            body_accumulator_.clear();
        }

        /**
         * @brief Parses an HTTP/2 stream and hydrates ANY existing HTTP/1.1 event structure.
         * @tparam HttpEvent The target event type (e.g., http_request_event, http_frame).
         */
        template <typename HttpEvent>
        status parse(std::string_view& stream, HttpEvent& out_ev) {
            if (!preface_received_) {
                constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
                // Check if the stream is long enough to contain the preface
                if (stream.size() < preface.size()) return status::INCOMPLETE;
                if (!stream.starts_with(preface)) return status::ERROR;
                preface_received_ = true;
                stream.remove_prefix(preface.size());
            }

            while (stream.size() >= 9) {
                // HTTP/2 frame header: Length (3 bytes), Type (1 byte), Flags (1 byte), R (1 bit), Stream ID (31 bits)
                const uint8_t* ptr = reinterpret_cast<const uint8_t*>(stream.data());
                uint32_t length = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];
                uint8_t type = ptr[3];
                uint8_t flags = ptr[4];
                // uint32_t stream_id = ((ptr[5] & 0x7F) << 24) | (ptr[6] << 16) | (ptr[7] << 8) | ptr[8];

                if (stream.size() < 9u + length) return status::INCOMPLETE;
                if (length > stream.size() - 9) return status::ERROR; // Malformed length

                std::string_view payload = stream.substr(9, length);

                if (type == 1) { // HEADERS (0x01)
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
                    std::vector<hpack_header> headers;
                    std::string_view hpack_data = payload;
                    if (flags & 0x08) { // PADDED
                        if (hpack_data.empty()) return status::ERROR; // Malformed padded frame
                        uint8_t pad_len = static_cast<uint8_t>(hpack_data[0]); // First byte is pad length
                        if (hpack_data.size() < 1u + pad_len) return status::ERROR; // Malformed: padding exceeds frame length
                        hpack_data.remove_prefix(1); // Remove the pad length byte
                        hpack_data.remove_suffix(pad_len); // Remove the actual padding bytes
                    }
                    if (flags & 0x20) { // PRIORITY
                        if (hpack_data.size() < 5) return status::ERROR;
                        hpack_data.remove_prefix(5);
                    }

                    if (!hpack_.decode(hpack_data, headers)) return status::ERROR;

                    for (const auto& h : headers) {
                        if (h.name == ":method") { out_ev.method = h.value; }
                        else if (h.name == ":path") { out_ev.uri = h.value; }
                        else if (h.name == ":authority") {
                            // Transparently map HTTP/2 Authority to HTTP/1.1 Host for legacy routing
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
                } else if (type == 0) { // DATA (0x00)
                    {
                        std::string_view data_payload = payload;
                        if (flags & 0x08) { // PADDED
                            if (data_payload.empty()) return status::ERROR;
                            uint8_t pad_len = static_cast<uint8_t>(data_payload[0]);
                            if (data_payload.size() < 1u + pad_len) return status::ERROR;
                            data_payload = data_payload.substr(1, data_payload.size() - 1 - pad_len);
                        }
                        body_accumulator_.append(data_payload);
                    }
                } else if (type == 3) { // RST_STREAM
                    return status::ERROR;
                }

                stream.remove_prefix(9 + length);

                if (flags & 0x01) { // END_STREAM
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
                    return status::OK; // Successfully parsed a complete HTTP/2 stream.
                }
            }
            // If the loop finishes without an END_STREAM flag, it means more frames are expected.
            return status::INCOMPLETE;
        }
    };

} // namespace slabflux::transport