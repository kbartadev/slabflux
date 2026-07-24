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
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/security/kinetic_inscription.hpp"
#include "slabflux/rte/error_arbiter.hpp"

namespace slabflux::compute {

    /**
     * @brief In-Band Physical Time Budget Enforcement.
     * @details Replaces the non-deterministic external watchdog.
     * It checks if the gap between two recorded ticks exceeds the physical budget.
     */
    class temporal_guard {
        uint64_t last_tsc_{ 0 };
        const uint64_t cycle_budget_; // Max allowed cycles between ticks (e.g., 3,000,000 for 1ms at 3GHz)

    public:
        explicit temporal_guard(uint64_t budget) : cycle_budget_(budget) {}

        /**
         * @brief Evaluates the physical passage of time deterministically.
         */
        template <size_t ArbiterCapacity = 1024>
        SLAB_HOT void evaluate_tick(uint64_t current_tsc, uint64_t current_lsn, 
                                    const security::semiotic_tapestry* tapestry = nullptr,
                                    rte::error_arbiter<ArbiterCapacity>* arbiter = nullptr) noexcept {
            if (SL_EXPECT_FALSE(last_tsc_ != 0)) {
                const uint64_t delta = current_tsc - last_tsc_;

                if (SL_EXPECT_FALSE(delta > cycle_budget_)) {
                    // DETERMINISTIC PANIC!
                    // This is guaranteed to happen in exactly the same way during Replay 
                    // because 'current_tsc' comes from the immutable Journal.
                    if (tapestry) tapestry->engrave_anomaly(0xDE, current_lsn);
                    if (arbiter) arbiter->record_error(rte::error_domain::compute, 0xDEAD71C5, rte::error_severity::fatal, current_lsn);

                    if (!tapestry && !arbiter) {
                        while(true) _mm_pause(); // Structural Oblivion fallback
                    }
                }
            }
            last_tsc_ = current_tsc;
        }

        /**
         * @brief Convenience overload for explicit Error Arbiter routing.
         * @details Resolves the signature so callers are not forced to pass `nullptr` 
         * for the Tapestry dependency when only routing to the Arbiter.
         */
        template <size_t ArbiterCapacity = 1024>
        SLAB_HOT void evaluate_tick(uint64_t current_tsc, uint64_t current_lsn, 
                                    rte::error_arbiter<ArbiterCapacity>* arbiter) noexcept {
            evaluate_tick(current_tsc, current_lsn, nullptr, arbiter);
        }
    };

} // namespace slabflux::compute
