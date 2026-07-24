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
 * @file bridge_sequence_test.cpp
 * @brief Verification of the atomic 4-step event handoff sequence.
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/bridge/authoritative_bridge.hpp"

namespace {
    struct seq_tracker {
        std::vector<std::string> log;
        void record(std::string step) { log.push_back(std::move(step)); }
    };

    // FIX: Remove 'override'. We use Static Polymorphism for 0ns overhead.
    struct seq_mock_ctx : public slabflux::core::sf_node_ctx {
        seq_tracker& tracker;
        seq_mock_ctx(seq_tracker& t) : tracker(t) {}

        uint64_t reserve_next() noexcept {
            tracker.record("SEQUENCE");
            return 1001;
        }
        bool should_broadcast(uint64_t) const noexcept { return true; }
        void commit(uint64_t) noexcept {}
    };

    struct seq_mock_journal {
        seq_tracker& tracker;
        slabflux::net::wire_frame_lsn<int> storage;

        auto* reserve_slot() noexcept { return &storage; }
        void commit_slot() noexcept { tracker.record("PERSIST"); }
    };

    struct seq_mock_engine {
        seq_tracker& tracker;
        void process_input(int, uint64_t) { tracker.record("EXECUTE"); }
        int compute_delta_since_last_sync() { return 0; }
    };

    struct seq_mock_broadcaster {
        seq_tracker& tracker;
        // FIX: Match the Bridge's expected method name: scatter_delta
        void scatter_delta(int delta, uint64_t lsn) {
            (void)delta; (void)lsn;
            tracker.record("SCATTER");
        }
    };
}

TEST(BridgeTest, SequenceEnforcement) {
    seq_tracker tracker;
    seq_mock_ctx ctx{tracker};
    seq_mock_journal journal{tracker};
    seq_mock_engine engine{tracker};
    seq_mock_broadcaster broadcaster{tracker};

    // ZERO-COPY INITIALIZATION
    slabflux::bridge::authoritative_bridge<int, int, seq_mock_ctx, seq_mock_engine, seq_mock_broadcaster, seq_mock_journal> bridge{
        ctx, journal, engine, broadcaster
    };

    // Trigger the hot-path
    bridge.on_raw_frame(42);

    // Verify the physical sequence of truth
    ASSERT_EQ(tracker.log.size(), 4);
    EXPECT_EQ(tracker.log[0], "SEQUENCE");
    EXPECT_EQ(tracker.log[1], "PERSIST");
    EXPECT_EQ(tracker.log[2], "EXECUTE");
    EXPECT_EQ(tracker.log[3], "SCATTER");

    // Verify the journal storage was actually updated
    EXPECT_EQ(journal.storage.lsn, 1001);
}
