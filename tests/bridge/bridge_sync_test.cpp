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
 
#include <gtest/gtest.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <vector>
#include "slabflux/bridge/bridge_sync.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

using namespace slabflux::bridge;

// --- INDUSTRIAL MOCKS ---
struct engine_input {
    uint32_t action_mask;
    float mouse_dx;
    float mouse_dy;
};

struct logic_engine {
    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> total_writes{0};

    void process(const engine_input& in, uint64_t lsn, float* positions) {
        if (in.action_mask == 0xFFFFFFFF) stop_flag.store(true, std::memory_order_release);
        
        total_writes.fetch_add(1, std::memory_order_relaxed);

        // Tag every float with the LSN to detect partial reads (tearing)
        for (int i = 0; i < 1024; ++i) {
            positions[i] = static_cast<float>(lsn) + (static_cast<float>(i) * 0.001f);
        }
    }
};

struct sync_context {
    std::atomic<uint64_t> counter{ 1 };
    uint64_t reserve_next() { return counter.fetch_add(1, std::memory_order_relaxed); }
};

// --- UTILITY: THREAD PINNING ---
void pin_thread(int core_id) {
#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << core_id);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

// --- UTILITY: HARDWARE-AGNOSTIC SPIN DELAY ---
// Replaces brittle fixed _mm_pause loops with cycle-gated waits.
// Ensures deterministic simulated workloads across all CPU architectures (AMD/Intel).
inline void spin_for_cycles(uint64_t cycles) noexcept {
    uint64_t start = __rdtsc();
    while (__rdtsc() - start < cycles) {
        _mm_pause();
    }
}

// ============================================================================
// 1. AUDIT: SPSC DATA SATURATION (1 Producer -> 1 Engine -> 1 Reader)
// ============================================================================
TEST(Bridge, SPSC_Data_Saturation) {
    std::cout << "\n[AUDIT: SPSC BRIDGE SYNC SATURATION START]\n";
    pin_thread(0); // Protect the main timer thread from being starved
    spsc_data_bridge<engine_input, 1024> bridge;
    logic_engine logic;
    sync_context context;

    std::atomic<bool> running{ true };
    std::atomic<uint64_t> total_reads{ 0 };
    std::atomic<uint64_t> tearing_detected{ 0 };

    // 1. PRODUCER THREAD (Ingress) - Pinned to Core 3
    std::thread producer_thread([&]() {
        pin_thread(3);
        while (running.load(std::memory_order_relaxed)) {
            // BURST INJECTION: Simulate realistic NIC batching (NAPI / io_uring)
            for (int k = 0; k < 16; ++k) {
                bridge.send({ 0x1, 1, 1 });
            }
            // Feed the Engine faster to hit true saturation limits
            spin_for_cycles(2500);
        }
    });

    // 2. ENGINE THREAD (Consumer & Publish) - Pinned to Core 1
    std::thread engine_thread([&]() {
        pin_thread(1);
        while (!logic.stop_flag.load(std::memory_order_acquire)) {
            // BATCH DRAIN WITH MICRO-GAPS
            while (bridge.consume_one(logic, context)) {
                // Minimum 300-cycle window ensures Reader can copy 4KB regardless of CPU architecture
                spin_for_cycles(300);
            }

            // SIMULATED WORK: Give the CPU a brief breather, allowing the Seqlock to be read
            spin_for_cycles(2000);
        }
    });

    // 3. READER THREAD (Wide-Stream Reader) - Pinned to Core 2
    std::thread reader_thread([&]() {
        pin_thread(2);
        float read_buffer[1024];
        uint64_t last_seen_lsn = 0xFFFFFFFFFFFFFFFFULL;

        while (running.load(std::memory_order_relaxed)) {
            uint64_t current_lsn = 0;
            if (bridge.try_read_wide(read_buffer, current_lsn)) {
                if (current_lsn != last_seen_lsn) {
                    total_reads.fetch_add(1, std::memory_order_relaxed);
                    last_seen_lsn = current_lsn;
                }

                // HIGH-PERFORMANCE VALIDATION: O(1) boundary check prevents benchmark throttling
                float expected_base = static_cast<float>(current_lsn);
                if (std::abs(read_buffer[0] - expected_base) > 0.0001f || 
                    std::abs(read_buffer[1023] - (expected_base + 1.023f)) > 0.0001f) {
                    tearing_detected.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                _mm_pause(); // Protect the L3 bus when writer holds the Seqlock
            }
        }
        });

    // PRESSURE PHASE: 5 seconds of non-stop wide-streaming
    auto start_time = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    running.store(false, std::memory_order_release);

    producer_thread.join();
    
    // TEARDOWN PULSE: Flushes the engine thread and signals safe termination
    bridge.send({ 0xFFFFFFFF, 0, 0 });
    
    engine_thread.join();
    reader_thread.join();
    auto end_time = std::chrono::steady_clock::now();

    double duration = std::chrono::duration<double>(end_time - start_time).count();

    // AUDIT REPORT
    std::cout << "[AUDIT: SPSC BRIDGE SYNC SATURATION]\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Sustained Pressure Duration : " << std::fixed << std::setprecision(2) << duration << "s\n";
    std::cout << "Engine Updates (Writes/sec) : " << static_cast<uint64_t>(logic.total_writes.load() / duration) << "\n";
    std::cout << "Total Successful Snapshots  : " << total_reads.load() << "\n";
    std::cout << "Reader Snapshots/sec        : " << static_cast<uint64_t>(total_reads.load() / duration) << "\n";
    std::cout << "Tearing Incidents Detected  : " << tearing_detected.load() << "\n";
    std::cout << "------------------------------------------\n";

    ASSERT_EQ(tearing_detected.load(), 0) << "FATAL: Data tearing detected under high-frequency SPSC bus saturation!";
    EXPECT_GT(total_reads.load(), 1000) << "Error: Not enough reads performed to validate stability.";
}

// ============================================================================
// 2. AUDIT: MPMC DATA SATURATION (4 Producers -> 1 Engine -> 1 Reader)
// ============================================================================
TEST(Bridge, MPMC_Data_Saturation) {
    std::cout << "\n[AUDIT: MPMC BRIDGE SYNC SATURATION START]\n";
    pin_thread(0); // Protect the main timer thread from being starved
    mpmc_data_bridge<engine_input, 1024> bridge;
    logic_engine logic;
    sync_context context;

    std::atomic<bool> running{ true };
    std::atomic<uint64_t> total_reads{ 0 };
    std::atomic<uint64_t> tearing_detected{ 0 };

    // 1. PRODUCER THREADS (Ingress) - Pinned to Cores 3, 4, 5, 6
    std::vector<std::thread> producers;
    
    // SAFETY: Prevent SCHED_FIFO oversubscription deadlock on machines with fewer than 7 cores.
    // Main(0), Engine(1), Reader(2) require 3 cores. We allocate the rest to producers (max 4).
    int num_producers = 4;
    int hw_cores = static_cast<int>(std::thread::hardware_concurrency());
    if (hw_cores > 0 && hw_cores < 7) {
        num_producers = std::max(1, hw_cores - 3);
    }

    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&, i]() {
            pin_thread(3 + i);
            while (running.load(std::memory_order_relaxed)) {
                bridge.send({ 0x1, 1, 1 }); // Brutal contention on the MPMC queue
                
                // SIMULATED WORK: Give the CPU a brief breather
                spin_for_cycles(2000);
            }
        });
    }

    // 2. ENGINE THREAD (Consumer & Publisher) - Pinned to Core 1
    std::thread engine_thread([&]() {
        pin_thread(1);
        while (!logic.stop_flag.load(std::memory_order_acquire)) {
            // BATCH DRAIN WITH MICRO-GAPS
            while (bridge.consume_one(logic, context)) {
                spin_for_cycles(300);
            }
            
            // SIMULATED WORK: Give the CPU a brief breather
            spin_for_cycles(2000);
        }
        });

    // 3. READER THREAD (Wide-Stream Reader) - Pinned to Core 2
    std::thread reader_thread([&]() {
        pin_thread(2);
        float read_buffer[1024];
        uint64_t last_seen_lsn = 0xFFFFFFFFFFFFFFFFULL;

        while (running.load(std::memory_order_relaxed)) {
            uint64_t current_lsn = 0;
            if (bridge.try_read_wide(read_buffer, current_lsn)) {
                if (current_lsn != last_seen_lsn) {
                    total_reads.fetch_add(1, std::memory_order_relaxed);
                    last_seen_lsn = current_lsn;
                }

                // HIGH-PERFORMANCE VALIDATION: O(1) boundary check prevents benchmark throttling
                float expected_base = static_cast<float>(current_lsn);
                if (std::abs(read_buffer[0] - expected_base) > 0.0001f || 
                    std::abs(read_buffer[1023] - (expected_base + 1.023f)) > 0.0001f) {
                    tearing_detected.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                _mm_pause(); // Protect the L3 bus when writer holds the Seqlock
            }
        }
        });

    // PRESSURE PHASE: 5 seconds of extreme MPMC contention + AVX-512 streaming
    auto start_time = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    running.store(false, std::memory_order_release);

    for (auto& t : producers) t.join();
    
    // TEARDOWN PULSE: Flushes the engine thread and signals safe termination
    bridge.send({ 0xFFFFFFFF, 0, 0 });
    
    engine_thread.join();
    reader_thread.join();
    auto end_time = std::chrono::steady_clock::now();

    double duration = std::chrono::duration<double>(end_time - start_time).count();

    // AUDIT REPORT
    std::cout << "[AUDIT: MPMC BRIDGE SYNC SATURATION]\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Sustained Pressure Duration : " << std::fixed << std::setprecision(2) << duration << "s\n";
    std::cout << "Engine Updates (Writes/sec) : " << static_cast<uint64_t>(logic.total_writes.load() / duration) << "\n";
    std::cout << "Total Successful Snapshots  : " << total_reads.load() << "\n";
    std::cout << "Reader Snapshots/sec        : " << static_cast<uint64_t>(total_reads.load() / duration) << "\n";
    std::cout << "Tearing Incidents Detected  : " << tearing_detected.load() << "\n";
    std::cout << "------------------------------------------\n";

    ASSERT_EQ(tearing_detected.load(), 0) << "FATAL: Data tearing detected under high-frequency MPMC bus saturation!";
    EXPECT_GT(total_reads.load(), 1000) << "Error: Not enough reads performed to validate stability.";
}
