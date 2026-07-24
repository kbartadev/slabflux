/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
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
 * ============================================================================* SLABFLUX SOFTWARE ENGINE - DIW-E INVARIANT GUARD
 * ============================================================================
 * @details Implements zero-overhead and cold-path invariant validations
 * for vector lane processors. Resolves structural alignment, ABI boundary 
 * compliance, and numeric integrity.
 *
 * DETERMINISTIC ILP WINDOW ENFORCEMENT (DIW-E):
 * Replaces legacy Shadow ILP with compile-time micro-op window stabilization.
 * Enforces strict alignment, unrolling invariants, and register pressure 
 * envelopes to guarantee 100% predictable execution port saturation.
 */

#pragma once

#include <cstdint>
#include <type_traits>
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"

// ============================================================================
// CONFIGURATION FLAGS
// ============================================================================

// Define to entirely disable runtime SIMD boundary checks (Dangerous)
// #define SLABFLUX_SIMD_NO_CHECKS

// Define to inject AVX NaN/Infinity checks into the ALU hot path
// #define SLABFLUX_SIMD_SANITIZE_NUMERICS

namespace slabflux::compute {

    // ========================================================================
    // COLD-PATH HARDWARE TRAP
    // ========================================================================
    
    /**
     * @brief Absolute execution truncation for invariant violations.
     * @details Marked cold and noreturn. Forces the compiler to move this
     * completely out of the L1 Instruction Cache for the hot path.
     */
    [[noreturn, gnu::cold]] SLAB_COLD inline void simd_hardware_trap(const char* violation) noexcept {
        core::handle_critical_error(violation);
        __builtin_trap();
    }

    // ========================================================================
    // C++20 METAPROGRAMMING CONCEPTS
    // ========================================================================

    template<typename T>
    concept SimdCompatibleData = std::is_arithmetic_v<T> && sizeof(T) <= 8;

    template<size_t Lanes, size_t Width>
    concept PerfectSIMDUnroll = (Lanes > 0) && (Lanes % Width == 0);

    /**
     * @brief Deterministic ILP Window Enforceable
     * @details Guarantees the unrolled loop block fits perfectly within the 
     * physical architectural register file (e.g. 16 YMM or 32 ZMM registers) 
     * preventing catastrophic L1 cache spillage in the hot path.
     */
    template <size_t Capacity, size_t SIMDWidth, size_t UnrollFactor, size_t MaxArchRegs>
    concept DIWEnforceable = 
        PerfectSIMDUnroll<Capacity, SIMDWidth> &&
        (Capacity % (SIMDWidth * UnrollFactor) == 0) &&
        ((UnrollFactor * 3) <= MaxArchRegs); // Assumes State, Input, and Temp per lane

    // ========================================================================
    // INLINE RUNTIME GUARDS
    // ========================================================================

#ifndef SLABFLUX_SIMD_NO_CHECKS

    #define SLAB_SIMD_CHECK_ALIGNMENT_64(ptr) \
        if (SL_UNLIKELY((reinterpret_cast<std::uintptr_t>(ptr) & 63) != 0)) { \
            ::slabflux::compute::simd_hardware_trap("SIMD Boundary Violation: Pointer not 64-byte aligned."); \
        } \
        ptr = static_cast<decltype(ptr)>(__builtin_assume_aligned(ptr, 64))

    #define SLAB_SIMD_CHECK_ALIGNMENT_32(ptr) \
        if (SL_UNLIKELY((reinterpret_cast<std::uintptr_t>(ptr) & 31) != 0)) { \
            ::slabflux::compute::simd_hardware_trap("SIMD Boundary Violation: Pointer not 32-byte aligned."); \
        } \
        ptr = static_cast<decltype(ptr)>(__builtin_assume_aligned(ptr, 32))

#else
    #define SLAB_SIMD_CHECK_ALIGNMENT_64(ptr) ptr = static_cast<decltype(ptr)>(__builtin_assume_aligned(ptr, 64))
    #define SLAB_SIMD_CHECK_ALIGNMENT_32(ptr) ptr = static_cast<decltype(ptr)>(__builtin_assume_aligned(ptr, 32))
#endif

    // ========================================================================
    // NUMERIC SANITIZERS (AVX/AVX-512)
    // ========================================================================

#ifdef SLABFLUX_SIMD_SANITIZE_NUMERICS

    #if defined(__AVX512F__)
    SLAB_FORCE_INLINE void sanitize_floats_512(__m512 v) noexcept {
        // fpclass immediate mask: 0x01(qNaN) | 0x80(sNaN) | 0x08(+Inf) | 0x10(-Inf) = 0x99
        __mmask16 bad_mask = _mm512_fpclass_ps_mask(v, 0x99);
        if (SL_UNLIKELY(bad_mask != 0)) {
            simd_hardware_trap("SIMD Numeric Violation: AVX-512 Lane contains NaN or Infinity.");
        }
    }
    #define SLAB_SIMD_SANITIZE_FLOATS_512(vec) ::slabflux::compute::sanitize_floats_512(vec)
    #else
    #define SLAB_SIMD_SANITIZE_FLOATS_512(vec)
    #endif

    #if defined(__AVX2__)
    SLAB_FORCE_INLINE void sanitize_floats_256(__m256 v) noexcept {
        // Detect NaNs: A NaN is not equal to itself
        __m256 nan_check = _mm256_cmp_ps(v, v, _CMP_UNORD_Q);
        if (SL_UNLIKELY(_mm256_movemask_ps(nan_check) != 0)) {
            simd_hardware_trap("SIMD Numeric Violation: AVX2 Lane contains NaN.");
        }
    }
    #define SLAB_SIMD_SANITIZE_FLOATS_256(vec) ::slabflux::compute::sanitize_floats_256(vec)
    #else
    #define SLAB_SIMD_SANITIZE_FLOATS_256(vec)
    #endif

#else
    #define SLAB_SIMD_SANITIZE_FLOATS_512(vec)
    #define SLAB_SIMD_SANITIZE_FLOATS_256(vec)
#endif

} // namespace slabflux::compute