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
#include <tuple>
#include <optional>
#include <utility>
#include <array>
#include "intrinsics.hpp"

namespace slabflux::compute {

    // Hardware-aligned vector signal (Former: simd_vector_signal)
    template <size_t Dim>
    struct alignas(32) simd_vector {
        float data[Dim];
    };

    // ============================================================================
    // ZERO-ALLOCATION SIMD RING BUFFER
    // ============================================================================
    template <size_t Dim, size_t ContextSize>
    struct alignas(64) simd_ring_buffer {
        static_assert((ContextSize& (ContextSize - 1)) == 0, "ContextSize must be a power of 2");

        // Physical memory: Isolated on its own cache lines
        alignas(64) std::array<simd_vector<Dim>, ContextSize> memory_ring;

        // State variables: Isolated to prevent False Sharing with the data
        alignas(64) mutable size_t head_index = 0;
        alignas(64) mutable size_t active_elements = 0;

        /**
         * @brief Prefetched O(1) Write.
         */
        std::optional<simd_vector<Dim>> push(const simd_vector<Dim>& input) const noexcept {
            const size_t current_head = head_index;
            const size_t next_head = (current_head + 1) & (ContextSize - 1);

            // 1. PREFETCH: Tell the CPU to fetch the NEXT-NEXT entry
            // This hides the memory latency of the next loop iteration.
            _mm_prefetch(reinterpret_cast<const char*>(&memory_ring[(next_head + 1) & (ContextSize - 1)]), _MM_HINT_T0);

            // 2. WRITE (Using standard store or stream based on Dim size)
            memory_ring[current_head] = input;

            // 3. BRANCHLESS COUNTER: No 'if' needed
            // active_elements increases until it hits ContextSize, then stays there.
            active_elements += (active_elements < ContextSize);

            head_index = next_head;

            // Feedback oldest only if full
            if (SL_EXPECT_FALSE(active_elements == ContextSize)) {
                return memory_ring[next_head];
            }
            return std::nullopt;
        }

        /**
         * @brief Constant-time Backtracking.
         */
        SLAB_FORCE_INLINE const simd_vector<Dim>& peek_historical(size_t steps_back) const noexcept {
            // Note: We don't check steps_back < active_elements for performance.
            // The caller must be sovereign.
            size_t index = (head_index - 1 - steps_back) & (ContextSize - 1);
            return memory_ring[index];
        }
    };

} // namespace slabflux::compute
