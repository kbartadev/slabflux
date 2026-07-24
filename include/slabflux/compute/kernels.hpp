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
#include <concepts>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::compute::kernels {

    struct fused_exponential_decay {
        template <typename VecT>
        static SLAB_FORCE_INLINE VecT compute(VecT m_tau, VecT v_input, VecT v_param) noexcept {
            if constexpr (sizeof(VecT) == 32) {
                // M_new = (m_tau * (1.0f - v_param)) + (v_input * v_param)
                __m256 v_one = _mm256_set1_ps(1.0f);
                __m256 v_scale = _mm256_sub_ps(v_one, v_param);
                __m256 v_impulse = _mm256_mul_ps(v_input, v_param);

                // Single-cycle Hardware Fused Multiply-Add instruction to stop precision drift
                return _mm256_fmadd_ps(m_tau, v_scale, v_impulse);
            } else if constexpr (sizeof(VecT) == 64) {
#if defined(__AVX512F__)
                __m512 v_one = _mm512_set1_ps(1.0f);
                __m512 v_scale = _mm512_sub_ps(v_one, v_param);
                __m512 v_impulse = _mm512_mul_ps(v_input, v_param);
                
                return _mm512_fmadd_ps(m_tau, v_scale, v_impulse);
#else
                return m_tau;
#endif
            } else {
                return m_tau * (1.0f - v_param) + (v_input * v_param);
            }
        }
    };

    struct rectified_linear_unit {
        template <typename VecT>
        static SLAB_FORCE_INLINE VecT compute(VecT m_tau, VecT v_input, VecT) noexcept {
            if constexpr (sizeof(VecT) == 32) {
                // M_new = max(0, m_tau + v_input)
                __m256 v_sum = _mm256_add_ps(m_tau, v_input);
                return _mm256_max_ps(_mm256_setzero_ps(), v_sum);
            } else if constexpr (sizeof(VecT) == 64) {
#if defined(__AVX512F__)
                __m512 v_sum = _mm512_add_ps(m_tau, v_input);
                return _mm512_max_ps(_mm512_setzero_ps(), v_sum);
#else
                return m_tau;
#endif
            } else {
                return (m_tau + v_input) > 0.0f ? (m_tau + v_input) : 0.0f;
            }
        }
    };

    // Graph Logic Identities
    struct exponential_decay_op {};
    struct relu_op {};

    /**
     * @brief Metaprogrammed Execution Node.
     * @details Abstracts specific SIMD instruction sequences behind a functional intent layer.
     */
    template<typename Op>
    struct execution_node {
        template<typename VecT>
        static SLAB_FORCE_INLINE VecT process(VecT state, VecT input, VecT param) noexcept {
            if constexpr (std::same_as<Op, exponential_decay_op>) {
                if constexpr (sizeof(VecT) == 64) { // AVX-512 Path
#if defined(__AVX512F__)
                    return _mm512_fmadd_ps(state, _mm512_sub_ps(_mm512_set1_ps(1.0f), param), _mm512_mul_ps(input, param));
#else
                    return state;
#endif
                } else if constexpr (sizeof(VecT) == 32) { // AVX2 Path
                    return _mm256_fmadd_ps(state, _mm256_sub_ps(_mm256_set1_ps(1.0f), param), _mm256_mul_ps(input, param));
                } else { // Scalar Fallback
                    return state * (1.0f - param) + (input * param);
                }
            } else if constexpr (std::same_as<Op, relu_op>) {
                if constexpr (sizeof(VecT) == 64) {
#if defined(__AVX512F__)
                    return _mm512_max_ps(_mm512_setzero_ps(), _mm512_add_ps(state, input));
#else
                    return state;
#endif
                } else if constexpr (sizeof(VecT) == 32) {
                    return _mm256_max_ps(_mm256_setzero_ps(), _mm256_add_ps(state, input));
                } else {
                    return (state + input) > 0.0f ? (state + input) : 0.0f;
                }
            }
            return state;
        }
    };

    /**
     * @brief Execution Graph Composition.
     * @details Fuses mathematical nodes into a contiguous compute block for the engine.
     * The composition logic ensures that the arithmetic remains within the GPR/XMM
     * register file as long as possible before storing back to the Slab.
     */
    template<typename... Ops>
    struct execution_graph {
        template<typename VecT>
        static SLAB_FORCE_INLINE VecT compute(VecT m, VecT i, VecT p) noexcept {
            VecT res = m;
            ((res = execution_node<Ops>::template process(res, i, p)), ...);
            return res;
        }
    };

} // namespace slabflux::compute::kernels
