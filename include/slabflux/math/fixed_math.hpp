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
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/hw/intrinsics.hpp"

namespace slabflux::math {

    /**
     * @brief Consteval Scaling Factor Synthesis.
     * @details Statically derives and optimizes multipliers and 
     * inverse-scale constants for fixed-point arithmetic.
     */
    template <std::size_t FractionalBits>
    struct fixed_topology_synthesis {
        static_assert(FractionalBits < 64, "Fractional width exceeds register capacity.");

        static consteval uint64_t synthesize_scale() noexcept { return 1ULL << FractionalBits; }
        static consteval double   synthesize_inverse() noexcept { return 1.0 / static_cast<double>(1ULL << FractionalBits); }

        static constexpr uint64_t SCALE     = synthesize_scale();
        static constexpr double   INV_SCALE = synthesize_inverse();

        /** @brief Synthesis-driven conversion from floating point. */
        static SLAB_FORCE_INLINE constexpr int64_t from_double(double val) noexcept {
            return static_cast<int64_t>(val * static_cast<double>(SCALE) + (val >= 0 ? 0.5 : -0.5));
        }

        /** @brief Synthesis-driven conversion to floating point. */
        static SLAB_FORCE_INLINE constexpr double to_double(int64_t val) noexcept {
            return static_cast<double>(val) * INV_SCALE;
        }
    };

    /**
     * @brief Deterministic Division.
     * @details Implements constant-time division using rounded reciprocals.
     * Bypasses the CPU division unit (IDIV) which has non-deterministic 
     * latency (10-40 cycles) with a MUL + SHR sequence (1-2 cycles).
     * 
     * Algorithm: n / D = (n * M) >> 64
     * where M = floor(2^64 / D) + 1. Guarantees exactness for all uint32_t.
     */
    template <uint32_t Divisor>
    SLAB_FORCE_INLINE constexpr uint32_t fast_div(uint32_t n) noexcept {
        static_assert(Divisor > 0, "Division by zero is undefined.");

        if constexpr (Divisor == 1) return n;
        
        // Optimization for powers of two
        if constexpr ((Divisor & (Divisor - 1)) == 0) {
            constexpr uint32_t shift = __builtin_ctz(Divisor);
            return n >> shift;
        }

        // Reciprocal: Uses floor(2^64 / D) + 1 to ensure 
        // bit-perfect results across the entire 32-bit integer range.
        constexpr uint64_t M = (0xFFFFFFFFFFFFFFFFULL / Divisor) + 1;

        #if defined(__SIZEOF_INT128__)
            return static_cast<uint32_t>((static_cast<unsigned __int128>(n) * M) >> 64);
        #else
            return n / Divisor; // Fallback for limited architectures
        #endif
    }

} // namespace slabflux::math