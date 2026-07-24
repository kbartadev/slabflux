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
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "slabflux/core.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::supplemental {

// ============================================================
// SUPPLEMENTAL LAYER: ZERO-OVERHEAD TELEMETRY
// Zero locks, zero atomics, zero false sharing.
// ============================================================

// 1. The Zero-Cost Wrapper
// Wraps the user’s business logic. The C++20 compiler fully inlines this.
template <typename InnerNode>
class telemetry_wrapper {
    InnerNode& inner_;

    // PHYSICAL TRICK: alignas(64)
    // Ensures this counter occupies a dedicated L1 cache line (64 bytes).
    // This way, memory operations from other threads never cause false sharing.
    alignas(64) std::atomic<uint64_t> processed_events_{ 0 };

   public:
    explicit telemetry_wrapper(InnerNode& inner) : inner_(inner) {}

    // C++20 auto template: captures every event the InnerNode can handle
    template <typename Event>
    SLAB_FORCE_INLINE void on(Event* ev) {
        on_event();           // One raw CPU register inslabfluxment (zero cost)
        inner_.on(ev);        // Forward to the actual logic
    }

    inline void on_event() noexcept {
        processed_events_.fetch_add(1, std::memory_order_relaxed);
    }

    // This is what the metrics thread will read
    [[nodiscard]] uint64_t get_count() const volatile noexcept { return processed_events_; }
};

// 2. The Asynchronous Metrics Reader (Scraper I/O Node)
// Runs on a dedicated I/O thread and “glances” at the counters once per second
class telemetry_scraper {
    std::vector<std::function<uint64_t()>> targets_;
    bool is_running_{false};

   public:
    // Attach a wrapper to the scraper
    template <typename Node>
    void register_target(const telemetry_wrapper<Node>& target) {
        targets_.emplace_back([&target]() { return target.get_count(); });
    }

    void run() noexcept {
        is_running_ = true;
        uint64_t last_total = 0;

        while (is_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            uint64_t current_total = 0;
            // Read through memory. Since the counters are stored in a volatile,
            // cache-aligned manner, reading does not block Compute threads.
            for (const auto& read_func : targets_) {
                current_total += read_func();
            }

            uint64_t eps = current_total - last_total;  // Events Per Second (EPS)
            last_total = current_total;

            // Prometheus HTTP export could go here, but for now we print to console
            std::cout << "[SLABFLUX Telemetry] Throughput: " << eps
                      << " EPS | Total: " << current_total << "\n";
        }
    }

    void stop() noexcept { is_running_ = false; }
};

}  // namespace slabflux::supplemental
