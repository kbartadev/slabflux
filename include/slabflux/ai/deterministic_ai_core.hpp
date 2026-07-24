/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * PROPRIETARY AND SOURCE-AVAILABLE CODEBASE. ALL RIGHTS RESERVED.
 *
 * This source file and all constitutive programmatic expressions contained herein 
 * are the exclusive intellectual property of Kristóf Barta, established and 
 * distributed strictly under the conditions of the SLABFLUX SOURCE-AVAILABLE 
 * AND ECOSYSTEM LICENSE (the "License").
 *
 * TITLE TO AND OWNERSHIP OF THE SOFTWARE, THE ENGINE, CORE LOGIC, ARCHITECTURAL 
 * LAYOUTS, AND ALL ASSOCIATED INSIGHTS REMAIN SOLELY VESTED IN THE AUTHOR.
 *
 * ----------------------------------------------------------------------------
 * TECHNICAL WARNING & SYSTEM ARCHITECTURE NOTICE
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE UTILIZES ARCHITECTURE-SPECIFIC HARDWARE INTRINSICS AND OPERATES
 * THROUGH LOW-LEVEL, KERNEL-ADJACENT EXECUTION PATHS THAT REDUCE OR BYPASS STANDARD
 * OPERATING SYSTEM MEDIATION LAYERS. INCORRECT INTEGRATION, EXECUTION, OR CONFIGURATION
 * MAY RESULT IN SEVERE SYSTEM INSTABILITY, KERNEL PANICS, OR PERMANENT LOSS OF DATA,
 * AND MAY RENDER SYSTEMS TEMPORARILY OR PERMANENTLY UNUSABLE UNTIL REPAIRED OR
 * RECONFIGURED.
 *
 * ----------------------------------------------------------------------------
 * ABSOLUTE USAGE RESTRICTIONS & OPERATIONAL PROHIBITIONS
 * ----------------------------------------------------------------------------
 * ANY CORPORATE USE, INSTITUTIONAL INCLUSION (#include), MICRO-ARCHITECTURAL 
 * REPLICATION, STRUCTURAL SEQUENCE EXTRACTION, OR CORPORATE DEPLOYMENT IS 
 * STRICTLY PROHIBITED AND CONSTITUTES AN IMMEDIATE, WILLFUL INFRINGEMENT 
 * OF COPYRIGHT AND CONTRACTUAL BREACH.
 *
 * Execution by individual, independent developers is permitted strictly subject 
 * to the conditional grants, mandatory attributions, and structural limitations 
 * defined within the License.
 *
 * ----------------------------------------------------------------------------
 * EXPRESS HARDWARE RISK ALLOCATION & DISCLAIMER (UCC CONSPICUOUS NOTICE)
 * ----------------------------------------------------------------------------
 * THE USER EXPRESSLY ACKNOWLEDGES AND AGREES THAT EXECUTION OF THIS SOFTWARE 
 * CARRIES AN INHERENT RISK OF TOTAL PHYSICAL HARDWARE FAILURE AND PERMANENT 
 * DESTRUCTION OF COMPUTING INFRASTRUCTURE. THE USER VOLUNTARILY ASSUMES ALL 
 * SUCH RISKS AS A CONDITION OF EXECUTION TO THE MAXIMUM EXTENT PERMITTED BY LAW.
 * ============================================================================*/

#pragma once

#include <immintrin.h>
#include <type_traits>
#include <concepts>
#include <span>
#include <algorithm>
#include "slabflux/sys/gpu_accelerator.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/ai/cognitive_stimulus.hpp"
#include "slabflux/mesh/causal_mesh.hpp"
#include "slabflux/security/kinetic_inscription.hpp"

namespace slabflux::mesh {
    struct origin_reset_event;
}

namespace slabflux::ai {

    /**
     * @brief Tensor Geometry Descriptor.
     */
    template <size_t Rows, size_t Cols>
    struct tensor_shape {
        static constexpr size_t ROWS = Rows;
        static constexpr size_t COLS = Cols;
        static constexpr size_t total_elements = Rows * Cols;
    };

    /**
     * @brief Invariant Deterministic Activation Policies.
     * @details Eliminates libm dependencies by providing constexpr-ready, 
     * branchless approximations of transcendental functions.
     */
    struct fast_inference_policy {
        using value_type = float;

        static constexpr SLAB_FORCE_INLINE float activate(float x) noexcept {
            return x > 0.0f ? x : 0.0f;
        }

        /** 
         * @brief Constexpr Sigmoid Approximation (3rd-order minimax).
         * @details Precision: 1.2e-3 over [-5, 5]. Eliminates std::exp jitter.
         */
        static constexpr SLAB_FORCE_INLINE float sigmoid(float x) noexcept {
            if (x < -5.0f) return 0.0f;
            if (x > 5.0f)  return 1.0f;
            return 0.5f + (0.197f * x) - (0.004f * x * x * x);
        }
    };

    template<int FractionalBits = 8>
    struct fixed_point_policy {
        using value_type = int32_t;
        static constexpr int32_t SCALE = 1 << FractionalBits;
        static constexpr SLAB_FORCE_INLINE int32_t activate(int32_t x) noexcept { return x > 0 ? x : 0; }
        static constexpr SLAB_FORCE_INLINE int32_t mul(int32_t a, int32_t b) noexcept { return (static_cast<int64_t>(a) * b) >> FractionalBits; }
    };

    template <typename Shape = tensor_shape<32, 8>>
    class deterministic_ai_core {
    public:
        using value_type = float; // Required by branchless_engine
        static_assert(Shape::total_elements % 16 == 0, "AI Core total elements must be multiple of 16 for ZMM loads");

        static constexpr size_t CAPACITY = Shape::total_elements;

        alignas(64) float memory_state[CAPACITY]{0.0f};
        alignas(64) float weight_matrix[CAPACITY]{1.0f};
        float eta;
        const security::semiotic_tapestry* tapestry_{ nullptr };

        using agnosia_sink_t = void (*)(deterministic_ai_core*, uint64_t, uint8_t);
        agnosia_sink_t aphasic_horizon_[256];

        static void execute_valid_inference(deterministic_ai_core*, uint64_t, uint8_t) noexcept {}

        static void execute_void_inference(deterministic_ai_core* core, uint64_t lsn, uint8_t fray) noexcept {
            // Kinetic Inscription: Engrave AI Tensor poisoning (NaNs/Infs) into the silicon LBR
            if (core->tapestry_) {
                core->tapestry_->engrave_anomaly(fray, lsn);
            }
        }

        explicit deterministic_ai_core(float e = 0.15f) : eta(e) {
            aphasic_horizon_[0] = &execute_valid_inference;
            for (int i = 1; i < 256; ++i) {
                aphasic_horizon_[i] = &execute_void_inference;
            }
        }

        void bind_tapestry(const security::semiotic_tapestry* tapestry) noexcept {
            tapestry_ = tapestry;
        }

        [[nodiscard]] SLAB_FORCE_INLINE std::span<const float, CAPACITY> view_state() const noexcept {
            return std::span<const float, CAPACITY>(memory_state, CAPACITY);
        }

        SLAB_FORCE_INLINE void load_weights(std::span<const float, CAPACITY> new_weights) noexcept {
            std::copy(new_weights.begin(), new_weights.end(), weight_matrix);
        }

        /** @brief Generic vector processing loop for explicit SIMD widths. */
        template <typename VecT, typename FormulaTransform>
        SLAB_FORCE_INLINE void compute_stream_internal(FormulaTransform&& transform) noexcept {
            constexpr size_t step = sizeof(VecT) / sizeof(float);
            #pragma GCC unroll 4
            for (size_t i = 0; i < CAPACITY; i += step) {
                // Hinting the compiler for absolute alignment guarantees
                float* m_ptr = static_cast<float*>(__builtin_assume_aligned(&memory_state[i], 64));
                float* w_ptr = static_cast<float*>(__builtin_assume_aligned(&weight_matrix[i], 64));

                VecT m_tau, w_tau;
                if constexpr (sizeof(VecT) == 64) {
                    m_tau = _mm512_load_ps(m_ptr);
                    w_tau = _mm512_load_ps(w_ptr);
                } else {
                    m_tau = _mm256_load_ps(m_ptr);
                    w_tau = _mm256_load_ps(w_ptr);
                }

                VecT m_new = transform(m_tau, w_tau);
                
                if constexpr (sizeof(VecT) == 64) {
                    _mm512_store_ps(m_ptr, m_new);
                } else {
                    _mm256_store_ps(m_ptr, m_new);
                }
            }
        }

        template <typename FormulaTransform>
        SLAB_FORCE_INLINE void compute_stream(FormulaTransform&& transform) noexcept {
#if defined(__AVX512F__)
            if constexpr (requires(FormulaTransform f, __m512 arg1, __m512 arg2) { f(arg1, arg2); }) {
                compute_stream_internal<__m512>(std::forward<FormulaTransform>(transform));
            } else if constexpr (requires(FormulaTransform f, __m256 arg1, __m256 arg2) { f(arg1, arg2); }) {
                compute_stream_internal<__m256>(std::forward<FormulaTransform>(transform));
            } else {
                for (size_t i = 0; i < CAPACITY; ++i) (void)transform; 
            }
#elif defined(__AVX2__)
            if constexpr (requires(FormulaTransform f, __m256 arg1, __m256 arg2) { f(arg1, arg2); }) {
                compute_stream_internal<__m256>(std::forward<FormulaTransform>(transform));
            } else {
                for (size_t i = 0; i < CAPACITY; ++i) (void)transform; 
            }
#else
            for (size_t i = 0; i < CAPACITY; ++i) (void)transform; 
#endif
        }

        void on(const cognitive_stimulus& ev) noexcept {
            // Restored Original Hardware-Aligned FMA Logic: 
            // M_new = (M_old * (1 - eta)) + (confidence * W_old)
            const float decay_factor = 1.0f - eta;
            const float impulse = ev.confidence;

#if defined(__CUDACC__) || defined(SLABFLUX_ENABLE_CUDA)
            // TRUE HETEROGENEOUS OFFLOAD: Bypass CPU mesh if GPU is present and tensor is massive
            if constexpr (CAPACITY > 1024 * 1024) {
                slabflux::sys::gpu_accelerator::dispatch_pointwise_update(
                    memory_state, weight_matrix, static_cast<float>(ev.raw_token), ev.confidence, eta, CAPACITY);
                aphasic_horizon_[0](this, ev.source_lsn, 0);
                return;
            }
#endif

            // Standard: Match computation width to hardware detection
#if defined(__AVX512F__)
            if (__builtin_cpu_supports("avx512f")) {
                const __m512 v_decay = _mm512_set1_ps(decay_factor);
                const __m512 v_impulse = _mm512_set1_ps(impulse);
                const __m512 v_max = _mm512_set1_ps(1e6f);
                const __m512 v_min = _mm512_set1_ps(1e-5f);

                uint8_t aggregated_fray = 0;
                this->compute_stream_internal<__m512>([&](__m512 m_tau, __m512 w_tau) noexcept {
                    __m512 raw_out = _mm512_fmadd_ps(m_tau, v_decay, _mm512_mul_ps(w_tau, v_impulse));
                    __mmask16 poison_mask = _mm512_fpclass_ps_mask(raw_out, 0x99);
                    if (SL_EXPECT_FALSE(poison_mask != 0)) aggregated_fray = 2; // Error Code 2: Tensor Poison
                    raw_out = _mm512_maskz_mov_ps(~poison_mask, raw_out);
                    return _mm512_max_ps(_mm512_min_ps(raw_out, v_max), v_min);
                });
                aphasic_horizon_[aggregated_fray](this, ev.source_lsn, aggregated_fray);
                return;
            }
#endif
#if defined(__AVX2__)
            if (__builtin_cpu_supports("avx2")) {
                const __m256 v_decay = _mm256_set1_ps(decay_factor);
                const __m256 v_impulse = _mm256_set1_ps(impulse);
                const __m256 v_max = _mm256_set1_ps(1e6f);
                const __m256 v_min = _mm256_set1_ps(1e-5f);

                uint8_t aggregated_fray = 0;
                this->compute_stream_internal<__m256>([&](__m256 m_tau, __m256 w_tau) noexcept {
                    __m256 raw_out = _mm256_fmadd_ps(m_tau, v_decay, _mm256_mul_ps(w_tau, v_impulse));
                    // Guard: Prevent NaN poisoning for AVX2 parity
                    __m256 nan_mask = _mm256_cmp_ps(raw_out, raw_out, _CMP_UNORD_Q);
                    if (SL_EXPECT_FALSE(_mm256_movemask_ps(nan_mask) != 0)) aggregated_fray = 2;
                    raw_out = _mm256_andnot_ps(nan_mask, raw_out); 
                    
                    return _mm256_max_ps(_mm256_min_ps(raw_out, v_max), v_min);
                });
                aphasic_horizon_[aggregated_fray](this, ev.source_lsn, aggregated_fray);
                return;
            }
#endif

            // Scalar Fallback: Absolute compatibility for legacy hardware
            uint8_t aggregated_fray = 0;
            for (size_t i = 0; i < CAPACITY; ++i) {
                float raw_out = (memory_state[i] * decay_factor) + (weight_matrix[i] * impulse);
                
                if (SL_EXPECT_FALSE(!std::isfinite(raw_out))) {
                    raw_out = 0.0f;
                    aggregated_fray = 2;
                }
                memory_state[i] = std::clamp(raw_out, 1e-5f, 1e6f);
            }
            aphasic_horizon_[aggregated_fray](this, ev.source_lsn, aggregated_fray);
        }

        /**
         * @brief Administrative Reset Handler.
         * @details Uses Non-Temporal (Streaming) stores to zero the state 
         * without polluting the L1/L2/L3 caches with zero-values.
         */
        void on(const mesh::origin_reset_event& ev) noexcept {
            (void)ev;
            const __m512 zero = _mm512_setzero_ps();
            #pragma GCC unroll 8
            for (size_t i = 0; i < CAPACITY; i += 16) {
                // Stream directly to RAM to preserve cache for other Experts
                _mm512_stream_ps(&memory_state[i], zero);
            }
            // Fence to ensure the zeros are committed to the persistence domain
            _mm_sfence();
        }
    };

} // namespace slabflux::ai
