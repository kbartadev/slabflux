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

#include <gtest/gtest.h>
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/bridge/authoritative_bridge.hpp"
#include "slabflux/core/sf_node_ctx.hpp"

using namespace slabflux;

namespace {
    struct ab_mock_context : public core::sf_node_ctx {
        uint64_t reserve_next() noexcept { return 1000; }
        bool should_broadcast(uint64_t) const noexcept { return true; }
        void commit(uint64_t) noexcept {}
    };

    struct ab_mock_journal {
        slabflux::net::wire_frame_lsn<float> storage;
        uint64_t last_committed_lsn{0};

        // Matches YOUR API
        auto* reserve_slot() noexcept { return &storage; }
        void commit_slot() noexcept { last_committed_lsn = storage.lsn; }
    };

    struct ab_mock_engine {
        uint64_t processed_lsn{0};
        void process_input(float, uint64_t lsn) { processed_lsn = lsn; }
        int compute_delta_since_last_sync() { return 0; }
    };

    struct ab_mock_replicator {
        void scatter_delta(int, uint64_t) {}
    };
}

TEST(BridgeTest, AtomicSequenceConsistency) {
    ab_mock_context ctx;
    ab_mock_journal journal;
    ab_mock_engine engine;
    ab_mock_replicator broadcaster;

    // Bridge using YOUR Journal API
    bridge::authoritative_bridge<float, int, ab_mock_context, ab_mock_engine, ab_mock_replicator, ab_mock_journal> bridge{
        ctx, journal, engine, broadcaster
    };

    bridge.on_raw_frame(42.0f);

    EXPECT_EQ(engine.processed_lsn, 1000);
    EXPECT_EQ(journal.last_committed_lsn, 1000);
}
