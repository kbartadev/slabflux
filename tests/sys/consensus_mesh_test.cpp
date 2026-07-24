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
 * ============================================================================* SLABFLUX - Distributed Mesh Test Suite
 */

#include <gtest/gtest.h>
#include "slabflux/core/sequence_generator.hpp"

namespace slabflux::dist {
    struct mesh_packet {
        uint64_t global_lsn;
        uint32_t node_id;
    };

    class causal_sequencer {
        uint64_t last_seen_lsn_ = 0;
    public:
        bool validate_arrival(uint64_t lsn) noexcept {
            if (lsn <= last_seen_lsn_) return false; // Causal violation (Out of order)
            last_seen_lsn_ = lsn;
            return true;
        }
    };

    struct rdma_handle { uint32_t rkey; uint64_t vaddr; };
    class rdma_fabric {
    public:
        static rdma_handle register_region(void* ptr) {
            return {0xABC, reinterpret_cast<uint64_t>(ptr)};
        }
    };

    class failover_signal {
        std::atomic<bool> heartbeat_lost_{false};
    public:
        void trigger() noexcept { heartbeat_lost_.store(true); }
        bool active() const noexcept { return heartbeat_lost_.load(); }
    };
}

using namespace slabflux::core;
using namespace slabflux::dist;

TEST(DistLabs, CausalLSNOrdering) {
    causal_sequencer sequencer;

    // Valid sequence
    EXPECT_TRUE(sequencer.validate_arrival(100));
    EXPECT_TRUE(sequencer.validate_arrival(101));

    // Replay/Old Packet (Network jitter simulation)
    EXPECT_FALSE(sequencer.validate_arrival(100));
    EXPECT_FALSE(sequencer.validate_arrival(99));
}

TEST(DistLabs, NodeDirectoryIntegrity) {
    // Pattern for node_directory.hpp
    struct node_entry { uint32_t id; char ip[16]; };
    EXPECT_EQ(sizeof(node_entry), 20); // Packed entry

    node_entry cluster[2] = {
        {1, "10.0.0.1"},
        {2, "10.0.0.2"}
    };

    EXPECT_STREQ(cluster[1].ip, "10.0.0.2");
}

TEST(DistLabs, RdmaFabricRegistration) {
    alignas(4096) char buffer[4096];
    auto handle = rdma_fabric::register_region(buffer);
    EXPECT_EQ(handle.vaddr, reinterpret_cast<uint64_t>(buffer));
    EXPECT_EQ(handle.rkey, 0xABC);
}

TEST(DistLabs, FailoverHeartbeatLost) {
    failover_signal sig;
    EXPECT_FALSE(sig.active());
    sig.trigger();
    EXPECT_TRUE(sig.active());
}
