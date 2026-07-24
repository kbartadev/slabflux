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
 * ============================================================================* @file 02_bridge_sync_benchmark.cpp
 * @brief Benchmark: SPSC Bridge vs Pure SPSC Conduit Saturation
 * @details Side-by-side throughput audit comparing the spsc_event_bridge
 * against a bare-metal pure spsc_conduit implementation.
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <vector>
#include <immintrin.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

// Slabflux infrastructure headers
#include "slabflux/core.hpp"
#include "slabflux/bridge/bridge_sync.hpp"

using namespace slabflux;
using namespace slabflux::core;
using namespace slabflux::bridge;

// ============================================================================
// PERFORMANCE UTILITY: THREAD PINNING
// ============================================================================
void pin_thread(int core_id) {
#ifdef _WIN32
    HANDLE thread = GetCurrentThread();
    DWORD_PTR mask = (DWORD_PTR)1 << core_id;
    SetThreadAffinityMask(thread, mask);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
#endif
}

// ============================================================================
// DATA STRUCTURES & LOGIC MOCKS
// ============================================================================
struct market_snapshot {
    uint64_t sequence;
    uint32_t instrument_id;
    float bid_prices[10];
    float ask_prices[10];
};

// Pure logic container to mimic SIMD payload processing
struct book_logic_engine {
    void process(const market_snapshot& ev, uint64_t lsn, float* positions) noexcept {
        (void)lsn;
        (void)positions;
        volatile float sink = 0.0f;
        for (int i = 0; i < 10; ++i) {
            sink += ev.bid_prices[i] + ev.ask_prices[i];
        }
    }
};

struct benchmark_context {
    std::atomic<uint64_t> lsn_generator{0};
    float positions[1024]{0.0f};

    uint64_t reserve_next() noexcept {
        return lsn_generator.fetch_add(1, std::memory_order_relaxed);
    }
};

// ============================================================================
// BENCHMARK RUNNERS
// ============================================================================
constexpr size_t POOL_CAPACITY = 32768;
constexpr size_t CONDUIT_CAPACITY = 16384;
constexpr size_t WARMUP_ITERATIONS = 100000;
constexpr size_t BENCHMARK_DURATION_SEC = 3;

void run_bridge_benchmark() {
    std::cout << "[RUNNING] spsc_event_bridge Saturation Test...\n";

    spsc_pool<market_snapshot, POOL_CAPACITY> engine_pool;
    spsc_event_bridge<market_snapshot, CONDUIT_CAPACITY> bridge(engine_pool);
    book_logic_engine logic;
    benchmark_context ctx;

    std::atomic<bool> running{true};
    std::atomic<uint64_t> total_processed{0};

    // Consumer Thread
    std::thread consumer([&]() {
        pin_thread(1); // Dedicated processing core
        while (running.load(std::memory_order_relaxed)) {
            bridge.consume(logic, ctx);
            _mm_pause();
        }
        // Final Drain
        bridge.consume(logic, ctx);
    });

    // Warmup phase
    for (size_t i = 0; i < WARMUP_ITERATIONS; ++i) {
        auto ev = engine_pool.make();
        if (ev) {
            ev->sequence = i;
            ev->instrument_id = 42;
            bridge.send(ev);
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Producer Thread / Main Saturator
    pin_thread(0); // Dedicated ingestion core
    auto start_time = std::chrono::high_resolution_clock::now();

    uint64_t local_sequence = WARMUP_ITERATIONS;
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= BENCHMARK_DURATION_SEC) {
            break;
        }

        auto ev = engine_pool.make();
        if (ev) {
            ev->sequence = ++local_sequence;
            ev->instrument_id = 42;
            ev->bid_prices[0] = 100.25f;
            ev->ask_prices[0] = 100.30f;

            bridge.send(ev);
            total_processed.fetch_add(1, std::memory_order_relaxed);
        }
    }

    running.store(false, std::memory_order_release);
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double>(end_time - start_time).count();
    uint64_t total = total_processed.load();

    std::cout << "\n[AUDIT: SPSC BRIDGE SYNC SATURATION]\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Sustained Pressure Duration : " << std::fixed << std::setprecision(2) << duration << "s\n";
    std::cout << "Total Successful Snapshots  : " << total << "\n";
    std::cout << "Throughput (Snapshots/sec)  : " << static_cast<uint64_t>(total / duration) << "\n";
    std::cout << "------------------------------------------\n\n";
}

void run_pure_conduit_benchmark() {
    std::cout << "[RUNNING] Bare-Metal Pure pod_spsc_conduit Saturation Test...\n";

    spsc_pool<market_snapshot, POOL_CAPACITY> engine_pool;
    spsc_conduit<market_snapshot*, CONDUIT_CAPACITY> pure_bus;
    book_logic_engine logic;

    std::atomic<bool> running{true};
    std::atomic<uint64_t> total_processed{0};

    // Consumer Thread
    std::thread consumer([&]() {
        pin_thread(1);
        market_snapshot* raw_ptr = nullptr;
        float dummy_positions[1024]{0.0f};

        while (running.load(std::memory_order_relaxed)) {
            while (pure_bus.try_pop(raw_ptr)) {
                if (raw_ptr) {
                    logic.process(*raw_ptr, 0, dummy_positions);
                    engine_pool.release(raw_ptr);
                }
            }
            _mm_pause();
        }
        // Final Drain
        while (pure_bus.try_pop(raw_ptr)) {
            if (raw_ptr) {
                logic.process(*raw_ptr, 0, dummy_positions);
                engine_pool.release(raw_ptr);
            }
        }
    });

    // Warmup phase
    for (size_t i = 0; i < WARMUP_ITERATIONS; ++i) {
        auto ev = engine_pool.make();
        if (ev) {
            market_snapshot* raw = ev.release();
            raw->sequence = i;
            while (!pure_bus.try_push(raw)) { _mm_pause(); }
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Producer Thread / Main Saturator
    pin_thread(0);
    auto start_time = std::chrono::high_resolution_clock::now();

    uint64_t local_sequence = WARMUP_ITERATIONS;
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= BENCHMARK_DURATION_SEC) {
            break;
        }

        auto ev = engine_pool.make();
        if (ev) {
            market_snapshot* raw = ev.release();
            raw->sequence = ++local_sequence;
            raw->instrument_id = 42;
            raw->bid_prices[0] = 100.25f;
            raw->ask_prices[0] = 100.30f;

            while (!pure_bus.try_push(raw)) {
                _mm_pause();
            }
            total_processed.fetch_add(1, std::memory_order_relaxed);
        }
    }

    running.store(false, std::memory_order_release);
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double>(end_time - start_time).count();
    uint64_t total = total_processed.load();

    std::cout << "\n[PURE BARE-METAL AUDIT: SPSC CONDUIT SATURATION]\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Sustained Pressure Duration : " << std::fixed << std::setprecision(2) << duration << "s\n";
    std::cout << "Total Successful Snapshots  : " << total << "\n";
    std::cout << "Throughput (Snapshots/sec)  : " << static_cast<uint64_t>(total / duration) << "\n";
    std::cout << "------------------------------------------\n\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "        SLABFLUX - SPSC COMPARATIVE SATURATION      \n";
    std::cout << "========================================================\n\n";

    run_bridge_benchmark();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    run_pure_conduit_benchmark();

    return 0;
}
