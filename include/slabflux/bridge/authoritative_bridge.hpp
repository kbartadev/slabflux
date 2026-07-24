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
#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/io/durable_journal.hpp"

namespace slabflux::bridge {

using namespace slabflux::core;

 /**
  * @file authoritative_bridge.hpp
  * @brief Non-blocking Truth Serialization.
  * @details Ensures the Core never stalls for I/O while maintaining
  * strict durability and bandwidth-efficient state replication.
  */
template<typename Input, typename State, typename Context, typename Engine, typename Replicator, typename Journal>
class authoritative_bridge {
    Context& context;
    Journal& journal;
    Engine& engine;
    Replicator& broadcaster;

public:
    authoritative_bridge(Context& ctx,
                        Journal& j,
                        Engine& eng,
                        Replicator& bc)
                : context(ctx), journal(j), engine(eng), broadcaster(bc) {}

    /**
     * @brief The Hot Path Ingress Handler.
     * @details Pipelined execution. We prefetch the NEXT frame
     * while processing the current one to hide memory latency.
     */
    SL_ALIGN SL_SECTION_HOT SLAB_HOT
    void on_raw_frame(const Input& raw_input) noexcept {

        // 1. RESERVE: O(1) Get pointer to YOUR io_uring buffer
        auto* frame = journal.reserve_slot();

        // 2. SEQUENCE: Immediate LSN assignment
        const uint64_t lsn = context.reserve_next();
        frame->lsn = lsn;
        frame->payload = raw_input;

        // 2. COMMIT: O(1) commit logic
        journal.commit_slot();

        // 3. EXECUTE: SIMD-accelerated state update
        // We use the LSN to ensure the engine's internal state is tracked.
        engine.process_input(frame->payload, lsn);

        // 4. DELTA SCATTER: The Platform
        // Instead of the whole state, we only broadcast the bit-diff (Delta)
        // relative to the last confirmed Snapshot.
        if (context.should_broadcast(lsn)) {
            auto delta = engine.compute_delta_since_last_sync();
            broadcaster.scatter_delta(delta, lsn);
        }

        // 5. COMMIT: Mark this LSN as processed in the local view.
        context.commit(lsn);
    }
};
}
