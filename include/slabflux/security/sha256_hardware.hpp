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
 * ============================================================================* @file sha256_hardware.hpp
 * @brief Zero-Allocation Hardware SHA-256 (Intel SHA Extensions).
 * @details Bypasses OpenSSL overhead by executing raw SHA-NI 
 * intrinsics directly on the L1 cache.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/endian.hpp"

namespace slabflux::security {

    struct sha256_hardware {
        // Round Constants (K) for SHA-256
        alignas(64) static constexpr uint32_t K256[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        /**
         * @brief Intel SHA-NI 64-byte Block Compressor.
         * @details Compresses a 512-bit block into the 256-bit state using purely hardware ops.
         */
        static SLAB_HOT void compress_block(uint32_t state[8], const uint8_t* block) noexcept {
            __m128i state0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[0]));
            __m128i state1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[4]));

            // SHA256 relies on big-endian words. We must swap bytes on x86 processors.
            const __m128i shuf_mask = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
            __m128i msg0 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 0)), shuf_mask);
            __m128i msg1 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 16)), shuf_mask);
            __m128i msg2 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 32)), shuf_mask);
            __m128i msg3 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 48)), shuf_mask);

            __m128i abcd = state0;
            __m128i efgh = state1;
            __m128i wk;

            // 16-Round Unrolled Hardware Macros
            #define SHA256_RNDS(m, k) \
                wk = _mm_add_epi32(m, _mm_load_si128(reinterpret_cast<const __m128i*>(&K256[k]))); \
                state1 = _mm_sha256rnds2_epu32(state1, state0, wk); \
                state0 = _mm_sha256rnds2_epu32(state0, state1, _mm_shuffle_epi32(wk, 0x0E));

            #define SHA256_MSG(m0, m1, m2, m3) \
                m0 = _mm_sha256msg1_epu32(m0, m1); \
                m0 = _mm_add_epi32(m0, _mm_alignr_epi8(m3, m2, 4)); \
                m0 = _mm_sha256msg2_epu32(m0, m3);

            // Rounds 0 - 15 (Direct from Message Block)
            SHA256_RNDS(msg0, 0); SHA256_RNDS(msg1, 4);
            SHA256_RNDS(msg2, 8); SHA256_RNDS(msg3, 12);
            
            // Rounds 16 - 63 (Hardware Message Scheduling)
            SHA256_MSG(msg0, msg1, msg2, msg3); SHA256_RNDS(msg0, 16);
            SHA256_MSG(msg1, msg2, msg3, msg0); SHA256_RNDS(msg1, 20);
            SHA256_MSG(msg2, msg3, msg0, msg1); SHA256_RNDS(msg2, 24);
            SHA256_MSG(msg3, msg0, msg1, msg2); SHA256_RNDS(msg3, 28);
            
            SHA256_MSG(msg0, msg1, msg2, msg3); SHA256_RNDS(msg0, 32);
            SHA256_MSG(msg1, msg2, msg3, msg0); SHA256_RNDS(msg1, 36);
            SHA256_MSG(msg2, msg3, msg0, msg1); SHA256_RNDS(msg2, 40);
            SHA256_MSG(msg3, msg0, msg1, msg2); SHA256_RNDS(msg3, 44);
            
            SHA256_MSG(msg0, msg1, msg2, msg3); SHA256_RNDS(msg0, 48);
            SHA256_MSG(msg1, msg2, msg3, msg0); SHA256_RNDS(msg1, 52);
            SHA256_MSG(msg2, msg3, msg0, msg1); SHA256_RNDS(msg2, 56);
            SHA256_MSG(msg3, msg0, msg1, msg2); SHA256_RNDS(msg3, 60);

            // Feed-forward addition (incorporating the previous state block)
            state0 = _mm_add_epi32(state0, abcd);
            state1 = _mm_add_epi32(state1, efgh);

            _mm_storeu_si128(reinterpret_cast<__m128i*>(&state[0]), state0);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&state[4]), state1);

            #undef SHA256_RNDS
            #undef SHA256_MSG
        }

        /**
         * @brief Statically calculates the ultimate RFC padding payload.
         */
        static SLAB_HOT void finalize(uint32_t state[8], uint8_t* transcript_buffer, uint32_t buffer_length, uint64_t total_bytes) noexcept {
            transcript_buffer[buffer_length++] = 0x80; // Padding separator bit
            
            if (buffer_length > 56) {
                std::memset(transcript_buffer + buffer_length, 0, 64 - buffer_length);
                compress_block(state, transcript_buffer);
                buffer_length = 0;
            }

            std::memset(transcript_buffer + buffer_length, 0, 56 - buffer_length);
            
            // Write the total bit length (Big Endian) at the exact end of the block
            uint64_t total_bits = core::endian::host_to_network64(total_bytes * 8);
            std::memcpy(transcript_buffer + 56, &total_bits, 8);
            
            compress_block(state, transcript_buffer);
            
            // Final Byte Swap for Endian Normalization Output
            for (int i = 0; i < 8; ++i) {
                state[i] = core::endian::host_to_network32(state[i]);
            }
        }
    };

    /**
     * @brief Stateful wrapper for continuous SHA-256 updates.
     * @details Statically sized to fit into local L1 cache lines (104 bytes).
     */
    struct sha256_state {
        uint32_t h[8]{
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        uint8_t buffer[64]{0};
        uint32_t length{0};
        uint64_t total_bytes{0};

        SLAB_FORCE_INLINE void update(const uint8_t* data, size_t len) noexcept {
            size_t pos = 0;
            total_bytes += len;
            if (length > 0) {
                size_t space = 64 - length;
                size_t chunk = (len < space) ? len : space;
                std::memcpy(buffer + length, data, chunk);
                length += chunk;
                pos += chunk;
                if (length == 64) {
                    sha256_hardware::compress_block(h, buffer);
                    length = 0;
                }
            }
            while (pos + 64 <= len) {
                sha256_hardware::compress_block(h, data + pos);
                pos += 64;
            }
            if (pos < len) {
                std::memcpy(buffer, data + pos, len - pos);
                length = static_cast<uint32_t>(len - pos);
            }
        }

        SLAB_FORCE_INLINE void finalize(uint8_t out[32]) noexcept {
            sha256_hardware::finalize(h, buffer, length, total_bytes);
            for (int i = 0; i < 8; ++i) {
                uint32_t be = h[i];
                std::memcpy(out + i * 4, &be, 4);
            }
        }
    };

} // namespace slabflux::security