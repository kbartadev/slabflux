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
#include "slabflux/core/pool.hpp"
#include "slabflux/compute/vector_lane_256.hpp"
#include "slabflux/core/non_temporal_writer.hpp"
#include "slabflux/hw/intrinsics.hpp"

using namespace slabflux;

/**
 * @brief Benchmark Payload.
 * Aligned to 64 bytes to ensure each object occupies exactly one cache line.
 */
struct alignas(64) bench_event { uint64_t data[8]; };

/**
 * @brief Benchmark for O(1) Wait-Free Allocation.
 * Validates that HugePage-backed allocation remains constant time
 * regardless of pool saturation.
 */
static void BM_HugeSlabPool_Allocation(benchmark::State& state) {
    // Using vector_lane_engine as payload for cache-line sizing
    core::pool<compute::vector_lane_256<64>, 131072> pool;

    for (auto _ : state) {
        auto ev = pool.make();
        if (!ev) state.SkipWithError("Pool exhausted");
        benchmark::DoNotOptimize(ev);
        // RAII handles the wait-free return to the pool
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HugeSlabPool_Allocation)->ThreadRange(1, 8)->UseRealTime(); // Ensure UseRealTime is applied

/**
 * @brief Measures the peak throughput of the SPSC Conduit.
 * Isolates the overhead of the ring buffer between two physical cores.
 */
static void BM_Conduit_Bus_Throughput(benchmark::State& state) {
    struct test_event { uint64_t lsn; };

    // STATIC, so all threads see the same memory!
    static core::pool<test_event, 262144> global_pool;
    static conduit<test_event*, 65536> global_bus;

    if (state.thread_index() == 0) {
        // Producer
        for (auto _ : state) {
            auto ev = global_pool.make();
            if (!ev) continue;
            // This is where we send the data
            while (!global_bus.try_push(ev.get())) { _mm_pause(); }
            ev.release();
        }
    }
    else {
        // Consumer
        for (auto _ : state) {
            test_event* ev = nullptr;
            // This is where we receive it
            while (!(ev = global_bus.try_pop(global_pool))) {
                // Safety brake: if the test ends, don't keep spinning
                if (!state.KeepRunningBatch(1)) return;
                _mm_pause();
            }
            benchmark::DoNotOptimize(ev);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Conduit_Bus_Throughput)->Threads(2)->UseRealTime(); // Ensure UseRealTime is applied

/**
 * @brief Benchmarks the SIMD State Transformer (Vector Lane Engine).
 * Measures the clock-cycle cost of branchless AVX2/AVX-512 propagation.
 */
static void BM_VectorLane_Propagate(benchmark::State& state) {
    compute::vector_lane_256<64> engine;
    float signal = 0.5f;
    uint64_t lsn = 1;

    for (auto _ : state) {
        engine.propagate(signal, lsn++);
        benchmark::DoNotOptimize(engine);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_VectorLane_Propagate)->UseRealTime(); // Ensure UseRealTime is applied

/**
 * @brief Cache Sovereignty Test: Streaming vs Standard Memcpy.
 * Measures the throughput of writing 64-byte blocks.
 * Non-temporal stores (NT) bypass the cache hierarchy, preserving L1/L2 for logic.
 */
static void BM_Write_Path_NT(benchmark::State& state) {
    alignas(64) bench_event src;
    alignas(64) bench_event dst;

    for (auto _ : state) {
        core::non_temporal_writer::stream_write(&dst, &src);
        benchmark::DoNotOptimize(dst);
    }
    state.SetBytesProcessed(state.iterations() * sizeof(bench_event));
}
BENCHMARK(BM_Write_Path_NT)->UseRealTime();

static void BM_Write_Path_Standard_Memcpy(benchmark::State& state) {
    alignas(64) bench_event src;
    alignas(64) bench_event dst;
    for (auto _ : state) {
        std::memcpy(&dst, &src, sizeof(bench_event));
        benchmark::DoNotOptimize(dst);
    }
    state.SetBytesProcessed(state.iterations() * sizeof(bench_event));
}
BENCHMARK(BM_Write_Path_Standard_Memcpy)->UseRealTime();

BENCHMARK_MAIN();
