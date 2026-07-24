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
 * @file orchestrator_reconnect_test.cpp
 * @brief Split-brain recovery: rebind after link-dead and gap-fill replay contract.
 */

#include <gtest/gtest.h>

#include <vector>

#include "slabflux/orchestration/failover_orchestrator.hpp"

using namespace slabflux;
using namespace slabflux::orchestration;

namespace {

struct recon_mock_conduit {
    bool alive = true;
    [[nodiscard]] bool is_alive() const noexcept { return alive; }
};

struct recon_mock_router {
    using conduit_type = recon_mock_conduit;

    uint16_t bound_node = 0;
    recon_mock_conduit* bound_conduit = nullptr;
    uint16_t last_unbound_node = 0;

    void unbind_route(uint16_t node_id) {
        last_unbound_node = node_id;
        if (bound_node == node_id) {
            bound_conduit = nullptr;
        }
    }

    void bind_route(uint16_t node_id, recon_mock_conduit& conduit) {
        bound_node = node_id;
        bound_conduit = &conduit;
    }
};

// Contract for authoritative replay after a 5s partition: apply only LSNs > watermark.
std::vector<uint64_t> replay_from_journal(const std::vector<uint64_t>& journal,
                                         uint64_t watermark_lsn) {
    std::vector<uint64_t> applied;
    for (uint64_t lsn : journal) {
        if (lsn > watermark_lsn) {
            applied.push_back(lsn);
        }
    }
    return applied;
}

}  // namespace

TEST(OrchestratorReconnect, LinkReadyRebindsNewConduit) {
    recon_mock_router router;
    failover_orchestrator<recon_mock_router> orchestrator(router);

    recon_mock_conduit old_link;
    recon_mock_conduit new_link;
    router.bind_route(2, old_link);

    core::local_pool<sys::link_dead_event, 4> dead_pool;
    auto dead = dead_pool.make();
    dead->node_id = 2;
    orchestrator.on(dead);
    EXPECT_EQ(router.last_unbound_node, 2);
    EXPECT_EQ(router.bound_conduit, nullptr);

    core::local_pool<sys::link_ready_event, 4> ready_pool;
    auto ready = ready_pool.make();
    ready->node_id = 2;
    ready->new_conduit_ptr = reinterpret_cast<uintptr_t>(&new_link);
    orchestrator.on(ready);

    EXPECT_EQ(router.bound_node, 2);
    EXPECT_EQ(router.bound_conduit, &new_link);
}

TEST(OrchestratorReconnect, GapFillReplayDoesNotDuplicateCommittedLsns) {
    const std::vector<uint64_t> journal = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const uint64_t last_committed_before_partition = 3;

    const auto gap_fill = replay_from_journal(journal, last_committed_before_partition);
    ASSERT_EQ(gap_fill.size(), 7u);
    EXPECT_EQ(gap_fill.front(), 4u);
    EXPECT_EQ(gap_fill.back(), 10u);

    std::vector<uint64_t> live_applied = {1, 2, 3};
    live_applied.insert(live_applied.end(), gap_fill.begin(), gap_fill.end());
    EXPECT_EQ(live_applied.size(), 10u);
    for (size_t i = 0; i < live_applied.size(); ++i) {
        EXPECT_EQ(live_applied[i], i + 1);
    }
}
