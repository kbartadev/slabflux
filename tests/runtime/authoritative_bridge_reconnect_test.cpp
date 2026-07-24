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
 * @file authoritative_bridge_reconnect_test.cpp
 * @brief End-to-end reconnect: journal gap-fill without duplicate LSN application.
 */

#include <gtest/gtest.h>

#include <vector>

#include "slabflux/bridge/authoritative_bridge.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/core/sf_node_ctx.hpp"

using namespace slabflux;

namespace {

struct abr_journal_log {
    std::vector<net::wire_frame_lsn<float>> committed;
    net::wire_frame_lsn<float> staging{};
    uint64_t next_lsn = 1;

    net::wire_frame_lsn<float>* reserve_slot() noexcept { return &staging; }

    void commit_slot() noexcept {
        staging.cluster_id = 0x4653;
        staging.lsn = next_lsn++;
        committed.push_back(staging);
    }
};

struct abr_mock_ctx : core::sf_node_ctx {
    uint64_t last_committed = 0;

    uint64_t reserve_next() noexcept { return last_committed + 1; }

    bool should_broadcast(uint64_t) const noexcept { return false; }

    void commit(uint64_t lsn) noexcept { last_committed = lsn; }
};

struct abr_mock_engine {
    std::vector<uint64_t> applied_lsns;
    float last_payload = 0.0f;

    void process_input(float payload, uint64_t lsn) {
        applied_lsns.push_back(lsn);
        last_payload = payload;
    }

    int compute_delta_since_last_sync() { return 0; }
};

struct abr_mock_replicator {
    void scatter_delta(int, uint64_t) {}
};

void abr_replay_gap(abr_journal_log& journal, abr_mock_engine& engine, abr_mock_ctx& ctx, uint64_t watermark) {
    for (const auto& frame : journal.committed) {
        if (frame.lsn > watermark) {
            engine.process_input(frame.payload, frame.lsn);
            ctx.commit(frame.lsn);
        }
    }
}

}  // namespace

TEST(AuthoritativeBridgeReconnect, LiveThenGapFillPreservesMonotonicLsns) {
    abr_mock_ctx ctx;
    abr_journal_log journal;
    abr_mock_engine engine;
    abr_mock_replicator replicator;

    bridge::authoritative_bridge<float, int, abr_mock_ctx, abr_mock_engine, abr_mock_replicator, abr_journal_log> bridge{
        ctx, journal, engine, replicator};

    // Live window before partition.
    for (int i = 0; i < 5; ++i) {
        bridge.on_raw_frame(10.0f + static_cast<float>(i));
    }

    const uint64_t watermark = ctx.last_committed;
    ASSERT_EQ(watermark, 5u);
    ASSERT_EQ(engine.applied_lsns.size(), 5u);

    // Partition: no new live frames, but journal retained all committed LSNs.
    abr_mock_engine reconnected_engine;
    abr_mock_ctx reconnected_ctx;
    reconnected_ctx.last_committed = watermark;

    abr_replay_gap(journal, reconnected_engine, reconnected_ctx, watermark);

    ASSERT_EQ(reconnected_engine.applied_lsns.size(), 0u);

    // Reconnect: continue live + replay only the gap.
    for (int i = 0; i < 3; ++i) {
        bridge.on_raw_frame(100.0f + static_cast<float>(i));
    }
    abr_replay_gap(journal, reconnected_engine, reconnected_ctx, watermark);

    ASSERT_EQ(reconnected_engine.applied_lsns.size(), 3u);
    EXPECT_EQ(reconnected_engine.applied_lsns[0], 6u);
    EXPECT_EQ(reconnected_engine.applied_lsns[2], 8u);
    EXPECT_EQ(reconnected_ctx.last_committed, 8u);
}
