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

#include "../core/physical_layout.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <iostream>
#include <stdexcept>

namespace slabflux::supplemental {

    // ============================================================
    // SLABFLUX SUPPLEMENTAL: SYNTHETIC BLASTER
    // Zero-allocation load generator for maximum saturation.
    // ============================================================

    template <typename ConduitType, typename AllocatorType, typename EventType>
    class load_generator {
        ConduitType& target_conduit_;
        AllocatorType& memory_pool_;
        uint32_t total_events_to_send_;

        // Pre-allocated pointer array (the weapon magazine)
        std::vector<EventType*> pre_allocated_ammo_;

    public:
        load_generator(ConduitType& conduit, AllocatorType& pool, uint32_t events_count)
            : target_conduit_(conduit), memory_pool_(pool), total_events_to_send_(events_count)
        {
            pre_allocated_ammo_.reserve(events_count);
        }

        ~load_generator() {
            // Return the ammo to the pool when finished
            for (auto* ev : pre_allocated_ammo_) {
                memory_pool_.deallocate(ev);
            }
        }

        // Phase 1: Loading the magazine (not included in performance measurement)
        void arm_weapon() {
            std::cout << "[SLABFLUX Blaster] Arming weapon with " << total_events_to_send_ << " events...\n";
            for (uint32_t i = 0; i < total_events_to_send_; ++i) {
                EventType* ev = memory_pool_.make_raw();
                if (!ev) {
                    throw std::runtime_error("SLABFLUX: Memory pool exhausted during arming phase!");
                }
                ev->size_bytes = sizeof(EventType);
                pre_allocated_ammo_.push_back(ev);
            }
            std::cout << "[SLABFLUX Blaster] Weapon armed. Ready to fire.\n";
        }

        // Phase 2: Firing (raw physical O(1) loop)
        void fire() {
            std::cout << "[SLABFLUX Blaster] Firing...\n";
            auto start_time = std::chrono::high_resolution_clock::now();

            uint32_t successful_pushes = 0;
            uint32_t backpressure_hits = 0;
            // Number of times the conduit was full

            for (uint32_t i = 0; i < total_events_to_send_; /* manual increment */) {
                // Attempt to push into the conduit in O(1)
                if (target_conduit_.push(pre_allocated_ammo_[i])) {
                    i++;
                    successful_pushes++;
                }
                else {
                    // If the conduit is full (the Core cannot keep up), spin
                    // We intentionally avoid sleeping to measure raw pressure
                    backpressure_hits++;
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_seconds = end_time - start_time;

            // Phase 3: Telemetry and Results
            double ops_per_sec = successful_pushes / elapsed_seconds.count();

            std::cout << "=========================================\n";
            std::cout << " SLABFLUX PHYSICAL STRESS TEST RESULTS\n";
            std::cout << "=========================================\n";
            std::cout << " Total Events Sent : " << successful_pushes << "\n";
            std::cout << " Time Elapsed      : " << elapsed_seconds.count() << " seconds\n";
            std::cout << " Backpressure Hits : " << backpressure_hits << " (Conduit full events)\n";
            std::cout << "-----------------------------------------\n";
            std::cout << " THROUGHPUT        : " << static_cast<uint64_t>(ops_per_sec) << " events/sec\n";
            std::cout << "=========================================\n";
        }
    };
} // namespace slabflux::supplemental