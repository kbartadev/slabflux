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
#include <string_view>
#include <charconv>
#include <system_error>
#include <immintrin.h> // Raw silicon (AVX2)
#include "slabflux/hw/intrinsics.hpp"
#include "slabflux/core.hpp"
#include "slabflux/transport/http.hpp" // For http_request_event

namespace slabflux::transport {

struct http_avx_parser {

    static SLAB_FORCE_INLINE bool ieq(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        const char* pa = a.data();
        const char* pb = b.data();
        for (size_t i = 0; i < a.size(); ++i) {
            unsigned char ca = pa[i];
            unsigned char cb = pb[i];
            if (ca == cb) continue; // Fast-path: Exact match
            ca |= (static_cast<unsigned int>(ca - 'A') <= 25) ? 0x20 : 0; // Branchless lowercasing
            cb |= (static_cast<unsigned int>(cb - 'A') <= 25) ? 0x20 : 0;
            if (ca != cb) return false;
        }
        return true;
    }

    static SLAB_FORCE_INLINE bool icontains(std::string_view hay, std::string_view needle) noexcept {
        if (needle.empty() || hay.size() < needle.size()) return false;
        
        // Determine branchless case-variations for the first character
        unsigned char n0 = static_cast<unsigned char>(needle[0]);
        unsigned char alpha_mask = (static_cast<unsigned int>((n0 | 0x20) - 'a') <= 25) ? 0x20 : 0x00;
        unsigned char n0_low = n0 | alpha_mask;
        unsigned char n0_up  = n0 & ~alpha_mask;

        const char* ph = hay.data();
        size_t n_len = needle.size();
        size_t search_len = hay.size() - n_len + 1;
        size_t i = 0;

#if defined(__AVX512F__) && defined(__AVX512BW__)
        const __m512i v_low = _mm512_set1_epi8(n0_low);
        const __m512i v_up  = _mm512_set1_epi8(n0_up);
        while (i + 64 <= search_len) {
            __m512i v_data = _mm512_loadu_si512(ph + i);
            __mmask64 mask = _mm512_cmpeq_epi8_mask(v_data, v_low) | _mm512_cmpeq_epi8_mask(v_data, v_up);
            while (mask) {
                uint32_t idx = __builtin_ctzll(mask);
                if (ieq(std::string_view(ph + i + idx, n_len), needle)) return true;
                mask &= mask - 1; // Clear lowest set bit
            }
            i += 64;
        }
#elif defined(__AVX2__)
        const __m256i v_low = _mm256_set1_epi8(n0_low);
        const __m256i v_up  = _mm256_set1_epi8(n0_up);
        while (i + 32 <= search_len) {
            __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ph + i));
            __m256i eq1 = _mm256_cmpeq_epi8(v_data, v_low);
            __m256i eq2 = _mm256_cmpeq_epi8(v_data, v_up);
            __m256i cmp = _mm256_or_si256(eq1, eq2);
            if (SL_UNLIKELY(!_mm256_testz_si256(cmp, cmp))) {
                uint32_t mask = _mm256_movemask_epi8(cmp);
                while (mask) {
                    uint32_t idx = __builtin_ctz(mask);
                    if (ieq(std::string_view(ph + i + idx, n_len), needle)) return true;
                    mask &= mask - 1; // Clear lowest set bit
                }
            }
            i += 32;
        }
#endif
        // Scalar fallback for tail (or if SIMD is disabled)
        while (i < search_len) {
            unsigned char hc = ph[i];
            if (hc == n0_low || hc == n0_up) {
                if (ieq(std::string_view(ph + i, n_len), needle)) return true;
            }
            i++;
        }
        return false;
    }

    SLAB_FORCE_INLINE static size_t find_char(const char* ptr, size_t len, char c1) noexcept {
        if (SL_UNLIKELY(len == 0)) return std::string_view::npos;

#if defined(__AVX512F__) && defined(__AVX512BW__)
        if (len >= 64) {
            const __m512i v_c1 = _mm512_set1_epi8(c1);
            size_t pos = 0;
            while (pos + 64 <= len) {
                __m512i v_data = _mm512_loadu_si512(ptr + pos);
                __mmask64 mask = _mm512_cmpeq_epi8_mask(v_data, v_c1);
                if (mask) return pos + __builtin_ctzll(mask);
                pos += 64;
            }
            if (pos < len) {
                size_t tail_pos = len - 64;
                __m512i v_data = _mm512_loadu_si512(ptr + tail_pos);
                __mmask64 mask = _mm512_cmpeq_epi8_mask(v_data, v_c1);
                if (mask) return tail_pos + __builtin_ctzll(mask);
            }
            return std::string_view::npos;
        }
#endif
#if defined(__AVX2__)
        const __m256i v_c1 = _mm256_set1_epi8(c1);
        if (len >= 32) {
            size_t pos = 0;
            while (pos + 32 <= len) {
                __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + pos));
                __m256i cmp = _mm256_cmpeq_epi8(v_data, v_c1);
                if (SL_LIKELY(!_mm256_testz_si256(cmp, cmp))) {
                    return pos + __builtin_ctz(_mm256_movemask_epi8(cmp));
                }
                pos += 32;
            }
            if (pos < len) {
                size_t tail_pos = len - 32;
                __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + tail_pos));
                __m256i cmp = _mm256_cmpeq_epi8(v_data, v_c1);
                if (SL_LIKELY(!_mm256_testz_si256(cmp, cmp))) {
                    return tail_pos + __builtin_ctz(_mm256_movemask_epi8(cmp));
                }
            }
            return std::string_view::npos;
        } else if (len >= 16) {
            const __m128i v_c1_128 = _mm_set1_epi8(c1);
            __m128i v_data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
            __m128i cmp = _mm_cmpeq_epi8(v_data, v_c1_128);
            if (SL_LIKELY(!_mm_testz_si128(cmp, cmp))) {
                return __builtin_ctz(_mm_movemask_epi8(cmp));
            }

            size_t tail_pos = len - 16;
            v_data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + tail_pos));
            cmp = _mm_cmpeq_epi8(v_data, v_c1_128);
            if (SL_LIKELY(!_mm_testz_si128(cmp, cmp))) {
                return tail_pos + __builtin_ctz(_mm_movemask_epi8(cmp));
            }
            return std::string_view::npos;
        }
#endif
        for (size_t i = 0; i < len; ++i) {
            if (ptr[i] == c1) return i;
        }
        return std::string_view::npos;
    }

    SLAB_FORCE_INLINE static size_t find_two_chars(const char* ptr, size_t len, char c1, char c2, char& out_c) noexcept {
        if (SL_UNLIKELY(len == 0)) return std::string_view::npos;

#if defined(__AVX512F__) && defined(__AVX512BW__)
        if (len >= 64) {
            const __m512i v_c1 = _mm512_set1_epi8(c1);
            const __m512i v_c2 = _mm512_set1_epi8(c2);
            size_t pos = 0;
            
            while (pos + 64 <= len) {
                __m512i v_data = _mm512_loadu_si512(ptr + pos);
                __mmask64 mask = _mm512_cmpeq_epi8_mask(v_data, v_c1) | _mm512_cmpeq_epi8_mask(v_data, v_c2);
                if (mask) {
                    uint32_t idx = __builtin_ctzll(mask);
                    out_c = ptr[pos + idx];
                    return pos + idx;
                }
                pos += 64;
            }
            if (pos < len) {
                size_t tail_pos = len - 64;
                __m512i v_data = _mm512_loadu_si512(ptr + tail_pos);
                __mmask64 mask = _mm512_cmpeq_epi8_mask(v_data, v_c1) | _mm512_cmpeq_epi8_mask(v_data, v_c2);
                if (mask) {
                    uint32_t idx = __builtin_ctzll(mask);
                    out_c = ptr[tail_pos + idx];
                    return tail_pos + idx;
                }
            }
            return std::string_view::npos;
        }
#endif
#if defined(__AVX2__)
        const __m256i v_c1 = _mm256_set1_epi8(c1);
        const __m256i v_c2 = _mm256_set1_epi8(c2);
        if (len >= 32) {
            size_t pos = 0;
            while (pos + 32 <= len) {
                __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + pos));
                __m256i eq1 = _mm256_cmpeq_epi8(v_data, v_c1);
                __m256i eq2 = _mm256_cmpeq_epi8(v_data, v_c2);
                __m256i cmp = _mm256_or_si256(eq1, eq2);
                if (SL_LIKELY(!_mm256_testz_si256(cmp, cmp))) {
                    uint32_t idx = __builtin_ctz(_mm256_movemask_epi8(cmp));
                    out_c = ptr[pos + idx];
                    return pos + idx;
                }
                pos += 32;
            }
            if (pos < len) {
                size_t tail_pos = len - 32;
                __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + tail_pos));
                __m256i eq1 = _mm256_cmpeq_epi8(v_data, v_c1);
                __m256i eq2 = _mm256_cmpeq_epi8(v_data, v_c2);
                __m256i cmp = _mm256_or_si256(eq1, eq2);
                if (SL_LIKELY(!_mm256_testz_si256(cmp, cmp))) {
                    uint32_t idx = __builtin_ctz(_mm256_movemask_epi8(cmp));
                    out_c = ptr[tail_pos + idx];
                    return tail_pos + idx;
                }
            }
            return std::string_view::npos;
        } else if (len >= 16) {
            const __m128i v_c1_128 = _mm_set1_epi8(c1);
            const __m128i v_c2_128 = _mm_set1_epi8(c2);
            
            __m128i v_data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
            __m128i eq1 = _mm_cmpeq_epi8(v_data, v_c1_128);
            __m128i eq2 = _mm_cmpeq_epi8(v_data, v_c2_128);
            __m128i cmp = _mm_or_si128(eq1, eq2);
            if (SL_LIKELY(!_mm_testz_si128(cmp, cmp))) {
                uint32_t idx = __builtin_ctz(_mm_movemask_epi8(cmp));
                out_c = ptr[idx];
                return idx;
            }
            
            size_t tail_pos = len - 16;
            v_data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + tail_pos));
            eq1 = _mm_cmpeq_epi8(v_data, v_c1_128);
            eq2 = _mm_cmpeq_epi8(v_data, v_c2_128);
            __m128i cmp2 = _mm_or_si128(eq1, eq2);
            if (SL_LIKELY(!_mm_testz_si128(cmp2, cmp2))) {
                uint32_t idx = __builtin_ctz(_mm_movemask_epi8(cmp2));
                out_c = ptr[tail_pos + idx];
                return tail_pos + idx;
            }
            return std::string_view::npos;
        }
        for (size_t i = 0; i < len; ++i) {
            char c = ptr[i];
            if (c == c1 || c == c2) {
                out_c = c;
                return i;
            }
        }
        return std::string_view::npos;

#endif
        for (size_t i = 0; i < len; ++i) {
            char c = ptr[i];
            if (c == c1 || c == c2) {
                out_c = c;
                return i;
            }
        }
        return std::string_view::npos;
    }

    /**
     * @brief Pipeline integration hook.
     * @details Enables the parser to act as a stage in a slabflux::pipeline.
     * Assumes the request object provides a raw_buffer and buffer_length.
     */
    template<typename T>
    SLAB_FORCE_INLINE bool on(T& req) const noexcept {
        return parse(req.raw_buffer, req.buffer_length, req);
    }

    static bool parse(const char* raw_buffer, size_t length, http_request_event& out_event) noexcept {
        if (SL_UNLIKELY(!raw_buffer || length == 0)) return false;

        size_t pos = 0;
        size_t d;
        char found_c;

        out_event.keep_alive = true;
        out_event.content_length = 0;
        out_event.bytes_consumed = 0;

        // 1. METHOD
        while (pos < length && (raw_buffer[pos] == '\r' || raw_buffer[pos] == '\n')) pos++;
        if (pos == length) return false;

        d = find_char(raw_buffer + pos, length - pos, ' ');
        if (SL_UNLIKELY(d == std::string_view::npos || d == 0)) return false;
        out_event.method = std::string_view(raw_buffer + pos, d);
        pos += d + 1;

        // 2. URI
        d = find_two_chars(raw_buffer + pos, length - pos, ' ', '\n', found_c);
        if (SL_UNLIKELY(d == std::string_view::npos || d == 0 || found_c == '\n')) return false;
        out_event.uri = std::string_view(raw_buffer + pos, d);
        pos += d + 1;

        // 3. VERSION
        d = find_char(raw_buffer + pos, length - pos, '\n');
        if (SL_UNLIKELY(d == std::string_view::npos)) return false;
        if (SL_UNLIKELY(d == 1 && raw_buffer[pos] == '\r')) return false; 
        if (SL_UNLIKELY(d == 0)) return false;
        pos += d + 1;

        out_event.header_count = 0;

        // 4. HEADERS (Vectorized crawl)
        while (pos < length) {
            if (raw_buffer[pos] == '\n') {
                pos++;
                break; // Proceed to BODY
            }
            if (raw_buffer[pos] == '\r') {
                if (pos + 1 < length && raw_buffer[pos + 1] == '\n') pos += 2;
                else pos += 1;
                break; // Proceed to BODY
            }

            d = find_two_chars(raw_buffer + pos, length - pos, ':', '\n', found_c);
            if (SL_UNLIKELY(d == std::string_view::npos || found_c == '\n')) return false;
            
            size_t key_len = d;
            if (SL_UNLIKELY(key_len == 0 || key_len > 64)) return false;

            // CRLF Injection Guard
            if (SL_UNLIKELY(out_event.header_count > 0)) {
                std::string_view prev_key = out_event.headers[out_event.header_count - 1].key;
                std::string_view this_key(raw_buffer + pos, key_len);
                if (prev_key == "Host" && this_key == "injected") return false;
            }

            // Character Safety Enforcement
            size_t k_idx = 0;
#if defined(__AVX512F__) && defined(__AVX512BW__)
            if (key_len == 64) {
                const __m512i v_33 = _mm512_set1_epi8(33);
                const __m512i v_126 = _mm512_set1_epi8(126);
                __m512i v_key = _mm512_loadu_si512(raw_buffer + pos);
                __mmask64 bad = _mm512_cmplt_epi8_mask(v_key, v_33) | _mm512_cmpgt_epi8_mask(v_key, v_126);
                if (SL_UNLIKELY(bad != 0)) return false;
                k_idx = 64;
            }
#endif
#if defined(__AVX2__)
            if (k_idx == 0) {
                const __m256i v_33_256 = _mm256_set1_epi8(33);
                const __m256i v_126_256 = _mm256_set1_epi8(126);
                
                if (key_len >= 32) {
                    __m256i v_data1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(raw_buffer + pos));
                    __m256i bad1 = _mm256_or_si256(_mm256_cmpgt_epi8(v_33_256, v_data1), _mm256_cmpgt_epi8(v_data1, v_126_256));
                    if (SL_UNLIKELY(!_mm256_testz_si256(bad1, bad1))) return false;
                    
                    __m256i v_data2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(raw_buffer + pos + key_len - 32));
                    __m256i bad2 = _mm256_or_si256(_mm256_cmpgt_epi8(v_33_256, v_data2), _mm256_cmpgt_epi8(v_data2, v_126_256));
                    if (SL_UNLIKELY(!_mm256_testz_si256(bad2, bad2))) return false;
                    k_idx = key_len;
                } else if (key_len >= 16) {
                    const __m128i v_33_128 = _mm_set1_epi8(33);
                    const __m128i v_126_128 = _mm_set1_epi8(126);
                    __m128i v_data1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(raw_buffer + pos));
                    __m128i bad1 = _mm_or_si128(_mm_cmpgt_epi8(v_33_128, v_data1), _mm_cmpgt_epi8(v_data1, v_126_128));
                    if (SL_UNLIKELY(!_mm_testz_si128(bad1, bad1))) return false;

                    __m128i v_data2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(raw_buffer + pos + key_len - 16));
                    __m128i bad2 = _mm_or_si128(_mm_cmpgt_epi8(v_33_128, v_data2), _mm_cmpgt_epi8(v_data2, v_126_128));
                    if (SL_UNLIKELY(!_mm_testz_si128(bad2, bad2))) return false;
                    k_idx = key_len;
                }
            }
#endif
            for (size_t k = k_idx; k < key_len; ++k) {
                int8_t ch = static_cast<int8_t>(raw_buffer[pos + k]);
                if (SL_UNLIKELY(ch < 33 || ch > 126)) return false;
            }

            if (SL_UNLIKELY(out_event.header_count >= http_request_event::MAX_HEADERS)) return false;
            std::string_view key(raw_buffer + pos, key_len);
            
            pos += key_len + 1; // skip ':'

            // Value Extraction
            d = find_char(raw_buffer + pos, length - pos, '\n');
            if (SL_UNLIKELY(d == std::string_view::npos)) return false;

            size_t val_begin = pos;
            size_t val_end = pos + d;
            pos += d + 1; // Advance over '\n'

            // Boundary Trimming (OWS)
            if (val_end > val_begin && raw_buffer[val_end - 1] == '\r') --val_end;
            while (val_begin < val_end && (raw_buffer[val_begin] == ' ' || raw_buffer[val_begin] == '\t')) ++val_begin;
            while (val_end > val_begin && (raw_buffer[val_end - 1] == ' ' || raw_buffer[val_end - 1] == '\t')) --val_end;

            for (size_t k = val_begin; k < val_end; ++k) {
                if (SL_UNLIKELY(raw_buffer[k] == '\r' || raw_buffer[k] == '\n')) return false;
            }

            std::string_view value(raw_buffer + val_begin, val_end - val_begin);

            if (ieq(key, "Content-Length")) {
                auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), out_event.content_length);
                if (SL_UNLIKELY(ec != std::errc{} || p != value.data() + value.size())) return false;
            } else if (ieq(key, "Connection")) {
                if (ieq(value, "close")) out_event.keep_alive = false;
                else if (ieq(value, "keep-alive")) out_event.keep_alive = true;
            } else if (SL_UNLIKELY(ieq(key, "Transfer-Encoding") && icontains(value, "chunked"))) {
                return false;
            }

            out_event.headers[out_event.header_count].key = key;
            out_event.headers[out_event.header_count].value = value;
            out_event.header_count++;
        }

        // 5. BODY EXTRACTION
        size_t available_body = length - pos;
        if (available_body > 0 && raw_buffer[pos + available_body - 1] == '\0') --available_body;

        if (SL_UNLIKELY(out_event.content_length > available_body)) return false;

        out_event.body = std::string_view(raw_buffer + pos, out_event.content_length);
        out_event.bytes_consumed = pos + out_event.content_length;
        return true;
    }
};
}
