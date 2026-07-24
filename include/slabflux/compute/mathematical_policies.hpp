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
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::compute::policies {

    /**
     * @brief Refactored FMA-accelerated exponential decay AI model calculation.
     */
    struct ai_exponential_decay {
        static SLAB_FORCE_INLINE __m256 compute(__m256 m_tau, const auto& ctx) noexcept {
            // Context contains broadcasted parameter registers initialized outside the loop
            // M_new = m_tau * (1.0f - K - eta) + (K * E)
            return _mm256_fmadd_ps(m_tau, ctx.v_scale, ctx.v_impulse);
        }
    };

    /**
     * @brief Unified physical viscosity integration calculation policy.
     */
    struct physics_viscosity_integration {
        static SLAB_FORCE_INLINE __m512 compute(__m512 v_state, const auto& ctx) noexcept {
            // Integration: S_new = (S_old + (I * C)) * (1.0 - Viscosity)
            __m512 v_integrated = _mm512_fmadd_ps(ctx.v_stim, ctx.v_conf, v_state);
            return _mm512_mul_ps(v_integrated, ctx.v_visc_scale);
        }
    };

} // namespace slabflux::compute::policies
