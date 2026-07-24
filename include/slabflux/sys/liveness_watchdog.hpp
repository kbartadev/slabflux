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
 * @file liveness_watchdog.hpp
 * @brief Physical Time Budget Enforcement.
 * @details Monitors the compute core's TSC progress. If a single event 
 * takes longer than the predefined budget, it triggers a deterministic emergency halt.
 */

#pragma once

#include <x86intrin.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include <atomic>
#include <concepts>
#include "slabflux/security/kinetic_inscription.hpp"
#include <iostream>
#include <cstdlib>

namespace slabflux::sys {

    /**
     * @brief Liveness Policy Concept.
     * @details Defines the contract for how a core's liveness is determined.
     */
    template <typename T, typename MarkerType>
    concept LivenessPolicy = requires(T policy, uint64_t current_tsc, MarkerType active_marker, MarkerType last_marker, uint64_t cycle_budget) {
        { policy.is_hung(current_tsc, active_marker, last_marker, cycle_budget) } -> std::same_as<bool>;
    };

    /**
     * @brief Default Liveness Policy.
     * @details Detects a hang if the observed marker hasn't advanced within the cycle budget.
     */
    struct default_liveness_policy {
        template <typename MarkerType>
        static SLAB_FORCE_INLINE bool is_hung(uint64_t current_tsc, MarkerType active_marker, MarkerType last_marker, uint64_t cycle_budget) noexcept {
            return (active_marker == last_marker && (current_tsc - active_marker) > cycle_budget);
        }
    };

    /**
     * @brief Monotonic Clock State Observer.
     * @details Monitors the compute core's TSC progress against a shared marker.
     * If a single event takes longer than the predefined budget, it triggers a deterministic emergency halt.
     * @tparam Policy The liveness detection policy to use.
     */
    template <typename Policy = default_liveness_policy>
    class liveness_watchdog {
        const uint64_t cycle_budget_;
        std::atomic<uint64_t>& shared_tsc_marker_;
        std::atomic<bool> active_{true};
        const security::semiotic_tapestry* tapestry_{ nullptr };

        static void execute_valid_monitor(liveness_watchdog*, uint8_t) noexcept {
            _mm_pause();
        }

        static void execute_void_monitor(liveness_watchdog* wd, uint8_t fray) noexcept {
            // Kinetic Inscription: LBR MSR engraving for cycle budget overrun.
            if (wd->tapestry_) wd->tapestry_->engrave_anomaly(fray, 0);
            
            std::cerr << "Liveness budget exceeded - Core Hang detected\n";
            std::abort();
        }

    public:
        liveness_watchdog(uint64_t max_cycles, std::atomic<uint64_t>& marker)
            : cycle_budget_(max_cycles), shared_tsc_marker_(marker) {}

        void bind_tapestry(const security::semiotic_tapestry* tapestry) noexcept {
            tapestry_ = tapestry;
        }

        void stop() noexcept { active_.store(false, std::memory_order_release); }

        /**
         * @brief Executed on a separate "Supervisor" core.
         */
        void monitor() {
            uint64_t last_marker = shared_tsc_marker_.load(std::memory_order_acquire);
            using sink_t = void (*)(liveness_watchdog*, uint8_t) noexcept;
            const sink_t aphasic_horizon_[2] = { &execute_valid_monitor, &execute_void_monitor };

            while (active_.load(std::memory_order_relaxed)) {
                uint64_t current_tsc = __rdtsc();
                uint64_t active_marker = shared_tsc_marker_.load(std::memory_order_acquire);

                uint8_t fray = Policy::is_hung(current_tsc, active_marker, last_marker, cycle_budget_) ? 1 : 0;
                
                // Teleological Agnosia: Natively index into the terminal void if frayed.
                aphasic_horizon_[fray](this, fray);

                last_marker = active_marker;
            }
        }

        /**
        * @brief Checkpoint for the current task. 
        * @details In production, this would communicate with a supervisor core.
        */
        inline void checkpoint() noexcept {
            asm volatile("" : : : "memory");
        }
    };
}
