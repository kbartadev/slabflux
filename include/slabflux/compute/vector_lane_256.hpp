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
 * ============================================================================*
 * @brief SLABFLUX Parallel Vector Lane Engine
 * @details Parallel Vector Lane Engine (SIMD Stateful Reduction).
 */

#pragma once
#include <immintrin.h>
#include <cstdint>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/compute/simd_invariant_guard.hpp"

namespace slabflux::compute {

    /*
     * INVARIANTS ENFORCED:
     * - Compile-time: DIW-E Register Pressure & YMM Allocation Envelopes.
     * - Compile-time: Structural size modulus 64 for cache-line packing.
     * - Runtime Boundary: sanctified_states pointer 32-byte alignment.
     * - Runtime Config: SLABFLUX_SIMD_NO_CHECKS bypass.
     */

    /**
     * @brief Fixed-point state engine (AVX2).
     */
    template<size_t NumLanes = 64, bool StrictDIWE = true>
    struct alignas(64) vector_lane_256 {
        static_assert(PerfectSIMDUnroll<NumLanes, 8>, "NumLanes must be a multiple of 8 for SIMD AVX2 alignment.");
        // Structural integrity: Ensure trailing members do not break cache packing
        static_assert(sizeof(int32_t) * NumLanes % 32 == 0, "State matrix breaks 256-bit boundary.");

        alignas(64) int32_t states[NumLanes]{0};
        
        int32_t alpha{922}; 
        int32_t beta{102};  

        uint64_t last_lsn{0}; // Temporal horizon marker

        // Preserved for API parity
        SLAB_FORCE_INLINE void set_expected_identity(uint32_t) noexcept {}

        /** @brief State update. */
        inline void propagate(int32_t signal, uint64_t lsn) noexcept {
            last_lsn = lsn;
            __m256i v_signal = _mm256_set1_epi32(signal);
            __m256i v_alpha = _mm256_set1_epi32(alpha);
            __m256i v_beta = _mm256_set1_epi32(beta);
            __m256i v_signal_beta = _mm256_mullo_epi32(v_signal, v_beta);
            __m256i v_round = _mm256_set1_epi32(512); // Rounding bit (0.5 in 10-bit scale)
            
            if constexpr (StrictDIWE) {
                // DIW-E Pipelining: 4-Way unroll safely allocates inside YMM0-YMM15
                if constexpr (DIWEnforceable<NumLanes, 8, 4, 16>) {
                    for (size_t i = 0; i < NumLanes; i += 32) {
                        __m256i v0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&states[i]));
                        __m256i v1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&states[i + 8]));
                        __m256i v2 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&states[i + 16]));
                        __m256i v3 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&states[i + 24]));

                        v0 = _mm256_add_epi32(_mm256_mullo_epi32(v0, v_alpha), v_signal_beta);
                        v1 = _mm256_add_epi32(_mm256_mullo_epi32(v1, v_alpha), v_signal_beta);
                        v2 = _mm256_add_epi32(_mm256_mullo_epi32(v2, v_alpha), v_signal_beta);
                        v3 = _mm256_add_epi32(_mm256_mullo_epi32(v3, v_alpha), v_signal_beta);

                        v0 = _mm256_srai_epi32(_mm256_add_epi32(v0, v_round), 10);
                        v1 = _mm256_srai_epi32(_mm256_add_epi32(v1, v_round), 10);
                        v2 = _mm256_srai_epi32(_mm256_add_epi32(v2, v_round), 10);
                        v3 = _mm256_srai_epi32(_mm256_add_epi32(v3, v_round), 10);

                        _mm256_store_si256(reinterpret_cast<__m256i*>(&states[i]), v0);
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&states[i + 8]), v1);
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&states[i + 16]), v2);
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&states[i + 24]), v3);
                    }
                } else if constexpr (DIWEnforceable<NumLanes, 8, 2, 16>) {
                    for (size_t i = 0; i < NumLanes; i += 16) {
                        __m256i v0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&states[i]));
                        __m256i v1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&states[i + 8]));
                        v0 = _mm256_add_epi32(_mm256_mullo_epi32(v0, v_alpha), v_signal_beta);
                        v1 = _mm256_add_epi32(_mm256_mullo_epi32(v1, v_alpha), v_signal_beta);
                        v0 = _mm256_srai_epi32(_mm256_add_epi32(v0, v_round), 10);
                        v1 = _mm256_srai_epi32(_mm256_add_epi32(v1, v_round), 10);
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&states[i]), v0);
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&states[i + 8]), v1);
                    }
                } else {
                    static_assert(DIWEnforceable<NumLanes, 8, 1, 16>, "DIW-E Fatal: Missing 8-lane alignment.");
                    for (size_t i = 0; i < NumLanes; i += 8) {
                        __m256i v0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&states[i]));
                        v0 = _mm256_add_epi32(_mm256_mullo_epi32(v0, v_alpha), v_signal_beta);
                        v0 = _mm256_srai_epi32(_mm256_add_epi32(v0, v_round), 10);
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&states[i]), v0);
                    }
                }
            } else {
                for (size_t i = 0; i < NumLanes; i += 8) {
                    __m256i v0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&states[i]));
                    v0 = _mm256_add_epi32(_mm256_mullo_epi32(v0, v_alpha), v_signal_beta);
                    v0 = _mm256_srai_epi32(_mm256_add_epi32(v0, v_round), 10);
                    _mm256_store_si256(reinterpret_cast<__m256i*>(&states[i]), v0);
                }
            }
        }

        /**
         * @brief State sync.
         */
        inline void sync_state(const int32_t* __restrict__ sanctified_states_in, uint64_t lsn) noexcept {
            const int32_t* __restrict__ sanctified_states = sanctified_states_in;
            SLAB_SIMD_CHECK_ALIGNMENT_32(sanctified_states);
            
            last_lsn = lsn;
            for (size_t i = 0; i < NumLanes; i += 8) {
                __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&sanctified_states[i]));
                _mm256_stream_si256(reinterpret_cast<__m256i*>(&states[i]), v);
            }
            _mm_sfence();
        }
    };

} // namespace slabflux::compute
