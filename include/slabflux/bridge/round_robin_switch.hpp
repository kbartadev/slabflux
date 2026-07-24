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
 * @file round_robin_switch.hpp
 * @brief Deterministic Fan-Out Load Balancer.
 * @details Distributes raw pointer payloads across multiple output conduits 
 * via lock-free O(1) stepping.
 * 
 * High-Performance Design:
 * 1. Load Balancing: Distributes traffic across worker conduits to prevent 
 *    hot-spotting on specific Compute cores.
 *    hot-spotting on specific Compute cores.
 * 2. Zero-Copy: Operates strictly on memory addresses, never dereferencing 
 *    the payload during transit.
 * 3. Branch Predictor Shielding: Replaces complex sharding logic with mathematical 
 *    rotation, stabilizing the CPU's branch target buffer.
 * 
 * API Contract:
 * - Uniformity: Guarantees even distribution of work across healthy consumers.
 * - Backpressure Awareness: If a target conduit is full, the switch attempts 
 *   the next available slot in the same cycle.
 * 
 * ========================================================================
 * API REFERENCE: round_robin_switch<T, NumOutputs>
 * ========================================================================
 * 1. CONFIGURATION:
 *    - void bind_track(size_t, Conduit&) : Associates an output conduit.
 * 
 * 2. HOT PATH:
 *    - bool route(T*)                    : Distributes the pointer to the next 
 *                                          available worker. Returns false 
 *                                          if all outputs are saturated.
 */

#pragma once

#include <array>
#include <cstdint>
#include <immintrin.h> // For _mm_pause
#include <immintrin.h>
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::bridge {

    /**
     * @brief Deterministic Fan-Out Router.
     * @tparam T The payload type.
     * @tparam NumOutputs Number of downstream conduits.
     */
    template <typename T, std::size_t NumOutputs>
    class alignas(64) round_robin_switch {
    private:
        struct slot {
            void* conduit_ptr;
            bool (*push_fn)(void*, T*);
        };

        slot outputs_[NumOutputs]{};
        
        alignas(64) std::size_t next_{0}; // Isolated cursor to prevent RFO stalls

    public:
         /**
         * @brief Statically binds an output conduit to the routing registry.
         */
        template <typename Conduit>
        void bind_track(std::size_t index, Conduit& conduit) noexcept {
            if (index < NumOutputs) {
                outputs_[index].conduit_ptr = &conduit;
                outputs_[index].push_fn = [](void* c, T* item) -> bool {
                    return static_cast<Conduit*>(c)->try_push(item);
                };
            }
        }

        /**
         * @brief Dispatches a pointer to the next available worker conduit.
         * @return true if successfully queued, false if all targets are full.
         */
        SLAB_HOT bool route(T* raw_ptr) noexcept {
            SLAB_FLAT_PATH
            if (!raw_ptr) return false;
            std::size_t idx = next_; // Start from the last successful position
            for (std::size_t i = 0; i < NumOutputs; ++i) {
                // Optimized Switching: Conditional subtraction instead of modulo
                if (outputs_[idx].push_fn && outputs_[idx].push_fn(outputs_[idx].conduit_ptr, raw_ptr)) {
                    next_ = (idx + 1 == NumOutputs) ? 0 : idx + 1;
                    return true;
                }
                if (++idx == NumOutputs) idx = 0;
            }
            return false;
        }
    };
}
