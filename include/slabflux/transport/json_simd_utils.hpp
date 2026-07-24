/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file json_simd_utils.hpp
 * @brief Hardware-accelerated SIMD scanning logic for JSON string topologies.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <immintrin.h>
#include "slabflux/core.hpp"

namespace slabflux::transport::json {

    /**
     * @brief Vectorized search for '"', '\', or control characters (< 32).
     * @details Employs the `_mm_testz` optimization to bypass movemask latencies
     * and utilizes overlapping tails to eliminate scalar fallback loops.
     */
    static SLAB_FORCE_INLINE size_t find_string_delimiter(const char* buf, size_t len, size_t pos) noexcept {
        if (SL_UNLIKELY(pos >= len)) return std::string_view::npos;

#if defined(__AVX512F__) && defined(__AVX512BW__)
        if (SL_LIKELY(len - pos >= 64)) {
            const __m512i v_quote = _mm512_set1_epi8('"');
            const __m512i v_slash = _mm512_set1_epi8('\\');
            const __m512i v_31 = _mm512_set1_epi8(31);
            while (pos + 64 <= len) {
                __m512i v_data = _mm512_loadu_si512(buf + pos);
                __mmask64 mask = _mm512_cmpeq_epi8_mask(v_data, v_quote) | 
                                 _mm512_cmpeq_epi8_mask(v_data, v_slash) | 
                                 _mm512_cmpeq_epi8_mask(_mm512_max_epu8(v_data, v_31), v_31);
                if (mask) return pos + __builtin_ctzll(mask);
                pos += 64;
            }
            if (pos < len) {
                size_t tail_pos = len - 64;
                __m512i v_data = _mm512_loadu_si512(buf + tail_pos);
                __mmask64 mask = _mm512_cmpeq_epi8_mask(v_data, v_quote) | 
                                 _mm512_cmpeq_epi8_mask(v_data, v_slash) | 
                                 _mm512_cmpeq_epi8_mask(_mm512_max_epu8(v_data, v_31), v_31);
                uint32_t shift = pos - tail_pos;
                mask &= ~((1ULL << shift) - 1);
                if (mask) return tail_pos + __builtin_ctzll(mask);
            }
            return std::string_view::npos;
        }
#endif
#if defined(__AVX2__)
        if (SL_LIKELY(len - pos >= 32)) {
            const __m256i v_quote = _mm256_set1_epi8('"');
            const __m256i v_slash = _mm256_set1_epi8('\\');
            const __m256i v_31 = _mm256_set1_epi8(31);
            while (pos + 32 <= len) {
                __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + pos));
                __m256i eq_q = _mm256_cmpeq_epi8(v_data, v_quote);
                __m256i eq_s = _mm256_cmpeq_epi8(v_data, v_slash);
                __m256i eq_c = _mm256_cmpeq_epi8(_mm256_max_epu8(v_data, v_31), v_31);
                __m256i cmp = _mm256_or_si256(_mm256_or_si256(eq_q, eq_s), eq_c);
                if (SL_UNLIKELY(!_mm256_testz_si256(cmp, cmp))) {
                    return pos + __builtin_ctz(_mm256_movemask_epi8(cmp));
                }
                pos += 32;
            }
            if (pos < len) {
                size_t tail_pos = len - 32;
                __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + tail_pos));
                __m256i eq_q = _mm256_cmpeq_epi8(v_data, v_quote);
                __m256i eq_s = _mm256_cmpeq_epi8(v_data, v_slash);
                __m256i eq_c = _mm256_cmpeq_epi8(_mm256_max_epu8(v_data, v_31), v_31);
                __m256i cmp = _mm256_or_si256(_mm256_or_si256(eq_q, eq_s), eq_c);
                if (SL_UNLIKELY(!_mm256_testz_si256(cmp, cmp))) {
                    uint32_t mask = _mm256_movemask_epi8(cmp);
                    uint32_t shift = pos - tail_pos;
                    mask &= ~((1ULL << shift) - 1);
                    if (mask) return tail_pos + __builtin_ctz(mask);
                }
            }
            return std::string_view::npos;
        }
#endif
        // Scalar fallback purely for extremely short tails
        for (; pos < len; ++pos) {
            unsigned char c = buf[pos];
            if (c == '"' || c == '\\' || c < 32) return pos;
        }
        return std::string_view::npos;
    }

} // namespace slabflux::transport::json