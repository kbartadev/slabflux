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
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/spsc_conduit.hpp"

namespace slabflux::rte {

    /**
     * @brief Type-Safe Integral Control Loop.
     * @details Statically derives setpoints and synthesizes state 
     * transition logic for flow regulation.
     */
    template <std::size_t Capacity>
    struct integral_control_loop {
        static constexpr size_t UPPER_SET_POINT = (Capacity * 80) / 100;
        static constexpr size_t LOWER_SET_POINT = (Capacity * 40) / 100;

        SLAB_FORCE_INLINE static bool evaluate(size_t load, bool current_state) noexcept {
            // Synthesis: Branchless state transition based on setpoint crossing.
            if (SL_EXPECT_FALSE(load >= UPPER_SET_POINT)) return true;
            if (load < LOWER_SET_POINT) return false;
            return current_state;
        }
    };

    /**
     * @brief High-Velocity Flow Regulator.
     * @details Implements a Type-Safe Control Loop Injector that 
     * decouples the monitoring mechanism from 
     * the decision policy to ensure O(1) determinism.
     */
    template<typename Conduit, std::size_t Capacity, typename ControlLoop = integral_control_loop<Capacity>>
    class alignas(64) flow_controller {
        Conduit& monitored_conduit_;
        alignas(64) std::atomic<bool> is_congested_{ false };

    public:
        explicit flow_controller(Conduit& conduit) noexcept 
        : monitored_conduit_(conduit) {}

        /**
         * @brief Evaluates backpressure status.
         */
        SLAB_HOT inline bool check_backpressure() noexcept {
            const size_t current_load = monitored_conduit_.approx_size();
            
            // Injection: Delegate the decision to the statically-resolved control loop.
            const bool next_state = ControlLoop::evaluate(current_load, is_congested_.load(std::memory_order_relaxed));
            is_congested_.store(next_state, std::memory_order_relaxed);

            return next_state;
        }

        [[nodiscard]] inline bool is_congested() const noexcept {
            return is_congested_.load(std::memory_order_relaxed);
        }
    };
}
