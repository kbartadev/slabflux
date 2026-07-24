/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file json_producer.hpp
 * @brief Zero-Allocation, SIMD-Accelerated RFC 8259 JSON Builder.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <charconv>
#include <cstdio>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/transport/json_simd_utils.hpp"

namespace slabflux::transport {

    class json_producer {
    private:
        char* SLAB_RESTRICT buffer_;
        size_t capacity_;
        size_t cursor_{0};

        uint64_t comma_mask_{0};
        uint32_t depth_{0};
        bool needs_comma_{false};

        SLAB_FORCE_INLINE bool ensure_capacity(size_t required_bytes) const noexcept {
            return SL_EXPECT_TRUE(required_bytes <= capacity_ - cursor_);
        }

        SLAB_FORCE_INLINE void comma_check() noexcept {
            if (needs_comma_) { buffer_[cursor_++] = ','; }
        }

        SLAB_FORCE_INLINE void push_comma_state(bool initial) noexcept {
            if (needs_comma_) comma_mask_ |= (1ULL << depth_);
            else comma_mask_ &= ~(1ULL << depth_);
            depth_++;
            needs_comma_ = initial;
        }

        SLAB_FORCE_INLINE void pop_comma_state() noexcept {
            depth_--;
            needs_comma_ = (comma_mask_ & (1ULL << depth_)) != 0;
        }

    public:
        explicit constexpr json_producer(char* SLAB_RESTRICT outbound_buffer, size_t max_capacity) noexcept
            : buffer_(outbound_buffer), capacity_(max_capacity) {}

        SLAB_FORCE_INLINE void reset() noexcept { cursor_ = 0; depth_ = 0; comma_mask_ = 0; needs_comma_ = false; }
        SLAB_FORCE_INLINE size_t bytes_written() const noexcept { return cursor_; }
        SLAB_FORCE_INLINE std::string_view view() const noexcept { return {buffer_, cursor_}; }

        SLAB_FORCE_INLINE bool begin_object() noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(2))) return false;
            comma_check();
            buffer_[cursor_++] = '{';
            push_comma_state(false);
            return true;
        }

        SLAB_FORCE_INLINE bool end_object() noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(1))) return false;
            buffer_[cursor_++] = '}';
            pop_comma_state();
            needs_comma_ = true;
            return true;
        }

        SLAB_FORCE_INLINE bool begin_array() noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(2))) return false;
            comma_check();
            buffer_[cursor_++] = '[';
            push_comma_state(false);
            return true;
        }

        SLAB_FORCE_INLINE bool end_array() noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(1))) return false;
            buffer_[cursor_++] = ']';
            pop_comma_state();
            needs_comma_ = true;
            return true;
        }

        SLAB_FORCE_INLINE bool add_key(std::string_view key) noexcept {
            if (SL_EXPECT_FALSE(!add_string(key))) return false;
            if (SL_EXPECT_FALSE(!ensure_capacity(1))) return false;
            buffer_[cursor_++] = ':';
            needs_comma_ = false; // Next value shouldn't have a preceding comma
            return true;
        }

        SLAB_FORCE_INLINE bool add_string(std::string_view s) noexcept {
            // Worst case: every character needs \u00XX escaping (1 char -> 6 chars) + 2 quotes
            size_t req_len = s.size() * 6 + 2;
            if (SL_EXPECT_FALSE(req_len < s.size())) return false; // Overflow guard
            if (SL_EXPECT_FALSE(!ensure_capacity(req_len))) return false;
            comma_check();
            buffer_[cursor_++] = '"';
            size_t pos = 0; // Current position in the input string_view 's'
            
            // SIMD-Accelerated String Escaping
            while (pos < s.size()) {
                size_t esc = json::find_string_delimiter(s.data(), s.size(), pos);
                size_t chunk = (esc == std::string_view::npos ? s.size() : esc) - pos;
                if (chunk > 0) {
                    if (SL_EXPECT_FALSE(!ensure_capacity(chunk))) return false;
                    std::memcpy(buffer_ + cursor_, s.data() + pos, chunk);
                    cursor_ += chunk;
                }
                if (esc == std::string_view::npos) break;
                
                if (SL_EXPECT_FALSE(!ensure_capacity(6))) return false;
                unsigned char c = s[esc];
                if (c == '"') { buffer_[cursor_++] = '\\'; buffer_[cursor_++] = '"'; }
                else if (c == '\\') { buffer_[cursor_++] = '\\'; buffer_[cursor_++] = '\\'; }
                else if (c == '\n') { buffer_[cursor_++] = '\\'; buffer_[cursor_++] = 'n'; }
                else if (c == '\r') { buffer_[cursor_++] = '\\'; buffer_[cursor_++] = 'r'; }
                else if (c == '\t') { buffer_[cursor_++] = '\\'; buffer_[cursor_++] = 't'; }
                else {
                    buffer_[cursor_++] = '\\'; buffer_[cursor_++] = 'u';
                    buffer_[cursor_++] = '0'; buffer_[cursor_++] = '0';
                    auto hex_char = [](uint8_t v) { return v < 10 ? '0' + v : 'a' + (v - 10); };
                    buffer_[cursor_++] = hex_char((c >> 4) & 0x0F);
                    buffer_[cursor_++] = hex_char(c & 0x0F);
                }
                pos = esc + 1;
            }
            if (SL_EXPECT_FALSE(!ensure_capacity(1))) return false;
            buffer_[cursor_++] = '"';
            needs_comma_ = true;
            return true;
        }

        template <typename T>
        SLAB_FORCE_INLINE bool add_number(T val) noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(32))) return false;
            comma_check();
            auto [p, ec] = std::to_chars(buffer_ + cursor_, buffer_ + capacity_, val);
            if (SL_EXPECT_FALSE(ec != std::errc{})) {
                // Fallback for GCC environments lacking floating-point to_chars support
                if constexpr (std::is_floating_point_v<T>) {
                    int written = std::snprintf(buffer_ + cursor_, capacity_ - cursor_, "%g", static_cast<double>(val));
                    if (written <= 0 || written >= static_cast<int>(capacity_ - cursor_)) return false;
                    p = buffer_ + cursor_ + written;
                } else {
                    return false;
                }
            }
            cursor_ = p - buffer_;
            needs_comma_ = true;
            return true;
        }

        SLAB_FORCE_INLINE bool add_bool(bool val) noexcept {
            if (val) {
                if (SL_EXPECT_FALSE(!ensure_capacity(5))) return false;
                comma_check(); std::memcpy(buffer_ + cursor_, "true", 4); cursor_ += 4;
            } else {
                if (SL_EXPECT_FALSE(!ensure_capacity(6))) return false;
                comma_check(); std::memcpy(buffer_ + cursor_, "false", 5); cursor_ += 5;
            }
            needs_comma_ = true;
            return true;
        }

        SLAB_FORCE_INLINE bool add_null() noexcept {
            if (SL_EXPECT_FALSE(!ensure_capacity(5))) return false;
            comma_check(); std::memcpy(buffer_ + cursor_, "null", 4); cursor_ += 4;
            needs_comma_ = true;
            return true;
        }
    };

} // namespace slabflux::transport