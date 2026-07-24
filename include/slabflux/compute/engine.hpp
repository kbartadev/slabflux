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
/* slabflux/compute/engine.hpp */
#pragma once
#include <immintrin.h>
#include <cstddef>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::compute {

    /**
     * @brief Statically bounded, non-allocating memory matrix block.
     */
    template <typename T, size_t Extent>
    struct alignas(64) matrix {
        T storage[Extent]{0};
    };

    /**
     * @brief Deduces and encapsulates vector register dimensions and load/store primitives at compile-time.
     * Ensures the system maintains optimal hardware throughput without leaking architecture leaks into user interfaces.
     */
    template <size_t StepSize>
    struct hardware_vector_traits;

    template <>
    struct hardware_vector_traits<8> {
        using reg_type = __m256;
        static SLAB_FORCE_INLINE reg_type load(const float* ptr) noexcept { return _mm256_load_ps(ptr); }
        static SLAB_FORCE_INLINE void store(float* ptr, reg_type val) noexcept { _mm256_store_ps(ptr, val); }
        static SLAB_FORCE_INLINE reg_type set1(float val) noexcept { return _mm256_set1_ps(val); }
    };

    template <>
    struct hardware_vector_traits<16> {
        using reg_type = __m512;
        static SLAB_FORCE_INLINE reg_type load(const float* ptr) noexcept { return _mm512_load_ps(ptr); }
        static SLAB_FORCE_INLINE void store(float* ptr, reg_type val) noexcept { _mm512_store_ps(ptr, val); }
        static SLAB_FORCE_INLINE reg_type set1(float val) noexcept { return _mm512_set1_ps(val); }
    };

    /**
     * @brief Policy-Driven, branchless hardware processing chassis.
     * O(1) steady-state step latency with zero hidden side-effects.
     */
    template <typename MathPolicy, size_t StepSize = 8>
    class engine {
        using traits = hardware_vector_traits<StepSize>;
        using vec_t  = typename traits::reg_type;

    public:
        /**
         * @brief Hot-path execution ring processing a localized memory matrix.
         */
        template <size_t Extent, typename InputContext>
        SLAB_FORCE_INLINE void execute_pulse(matrix<float, Extent>& matrix, const InputContext& ctx) noexcept {
            static_assert(Extent % StepSize == 0, "Matrix Extent must be a direct multiple of the SIMD StepSize.");

            // Extract the data structures transparently to feed the 3-argument baseline kernels
            vec_t v_in  = traits::set1(ctx.signal_value);
            vec_t v_par = traits::set1(ctx.modifier_weight);

            #pragma GCC unroll 8
            for (size_t i = 0; i < Extent; i += StepSize) {
                // Direct L1 cache streaming load via deduced architecture traits
                vec_t m_old = traits::load(&matrix.storage[i]);

                // Statically dispatch calculation logic to the decoupled math policy
                vec_t m_new = MathPolicy::compute(m_old, v_in, v_par);

                // Stream state mutations back into persistent storage
                traits::store(&matrix.storage[i], m_new);
            }
        }

        /**
         * @brief Vectorized Stream Pulse.
         * @details Executes the mathematical kernel using a parallel input stream provided 
         * by the automation layer. Broadcasts scalar modifiers if count == 1.
         */
        template <size_t Extent>
        SLAB_HOT void execute_pulse(matrix<float, Extent>& matrix, const float* __restrict__ input, size_t count) noexcept {
            static_assert(Extent % StepSize == 0, "Matrix Extent must be a direct multiple of the SIMD StepSize.");
            
            // Physical Mapping: Extract scalar stimulus safely for any non-empty input array.
            const vec_t v_in_scalar = (count > 0) ? traits::set1(input[0]) : traits::set1(0.0f);
            // Fallback for modifier_weight expected by the ergonomic test suite
            const vec_t v_par = (count > 1) ? traits::set1(input[1]) : traits::set1(0.5f);

            for (size_t i = 0; i < Extent; i += StepSize) {
                vec_t m_old = traits::load(&matrix.storage[i]);
                
                // Statically select between scalar broadcast or parallel lane loading
                vec_t v_in = (count >= Extent) ? traits::load(&input[i]) : v_in_scalar;
                vec_t m_new = MathPolicy::compute(m_old, v_in, v_par);

                traits::store(&matrix.storage[i], m_new);
            }
        }
    };

} // namespace slabflux::compute
