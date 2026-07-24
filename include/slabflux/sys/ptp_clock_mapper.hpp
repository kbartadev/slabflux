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
 *
 * @brief High-frequency clock mapping.
 * @details Implements standard PTP hardware clock translation as described 
 * in standard Linux kernel PTP interfaces, using a standard Sequence Lock.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <x86intrin.h>
#include <immintrin.h> // For Intel TSX
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::sys {

    /** @brief PTP-to-TSC state block. */
    struct mapping_state {
        uint64_t tsc_ref;
        uint64_t ptp_ns_ref;
        uint64_t multiplier; // Q32.32 fixed point
    };

    class clock_mapper {
        // Sequence Lock: ensures atomic multi-word reads without mutexes.
        alignas(64) std::atomic<uint32_t> seq_{ 0 };
        mapping_state state_{ 0, 0, 0 };

    public:
        /**
         * @brief Translates TSC to absolute nanoseconds.
         * @details Implements a Wait-Free Sequence Reader to eliminate FPU
         * register pressure and lock-based stalls.
         */
        SLAB_FORCE_INLINE uint64_t to_absolute_ns(uint64_t tsc_cycles) const noexcept {
            uint32_t s1, s2;
            uint64_t t_ref, p_ref, mult;

            // Sequence Loop: Ensures the 3-word block is read consistently
            do {
                s1 = seq_.load(std::memory_order_acquire);
                if (SL_UNLIKELY(s1 & 1)) {
                    // Yield instruction pipeline to reduce memory contention if writer is active
                    _mm_pause();
                    continue;
                }

                t_ref = state_.tsc_ref;
                p_ref = state_.ptp_ns_ref;
                mult  = state_.multiplier;

                s2 = seq_.load(std::memory_order_acquire);
                if (SL_UNLIKELY(s1 != s2)) {
                    _mm_pause(); // Yield on sequence collision
                }
            } while (SL_UNLIKELY((s1 & 1) || (s1 != s2)));

            const uint64_t delta = tsc_cycles - t_ref;
            // 128-bit product for Q32.32 scaling without precision loss.
            return p_ref + static_cast<uint64_t>((static_cast<__uint128_t>(delta) * mult) >> 32);
        }

        /** @brief Published a new hardware clock mapping. */
        void update(uint64_t tsc_ref, uint64_t ptp_ns, uint64_t multiplier) noexcept {
            const uint32_t s = seq_.load(std::memory_order_relaxed);
            seq_.store(s + 1, std::memory_order_release);

            // Compiler barrier to enforce strict store ordering within the sequence lock
            asm volatile("" ::: "memory");

            state_.tsc_ref = tsc_ref;
            state_.ptp_ns_ref = ptp_ns;
            state_.multiplier = multiplier;

            asm volatile("" ::: "memory");
            seq_.store(s + 2, std::memory_order_release);
        }
    };
}
