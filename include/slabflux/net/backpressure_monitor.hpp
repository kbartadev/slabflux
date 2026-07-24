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

#include "slabflux/core/hot_path_alignment.hpp"
#include <x86intrin.h>

namespace slabflux::net {

    /**
     * @brief Preemptive Load Shedding Router.
     * @details Resilience. Protects the Chip from network
     * storms. Drops incoming data IF the egress pipeline cannot handle the output.
     */
    template<typename IngressConduit, typename EgressConduit>
    class load_shedding_router {
        IngressConduit& ingress_;
        const EgressConduit& egress_;
        const size_t EgressCapacity;

        uint64_t last_tsc_{ 0 };
        size_t last_occupancy_{ 0 };
        int64_t velocity_ewma_q16_{ 0 }; // Fixed-point Q16 velocity tracker

        uint64_t dropped_frames_{ 0 };

    public:
        load_shedding_router(IngressConduit& in, const EgressConduit& out, size_t cap)
            : ingress_(in), egress_(out), EgressCapacity(cap) {
        }

        /**
         * @brief Called by the Linux/Network polling thread (NOT the Chip).
         */
        void route_incoming_packet(const void* raw_data) {
            // 1. Snapshot of the egress state
            size_t egress_occupancy = egress_.approx_size();
            uint64_t now = __rdtsc();

            // 2. Predictive Load Shedding
            // Calculates the mathematical derivative of queue growth to preemptively shed load.
            if (SL_EXPECT_TRUE(last_tsc_ != 0)) {
                int64_t dt = now - last_tsc_;
                int64_t dq = static_cast<int64_t>(egress_occupancy) - static_cast<int64_t>(last_occupancy_);
                int64_t inst_velocity = (dq << 16) / (dt > 0 ? dt : 1);
                
                // Fast IIR Filter (EWMA): alpha = ~0.25
                velocity_ewma_q16_ = (velocity_ewma_q16_ * 3 + inst_velocity) / 4;
                
                // Bypasses textbook static watermarks. Drops if capacity > 70% AND rapidly filling.
                if (SL_EXPECT_FALSE(egress_occupancy > (EgressCapacity * 7 / 10) && velocity_ewma_q16_ > (1 << 14))) {
                    dropped_frames_++;
                    return;
                }
            }
            last_tsc_ = now;
            last_occupancy_ = egress_occupancy;

            // 3. If there is room, forward it to the Chip
            if (!ingress_.try_push(raw_data)) {
                dropped_frames_++;
            }
        }
    };
} // namespace slabflux::net
