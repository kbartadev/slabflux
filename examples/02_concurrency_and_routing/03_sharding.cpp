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
 * ============================================================================* @file 03_sharding.cpp
 * @brief Hardware-isolated lock-free sharding using round_robin_switch.
 */
#include <iostream>
#include <thread>
#include <immintrin.h>
#include "slabflux/meta.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/conduit.hpp"
#include "slabflux/core/router.hpp"
#include "slabflux/bridge/round_robin_switch.hpp"

using namespace slabflux;
using namespace slabflux::core;
using namespace slabflux::bridge;

struct task_event {
    int job_id;
    task_event(int id) : job_id(id) {}
};

// Worker thread: Zero-lock processing from its own dedicated conduit
template <typename Conduit>
void shard_worker(int shard_id, Conduit& in_pipe, spsc_pool<task_event, 4096>& pool) {
    int processed = 0;

    while (true) {
        task_event* ev = in_pipe.pop(); // Now returns raw pointer

        if (ev->job_id == -1) { pool.release(ev); break; }

        processed++;
        pool.release(ev); // O(1) lock-free return
    }
    std::cout << "[Shard " << shard_id << "] Processed " << processed << " tasks.\n";
}

int main() {
    std::cout << "=== Showcase 03: Local Sharding via Round Robin Switch ===\n\n";

    spsc_pool<task_event, 4096> memory;

    // Physically separated conduits for the threads
    spsc_conduit<task_event*, 1024> track_A;
    spsc_conduit<task_event*, 1024> track_B;

    // Deterministic O(1) Load Balancer
    round_robin_switch<task_event, 2> load_balancer;
    load_balancer.bind_track(0, track_A);
    load_balancer.bind_track(1, track_B);

    std::thread t1([&track_A, &memory]() { shard_worker(0, track_A, memory); });
    std::thread t2([&track_B, &memory]() { shard_worker(1, track_B, memory); });

    std::cout << "Dispatching 100,000 tasks across shards...\n";

    for (int i = 0; i < 100000; ++i) {
        // Clean Active Polling: The pool auto-reclaims internally!
        load_balancer.route(memory.make(i)); // make now returns T*
    }

    // Graceful Shutdown
    load_balancer.route(memory.make(-1));
    load_balancer.route(memory.make(-1));

    t1.join();
    t2.join();

    return 0;
}
