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

#ifndef _WIN32
#include "slabflux/core.hpp"
#include "slabflux/io/uring_hft.hpp"
#include "slabflux/domain/moe_spark.hpp"
#include <chrono>
#include <vector>
#include <limits>
#include <iomanip>

namespace slabflux::test {

    // ============================================================
    // THE VERIFICATION RIG
    // Ruthless physical measurement: Latency, Throughput, and Stall-check.
    // ============================================================

    template <typename EventType>
    class industrial_rig {
        struct alignas(64) stats {
            uint64_t total_processed{0};
            uint64_t backpressure_events{0};
            uint64_t min_cycles{std::numeric_limits<uint64_t>::max()};
            uint64_t max_cycles{0};
        };

        stats global_stats_;

    public:
        // 1. SATURATION TEST: Checking where the barrier breaks
        // Measures how many microseconds it takes for the Conduit to fill
        // and for the Pool to drain.
        void run_saturation_test(slabflux::pool<EventType>& pool, 
                                 slabflux::conduit<EventType, 131072>& target) {
            
            const auto start = std::chrono::high_resolution_clock::now();
            uint32_t count = 0;

            // We keep pushing until the Pool runs out (Wait-Free stress)
            while (auto ev = pool.make_uninitialized()) {
                if (target.push(ev.get())) {
                    ev.release(); 
                    count++;
                } else {
                    // Backpressure hit! (Conduit is full)
                    global_stats_.backpressure_events++;
                    break; 
                }
            }

            const auto end = std::chrono::high_resolution_clock::now();
            global_stats_.total_processed = count;
            
            report_saturation(count, std::chrono::duration<double>(end - start).count());
        }

        // 2. STALL PROFILER: Verifying Zero-Stall Telemetry
        // Using RDTSC to measure clock cycles per event.
        static inline uint64_t rdtsc() {
            unsigned int lo, hi;
            __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
            return ((uint64_t)hi << 32) | lo;
        }

        void profile_pipeline_step(auto&& logic_func) {
            const uint64_t t1 = rdtsc();
            logic_func();
            const uint64_t t2 = rdtsc();
            
            uint64_t delta = t2 - t1;
            if (delta < global_stats_.min_cycles) global_stats_.min_cycles = delta;
            if (delta > global_stats_.max_cycles) global_stats_.max_cycles = delta;
        }

    private:
        void report_saturation(uint32_t count, double duration) {
            double eps = count / duration;
            std::cout << "\n[INDUSTRIAL RIG - SATURATION REPORT @ " << typeid(EventType).name() << "]\n";
            std::cout << "------------------------------------------\n";
            std::cout << "Events Processed  : " << count << "\n";
            std::cout << "Time Taken        : " << std::fixed << std::setprecision(6) << duration << "s\n";
            std::cout << "Throughput (EPS)  : " << static_cast<uint64_t>(eps) << " ops/sec\n";
            std::cout << "Backpressure Hits : " << global_stats_.backpressure_events << "\n";
            std::cout << "Status   : " 
                      << (global_stats_.backpressure_events > 0 ? "STABLE" : "UNDER-LOADED") 
                      << "\n";
            std::cout << "------------------------------------------\n";
        }
    };
}
#else
// Windows Stub - Keeps the compiler happy without liburing
namespace slabflux::io {
    class uring_ingress_stream_engine {
    public:
        void process_tick() noexcept {}
    };
}
#endif