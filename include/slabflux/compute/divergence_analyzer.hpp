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
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <algorithm>
#include <fstream>
#include <immintrin.h>
#include "numerical_sanitizer.hpp"
#include "slabflux/sys/blackbox_recorder.hpp"
#include "slabflux/security/kinetic_inscription.hpp"

namespace slabflux::compute {

    template<typename T, size_t Capacity>
    struct alignas(4096) avx512_search_backend;

    /**
     * @brief Policy for state drift detection and reporting.
     */
    enum class drift_policy : uint8_t {
        BIT_IDENTICAL,   // Report any non-zero difference
        MSE_BASED,       // Report if aggregate MSE exceeds threshold
        PSNR_BASED       // Report if aggregate PSNR falls below threshold
    };

    /**
     * @brief Divergence Analyzer.
     * @details Compares the current engine state against a reference state
     * and identifies specific SIMD lanes that have drifted.
     */
    template<typename T, size_t Capacity>
    class divergence_analyzer {
    public:
        struct drift_offender {
            size_t index;
            T delta;
        };

        /**
         * @brief Rapidly checks for NaN or Infinity values using AVX-512.
         */
        static SLAB_HOT bool has_anomalies(const auto& block) noexcept {
            if constexpr (Capacity == 64 && std::is_same_v<T, float>) {
                #pragma unroll 4
                for (size_t i = 0; i < 64; i += 16) {
                    __m512 v = _mm512_load_ps(&block.elements[i]);
                    // Safety Mask: 0x99 detects QNaN, SNaN, and +/- Infinity.
                    if (_mm512_fpclass_ps_mask(v, 0x99) != 0) return true;
                }
                return false;
            }
            
            for (size_t i = 0; i < Capacity; ++i) {
                if (SL_EXPECT_FALSE(!std::isfinite(block.elements[i]))) return true;
            }
            return false;
        }

        /**
         * @brief Computes the SIMD-accelerated Mean Squared Error (MSE) of the state drift.
         * @details Refactored to ignore lanes that have been sanitized (NaN/Inf).
         * MSE = sum_masked((live_i - ref_i)^2) / (Capacity - Anomalies)
         */
        static SLAB_HOT T calculate_mse(const auto& live,
                                        const auto& reference) noexcept {
            if constexpr (std::is_same_v<T, float>) {
#if defined(__AVX512F__)
                __m512 v_sum_sq = _mm512_setzero_ps();
                uint32_t total_ignored_vectorized = 0;
                size_t i = 0;
                static constexpr size_t VEC_WIDTH = 16; // 16 floats in __m512

                for (; i + VEC_WIDTH <= Capacity; i += VEC_WIDTH) {
                    __m512 v_live = _mm512_load_ps(&live.elements[i]);
                    __m512 v_ref  = _mm512_load_ps(&reference.elements[i]);

                    // Masked Logic: Identify lanes that are numerical anomalies
                    __mmask16 anomaly_mask = _mm512_fpclass_ps_mask(v_live, 0x99);
                    total_ignored_vectorized += __builtin_popcount(anomaly_mask);

                    __m512 v_diff = _mm512_sub_ps(v_live, v_ref);
                    // Only add squared difference for healthy lanes
                    v_sum_sq = _mm512_mask_fmadd_ps(v_diff, ~anomaly_mask, v_diff, v_sum_sq);
                }
                
                const uint32_t valid_lanes = Capacity - total_ignored_vectorized;
                if (SL_EXPECT_FALSE(valid_lanes == 0)) return 0;
                return _mm512_reduce_add_ps(v_sum_sq) / static_cast<T>(valid_lanes);
                
#elif defined(__AVX2__)
                __m256 v_sum_sq = _mm256_setzero_ps();
                uint32_t total_ignored_vectorized = 0;
                size_t i = 0;
                static constexpr size_t VEC_WIDTH = 8; // 8 floats in __m256
                const __m256 v_inf = _mm256_set1_ps(std::numeric_limits<float>::infinity());
                const __m256 v_neg_inf = _mm256_set1_ps(-std::numeric_limits<float>::infinity());

                for (; i + VEC_WIDTH <= Capacity; i += VEC_WIDTH) {
                    __m256 v_live = _mm256_loadu_ps(&live.elements[i]);
                    __m256 v_ref  = _mm256_loadu_ps(&reference.elements[i]);

                    // AVX2: Detect NaN or Inf for std::isfinite equivalent
                    __m256 v_nan_mask = _mm256_cmp_ps(v_live, v_live, _CMP_UNORD_Q); // NaN check
                    __m256 v_pos_inf_mask = _mm256_cmp_ps(v_live, v_inf, _CMP_EQ_OQ); // +Inf check
                    __m256 v_neg_inf_mask = _mm256_cmp_ps(v_live, v_neg_inf, _CMP_EQ_OQ); // -Inf check
                    __m256 v_not_finite_mask = _mm256_or_ps(_mm256_or_ps(v_nan_mask, v_pos_inf_mask), v_neg_inf_mask);
                    
                    total_ignored_vectorized += _mm256_movemask_ps(v_not_finite_mask);

                    __m256 v_diff = _mm256_sub_ps(v_live, v_ref);
                    __m256 v_diff_sq = _mm256_mul_ps(v_diff, v_diff);
                    // Mask out non-finite contributions by blending with zero
                    v_sum_sq = _mm256_add_ps(v_sum_sq, _mm256_andnot_ps(v_not_finite_mask, v_diff_sq));
                }

                // Horizontal reduction for AVX2
                alignas(32) float tmp[VEC_WIDTH];
                _mm256_store_ps(tmp, v_sum_sq);
                T final_sum_sq = 0;
                for (size_t j = 0; j < VEC_WIDTH; ++j) final_sum_sq += tmp[j];

                // Scalar tail processing for remaining elements
                size_t scalar_valid_count = 0;
                for (; i < Capacity; ++i) {
                    if (std::isfinite(live.elements[i])) {
                        T d = live.elements[i] - reference.elements[i];
                        final_sum_sq += (d * d);
                        scalar_valid_count++;
                    }
                }
                const uint32_t final_valid_lanes = (Capacity - total_ignored_vectorized) + scalar_valid_count;
                if (SL_EXPECT_FALSE(final_valid_lanes == 0)) return 0;
                return final_sum_sq / static_cast<T>(final_valid_lanes);
#else // No AVX512 or AVX2, fallback to scalar
                T sum_sq = 0;
                size_t valid_count = 0;
                for (size_t i = 0; i < Capacity; ++i) {
                    if (std::isfinite(live.elements[i])) {
                        T d = live.elements[i] - reference.elements[i];
                        sum_sq += (d * d);
                        valid_count++;
                    }
                }
                return valid_count > 0 ? (sum_sq / static_cast<T>(valid_count)) : 0;
#endif
            } else { // Non-float types, use scalar
                T sum_sq = 0;
                size_t valid_count = 0;
                for (size_t i = 0; i < Capacity; ++i) {
                    // For non-float types, std::isfinite might not be applicable or needed.
                    // Assuming all elements are "valid" for non-float types for simplicity,
                    // or a custom validity check would be needed for specific integer types.
                    // For now, treat all as valid.
                    T d = live.elements[i] - reference.elements[i];
                    sum_sq += (d * d);
                    valid_count++;
                }
                return valid_count > 0 ? (sum_sq / static_cast<T>(valid_count)) : 0;
            }
        }

        /**
         * @brief Computes the SIMD-accelerated Peak Signal-to-Noise Ratio (PSNR).
         * @details PSNR = 10 * log10(max_val^2 / MSE)
         * @param max_val The maximum possible value of the signal (e.g. 1.0f).
         */
        static SLAB_HOT T calculate_psnr(const auto& live,
                                         const auto& reference,
                                         T max_val = 1.0f) noexcept {
            T mse = calculate_mse(live, reference);
            if (mse <= 0) return 100.0f; // Cap at 100dB for perfect matches
            return static_cast<T>(10.0 * std::log10((static_cast<double>(max_val) * max_val) / mse));
        }

        /**
         * @brief Rapidly checks if two state blocks are bit-identical using SIMD.
         * @details Returns true if blocks are identical, false if they have drifted.
         */
        static SLAB_HOT bool is_identical(const auto& live,
                                          const auto& reference,
                                         T precision_delta = 0) noexcept {
            if constexpr (std::is_same_v<T, float>) {
                if (precision_delta == 0) return std::memcmp(&live, &reference, sizeof(live)) == 0;

#if defined(__AVX512F__)
                const __m512 v_delta = _mm512_set1_ps(precision_delta);
                const __m512i v_abs_mask = _mm512_set1_epi32(0x7FFFFFFF);
                size_t i = 0;
                static constexpr size_t VEC_WIDTH = 16;

                for (; i + VEC_WIDTH <= Capacity; i += VEC_WIDTH) {
                    __m512 v_live = _mm512_load_ps(&live.elements[i]);
                    __m512 v_ref  = _mm512_load_ps(&reference.elements[i]);
                    
                    // Standard: Ensure class parity (NaN/Inf status)
                    // before evaluating precision epsilon.
                    __mmask16 cls_live = _mm512_fpclass_ps_mask(v_live, 0x99);
                    __mmask16 cls_ref  = _mm512_fpclass_ps_mask(v_ref, 0x99);
                    if (SL_EXPECT_FALSE(cls_live != cls_ref)) [[unlikely]] return false;

                    __m512 v_diff = _mm512_sub_ps(v_live, v_ref);
                    v_diff = _mm512_castsi512_ps(_mm512_and_epi32(_mm512_castps_si512(v_diff), v_abs_mask));
                    if (_mm512_cmp_ps_mask(v_diff, v_delta, _CMP_GT_OQ) != 0) return false;
                }
                // Scalar tail
                for (; i < Capacity; ++i) {
                    if (std::fpclassify(live.elements[i]) != std::fpclassify(reference.elements[i])) return false;
                    if (std::abs(live.elements[i] - reference.elements[i]) > precision_delta) return false;
                }
                return true;
#elif defined(__AVX2__)
                const __m256 v_delta = _mm256_set1_ps(precision_delta);
                const __m256i v_abs_mask = _mm256_set1_epi32(0x7FFFFFFF);
                const __m256 v_inf = _mm256_set1_ps(std::numeric_limits<float>::infinity());
                const __m256 v_neg_inf = _mm256_set1_ps(-std::numeric_limits<float>::infinity());
                size_t i = 0;
                static constexpr size_t VEC_WIDTH = 8;

                for (; i + VEC_WIDTH <= Capacity; i += VEC_WIDTH) {
                    __m256 v_live = _mm256_loadu_ps(&live.elements[i]);
                    __m256 v_ref  = _mm256_loadu_ps(&reference.elements[i]);

                    // AVX2: Check for class parity (NaN/Inf)
                    __m256 v_not_finite_live = _mm256_or_ps(_mm256_or_ps(_mm256_cmp_ps(v_live, v_live, _CMP_UNORD_Q), _mm256_cmp_ps(v_live, v_inf, _CMP_EQ_OQ)), _mm256_cmp_ps(v_live, v_neg_inf, _CMP_EQ_OQ));
                    __m256 v_not_finite_ref  = _mm256_or_ps(_mm256_or_ps(_mm256_cmp_ps(v_ref, v_ref, _CMP_UNORD_Q), _mm256_cmp_ps(v_ref, v_inf, _CMP_EQ_OQ)), _mm256_cmp_ps(v_ref, v_neg_inf, _CMP_EQ_OQ));
                    if (_mm256_movemask_ps(v_not_finite_live) != _mm256_movemask_ps(v_not_finite_ref)) return false;

                    __m256 v_diff = _mm256_sub_ps(v_live, v_ref);
                    v_diff = _mm256_and_ps(v_diff, _mm256_castsi256_ps(v_abs_mask)); // Absolute difference
                    if (_mm256_movemask_ps(_mm256_cmp_ps(v_diff, v_delta, _CMP_GT_OQ)) != 0) return false;
                }
                // Scalar tail
                for (; i < Capacity; ++i) {
                    if (std::fpclassify(live.elements[i]) != std::fpclassify(reference.elements[i])) return false;
                    if (std::abs(live.elements[i] - reference.elements[i]) > precision_delta) return false;
                }
                return true;
#else // No AVX512 or AVX2, fallback to scalar
                for (size_t i = 0; i < Capacity; ++i) {
                    if (std::fpclassify(live.elements[i]) != std::fpclassify(reference.elements[i])) return false;
                    if (std::abs(live.elements[i] - reference.elements[i]) > precision_delta) return false;
                }
                return true;
#endif
            } else { // Non-float types, use memcmp for precision_delta == 0, or scalar for non-zero delta
                if (precision_delta == 0) return std::memcmp(&live, &reference, sizeof(live)) == 0;
                for (size_t i = 0; i < Capacity; ++i) {
                    if (live.elements[i] != reference.elements[i]) return false; // Assuming direct equality for non-float types with delta
                }
                return true;
            }
        }

        /**
         * @brief Scans and dumps differing SIMD lanes.
         * @param live The current state block from the engine.
         * @param reference The known-good state block (e.g., from a snapshot).
         * @param lsn The Logical Sequence Number where divergence was detected.
         */
        static SLAB_COLD T analyze(const auto& live,
                                   const auto& reference,
                                     uint64_t lsn,
                                     drift_policy policy = drift_policy::BIT_IDENTICAL,
                                     T precision_delta = 0,
                                     T baseline = 0,
                                     T critical_drift = 0,
                                     const security::semiotic_tapestry* tapestry = nullptr) noexcept {
            const T mse = calculate_mse(live, reference);

            uint64_t live_mask = 0;
            uint64_t ref_mask = 0;
            if constexpr (Capacity == 64 && std::is_same_v<T, float>) {
                for (size_t i = 0; i < 4; ++i) {
                    live_mask |= (static_cast<uint64_t>(_mm512_fpclass_ps_mask(_mm512_load_ps(&live.elements[i*16]), 0x99)) << (i*16));
                    ref_mask  |= (static_cast<uint64_t>(_mm512_fpclass_ps_mask(_mm512_load_ps(&reference.elements[i*16]), 0x99)) << (i*16));
                }
            } else {
                for (size_t i = 0; i < Capacity && i < 64; ++i) {
                    if (!std::isfinite(live.elements[i])) live_mask |= (1ULL << i);
                    if (!std::isfinite(reference.elements[i])) ref_mask |= (1ULL << i);
                }
            }

            // Normalization: Use masked MSE (healthy lanes only) for impact reporting.
            const uint32_t total_ignored = static_cast<uint32_t>(_mm_popcnt_u64(live_mask));
            const uint32_t valid_lanes = Capacity - total_ignored;

            auto clean_ref = reference;
            numerical_sanitizer<T, Capacity>::sanitize(clean_ref.elements, baseline);

            if (policy == drift_policy::BIT_IDENTICAL) {
                if (is_identical(live, clean_ref, 0)) return mse;
            } else if (policy == drift_policy::MSE_BASED) {
                if (calculate_mse(live, clean_ref) <= precision_delta) return mse;
            } else if (policy == drift_policy::PSNR_BASED) {
                if (calculate_psnr(live, clean_ref) >= precision_delta) return mse;
            }

            const T psnr = calculate_psnr(live, clean_ref);

            std::vector<drift_offender> offenders;
            offenders.reserve(64);

            for (size_t i = 0; i < Capacity; ++i) {
                T diff = (live.elements[i] > clean_ref.elements[i]) ? 
                         (live.elements[i] - clean_ref.elements[i]) : 
                         (clean_ref.elements[i] - live.elements[i]);
                if (SL_EXPECT_FALSE(diff > precision_delta)) {
                    offenders.push_back({i, diff});
                }
            }

            // Zero-Syscall Telemetry: Teleological Engraving
            // Completely eradicates std::cerr to prevent OS-level thread halting in the hot path.
            if (!offenders.empty() && tapestry) {
                // Engrave the divergence anomaly directly into the LBR.
                // 0x0D = Divergence Detected Error Code
                tapestry->engrave_anomaly(0x0D, lsn);
            }
            return mse;
        }

        /**
         * @brief Dumps historical performance and drift data to CSV for offline forensics.
         */
        template<size_t RecCap>
        static void export_csv(const sys::blackbox_recorder<RecCap>& recorder, const char* path) {
            // Decoupled Header Persistence: decide whether to write header based on file existence
            std::ifstream test(path);
            bool write_header = !test.is_open();
            test.close();

            std::ofstream out(path, std::ios::app);
            if (!out.is_open()) return;

            if (write_header) {
                out << "Cycle,IngressTSC,LogicTSC,JournalTSC,EgressTSC,CascadeLimit,MSE,MSE_EMA,Drops\n";
            }

            const auto* data = recorder.data();
            for (size_t i = 0; i < RecCap; ++i) {
                const auto& s = data[i];
                if (s.logic_tsc == 0) continue; // Skip uninitialized slots
                out << i << "," << s.ingress_tsc << "," << s.logic_tsc << "," 
                    << s.journal_tsc << "," << s.egress_tsc << "," 
                    << s.cascade_limit << "," << std::fixed << std::setprecision(8) 
                    << s.divergence_mse << "," << s.mse_ema << "," << s.full_drop_count << "\n";
            }
        }
    };
} // namespace slabflux::compute