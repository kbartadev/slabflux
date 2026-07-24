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
 * @file round_robin_poller.hpp
 * @brief Fair-share Fan-In routing component.
 * @details Aggregates multiple input conduits into a single processing stream 
 * while preventing source starvation.
 * 
 * High-Performance Design:
 * 1. Contention-Free Extraction: Polls multiple SPSC conduits using a local cursor, 
 *    maintaining I-Cache residency by processing batches from the same source.
 * 2. Starvation Prevention: The cursor is updated only after a successful pop, 
 *    maintaining I-Cache residency by processing batches from the same source.
 *    ensuring the engine doesn't "skip" low-frequency data sources.
 * 3. Branchless Math: Uses power-of-two logic (where applicable) or efficient 
 *    stepping to calculate rotation indices.
 * 
 * API Contract:
 * - Progress Guarantee: Every attached conduit is evaluated exactly once 
 *   per full rotation cycle.
 * - Ownership: Strictly transfers raw pointers (T*) from conduits to the caller.
 * 
 * ========================================================================
 * API REFERENCE: round_robin_poller<T, NumInputs>
 * ========================================================================
 * 1. CONFIGURATION:
 *    - void bind_track(size_t, Conduit&) : Associates an input conduit at index.
 * 
 * 2. HOT PATH:
 *    - T* poll()                         : Returns the next available pointer 
 *                                          from the rotation. Returns nullptr 
 *                                          if all inputs are empty.
 */

#pragma once
#include <cstdint>
#include <array>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::bridge {

    /**
     * @brief Fair-share Fan-In Multiplexer.
     * @tparam T The payload type (e.g. market_tick).
     * @tparam NumInputs Number of upstream tracks to poll.
     */
    template <typename T, std::size_t NumInputs>
    class alignas(64) round_robin_poller {
        struct slot {
            void* conduit_ptr;
            bool (*pop_fn)(void*, T*&);
        };

        slot inputs_[NumInputs]{};
        
        alignas(64) std::size_t next_{0}; // Isolated cursor to prevent RFO stalls

    public:
        round_robin_poller() = default;

        /**
         * @brief Statically binds an input conduit to the poller registry.
         * @param index The routing slot index.
         * @param conduit Reference to the SPSC/MPMC conduit.
         */
        template <typename Conduit>
        void bind_track(std::size_t index, Conduit& conduit) noexcept {
            if (index < NumInputs) {
                inputs_[index].conduit_ptr = &conduit;
                inputs_[index].pop_fn = [](void* c, T*& out) -> bool {
                    return static_cast<Conduit*>(c)->try_pop(out);
                };
            }
        }

        /**
         * @brief Performs a fair-share rotation across all bound inputs.
         * @return T* of the next event, or nullptr if none available.
         */
        [[nodiscard]] SLAB_HOT T* poll() noexcept {
            SLAB_FLAT_PATH
            std::size_t idx = next_; // Start from the last successful position
            for (std::size_t i = 0; i < NumInputs; ++i) {
                T* raw_ptr = nullptr;
                
                // Optimized Polling: Branchless state check and conditional reset
                const bool success = inputs_[idx].pop_fn && inputs_[idx].pop_fn(inputs_[idx].conduit_ptr, raw_ptr);
                if (success) [[likely]] {
                    next_ = (idx + 1 == NumInputs) ? 0 : idx + 1;
                    return raw_ptr;
                }
                idx = (idx + 1 == NumInputs) ? 0 : idx + 1;
            }
            return nullptr;
        }
    };
}
