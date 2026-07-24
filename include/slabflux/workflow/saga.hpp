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
#include <concepts>
#include <cstdint>
#include <utility>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::workflow {

    /**
     * @brief Reversible Hardware Transition Invariant.
     * @details Defines the structural requirements for state transitions 
     * that can be atomically applied or synthesized into a compensation pulse.
     * Eliminates generic microservices saga patterns by binding logic to 
     * the persistent state Context.
     */
    template <typename T, typename Context>
    concept ReversibleTransition = requires(T t, Context& ctx) {
        { t.apply(ctx) } -> std::same_as<bool>;
        { t.revert(ctx) } -> std::same_as<void>;
    };

    /**
     * @brief Concept-Verified Reversible State Machine.
     * @details Replaces generic priority-queue or saga registries with a 
     * statically-resolved transition chain. Synthesizes a branchless 
     * forward pulse and an automated reverse-unwinding trace.
     * 
     * High-Performance Design:
     * 1. Synthesis: Unrolls the forward/backward logic into a prioritized jump-chain.
     * 2. Zero-Abstraction: No virtual calls or heap-allocated command objects.
     * 3. Structural Honesty: The execution trace is fixed at compile-time,
     *    ensuring deterministic O(1) rollback performance.
     */
    template <typename Context, ReversibleTransition<Context>... Transitions>
    class saga_orchestrator {
        static_assert(sizeof...(Transitions) > 0, "Saga must contain at least one hardware transition.");
        std::tuple<Transitions...> transitions_;

    public:
        /**
         * @brief Sovereign Nexus Initialization.
         * @param transitions variadic sequence of reversible steps.
         */
        explicit saga_orchestrator(Transitions... transitions) noexcept 
            : transitions_(std::move(transitions)...) {}

        /**
         * @brief Executes the forward transition pulse.
         * @details Recursively evaluates the chain. If any transition fails, 
         * the synthesis unit automatically dispatches a compensation burst 
         * to restore the physical memory horizon.
         * @param ctx The persistent state container.
         * @return True if the entire saga reached the persistent horizon.
         */
        SLAB_FORCE_INLINE bool execute(Context& ctx) noexcept {
            return apply_step<0>(ctx);
        }

    private:
        /**
         * @brief Recursive Step Synthesis.
         * @tparam I Current index in the structural sequence.
         */
        template <std::size_t I>
        SLAB_FORCE_INLINE bool apply_step(Context& ctx) noexcept {
            if constexpr (I < sizeof...(Transitions)) {
                // Physical Pulse: Attempt forward state transition
                if (SL_EXPECT_TRUE(std::get<I>(transitions_).apply(ctx))) {
                    // Proceed to the next coordinate in the matrix
                    if (apply_step<I + 1>(ctx)) {
                        return true;
                    }
                    
                    // Horizon Breach: Execute compensation for the current step
                    // as the subsequent steps in the chain failed.
                    std::get<I>(transitions_).revert(ctx);
                    return false;
                }
                // Failure at current level; compensation for previous steps
                // is handled by the caller in the recursive unwinding.
                return false;
            }
            return true;
        }
    };

    /**
     * @brief Factory Utility for Inline Saga Synthesis.
     */
    template <typename Context, ReversibleTransition<Context>... Transitions>
    static SLAB_FORCE_INLINE auto make_saga(Transitions&&... ts) noexcept {
        return saga_orchestrator<Context, Transitions...>(std::forward<Transitions>(ts)...);
    }

} // namespace slabflux::workflow