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
#include <type_traits>
#include "slabflux/compute/no_recursion_check.hpp"

 /**
  * @file path_guard.hpp
  * @brief Static Path Complexity Enforcement.
  * @details Eliminates jump-based jitter by forcing the compiler to generate
  * linear instruction traces. No branches, no vector-throttling, no surprises.
  */

  // SLAB_FLAT_PATH: Prevents the CPU from making "guesses" at the loop level.
  // Full unrolling turns a loop into a straight line of instructions.
#define SLAB_FLAT_PATH \
    _Pragma("clang loop unroll(full)") \
    _Pragma("clang loop vectorize(disable)") \
    _Pragma("clang loop interleave(disable)")

// Branch hints to prime the hardware Branch Target Buffer (BTB)
#define SL_EXPECT_TRUE(x)  __builtin_expect(!!(x), 1)
#define SL_EXPECT_FALSE(x) __builtin_expect(!!(x), 0)

namespace slabflux::compute {

    /**
     * @brief Static Path Enforcer.
     * @details Ensures that logic blocks remain within deterministic bounds.
     */
    template<size_t MaxCycles>
    struct path_budget {
        // This is a placeholder for static analysis tools like 'clang-tidy'
        // or custom LLVM passes used in the SlabFlux toolchain to
        // verify cycle-count consistency.
    };

    /**
     * @brief Example of a Guarded Hot Path execution.
     * @details This function processes a fixed-size batch of signals.
     * Because of SLAB_FLAT_PATH, the CPU sees 8 identical instruction blocks
     * instead of a loop with a conditional jump at the end.
     */
    template<typename SignalT, size_t BatchSize>
    SLAB_HOT SLAB_LEAF
        void process_signal_batch(SignalT* signals, auto&& logic_func) noexcept {
        static_assert(BatchSize <= 16, "Batch size too large for full unrolling.");

        SLAB_FLAT_PATH
            for (size_t i = 0; i < BatchSize; ++i) {
                // Every iteration is physically laid out in the I-cache
                logic_func(signals[i]);
            }
    }

} // namespace slabflux::compute
