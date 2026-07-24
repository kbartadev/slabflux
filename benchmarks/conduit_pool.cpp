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

#include <benchmark/benchmark.h>

#include "slabflux/core.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/mpmc_conduit.hpp"
#include "slabflux/core/spsc_pool.hpp"
#include "slabflux/core/mpsc_pool.hpp" // Added for MPSC pool
#include "slabflux/core/local_pool.hpp" // Added for Local pool
#include "slabflux/core/mpmc_sharded_conduit.hpp" // Added for MPMC Sharded Conduit
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/spsc_ring_pool.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include <thread> // For multi-threaded full flux
#include <atomic> // For multi-threaded full flux

using namespace slabflux;

struct alignas(64) bench_event {
    int payload[16];  // 64 bytes (Exactly one cache line)
};

/**
 * @brief Measures how long a single O(1) allocation and push into the ring buffer takes.
 * @details This benchmark focuses on the producer-side cost of adding an item to the conduit.
 * Capacity is set high to avoid backpressure from a full conduit without a consumer.
 */
template <typename PoolType, typename ConduitType>
static void BM_Slabflux_Push_Template(benchmark::State& state) {
    // Capacity for push-only benchmarks needs to be large to avoid overflow without a consumer
    constexpr size_t capacity = 131072;
    PoolType p; // PoolType should be instantiated with bench_event and capacity
    ConduitType c; // ConduitType should be instantiated with bench_event* and capacity

    // Pre-allocate everything so we measure only PUSH speed, not allocation overhead.
    std::vector<bench_event*> pre_allocated;
    pre_allocated.reserve(state.range(0));
    for (int i = 0; i < state.range(0); ++i) {
        pre_allocated.push_back(p.make().release());
    }

    int idx = 0;
    for (auto _ : state) {
        c.push(pre_allocated[idx++ % pre_allocated.size()]);
    }
    state.SetItemsProcessed(state.iterations());
}

// Registrations for BM_Slabflux_Push_Template
// SPSC Push
BENCHMARK_TEMPLATE(BM_Slabflux_Push_Template, spsc_pool<bench_event, 131072>, spsc_conduit<bench_event*, 131072>)
    ->Name("BM_Slabflux_Push_SPSC")
    ->Range(1024, 8192)
    ->Iterations(100000)
    ->UseRealTime();

// Ring Push
BENCHMARK_TEMPLATE(BM_Slabflux_Push_Template, slabflux::core::spsc_ring_pool<bench_event, 131072>, slabflux::core::spsc_ring_conduit<bench_event*, 131072>)
    ->Name("BM_Slabflux_Push_Ring")
    ->Range(1024, 8192)
    ->Iterations(100000)
    ->UseRealTime();

// MPMC Push
BENCHMARK_TEMPLATE(BM_Slabflux_Push_Template, mpmc_pool<bench_event, 131072>, mpmc_conduit<bench_event*, 131072>)
    ->Name("BM_Slabflux_Push_MPMC")
    ->Range(1024, 8192)
    ->Iterations(100000)
    ->UseRealTime();

// Local Pool Push
BENCHMARK_TEMPLATE(BM_Slabflux_Push_Template, slabflux::core::local_pool<bench_event, 131072>, spsc_conduit<bench_event*, 131072>)
    ->Name("BM_Slabflux_Push_LocalPool_SPSC")
    ->Range(1024, 8192)
    ->Iterations(100000)
    ->UseRealTime();

// MPSC Push
BENCHMARK_TEMPLATE(BM_Slabflux_Push_Template, slabflux::core::mpsc_pool<bench_event, 131072, slabflux::core::reclaim_strategy::manual>, spsc_conduit<bench_event*, 131072>)
    ->Name("BM_Slabflux_Push_MPSC")
    ->Range(1024, 8192)
    ->Iterations(100000)
    ->UseRealTime();

// MPMC Sharded Conduit Push
BENCHMARK_TEMPLATE(BM_Slabflux_Push_Template, mpmc_pool<bench_event, 131072, 8>, slabflux::core::mpmc_sharded_conduit<bench_event*, 131072, 8>)
    ->Name("BM_Slabflux_Push_MPMC_Sharded")
    ->Range(1024, 8192)
    ->Iterations(100000)
    ->UseRealTime();

/**
 * @brief Full Pipeline Tax.
 * Measures the cycle-cost of a complete event lifecycle (allocate -> push -> pop -> release)
 * in a single thread. This provides the 'latency floor' for the entire framework.
 */
template <typename PoolType, typename ConduitType>
static void BM_Slabflux_FullFlux_Template(benchmark::State& state) {
    // Capacity for full-flux benchmarks can be smaller as items are immediately consumed
    constexpr size_t capacity = 4096;
    PoolType p;
    ConduitType c;

    for (auto _ : state) {
        c.push(p.make().release()); // Allocate and push
        auto ev = c.pop(p);         // Pop and release
        benchmark::DoNotOptimize(ev);

        // For MPSC pools, explicit reclamation might be needed if the pop doesn't trigger it.
        if constexpr (std::is_same_v<PoolType, mpsc_pool<bench_event, capacity, slabflux::core::reclaim_strategy::manual>>) {
            p.reclaim_returns();
        }
    }
    state.SetItemsProcessed(state.iterations());
}

// Registrations for BM_Slabflux_FullFlux_Template
// SPSC Full Flux
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_Template, spsc_pool<bench_event, 4096>, spsc_conduit<bench_event*, 4096>)
    ->Name("BM_Slabflux_FullFlux_SPSC")->Iterations(100000)->UseRealTime();
// Ring Full Flux
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_Template, slabflux::core::spsc_ring_pool<bench_event, 4096>, slabflux::core::spsc_ring_conduit<bench_event*, 4096>)
    ->Name("BM_Slabflux_FullFlux_Ring")->Iterations(100000)->UseRealTime();
// MPMC Full Flux
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_Template, mpmc_pool<bench_event, 4096>, mpmc_conduit<bench_event*, 4096>)
    ->Name("BM_Slabflux_FullFlux_MPMC")->Iterations(100000)->UseRealTime();

// Local Pool Full Flux
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_Template, slabflux::core::local_pool<bench_event, 4096>, spsc_conduit<bench_event*, 4096>)
    ->Name("BM_Slabflux_FullFlux_LocalPool_SPSC")->Iterations(100000)->UseRealTime();

// MPSC Full Flux
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_Template, mpsc_pool<bench_event, 4096, slabflux::core::reclaim_strategy::manual>, spsc_conduit<bench_event*, 4096>)
    ->Name("BM_Slabflux_FullFlux_MPSC")->Iterations(100000)->UseRealTime();

// MPMC Sharded Conduit Full Flux
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_Template, mpmc_pool<bench_event, 4096, 8>, slabflux::core::mpmc_sharded_conduit<bench_event*, 4096, 8>)
    ->Name("BM_Slabflux_FullFlux_MPMC_Sharded")->Iterations(100000)->UseRealTime();

// Measures the full lifecycle with multiple threads: O(1) Allocation -> Push -> Pop -> O(1) Deallocation
/**
 * @brief Interconnect Pressure Test (MPMC Specific).
 * Each thread performs a full lifecycle. In a standard MPMC pool, this would
 * collapse due to cache-line bouncing. Slabflux uses Sharding to scale.
 */
template <size_t NumLanes>
static void BM_Slabflux_FullFlux_MPMC_MultiThreaded_Scaling(benchmark::State& state) {
    // Use a larger pool and conduit capacity for multi-threaded test
    constexpr size_t capacity = 4096;
    mpmc_pool<bench_event, capacity, NumLanes> p;
    mpmc_conduit<bench_event*, capacity, NumLanes> c;

    // Ensure threads are pinned to avoid OS scheduler interference
    slabflux::core::hardware_topology::pin_thread(state.thread_index());

    for (auto _ : state) {
        // High-frequency churn
        auto ev = p.make();
        if (SL_EXPECT_TRUE(!!ev)) {
            c.push(ev.release());
            auto popped = c.pop(p);
            benchmark::DoNotOptimize(popped);
        } else {
            _mm_pause();
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_MPMC_MultiThreaded_Scaling, 1)
    ->Name("BM_Slabflux_FullFlux_MPMC_MultiThreaded_L1")
    ->ThreadRange(1, std::thread::hardware_concurrency())
    ->Iterations(100000)
    ->UseRealTime();
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_MPMC_MultiThreaded_Scaling, 8)
    ->Name("BM_Slabflux_FullFlux_MPMC_MultiThreaded_L8")
    ->ThreadRange(1, std::thread::hardware_concurrency())
    ->Iterations(100000)
    ->UseRealTime();

/**
 * @brief Interconnect Pressure Test (MPMC Sharded Specific).
 * Each thread performs a full lifecycle. This demonstrates the scaling of
 * mpmc_sharded_conduit under multi-threaded contention.
 */
template <size_t NumLanes>
static void BM_Slabflux_FullFlux_MPMC_Sharded_MultiThreaded_Scaling(benchmark::State& state) {
    constexpr size_t capacity = 4096;
    // mpmc_pool is used as the allocator, mpmc_sharded_conduit as the communication channel
    slabflux::core::mpmc_pool<bench_event, capacity, NumLanes> p;
    slabflux::core::mpmc_sharded_conduit<bench_event*, capacity, NumLanes> c;

    // Ensure threads are pinned to avoid OS scheduler interference
    slabflux::core::hardware_topology::pin_thread(state.thread_index());

    for (auto _ : state) {
        auto ev = p.make();
        if (SL_EXPECT_TRUE(!!ev)) {
            c.push(ev.release());
            auto popped = c.pop(p); // mpmc_sharded_conduit::pop(Pool&) is available
            benchmark::DoNotOptimize(popped);
        } else {
            _mm_pause();
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_MPMC_Sharded_MultiThreaded_Scaling, 1)
    ->Name("BM_Slabflux_FullFlux_MPMC_Sharded_MultiThreaded_L1")
    ->ThreadRange(1, std::thread::hardware_concurrency())
    ->Iterations(100000)
    ->UseRealTime();
BENCHMARK_TEMPLATE(BM_Slabflux_FullFlux_MPMC_Sharded_MultiThreaded_Scaling, 8)
    ->Name("BM_Slabflux_FullFlux_MPMC_Sharded_MultiThreaded_L8")
    ->ThreadRange(1, std::thread::hardware_concurrency())
    ->Iterations(100000)
    ->UseRealTime();
