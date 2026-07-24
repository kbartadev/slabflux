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
#include <atomic>
#include <cstdint>
#include <x86intrin.h>
#include <immintrin.h>
#include <chrono>
#include <concepts>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief Sovereign State Machine Concept.
     * @details Enforces a strict functional contract for entity lifecycle management.
     */
    struct null_logic { void transition(auto&, uint64_t) noexcept {} };

    template <typename T, typename EntityType>
    concept SovereignStateMachine = requires(T sm, EntityType& e, uint64_t tick) {
        { sm.transition(e, tick) } -> std::same_as<void>;
    };

    // ============================================================
    // SOVEREIGN CAUSAL ENTITY
    // ============================================================
    
    template <typename StateMachine = null_logic>
    struct alignas(64) causal_entity {
        // --- 1D: Movement meta (for Slab chaining) ---
        uint32_t next_free_idx;

        // --- 2. Space (3D Mesh):
        // Bitmask: 0: Free, 1: Core, 2: NLP GPU, 4: Vision GPU, 8: Logic CPU
        std::atomic<uint8_t> ownership_mask{0};

        // --- 3. Vortex (4D Time):
        // Temporal Vector: Hardware-calibrated TAI tracking.
        // Replaces standard library tai_clock to eliminate pipeline bubbles.
        uint64_t expiry_tick{0};

        /** @brief strictly monotonic, leap-second-immune timestamping via C++20 tai_clock. 
         *  @details Essential for causal ordering in distributed clusters. */
        static inline uint64_t generate_tai_time() noexcept {
            return static_cast<uint64_t>(std::chrono::tai_clock::now().time_since_epoch().count());
        }

        // --- 4. Identity Graph:
        uint32_t entity_id;       // Own unique identifier inside the Slab
        uint32_t parent_id;       // Who created it?
        
        // Children mask: Which Experts have I spawned branches to?
        std::atomic<uint32_t> child_active_mask{0};

        // --- 5. Sovereign Logic Injection:
        // The StateMachine defines the specific behavior of this entity.
        // It is injected at the type level to ensure zero-overhead transitions.
        StateMachine logic;

        /**
         * @brief Sovereign Pulse.
         * @details Executes the injected state machine logic.
         */
        SLAB_FORCE_INLINE void pulse(uint64_t current_tick) noexcept {
            logic.transition(*this, current_tick);
        }

        // --- 6. The Useful Payload ---
        alignas(64) float data[1024]; 
    };

    // Compatibility alias for legacy test suites
    using causal_tensor_entity = causal_entity<null_logic>;

} // namespace slabflux::core
