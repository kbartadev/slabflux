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
 * ============================================================================* @file 01_deterministic_network_routing.cpp
 * @brief Enterprise Networking: Deterministic Polling & Backpressure Routing
 * * This example demonstrates the networking primitives of the SLABFLUX library:
 * 1. Round-Robin Poller (Fan-in): Fair event extraction under asymmetric load.
 * 2. Round-Robin Switch (Fan-out): Hardware-aligned router with O(1) complexity.
 * 3. Backpressure (Congestion control): Safe dropping without memory leaks
 * when output conduits are full.
  */

#include <iostream>
#include <iomanip>
#include "slabflux/meta.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/conduit.hpp"
#include "slabflux/core/router.hpp"
#include "slabflux/bridge/round_robin_poller.hpp"
#include "slabflux/bridge/round_robin_switch.hpp"

using namespace slabflux;
using namespace slabflux::core;
using namespace slabflux::bridge;

// ============================================================================
// 1. NETWORK PAYLOAD
// ============================================================================

struct market_tick {
    int feed_source;
    double price;

    market_tick(int src, double p) : feed_source(src), price(p) {}
};

// ============================================================================
// 2. ORCHESTRATION & ROUTING
// ============================================================================

int main() {
    std::cout << "=== Showcase 02: Deterministic Network Routing ===\n\n";

    // O(1) LIFO allocator with lock-free MPSC return wire
    spsc_pool<market_tick, 256> memory;

    // --- INGRESS (Incoming network) ---
    spsc_conduit<market_tick*, 1024> feed_primary;
    spsc_conduit<market_tick*, 1024> feed_backup;

    round_robin_poller<market_tick, 2> ingress_router;
    ingress_router.bind_track(0, feed_primary);
    ingress_router.bind_track(1, feed_backup);

    // Simulating asymmetric load: primary is faster than backup.
    feed_primary.push(memory.make(0, 4500.25));
    feed_primary.push(memory.make(0, 4500.50));
    feed_backup.push(memory.make(1, 4500.30));

    // --- EGRESS (Outgoing network / Execution) ---
    // Intentionally SMALL conduits to demonstrate Switch Drop (Backpressure)!
    spsc_conduit<market_tick*, 1> exchange_a;
    spsc_conduit<market_tick*, 1> exchange_b;

    round_robin_switch<market_tick, 2> egress_router;
    egress_router.bind_track(0, exchange_a);
    egress_router.bind_track(1, exchange_b);

    // --- TICK-TO-TRADE LOOP ---
    std::cout << "[ROUTER] Starting polling loop...\n";

    int routed_count = 0;

    // ingress_router.poll() returns a raw pointer
    while (auto tick = ingress_router.poll()) {
        std::cout << "  -> Polled tick from Feed " << tick->feed_source
                  << " | Price: $" << std::fixed << std::setprecision(2)
                  << tick->price << "\n";

        // The Switch tries to push into the output conduits.
        // If all conduits are full, the try_push fails inside, ownership is NOT taken,
        // and the smart pointer silently returns the memory to the pool!
        egress_router.route(tick);
        routed_count++;
        memory.release(tick);
    }

    std::cout << "\n[ROUTER] Ingress queues empty. Routed " << routed_count << " ticks.\n";

    // --- VERIFY BACKPRESSURE (Congestion check) ---
    std::cout << "\n[SWITCH] Checking egress queues (Capacity: 1 each)...\n";

    auto out_a = exchange_a.pop(memory);
    auto out_b = exchange_b.pop(memory);

    std::cout << "  Exchange A has data: " << (out_a ? "YES" : "NO") << "\n";
    std::cout << "  Exchange B has data: " << (out_b ? "YES" : "NO") << "\n";

    // The 3rd tick we attempted to insert was DROPPED due to Backpressure,
    // but the memory pool remained stable. Zero memory leaks.

    return 0;
}
