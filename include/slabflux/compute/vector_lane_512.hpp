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
#include <cstdint>
#include <immintrin.h>
#include <cstring>
#include "slabflux/compute/simd_invariant_guard.hpp"

namespace slabflux::compute {

    /*
     * INVARIANTS ENFORCED:
     * - Compile-time: DIW-E Register Pressure limits (Unroll blocks max 32 ZMMs).
     * - Compile-time: Structural AVX-512 ZMM mapping boundary validations.
     * - Runtime Boundary: sanctified_states pointer 64-byte physical alignment.
     * - Runtime Config: SLABFLUX_SIMD_NO_CHECKS bypass.
     */

    /**
     * @brief State Engine (AVX-512).
     */
    template<size_t NumLanes = 64, bool StrictDIWE = true>
    struct alignas(64) vector_lane_512 {
        static_assert(PerfectSIMDUnroll<NumLanes, 16>, "NumLanes must be a multiple of 16 for AVX-512 alignment.");
        static_assert(sizeof(int32_t) * NumLanes % 64 == 0, "State array violates cache line bounding.");

        alignas(64) int32_t states[NumLanes]{0};
        
        int32_t alpha{922}; // State retention (0.9 * 1024)
        int32_t beta{102};  // Signal impulse (0.1 * 1024)

        uint64_t last_lsn{0};

#ifndef SLAB_FORCE_INLINE
#if defined(_MSC_VER)
#define SLAB_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SLAB_FORCE_INLINE inline __attribute__((always_inline))
#else
#define SLAB_FORCE_INLINE inline
#endif
#endif

        // Preserved for API parity
        SLAB_FORCE_INLINE void set_expected_identity(uint32_t) noexcept {}

        /**
         * @brief Propagation.
         */
        inline void propagate(int32_t signal, uint64_t lsn = 0) noexcept {
            last_lsn = lsn;
            const __m512i v_alpha = _mm512_set1_epi32(alpha);
            const __m512i v_sig_beta = _mm512_set1_epi32(signal * beta);
            const __m512i v_round = _mm512_set1_epi32(512);
            
            
            if constexpr (StrictDIWE) {
                // DIW-E Pipelining: 4-Way unroll perfectly fits inside ZMM0-ZMM31 
                if constexpr (DIWEnforceable<NumLanes, 16, 4, 32>) {
                    for (size_t i = 0; i < NumLanes; i += 64) {
                        __m512i v0 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&states[i]));
                        __m512i v1 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&states[i + 16]));
                        __m512i v2 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&states[i + 32]));
                        __m512i v3 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&states[i + 48]));

                        v0 = _mm512_add_epi32(_mm512_mullo_epi32(v0, v_alpha), v_sig_beta);
                        v1 = _mm512_add_epi32(_mm512_mullo_epi32(v1, v_alpha), v_sig_beta);
                        v2 = _mm512_add_epi32(_mm512_mullo_epi32(v2, v_alpha), v_sig_beta);
                        v3 = _mm512_add_epi32(_mm512_mullo_epi32(v3, v_alpha), v_sig_beta);

                        v0 = _mm512_srai_epi32(_mm512_add_epi32(v0, v_round), 10);
                        v1 = _mm512_srai_epi32(_mm512_add_epi32(v1, v_round), 10);
                        v2 = _mm512_srai_epi32(_mm512_add_epi32(v2, v_round), 10);
                        v3 = _mm512_srai_epi32(_mm512_add_epi32(v3, v_round), 10);

                        _mm512_store_si512(reinterpret_cast<__m512i*>(&states[i]), v0);
                        _mm512_store_si512(reinterpret_cast<__m512i*>(&states[i + 16]), v1);
                        _mm512_store_si512(reinterpret_cast<__m512i*>(&states[i + 32]), v2);
                        _mm512_store_si512(reinterpret_cast<__m512i*>(&states[i + 48]), v3);
                    }
                } else if constexpr (DIWEnforceable<NumLanes, 16, 2, 32>) {
                    for (size_t i = 0; i < NumLanes; i += 32) {
                        __m512i v0 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&states[i]));
                        __m512i v1 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&states[i + 16]));
                        v0 = _mm512_add_epi32(_mm512_mullo_epi32(v0, v_alpha), v_sig_beta);
                        v1 = _mm512_add_epi32(_mm512_mullo_epi32(v1, v_alpha), v_sig_beta);
                        v0 = _mm512_srai_epi32(_mm512_add_epi32(v0, v_round), 10);
                        v1 = _mm512_srai_epi32(_mm512_add_epi32(v1, v_round), 10);
                        _mm512_store_si512(reinterpret_cast<__m512i*>(&states[i]), v0);
                        _mm512_store_si512(reinterpret_cast<__m512i*>(&states[i + 16]), v1);
                    }
                } else {
                    static_assert(DIWEnforceable<NumLanes, 16, 1, 32>, "DIW-E Fatal: Missing 16-lane alignment.");
                    for (size_t i = 0; i < NumLanes; i += 16) {
                        __m512i v0 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&states[i]));
                        v0 = _mm512_add_epi32(_mm512_mullo_epi32(v0, v_alpha), v_sig_beta);
                        v0 = _mm512_srai_epi32(_mm512_add_epi32(v0, v_round), 10);
                        _mm512_store_si512(reinterpret_cast<__m512i*>(&states[i]), v0);
                    }
                }
            } else {
                for (size_t i = 0; i < NumLanes; i += 16) {
                    __m512i v0 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&states[i]));
                    v0 = _mm512_add_epi32(_mm512_mullo_epi32(v0, v_alpha), v_sig_beta);
                    v0 = _mm512_srai_epi32(_mm512_add_epi32(v0, v_round), 10);
                    _mm512_store_si512(reinterpret_cast<__m512i*>(&states[i]), v0);
                }
            }
        }

        /**
         * @brief State synchronization.
         */
        inline void sync_state(const int32_t* __restrict__ sanctified_states_in, uint64_t lsn) noexcept {
            const int32_t* __restrict__ sanctified_states = sanctified_states_in;
            SLAB_SIMD_CHECK_ALIGNMENT_64(sanctified_states);
            
            last_lsn = lsn;
            for (size_t i = 0; i < NumLanes; i += 32) {
                __m512i v0 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&sanctified_states[i]));
                __m512i v1 = _mm512_load_si512(reinterpret_cast<const __m512i*>(&sanctified_states[i + 16]));
                _mm512_stream_si512(reinterpret_cast<__m512i*>(&states[i]), v0);
                _mm512_stream_si512(reinterpret_cast<__m512i*>(&states[i + 16]), v1);
            }
            _mm_sfence();
        }
    };

} // namespace slabflux::core
    