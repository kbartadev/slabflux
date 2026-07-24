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

#include "delta_compressor.hpp"
#include "networked_replicator.hpp"
#include <x86intrin.h>

namespace slabflux::net {

    template<typename State>
    class delta_broadcaster {
        alignas(64) State previous_truth;
        networked_replicator<delta_block>& replicator;

    public:
        /**
         * @brief Scatters only the modified parts of the simulation.
         */
        void broadcast_state(const State& current_truth, uint64_t lsn) {
            alignas(64) delta_block deltas[1024]; // Max possible deltas

            size_t changed = delta_compressor::generate_delta(
                &current_truth, &previous_truth, sizeof(State)/sizeof(float), deltas
            );

            if (SL_EXPECT_FALSE(changed == 0)) return;

            // Vectorized Dispatch: Hardware-aligned wire frame formulation
            wire_frame_lsn<delta_block> frame;
            frame.lsn = lsn;
            frame.cluster_id = 0x4653;
            frame.type_id = 2; // Fixed delta marker

            // Software pipelining prevents L1 cache stalls during the io_uring scatter phase
            for (size_t i = 0; i < changed; ++i) {
                if (i + 1 < changed) _mm_prefetch(reinterpret_cast<const char*>(&deltas[i+1]), _MM_HINT_T0);
                frame.payload = deltas[i];
                replicator.scatter(&frame);
            }

            // Update local history for the next delta
            std::memcpy(&previous_truth, &current_truth, sizeof(State));
        }
    };

} // namespace slabflux::net
