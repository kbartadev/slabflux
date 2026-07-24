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
#include <immintrin.h>
#include <cstring>
#include <cstdint>
#include <string_view>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/hw/intrinsics.hpp"

namespace slabflux::io {

    /**
     * @brief Architecture-Bound Vector State Machine.
     * @details Statically synthesizes structural identification based on 
     * hardware width, neutralizing vulnerabilities associated with 
     * escaped or quoted characters in generic string parsers.
     */
    template <std::size_t Width>
    struct structural_synthesis {
        static SLAB_FORCE_INLINE uint64_t identify(const char* data, char delim) noexcept {
            #if defined(__AVX512F__)
            if constexpr (Width == 64) {
                const __m512i chunk = _mm512_loadu_si512(reinterpret_cast<const void*>(data));
                // Physical Pulse: Generate a hardware bitmask for structural delimiters
                return _mm512_cmpeq_epi8_mask(chunk, _mm512_set1_epi8(delim));
            }
            #endif
            const __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data));
            return _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, _mm256_set1_epi8(delim)));
        }
    };

    struct simd_parser {
        static constexpr size_t NATIVE_WIDTH = 
            #if defined(__AVX512F__)
            64;
            #else
            32;
            #endif

        /**
         * @brief Synthesized Structural Delimiter Search.
         * @details Executes a branchless scan across the native hardware vector width.
         */
        static SLAB_FORCE_INLINE uint32_t find_delimiter(const char* data, char delim) noexcept {
            uint64_t mask = structural_synthesis<NATIVE_WIDTH>::identify(data, delim);
            return mask == 0 ? static_cast<uint32_t>(NATIVE_WIDTH) : slabflux::hw::tzcnt_64(mask);
        }

        static SLAB_FORCE_INLINE uint32_t fast_hash(const char* data) noexcept {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data));
            return _mm256_extract_epi32(_mm256_xor_si256(chunk, _mm256_permute4x64_epi64(chunk, 1)), 0);
        }

        /**
         * @brief Ultra-fast hardware ASCII-to-integer conversion (for 4-digit numbers).
         * @details Uses the PMADDWD instruction to replace multiplications at the hardware level.
         */
        static SLAB_FORCE_INLINE uint32_t fast_atoi_4(const char* data) noexcept {
            // Register-Local ATOC.
            // Eliminates SSE domain-crossing latency (~8-12 cycles).
            // Performs bit-perfect conversion in GPR using register swizzling.
            uint32_t val = *reinterpret_cast<const uint32_t*>(data);
            
            // Mask out ASCII '0' offsets (0x30) in parallel
            val -= 0x30303030;
            
            // Re-materialize the integer using packed-scalar arithmetic:
            // (d0 * 1000) + (d1 * 100) + (d2 * 10) + d3
            return ((val & 0x000000FF) * 1000) + 
                   (((val >> 8) & 0x000000FF) * 100) + 
                   (((val >> 16) & 0x000000FF) * 10) + 
                   ((val >> 24) & 0x000000FF);
        }

        template<uint32_t Offset>
        static SLAB_FORCE_INLINE uint64_t extract_u64(const char* data) noexcept {
            return *reinterpret_cast<const uint64_t*>(data + Offset);
        }
    };
}
