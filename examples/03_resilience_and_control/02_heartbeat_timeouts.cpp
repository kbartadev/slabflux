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
 * ============================================================================* @file 02_heartbeat_timeouts.cpp
 * @brief Heartbeat and connection timeout handling.
 */
#include <chrono>
#include <iostream>
#include <thread>

#include "slabflux/meta.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/conduit.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

using namespace slabflux;
using namespace slabflux::core;

// NOTE: Assuming spsc_pool now returns T*
// System-level event carrying a timestamp.
// Time is just another event type in the fabric.
struct heartbeat_tick {
    uint64_t timestamp_ms;
    heartbeat_tick(uint64_t t) : timestamp_ms(t) {}
};

struct connection_monitor {
    uint64_t last_activity_ms = 0;

    // External activity updates (simulated here).
    void update_activity(uint64_t current_time) { last_activity_ms = current_time; }

    // On receiving a time event, compute idle duration and check timeout.
    void on(heartbeat_tick* ev) {
        if (!ev) return; // Check for null pointer
        uint64_t idle_time = ev->timestamp_ms - last_activity_ms;
        std::cout << "[Monitor] Heartbeat received. Idle for: " << idle_time << "ms.\n";

        if (idle_time > 2000) {
            std::cout << "[Monitor] WARNING: Connection Timeout! Dropping session.\n";
        }
    }
};

int main() {
    spsc_pool<heartbeat_tick, 1024> pool; // Assuming spsc_pool now returns T*
    spsc_conduit<heartbeat_tick*, 1024> time_pipe; // Conduit now uses T*

    // Chronos thread: emits a heartbeat event once per second.
    // Time is pushed into the same event fabric as business traffic.
    std::thread chronos_thread([&]() {
        for (int i = 1; i <= 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            uint64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;

            // NOTE: pool.make() now returns T*
            if (auto ev = pool.make(now)) {
                // NOTE: time_pipe.push now accepts T*
                while (!time_pipe.try_push(ev)) {
                    std::this_thread::yield();
                }
            }
        }

        // Send exit signal
        if (auto stop = pool.make(0)) {
            // NOTE: time_pipe.push now accepts T*
            while (!time_pipe.try_push(stop)) {
                std::this_thread::yield();
            }
        }
    });

    // Main thread acts as the processor.
    connection_monitor monitor;
    monitor.update_activity(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);

    while (true) {
        auto ev = time_pipe.pop();
        if (!ev) { std::this_thread::yield(); continue; }
        if (ev->timestamp_ms == 0) {
            pool.release(ev); // Release memory for the exit signal event
            break;
        }
        monitor.on(ev);
        pool.release(ev); // Release memory for the heartbeat event
    }

    chronos_thread.join();
    return 0;
}
