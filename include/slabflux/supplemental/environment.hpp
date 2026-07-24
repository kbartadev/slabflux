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

#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "../core.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace slabflux::supplemental {
    class environment {
    public:
        void spawn_worker(auto&, auto&, auto&, int) {}
        void spawn_io_node(auto&, int) {}
        void start() {}
    };
}

namespace slabflux::supplemental {

// ============================================================
// SUPPLEMENTAL LAYER: ENVIRONMENT BUILDER
// Declarative API for constructing threads, Nodes, and topology.
// Runs strictly during initialization; does not affect Core O(1) physics.
// ============================================================

class environment {
    // Threads are stored here only during initialization
    std::vector<std::thread> workers_;
    bool is_running_{false};

    // Internal helper for CPU core pinning (OS-level determinism)
    void pin_thread_to_core(std::thread& t, int core_id) {
        if (core_id < 0) return;  // No pinning

#if defined(_WIN32)
        HANDLE native_handle = t.native_handle();
        DWORD_PTR mask = (1ull << core_id);
        SetThreadAffinityMask(native_handle, mask);
#else
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
    }

   public:
    environment() = default;

    ~environment() { stop(); }

    // 1. Add a Compute/Worker Node to the topology
    // This hides the boilerplate “while(running)” loop and conduit popping
    template <typename Node, typename Conduit>
    void spawn_worker(Node& node, Conduit& rx_conduit, int cpu_core = -1) {
        workers_.emplace_back([this, &node, &rx_conduit]() {
            std::cout << "[SLABFLUX Env] Worker thread started.\n";

            // HOT PATH — This is where O(1) is decided
            while (is_running_) {
                if (auto ev = rx_conduit.pop()) {
                    node.on(ev);
                    // When leaving scope, 'ev' returns to the pool via RAII
                } else {
                    // If the conduit is empty, yield (or spin-wait)
                    // Backpressure tuning could be added here
                    std::this_thread::yield();
                }
            }
        });

        // OS-level optimization
        pin_thread_to_core(workers_.back(), cpu_core);
    }

    // 2. Start an I/O Node (e.g., Clock or Durable Source)
    // These Nodes have their own run() loop, which may block
    template <typename IONode>
    void spawn_io_node(IONode& io_node, int cpu_core = -1) {
        workers_.emplace_back([this, &io_node]() {
            std::cout << "[SLABFLUX Env] I/O node thread started.\n";

            // The I/O Node runs until stopped internally or by the Env
            io_node.run();
        });

        pin_thread_to_core(workers_.back(), cpu_core);
    }

    // Arm the system
    void start() {
        std::cout << "[SLABFLUX Env] Topology locked. Igniting physical simulation...\n";
        is_running_ = true;
    }

    // Block the main thread while the system is running
    void wait_for_shutdown() {
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    void stop() { is_running_ = false; }
};

}  // namespace slabflux::supplemental
