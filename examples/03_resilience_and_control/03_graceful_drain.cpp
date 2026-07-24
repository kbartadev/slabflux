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
 * ============================================================================* @file 03_graceful_drain.cpp
 * @brief Graceful shutdown of O(1) pipelines.
 */
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include "slabflux/meta.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/conduit.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

using namespace slabflux;
using namespace slabflux::core;

// NOTE: Assuming spsc_pool now returns T*
struct work_event {
    bool is_poison_pill = false;
    work_event(bool pill = false) : is_poison_pill(pill) {}
};

std::atomic<bool> global_shutdown_requested{false};

// OS-level signal handler (e.g., Ctrl+C).
// Translates an external interrupt into a controlled shutdown request.
void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[OS] SIGINT received. Initiating graceful shutdown...\n";
        global_shutdown_requested.store(true, std::memory_order_release);
    }
}

int main() {
    std::signal(SIGINT, signal_handler);

    spsc_pool<work_event, 1024> pool; // Assuming spsc_pool now returns T*
    spsc_conduit<work_event*, 1024> processing_pipe; // Conduit now uses T*

    // Background worker that runs until a poison pill is received or shutdown requested.
    // No forced termination, no half-processed queues.
    std::thread worker([&]() {
        int processed = 0;
        while (!global_shutdown_requested.load(std::memory_order_acquire)) {
            // processing_pipe.pop now returns T*
            auto ev = processing_pipe.pop();
            if (!ev) { std::this_thread::yield(); continue; }
            if (ev->is_poison_pill) {
                pool.release(ev); // Release memory for poison pill
                break;
            }
            processed++;
            pool.release(ev); // Release memory for regular event
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "[Worker] Safely processed " << processed << " events before shutdown.\n";
    });

    // Main thread acts as producer.
    for (int i = 0; i < 50; ++i) {
        if (auto ev = pool.make()) {
            while (!processing_pipe.try_push(ev)) {
                std::this_thread::yield();
            }
        }
    }

    // Send poison pill
    if (auto pill = pool.make(true)) {
        while (!processing_pipe.try_push(pill)) {
            std::this_thread::yield();
        }
    }

    worker.join();
    return 0;
}
