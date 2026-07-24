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
#include <atomic>
#include <thread>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <immintrin.h>

#include "slabflux/core.hpp"

// Aligned version: Each counter is explicitly placed on its own cache line.
// Accessing different instances of this event by different threads should not incur false sharing.
struct bench_event { uint64_t data[8]; };

/**
 * @brief Measures the "Interconnect Tax" on a single-lane MPMC pool.
 * This is the baseline where Slabflux's sharding is DISABLED (Lanes=1).
 * All threads fight for the same 64-byte cache line (the stack head).
 */
static void BM_Slabflux_Contention_Matrix_SingleLane(benchmark::State& state) {
    // Slabflux handles the allocation, but we force 1 lane to trigger false sharing
    static slabflux::core::mpmc_pool<bench_event, 131072, 1> pool;
    
    for (auto _ : state) {
        auto ev = pool.make();
        if (SL_EXPECT_FALSE(!ev)) {
            state.SkipWithError("Pool exhausted");
            break;
        }
        benchmark::DoNotOptimize(ev);
        // RAII release happens here
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Slabflux_Contention_Matrix_SingleLane)->ThreadRange(1, 8)->UseRealTime();

/**
 * @brief Measures Slabflux "Handling" the contention via Sharding.
 * Here Lanes=8. Slabflux automatically maps threads to core-local shards,
 * mathematically eliminating false sharing on the pool management structures.
 */
static void BM_Slabflux_Contention_Matrix_Sharded(benchmark::State& state) {
    // Default Slabflux behavior: Sharded Matrix (Lanes=8)
    static slabflux::core::mpmc_pool<bench_event, 131072, 8> pool;
    
    for (auto _ : state) {
        auto ev = pool.make();
        if (SL_EXPECT_FALSE(!ev)) {
            state.SkipWithError("Pool exhausted");
            break;
        }
        benchmark::DoNotOptimize(ev);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Slabflux_Contention_Matrix_Sharded)->ThreadRange(1, 8)->UseRealTime();