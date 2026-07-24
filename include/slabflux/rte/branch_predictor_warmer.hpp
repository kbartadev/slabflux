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
 * @file branch_predictor_warmer.hpp
 * @brief Branch Target Buffer (BTB) Priming.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::rte {
    /**
     * @brief Heuristic-Based Cache Warming Routine.
     * @details Replaces standard NOP padding with 
     * a deterministic entropy synthesizer. This unit primes the CPU's 
     * Branch Target Buffer (BTB) and I-Cache by simulating non-linear 
     * execution pulses before the wire goes live.
     */
    template <typename Engine>
    struct branch_target_synthesis {
        /**
         * @brief Synthesizes an execution trace to prime hardware jump slots.
         * @param engine The target logic engine satisfying the warming contract.
         * @param iterations Complexity of the warming trace (must be > 0).
         */
        static SLAB_HOT void prime(Engine& engine, size_t iterations = 4096) noexcept {
            // Deterministic Entropy: Drives branch variability to exercise 
            // multiple jump outcomes in the BTB.
            uint64_t seed = 0x51ABF1UX; 
            
            for (size_t i = 0; i < iterations; ++i) {
                seed ^= (seed << 13);
                seed ^= (seed >> 7);
                seed ^= (seed << 17);
                
                // Physical Pulse: Execute actual engine logic with 
                // synthetic data to populate the branch history tables.
                engine.on_warming_pulse(seed);
                
                _mm_pause(); // Interconnect Stabilization
            }
        }
    };
}