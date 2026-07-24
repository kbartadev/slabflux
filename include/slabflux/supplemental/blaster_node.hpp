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

#include "../core.hpp"
#include <chrono>
#include <iostream>

namespace slabflux::supplemental {

    // ============================================================
    // SLABFLUX BENCHMARK: THE BLASTER NODE
    // Brutal load generator that attempts to bring the system to its knees.
    // ============================================================

    template <typename PoolType, typename ConduitType, typename EventType>
    class blaster_node {
        PoolType& pool_;
        ConduitType& target_conduit_;
        uint64_t target_events_;
        bool is_running_{ false };

    public:
        blaster_node(PoolType& pool, ConduitType& conduit, uint64_t num_events_to_blast)
            : pool_(pool), target_conduit_(conduit), target_events_(num_events_to_blast) {
        }

        void run() noexcept {
            is_running_ = true;
            uint64_t successful_pushes = 0;
            uint64_t backpressure_hits = 0;

            std::cout << "[BLASTER] Igniting payload sequence: " << target_events_ << " events...\n";

            auto start_time = std::chrono::high_resolution_clock::now();

            // THE HOT PATH (Zero allocation, zero sys-call)
            while (is_running_ && successful_pushes < target_events_) {

                // 1. O(1) allocation from the Wait-Free Pool
                auto ev = pool_.template make<EventType>();

                if (!ev) [[unlikely]] {
                    // If the pool is empty, instead of complaining, use a CPU-friendly pause
                    backpressure_hits++;
                    continue;
                }

                // 2. O(1) submission into the Lock-Free Conduit
                if (target_conduit_.push(ev.get())) {
                    // CORE RESPECT: event_ptr releases the raw pointer,
                    // ownership of the memory transfers to the Node on the other end of the conduit!
                    ev.release();
                    successful_pushes++;
                }
                else {
                    // Backpressure from the Conduit (ring is full)
                    // RAII (event_ptr) immediately and at zero cost returns the event to the pool!
                    backpressure_hits++;
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            is_running_ = false;

            // ============================================================
            // RESULT CALCULATION
            // ============================================================
            std::chrono::duration<double> elapsed_seconds = end_time - start_time;
            double ops_per_sec = successful_pushes / elapsed_seconds.count();

            std::cout << "\n=========================================\n";
            std::cout << " Total Events Sent : " << successful_pushes << "\n";
            std::cout << " Time Elapsed      : " << elapsed_seconds.count() << " seconds\n";
            std::cout << " Backpressure Hits : " << backpressure_hits << " (Conduit full events)\n";
            std::cout << "-----------------------------------------\n";
            std::cout << " THROUGHPUT        : " << static_cast<uint64_t>(ops_per_sec) << " events/sec\n";
            std::cout << "=========================================\n";
        }

        void stop() noexcept {
            is_running_ = false;
        }
    };

} // namespace slabflux::supplemental
