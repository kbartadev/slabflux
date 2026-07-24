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
 *
 * @file event_arbiter.hpp
 * @brief Metaprogrammed Hierarchy Sorter and Prioritized Multi-Channel Polling.
 * @details Replaces generic priority-queue scheduling with a statically-resolved 
 * hierarchy. Synthesizes a branchless polling trace based on the structural 
 * sequence of template parameters to ensure deterministic O(1) arbitration.
 * Ensures Administrative commands and Temporal Ticks are processed with 
 * absolute priority over raw data frames to maintain system control.
 */

#pragma once

#include <tuple>
#include <cstdint>
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::rte {

    /**
     * @brief Strategic Event Arbitrator.
     * @tparam AdminBus Conduit for administrative and control commands.
     * @tparam TimeBus Conduit for temporal ticks determining timeouts.
     * @tparam DataBus Conduit for standard raw data frames.
     */
    template<typename AdminBus, typename TimeBus, typename DataBus>
    class event_arbiter {
        AdminBus& admin_;
        TimeBus&  time_;
        DataBus&  data_;
        uint32_t  admin_empty_strike_{ 0 };

    public:
        explicit event_arbiter(AdminBus& a, TimeBus& t, DataBus& d) noexcept 
            : admin_(a), time_(t), data_(d) {}

        /**
         * @brief Synthesized Hierarchy Polling with Starvation Prevention.
         * @details Evaluates conduits through an unrolled prioritization trace.
         * Data frames are processed only if the admin queue remains empty 
         * for 5 consecutive polls to prevent control-plane starvation.
         * @param out_type Reference to receive the ID of the reaped event type.
         * @return Pointer to the reaped event, or nullptr if all channels are empty.
         */
        SLAB_FORCE_INLINE void* poll_next(uint8_t& out_type) noexcept {
            // 1. Highest Priority: Admin/Control
            if (auto* ev = admin_.pop()) {
                admin_empty_strike_ = 0;
                out_type = 0; // ADMIN_TYPE
                return ev;
            }

            admin_empty_strike_++;

            // 2. High Priority: Temporal Ticks (determines state timeouts)
            if (auto* ev = time_.pop()) {
                out_type = 1; // TICK_TYPE
                return ev;
            }

            // 3. Normal Priority: Data (Subject to Weighted Strike)
            if (admin_empty_strike_ >= 5) {
                if (auto* ev = data_.pop()) {
                    out_type = 2; // DATA_TYPE
                    return ev;
                }
            }

            return nullptr;
        }
    };
}