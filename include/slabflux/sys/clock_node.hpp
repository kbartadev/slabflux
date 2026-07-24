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
 * @file clock_node.hpp
 * @brief The Temporal Motor.
 * @details Reads high-resolution hardware counters for ultra-low latency timing.
 * Pushes explicit time as 'tick_events' into the pipeline, establishing a strictly 
 * deterministic timeline for the state machine without hidden std::chrono calls.
 */

#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h> // Hardware intrinsic for __rdtsc
#include <cpuid.h>
#include <system_error>
#include <iostream>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/sys/tick_event.hpp"

namespace slabflux::io {

    /**
     * @brief Hardware-pinned temporal generator.
     * @tparam Allocator The SPSC Pinned Allocator for zero-overhead event creation.
     * @tparam Bus The SPSC Conduit to push the time events into.
     */
    template<typename Allocator, typename Bus>
    class alignas(64) clock_node {
        
        Allocator& mem_pool_;
        Bus& time_bus_;
        
        // The minimum time that must pass before generating a new tick to prevent bus flooding
        const uint64_t resolution_ns_;

        // Hardware calibration state
        uint64_t hw_freq_hz_{0};
        // Fixed-point multiplier for zero-FPU TSC math (Q32.32 format)
        uint64_t tsc_to_ns_mult_q32_{0};
        
        alignas(64) std::atomic<bool> running_{true};

    public:
        /**
         * @brief Initializes the clock and performs hardware TSC calibration.
         * @param pool The SPSC memory pool for tick_events.
         * @param bus The SPSC conduit routing events to the Compute Node.
         * @param resolution_ns The throttle limit (e.g., 1000ns = 1 microsecond ticks).
         */
        clock_node(Allocator& pool, Bus& bus, uint64_t resolution_ns)
            : mem_pool_(pool), time_bus_(bus), resolution_ns_(resolution_ns) {
            calibrate_hardware_tsc();
        }

        ~clock_node() {
            stop();
        }

        clock_node(const clock_node&) = delete;
        clock_node& operator=(const clock_node&) = delete;

        /**
         * @brief Ignites the Temporal Motor.
         * @details Should be executed on an isolated thread pinned to a specific core.
         */
        void run() noexcept {
            uint64_t start_tsc = __rdtsc();
            // Force the very first iteration to emit a T=0 baseline tick immediately
            uint64_t last_tick_ns = 0ULL - resolution_ns_;
            uint32_t pending_skips = 0;
            
            // The Hot Path: Infinite temporal pumping
            do {
                uint64_t current_tsc = __rdtsc();
                
                // Integer-only Fixed-Point Math.
                // Eliminates FPU/double-precision pipeline stalls in the spin loop.
                uint64_t tsc_delta = current_tsc - start_tsc;
                uint64_t elapsed_ns = static_cast<uint64_t>((static_cast<__uint128_t>(tsc_delta) * tsc_to_ns_mult_q32_) >> 32);
                
                uint64_t delta = elapsed_ns - last_tick_ns;

                // Throttle: Only emit a tick if the resolution threshold has passed
                if (delta >= resolution_ns_) [[unlikely]] {
                    
                    sys::tick_event* ev = mem_pool_.make_raw();
                    if (ev) [[likely]] {
                        ev->timestamp_ns = elapsed_ns;
                        ev->delta_ns = delta;
                        
                        // Fix: Non-blocking temporal alignment.
                        // If the bus is full (Compute core yielded), we do NOT spin.
                        // Spinning would cause 'Time Dilation' where we send stale ticks later.
                        if (SL_EXPECT_TRUE(time_bus_.push(ev))) {
                            last_tick_ns = elapsed_ns;
                        } else {
                            // Backpressure detected: Drop this tick and recycle memory.
                            // The next iteration will generate a fresh tick representing 
                            // the new TSC reality, effectively 'skipping' the jitter gap.
                            mem_pool_.release(ev);
                            // Optional: Increment a 'temporal_skip' counter in hardware_telemetry
                        }
                    }
                }
                
                // Yield the pipeline slightly to prevent the TSC read loop from monopolizing the memory bus
                _mm_pause(); 
            } while (running_.load(std::memory_order_acquire));
        }

        inline void stop() noexcept {
            running_.store(false, std::memory_order_release);
        }

    private:
        /**
         * @brief Calibration: Calculates exact hardware cycles per nanosecond.
         */
        void calibrate_hardware_tsc() {
            // Zero-Latency Deterministic Hardware Calibration.
            // Mathematically derives the exact TSC frequency directly from the 
            // silicon's Architectural Crystal Clock via CPUID Leaf 0x15.
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            if (__get_cpuid(0x15, &eax, &ebx, &ecx, &edx) && eax != 0 && ebx != 0 && ecx != 0) {
                hw_freq_hz_ = (static_cast<uint64_t>(ecx) * ebx) / eax;
                tsc_to_ns_mult_q32_ = (1000000000ULL << 32) / hw_freq_hz_;
                std::cout << "[CLOCK] Crystal-derived TSC Frequency: " << (hw_freq_hz_ / 1000000) << " MHz.\n";
                return;
            }

            // Fallback for older silicon lacking Leaf 0x15
            struct timespec ts_start, ts_end;
            
            if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts_start) != 0) {
                throw std::system_error(errno, std::generic_category(), "Clock init failed");
            }
            uint64_t tsc_start = __rdtsc();
            
            // Wait ~10ms for a measurable hardware cycle span
            ::usleep(10000); 
            
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts_end);
            uint64_t tsc_end = __rdtsc();
            
            uint64_t elapsed_ns = (ts_end.tv_sec - ts_start.tv_sec) * 1000000000ULL + 
                                  (ts_end.tv_nsec - ts_start.tv_nsec);
                                  
            uint64_t tsc_delta = tsc_end - tsc_start;
            
            hw_freq_hz_ = (tsc_delta * 1000000000ULL) / elapsed_ns;
            
            // Calculate Q32.32 fixed-point multiplier: (1e9 << 32) / hw_freq_hz
            tsc_to_ns_mult_q32_ = (1000000000ULL << 32) / hw_freq_hz_;
            
            std::cout << "[CLOCK] Temporal Motor calibrated. Frequency: " 
                      << (hw_freq_hz_ / 1000000) << " MHz. Multiplier: " 
                      << (static_cast<double>(tsc_to_ns_mult_q32_) / (1ULL << 32)) << "\n";
        }
    };

} // namespace slabflux::io
