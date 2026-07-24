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
 *
 * @file telemetry_node.hpp
 * @brief Lock-free, zero-overhead Flight Recorder.
 * @details Uses atomic shadow counters to provide 
 * real-time observability without contaminating the compute core's L1 cache.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <span>
#include <iostream>
#include <thread>
#include <cmath>

namespace slabflux::sys {

    /**
     * @brief Shared telemetry metrics.
     * @details Each core writes to its own aligned slot to prevent false sharing.
     */
    struct alignas(64) core_metrics {
        std::atomic<uint64_t> events_processed{0};
        std::atomic<uint64_t> total_latency_ns{0};
        std::atomic<uint64_t> min_latency_ns{0xFFFFFFFFFFFFFFFF};
        std::atomic<uint64_t> max_latency_ns{0};
        std::atomic<uint64_t> drops{0};
    };

} // namespace slabflux::sys

namespace slabflux::io {

    class telemetry_scraper {
        sys::core_metrics* targets_[8]{nullptr}; // Up to 8 cores
        size_t count_{0};

    public:
        void register_core(sys::core_metrics& m) { if(count_ < 8) targets_[count_++] = &m; }

        /**
         * @brief Executed on a dedicated "Observer" core.
         */
        void run_display_loop(std::atomic<bool>& running) {
            // C++20 span for safe hardware view
            std::span active_targets(targets_, count_);

            while (running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
                for (size_t i = 0; i < active_targets.size(); ++i) {
                    auto* m = active_targets[i];
                    const uint64_t processed = m->events_processed.load(std::memory_order_relaxed);

                    if (processed > 0) {
                        const uint64_t total = m->total_latency_ns.load(std::memory_order_relaxed);
                        const uint64_t max_l = m->max_latency_ns.load(std::memory_order_relaxed);
                        const uint64_t min_l = m->min_latency_ns.load(std::memory_order_relaxed);

                        // Aggregate: Strict IEEE 754 deterministic average calculation.
                        // Using double ensures no truncation bias while maintaining sub-nanosecond precision.
                        const double avg_lat = static_cast<double>(total) / static_cast<double>(processed);

                        std::cout << "[CORE " << i << "] TPS: " << processed 
                                  << " | Avg Lat: " << avg_lat << "ns"
                                  << " | Min: " << (min_l == 0xFFFFFFFFFFFFFFFF ? 0 : min_l) << "ns"
                                  << " | Max: " << max_l << "ns\n";
                    }
                }

                uint64_t global_p = 0;
                // Replaced complex view reduction with a robust linear loop to avoid 
                // iterator deduction failures in std::accumulate with certain GCC 14 builds.
                for (auto* m : active_targets) {
                    global_p += m->events_processed.load(std::memory_order_relaxed);
                }
                if (global_p > 0) std::cout << "[SYSTEM] Total: " << global_p << "\n";
            }
        }
    };

} // namespace slabflux::io
