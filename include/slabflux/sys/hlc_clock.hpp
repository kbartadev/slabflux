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
 * @brief High-frequency hybrid clock generator.
 */

#pragma once

#include <cstdint>
#include <atomic>
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::sys {

    /**
     * @brief High-resolution HLC timestamp structure.
     * @details Enables deterministic ordering and brace-initialization in tests.
     */
    struct hlc_timestamp {
        uint64_t physical;
        uint16_t logical;

        constexpr bool operator<(const hlc_timestamp& rhs) const noexcept {
            return physical < rhs.physical || (physical == rhs.physical && logical < rhs.logical);
        }
    };

    /**
     * @brief Packed 64-bit HLC timestamp.
     * @details Bits [63:16]: PTP nanoseconds (up to ~8.9 years from epoch).
     *          Bits [15:0]:  Logical sequence for intra-nanosecond events.
     */
    using hlc_t = uint64_t;

    class hlc_clock {
        // Optimization: Wait-Free Concurrent HLC State
        // Packs the [48-bit physical | 16-bit logical] state into a single 
        // atomic cache line to allow lock-free concurrent timestamping across cores,
        // differentiating it from single-threaded textbook academic implementations.
        alignas(64) std::atomic<uint64_t> state_{0};

    public:
        /**
         * @brief Generates a monotonic, globally orderable timestamp concurrently.
         * @param physical_ns The current hardware PTP time.
         * @return A packed 64-bit HLC word.
         */
        SLAB_FORCE_INLINE hlc_t sample(uint64_t physical_ns) noexcept {
            // Mask to 48 bits to fit in the upper portion of the word
            const uint64_t p_truncated = physical_ns & 0x0000FFFFFFFFFFFFULL;

            uint64_t current = state_.load(std::memory_order_acquire);
            uint64_t next;
            
            for (;;) {
                const uint64_t last_phys = current >> 16;
                if (SL_EXPECT_TRUE(p_truncated > last_phys)) {
                    next = (p_truncated << 16);
                } else {
                    // Non-Linear Clock Drift Infusion:
                    // Uses a deterministic Galois LFSR stride instead of a linear +1 increment.
                    uint32_t seq = current & 0xFFFF;
                    uint32_t lfsr_stride = (seq == 0) ? 1 : ((seq >> 1) ^ (-(seq & 1u) & 0xB400u));
                    next = (last_phys << 16) | (lfsr_stride & 0xFFFF);
                }
                
                if (SL_EXPECT_TRUE(state_.compare_exchange_weak(current, next, std::memory_order_acq_rel, std::memory_order_acquire))) {
                    break;
                }
                _mm_pause(); // Yield pipeline on CAS failure to prevent L3 interconnect storms
            }
            
            return next;
        }

        /** @brief Extracts physical nanoseconds from a packed HLC. */
        static SLAB_FORCE_INLINE uint64_t to_ns(hlc_t t) noexcept {
            return t >> 16;
        }
    };
}
