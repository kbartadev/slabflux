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
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::compute {

    // Stability Invariants
    struct baseline_stabilizer {};
    struct neighbor_weighted_stabilizer {};

    /**
     * @brief Template-Restricted NaN Guard.
     * @details Encapsulates numerical stability invariants and injects 
     * hardware-mapped recovery logic directly into the compute pipeline.
     */
    template<typename Policy>
    struct stability_guard {
        static SLAB_FORCE_INLINE __m512 apply(__m512 data, __mmask16 mask, __m512 baseline) noexcept {
            if constexpr (std::same_as<Policy, baseline_stabilizer>) {
                return _mm512_mask_blend_ps(mask, data, baseline);
            } else if constexpr (std::same_as<Policy, neighbor_weighted_stabilizer>) {
                // Hardware neighbor interpolation (SIMD-local ring)
                __m512i v_raw = _mm512_castps_si512(data);
                __m512 v_prev = _mm512_castsi512_ps(_mm512_alignr_epi32(v_raw, v_raw, 15));
                __m512 v_next = _mm512_castsi512_ps(_mm512_alignr_epi32(v_raw, v_raw, 1));
                __m512 v_smoothed = _mm512_mul_ps(_mm512_add_ps(v_prev, v_next), _mm512_set1_ps(0.5f));
                return _mm512_mask_blend_ps(mask, data, v_smoothed);
            }
        }
    };

    template<typename T, size_t Capacity>
    struct numerical_sanitizer {
        /**
         * @brief Differential Cleansing.
         * @details Employs Template Injection to enforce numerical invariants 
         * without relying on standard library branch-heavy checks.
         */
        template<typename GuardPolicy = baseline_stabilizer>
        static SLAB_HOT uint64_t sanitize(T* elements, T baseline, 
                                          bool weighted = false,
                                          const T* reference = nullptr, 
                                          T critical_drift = 0) noexcept {
            uint64_t cumulative_mask = 0;
            const __m512 v_baseline = _mm512_set1_ps(baseline);
            const __m512 v_drift = _mm512_set1_ps(critical_drift);
            const __m512 v_abs_mask = _mm512_castsi512_ps(_mm512_set1_epi32(0x7FFFFFFF));

            for (size_t i = 0; i < Capacity; i += 16) {
                __m512 v_data = _mm512_load_ps(&elements[i]);
                
                // Detect bit-level corruption (NaN/Inf)
                __mmask16 invalid_mask = _mm512_cmp_ps_mask(v_data, v_data, _CMP_UNORD_Q);
                
                // Differential cleansing against reference
                if (reference && critical_drift > 0) {
                    __m512 v_ref = _mm512_load_ps(&reference[i]);
                    __m512 v_diff = _mm512_and_ps(_mm512_sub_ps(v_data, v_ref), v_abs_mask);
                    invalid_mask |= _mm512_cmp_ps_mask(v_diff, v_drift, _CMP_GT_OQ);
                }

                if (SL_EXPECT_FALSE(invalid_mask != 0)) {
                    cumulative_mask |= (static_cast<uint64_t>(invalid_mask) << (i & 63));

                    __m512 v_correction;
                    if (weighted) {
                        // Weighted neighbor smoothing: Interpolate between left and right neighbors.
                        __m512i v_data_i = _mm512_castps_si512(v_data);
                        __m512 v_prev = _mm512_castsi512_ps(_mm512_alignr_epi32(v_data_i, v_data_i, 15));
                        __m512 v_next = _mm512_castsi512_ps(_mm512_alignr_epi32(v_data_i, v_data_i, 1));
                        v_correction = _mm512_mul_ps(_mm512_add_ps(v_prev, v_next), _mm512_set1_ps(0.5f));
                    } else {
                        v_correction = v_baseline;
                    }

                    __m512 v_sanitized = stability_guard<GuardPolicy>::apply(v_data, invalid_mask, v_baseline);
                    _mm512_store_ps(&elements[i], v_sanitized);
                }
            }
            return cumulative_mask;
        }
    };

} // namespace slabflux::compute