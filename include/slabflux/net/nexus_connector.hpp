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
#include "wire_frame_lsn.hpp"
#include "../core/hole_puncher.hpp"
#include "../core/sf_node_ctx.hpp"
#include "../io/durable_journal.hpp"
#include "../compute/vector_lane_engine.hpp"

namespace slabflux::net {

    template<typename Payload, size_t WindowSize>
    class nexus_connector {
        core::sf_node_ctx& context_;
        core::hole_puncher<Payload, WindowSize> gap_engine_;
        io::durable_journal<Payload> journal_;
        compute::vector_lane_256<64>& engine_;

    public:
        nexus_connector(core::sf_node_ctx& ctx, compute::vector_lane_256<64>& eng, const char* journal_path)
            : context_(ctx), engine_(eng), journal_(journal_path) {
        }

        inline void on_raw_frame(wire_frame_lsn<Payload>* frame) noexcept {
            // 1. Branchless Sequence Assignment
            // Evaluates to a mathematical mask, eliminating the unpredictable CPU branch 
            // penalty associated with "if (lsn == 0)".
            const uint64_t is_zero_mask = (frame->lsn == 0) ? 0xFFFFFFFFFFFFFFFFULL : 0;
            frame->lsn |= (context_.reserve_next() & is_zero_mask);

            // 2. The frame ALREADY resides in the Journal's physical memory.
            // We only tell the Journal to mark it ready for the background OS flusher.
            journal_.commit_slot();

            // 3. Order restoration and reduction (O(1)) - passing the EXACT SAME pointer
            if (gap_engine_.insert(frame->lsn, frame->payload)) {
                gap_engine_.flush_ready([&](const Payload& p, uint64_t lsn) {
                    engine_.propagate(static_cast<float>(p), lsn);
                    context_.commit(lsn);
                });
            }

            // 4. Scattering (zero-copy signal sent to replicas)
            // Note: The replicator_.scatter call is handled at the Nexus Node level using io_uring.
        }
    };
} // namespace slabflux::net
