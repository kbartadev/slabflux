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
 * @file telemetry_overhead_bm.cpp
 * @brief Measures the performance impact of asynchronous telemetry scraping.
 */

#include <benchmark/benchmark.h>
#include <atomic>
#include <thread>
#include <vector>
#include "slabflux/supplemental/telemetry.hpp"

using namespace slabflux;

// Mock Node to wrap with telemetry
struct mock_compute_node {
    void process_event() {
        // Simulating 50ns of work
        benchmark::DoNotOptimize(this);
    }
};

/**
 * @brief Measures raw compute throughput without any scraping.
 */
static void BM_Compute_No_Scraping(benchmark::State& state) {
    mock_compute_node node;
    slabflux::supplemental::telemetry_wrapper<mock_compute_node> wrapper(node);

    for (auto _ : state) {
        wrapper.on_event(); // Increments the internal counter
        node.process_event();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Compute_No_Scraping)->UseRealTime(); // Ensure UseRealTime is applied

// Measures raw compute throughput without any scraping, multi-threaded.
static void BM_Compute_No_Scraping_MultiThreaded(benchmark::State& state) {
    mock_compute_node node;
    slabflux::supplemental::telemetry_wrapper<mock_compute_node> wrapper(node);

    // Ensure threads are pinned to avoid OS scheduler interference
    slabflux::core::hardware_topology::pin_thread(state.thread_index());

    for (auto _ : state) {
        wrapper.on_event(); // Increments the internal counter
        node.process_event();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Compute_No_Scraping_MultiThreaded)
    ->ThreadRange(1, 8) // Test with 1 to 8 threads
    ->UseRealTime();

/**
 * @brief Measures compute throughput while a Scraper thread is hammering memory.
 * This detects if cache-line bouncing (False Sharing) occurs between threads.
 */
static void BM_Compute_With_Aggressive_Scraping(benchmark::State& state) {
    mock_compute_node node;
    slabflux::supplemental::telemetry_wrapper<mock_compute_node> wrapper(node);
    
    std::atomic<bool> running{true};
    
    // Start a background scraper thread that reads as fast as possible
    std::thread scraper([&]() {
        while (running.load(std::memory_order_relaxed)) {
            // Aggressively read the counter to force cache coherency traffic
            uint64_t count = wrapper.get_count();
            benchmark::DoNotOptimize(count);
        }
    });

    for (auto _ : state) {
        wrapper.on_event();
        node.process_event();
    }
    
    running = false;
    scraper.join();
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Compute_With_Aggressive_Scraping)->UseRealTime(); // Ensure UseRealTime is applied

BENCHMARK_MAIN();