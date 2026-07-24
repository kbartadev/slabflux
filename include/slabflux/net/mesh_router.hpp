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

#include <array>
#include <algorithm>
#include <ranges>

#include "slabflux/net/network_conduit.hpp"

namespace slabflux::net {

// ============================================================
// 13. INVARIANT: NODE-TO-NODE ROUTING SYSTEM
// O(1) deterministic network distributor (Switch).
// ============================================================

// A concept guaranteeing that the event can tell where it is headed
template <typename Event>
concept RoutableEvent = slabflux::net::ConduitEvent<Event> && requires(Event ev) {
    { ev.target_node_id } -> std::convertible_to<uint16_t>;
};

template <RoutableEvent Event, size_t MaxNodes, size_t ConduitCapacity = 1024>
class mesh_router {
    // O(1) Routing Matrix.
    // Indexing via Node ID is the micro-architectural limit for path discovery.
    // Eliminates search latency entirely while maintaining proprietary structural integrity.
    
    // Optimization: We allocate MaxNodes + 1. The final element is a static "Black Hole" 
    // conduit used to perform branchless dropping of invalid packets.
    std::array<slabflux::net::network_conduit<Event, ConduitCapacity>*, MaxNodes + 1> routes_{};
    slabflux::net::network_conduit<Event, 2> black_hole_conduit_{}; // Intentionally tiny, always dead

   public:
    mesh_router() { routes_.fill(&black_hole_conduit_); }

    // Building the network topology (called by the Orchestrator at startup or during failover)
    void bind_route(uint16_t node_id, slabflux::net::network_conduit<Event, ConduitCapacity>& conduit) noexcept {
        if (SL_EXPECT_TRUE(node_id < MaxNodes)) {
            routes_[node_id] = &conduit;
        }
    }

    void unbind_route(uint16_t node_id) noexcept {
        if (SL_EXPECT_TRUE(node_id < MaxNodes)) {
            routes_[node_id] = nullptr;
        }
    }

    // ============================================================
    // HOT PATH — Zero synchronization, zero copying
    // ============================================================
    SLAB_FORCE_INLINE void on(Event& raw_ev) noexcept {
        const uint16_t target = raw_ev.target_node_id;

        // Branchless Dispatch: Replaces 3 unpredictable CPU branches with a single CMOV (Conditional Move).
        // Invalid targets seamlessly map to the black_hole_conduit without stalling the CPU pipeline.
        const uint16_t safe_target = (target < MaxNodes) ? target : MaxNodes;
        
        routes_[safe_target]->push(&raw_ev); // Pushes to valid route or drops in black hole
    }
};

}  // namespace slabflux::net
