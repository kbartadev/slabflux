/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file hpack.hpp
 * @brief Stateful HPACK Decoder for HTTP/2.0 Header Compression.
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <deque>
#include <string>
#include "slabflux/transport/hpack_huffman.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::transport {

    struct hpack_header {
        std::string_view name;
        std::string_view value;
    };

    struct dynamic_header {
        std::string name;
        std::string value;
    };

    class hpack_decoder {
        // RFC 7541 Appendix A: Static Table Definition
        static constexpr hpack_header static_table[] = {
            {}, // Index 0 is invalid
            {":authority", ""},
            {":method", "GET"},
            {":method", "POST"},
            {":path", "/"},
            {":path", "/index.html"},
            {":scheme", "http"},
            {":scheme", "https"},
            {":status", "200"},
            {":status", "204"},
            {":status", "206"},
            {":status", "304"},
            {":status", "400"},
            {":status", "404"},
            {":status", "500"},
            {"accept-charset", ""},
            {"accept-encoding", "gzip, deflate"},
            {"accept-language", ""},
            {"accept-ranges", ""},
            {"accept", ""},
            {"access-control-allow-origin", ""},
            {"age", ""},
            {"allow", ""},
            {"authorization", ""},
            {"cache-control", ""},
            {"content-disposition", ""},
            {"content-encoding", ""},
            {"content-language", ""},
            {"content-length", ""},
            {"content-location", ""},
            {"content-range", ""},
            {"content-type", ""},
            {"cookie", ""},
            {"date", ""},
            {"etag", ""},
            {"expect", ""},
            {"expires", ""},
            {"from", ""},
            {"host", ""},
            {"if-match", ""},
            {"if-modified-since", ""},
            {"if-none-match", ""},
            {"if-range", ""},
            {"if-unmodified-since", ""},
            {"last-modified", ""},
            {"link", ""},
            {"location", ""},
            {"max-forwards", ""},
            {"proxy-authenticate", ""},
            {"proxy-authorization", ""},
            {"range", ""},
            {"referer", ""},
            {"refresh", ""},
            {"retry-after", ""},
            {"server", ""},
            {"set-cookie", ""},
            {"strict-transport-security", ""},
            {"transfer-encoding", ""},
            {"user-agent", ""},
            {"vary", ""},
            {"via", ""},
            {"www-authenticate", ""}
        };

        // Dynamic table for session-specific headers
        std::deque<dynamic_header> dynamic_table_;
        size_t dynamic_table_size_{0};
        size_t max_dynamic_table_size_{4096};
        size_t max_dynamic_table_size_limit_{4096};

        char string_arena_[8192];
        size_t arena_offset_{0};

    public:
        /**
         * @brief Decodes a variable-length integer according to RFC 7541, Section 5.1.
         * @param buffer The input buffer, advanced on successful parse.
         * @param prefix_bits The number of bits in the first byte used for the prefix.
         * @param out_value The decoded integer value.
         * @return True on success, false on incomplete data or overflow.
         */
        static bool decode_integer(std::string_view& buffer, uint8_t prefix_bits, uint64_t& out_value) noexcept {
            if (buffer.empty() || prefix_bits < 1 || prefix_bits > 8) return false;

            const uint8_t mask = (1 << prefix_bits) - 1;
            uint64_t value = buffer[0] & mask;
            buffer.remove_prefix(1);

            if (value < mask) {
                out_value = value;
                return true;
            }

            uint64_t m = 0;
            while (!buffer.empty()) {
                uint8_t byte = static_cast<uint8_t>(buffer[0]);
                buffer.remove_prefix(1);
                
                // Check for overflow and prevent infinite 0x80 loops (CVE-2016-1581)
                if (m >= 63) return false;

                value += static_cast<uint64_t>(byte & 0x7F) << m;
                if ((byte & 0x80) == 0) {
                    out_value = value;
                    return true;
                }
                m += 7;
            }

            return false; // Incomplete integer
        }

        /**
         * @brief Decodes a string literal according to RFC 7541, Section 5.2.
         * @note Huffman decoding is not yet implemented.
         */
        bool decode_string(std::string_view& buffer, std::string_view& out_str) {
            if (buffer.empty()) return false;

            bool huffman = (buffer[0] & 0x80) != 0;
            uint64_t len;
            if (!decode_integer(buffer, 7, len)) return false;

            if (len > buffer.size()) return false;

            if (huffman) {
                size_t start_offset = arena_offset_;
                int16_t node = 0;
                for (size_t i = 0; i < len; ++i) {
                    uint8_t b = buffer[i];
                    for (int j = 7; j >= 0; --j) {
                        uint8_t bit = (b >> j) & 1;
                        node = (bit == 0) ? hpack_huffman::huffman_tree[node].left : hpack_huffman::huffman_tree[node].right;
                        if (node < 0) {
                            if (arena_offset_ >= sizeof(string_arena_)) return false; // Arena exhausted
                            string_arena_[arena_offset_++] = static_cast<char>(~node);
                            node = 0; // Reset root
                        }
                    }
                }
                out_str = std::string_view(string_arena_ + start_offset, arena_offset_ - start_offset);
            } else {
                out_str = buffer.substr(0, len);
            }
            buffer.remove_prefix(len);
            return true;
        }

        /**
         * @brief Decodes a full HTTP/2 header block.
         * @param buffer The raw payload of one or more HEADERS/CONTINUATION frames.
         * @param out_headers A vector to store the decoded headers.
         * @return True on success, false on parsing error.
         */
        bool decode(std::string_view buffer, std::vector<hpack_header>& out_headers) {
            arena_offset_ = 0; // Reset allocation arena for this decoding pass
            while (!buffer.empty()) {
                uint8_t first_byte = buffer[0];

                if (first_byte & 0x80) { // 1xxxxxxx: Indexed Header Field
                    uint64_t index;
                    if (!decode_integer(buffer, 7, index) || !get_header_by_index(index, out_headers)) return false;
                } else if ((first_byte & 0xC0) == 0x40) { // 01xxxxxx: Literal w/ Incremental Indexing
                    uint64_t index;
                    if (!decode_integer(buffer, 6, index)) return false;
                    hpack_header header;
                    if (index == 0) { if (!decode_string(buffer, header.name)) return false; } 
                    else { if (!get_header_name_by_index(index, header.name)) return false; }
                    if (!decode_string(buffer, header.value)) return false;
                    out_headers.push_back(header);
                    add_to_dynamic_table(header);
                } else if ((first_byte & 0xF0) == 0x00) { // 0000xxxx: Literal w/o Indexing
                    uint64_t index;
                    if (!decode_integer(buffer, 4, index)) return false;
                    hpack_header header;
                    if (index == 0) { if (!decode_string(buffer, header.name)) return false; } 
                    else { if (!get_header_name_by_index(index, header.name)) return false; }
                    if (!decode_string(buffer, header.value)) return false;
                    out_headers.push_back(header);
                } else if ((first_byte & 0xF0) == 0x10) { // 0001xxxx: Literal, Never Indexed
                    uint64_t index;
                    if (!decode_integer(buffer, 4, index)) return false;
                    hpack_header header;
                    if (index == 0) { if (!decode_string(buffer, header.name)) return false; } 
                    else { if (!get_header_name_by_index(index, header.name)) return false; }
                    if (!decode_string(buffer, header.value)) return false;
                    out_headers.push_back(header);
                } else if ((first_byte & 0xE0) == 0x20) { // 001xxxxx: Dynamic Table Size Update
                    uint64_t size;
                    if (!decode_integer(buffer, 5, size) || size > max_dynamic_table_size_limit_) return false;
                    max_dynamic_table_size_ = size;
                    evict_from_dynamic_table();
                } else {
                    return false; // Should not happen
                }
            }
            return true;
        }

    private:
        bool get_header_by_index(uint64_t index, std::vector<hpack_header>& out_headers) {
            constexpr size_t static_table_size = sizeof(static_table) / sizeof(hpack_header);
            if (index == 0) return false;
            if (index < static_table_size) {
                out_headers.push_back(static_table[index]);
                return true;
            }
            index -= static_table_size;
            if (index < dynamic_table_.size()) {
                out_headers.push_back({dynamic_table_[index].name, dynamic_table_[index].value});
                return true;
            }
            return false;
        }

        bool get_header_name_by_index(uint64_t index, std::string_view& out_name) {
            constexpr size_t static_table_size = sizeof(static_table) / sizeof(hpack_header);
            if (index == 0) return false;
            if (index < static_table_size) { out_name = static_table[index].name; return true; }
            index -= static_table_size;
            if (index < dynamic_table_.size()) { out_name = dynamic_table_[index].name; return true; }
            return false;
        }

        void add_to_dynamic_table(const hpack_header& header) {
            // CRITICAL FIX: HTTP/2 HPACK Use-After-Free (RCE Vector)
            // We MUST copy the string data into local memory BEFORE mutating the dynamic table.
            // If 'header.name' is a string_view pointing to the oldest element in the table, 
            // the subsequent eviction loop will destroy the string underneath the pointer!
            std::string safe_name(header.name);
            std::string safe_value(header.value);
            
            size_t entry_size = safe_name.size() + safe_value.size() + 32;
            if (entry_size > max_dynamic_table_size_) { dynamic_table_.clear(); dynamic_table_size_ = 0; return; }
            
            while (dynamic_table_size_ + entry_size > max_dynamic_table_size_ && !dynamic_table_.empty()) {
                const auto& back = dynamic_table_.back();
                dynamic_table_size_ -= (back.name.size() + back.value.size() + 32);
                dynamic_table_.pop_back();
            }
            dynamic_table_.push_front({std::move(safe_name), std::move(safe_value)});
            dynamic_table_size_ += entry_size;
        }

        void evict_from_dynamic_table() {
            while (dynamic_table_size_ > max_dynamic_table_size_ && !dynamic_table_.empty()) {
                const auto& back = dynamic_table_.back();
                dynamic_table_size_ -= (back.name.size() + back.value.size() + 32);
                dynamic_table_.pop_back();
            }
        }
    };

} // namespace slabflux::transport