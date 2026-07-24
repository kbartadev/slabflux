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
#include <thread>
#include <atomic>
#include <vector>
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/core/mpmc_conduit.hpp"
#include "slabflux/core/mpmc_sharded_conduit.hpp"
#include "slabflux/core/mpmc_matrix_conduit.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::core;

/** @brief Latency Matrix: Measures atomic round-trip cost for 1 item. */
template <typename Conduit, typename T>
static void BM_Conduit_Latency(benchmark::State& state) {
    Conduit conduit;
    T val = {};
    T out;
    for (auto _ : state) {
        conduit.try_push(val);
        conduit.try_pop(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations());
}

/** @brief Throughput Matrix: Measures true AVX-512 burst speed (64 items). */
template <typename Conduit, typename T>
static void BM_Conduit_BatchThroughput_AVX512(benchmark::State& state) {
    Conduit conduit;
    alignas(64) T data[64] = {};
    alignas(64) T out_batch[64];
    for (auto _ : state) {
        conduit.push_batch(data, 64);
        size_t count = conduit.pop_batch(out_batch, 64);
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * 64);
}

/** @brief Throughput Matrix: Measures batch speed with varying batch sizes. */
template <typename Conduit, typename T>
static void BM_Conduit_BatchThroughput_VariableBatchSize(benchmark::State& state) {
    Conduit conduit;
    alignas(64) T data[64] = {}; // Max batch size
    alignas(64) T out_batch[64];
    const size_t batch_size = state.range(0);

    for (auto _ : state) {
        conduit.push_batch(data, batch_size);
        size_t count = conduit.pop_batch(out_batch, batch_size);
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK_TEMPLATE(BM_Conduit_BatchThroughput_VariableBatchSize, spsc_conduit<uint64_t, 4096>, uint64_t)
    ->Name("BM_Spsc_BatchThroughput_Variable")
    ->RangeMultiplier(2)->Range(1, 64) // Test batch sizes 1, 2, 4, ..., 64
    ->UseRealTime();

/** @brief Invalidation Matrix: Measures SIMD-accelerated pointer nulling. */
template <typename Conduit, typename T>
static void BM_Conduit_InvalidateByPtr_Latency(benchmark::State& state) {
    Conduit conduit;
    alignas(64) T target_val = (T)(uintptr_t)0xDEADC0DE;
    alignas(64) T dummy_batch[64] = {};
    for (auto _ : state) {
        state.PauseTiming();
        T trash[64];
        while(conduit.pop_batch(trash, 64)); // Drain
        for(int i = 0; i < 8; ++i) { // 512 total
            conduit.push_batch(dummy_batch, 64);
        }
        conduit.try_push(target_val);
        state.ResumeTiming();
        conduit.invalidate_by_ptr(target_val);
    }
    state.SetItemsProcessed(state.iterations());
}

/** @brief MPMC Contention Analysis */
template <std::size_t Lanes>
static void BM_MpmcConduit_ContentionThroughput(benchmark::State& state) {
    // Strict-Order Matrix: Use a static instance per configuration.
    // Note: In the current mpmc_conduit, Lanes is ignored by logic but valid as a template param.
    static mpmc_conduit<uint64_t, 262144, Lanes> shared_conduit;

    // Physical Affinity: Pinning isolates MESI overhead.
    hardware_topology::pin_thread(state.thread_index());

    const uint64_t val = 0xFEEDFACE;
    const bool is_producer = (state.thread_index() % 2 == 0);

    // Safety Brake: Manual loop evaluation prevents unbounded thread hangs
    // when the benchmark framework shuts down worker threads unevenly.
    while (state.KeepRunning()) {
        if (is_producer) {
            while (SL_EXPECT_FALSE(!shared_conduit.try_push(val))) {
                if (!state.KeepRunning()) return;
                _mm_pause();
            }
        } else {
            uint64_t out;
            while (SL_EXPECT_FALSE(!shared_conduit.try_pop(out))) {
                if (!state.KeepRunning()) return;
                _mm_pause();
            }
            benchmark::DoNotOptimize(out);
        }
    }
    state.SetItemsProcessed(state.iterations());
}

/** @brief MPMC Sharded Contention Analysis */
template <std::size_t Lanes>
static void BM_MpmcShardedConduit_ContentionThroughput(benchmark::State& state) {
    // Distributed Matrix: Sharded lane instance to bypass L3 bus bottlenecks
    static mpmc_sharded_conduit<uint64_t, 262144, Lanes> shared_conduit;

    // Physical Affinity: Pinning isolates MESI overhead and prevents migration jitter
    hardware_topology::pin_thread(state.thread_index());

    const uint64_t val = 0xFEEDFACE;
    const bool is_producer = (state.thread_index() % 2 == 0);

    // Safety Brake: Manual loop evaluation prevents unbounded thread hangs
    // when the benchmark framework shuts down worker threads unevenly.
    while (state.KeepRunning()) {
        if (is_producer) {
            while (SL_EXPECT_FALSE(!shared_conduit.try_push(val))) {
                if (!state.KeepRunning()) return;
                _mm_pause();
            }
        } else {
            uint64_t out;
            while (SL_EXPECT_FALSE(!shared_conduit.try_pop(out))) {
                if (!state.KeepRunning()) return;
                _mm_pause();
            }
            benchmark::DoNotOptimize(out);
        }
    }
    state.SetItemsProcessed(state.iterations());
}

// Ring (Zero-Copy)
static void BM_RingConduit_ZeroCopy_ReserveCommit(benchmark::State& state) {
    spsc_ring_conduit<uint64_t, 4096> conduit;
    for (auto _ : state) {
        uint64_t* slot = conduit.reserve();
        *slot = 0xFEED;
        conduit.commit();
        uint64_t out;
        conduit.try_pop(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations());
}

/**
 * @brief Matrix Conduit Latency Audit.
 * Measures the round-trip cost of a single push/pop cycle on a statically indexed lane.
 */
static void BM_MatrixConduit_Latency(benchmark::State& state) {
    mpmc_matrix_conduit<uint64_t, 4096, 8> conduit;
    uint64_t val = 0xFEEDFACE;
    uint64_t out_batch[1];

    for (auto _ : state) {
        // Static Lane 0 injection
        conduit.try_push_lane(0, val);
        // Amortized drain (1 item)
        benchmark::DoNotOptimize(conduit.pop_batch_lane(0, out_batch, 1));
    }
    state.SetItemsProcessed(state.iterations());
}

/**
 * @brief Variable Batch Throughput Audit.
 * Validates the hardware-optimized AVX-512 drain paths across different burst sizes.
 */
static void BM_MatrixConduit_BatchThroughput_Variable(benchmark::State& state) {
    const size_t batch_size = state.range(0);
    mpmc_matrix_conduit<uint64_t, 65536, 8> conduit;
    uint64_t in_batch[64];
    uint64_t out_batch[64];

    for (auto _ : state) {
        // Fill phase
        for (size_t i = 0; i < batch_size; ++i) {
            while (SL_UNLIKELY(!conduit.try_push_lane(0, i))) { _mm_pause(); }
        }
        // Vectorized drain phase
        size_t popped = 0;
        while (popped < batch_size) {
            popped += conduit.pop_batch_lane(0, out_batch + popped, batch_size - popped);
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
    state.counters["items_per_second"] = benchmark::Counter(
        static_cast<double>(batch_size),
                                                            benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert
    );
}

/**
 * @brief Cross-Core doff.
 * Measures throughput between two pinned threads using a fixed lane.
 */
static void BM_MatrixConduit_CrossCore_Throughput(benchmark::State& state) {
    const size_t lane_id = 0;
    mpmc_matrix_conduit<uint64_t, 65536, 8> conduit;
    std::atomic<bool> running{true};
    std::atomic<size_t> total_processed{0};

    std::thread consumer([&]() {
        hardware_topology::pin_thread(2); // Consumer on isolated core
        uint64_t out[32];
        while (running.load(std::memory_order_relaxed)) {
            size_t n = conduit.pop_batch_lane(lane_id, out, 32);
            total_processed.fetch_add(n, std::memory_order_relaxed);
            if (n == 0) _mm_pause();
        }
    });

    hardware_topology::pin_thread(1); // Producer on adjacent core
    uint64_t val = 1;
    for (auto _ : state) {
        while (SL_UNLIKELY(!conduit.try_push_lane(lane_id, val))) { _mm_pause(); }
    }

    running.store(false, std::memory_order_release);
    consumer.join();

    state.SetItemsProcessed(state.iterations());
    state.counters["total_popped"] = static_cast<double>(total_processed.load());
}




// SPSC POD
BENCHMARK_TEMPLATE(BM_Conduit_Latency, spsc_conduit<uint64_t, 4096>, uint64_t)->Name("BM_Spsc_Latency")->UseRealTime();
BENCHMARK_TEMPLATE(BM_Conduit_BatchThroughput_AVX512, spsc_conduit<uint64_t, 4096>, uint64_t)->Name("BM_Spsc_BatchThroughput")->UseRealTime();
BENCHMARK_TEMPLATE(BM_Conduit_InvalidateByPtr_Latency, spsc_conduit<uint64_t, 4096>, uint64_t)->Name("BM_Spsc_Invalidate")->UseRealTime();

BENCHMARK(BM_RingConduit_ZeroCopy_ReserveCommit);
BENCHMARK_TEMPLATE(BM_Conduit_Latency, spsc_ring_conduit<uint64_t, 4096>, uint64_t)->Name("BM_RingConduit_Latency");
BENCHMARK_TEMPLATE(BM_Conduit_BatchThroughput_AVX512, spsc_ring_conduit<uint64_t, 4096>, uint64_t)->Name("BM_RingConduit_BatchThroughput");
BENCHMARK_TEMPLATE(BM_Conduit_InvalidateByPtr_Latency, spsc_ring_conduit<uint64_t, 4096>, uint64_t)->Name("BM_RingConduit_Invalidate");

// MPMC Matrix Scaling Analysis (L1 Baseline vs L8 Distributed)
BENCHMARK_TEMPLATE(BM_Conduit_Latency, mpmc_conduit<uint64_t, 4096, 1>, uint64_t)->Name("BM_Mpmc_Latency_L1")->UseRealTime();
BENCHMARK_TEMPLATE(BM_Conduit_Latency, mpmc_conduit<uint64_t, 4096, 8>, uint64_t)->Name("BM_Mpmc_Latency_L8")->UseRealTime();

BENCHMARK_TEMPLATE(BM_Conduit_BatchThroughput_AVX512, mpmc_conduit<uint64_t, 4096, 1>, uint64_t)->Name("BM_Mpmc_BatchThroughput_L1")->UseRealTime();
BENCHMARK_TEMPLATE(BM_Conduit_BatchThroughput_AVX512, mpmc_conduit<uint64_t, 4096, 8>, uint64_t)->Name("BM_Mpmc_BatchThroughput_L8")->UseRealTime();

BENCHMARK_TEMPLATE(BM_Conduit_InvalidateByPtr_Latency, mpmc_conduit<uint64_t, 4096, 1>, uint64_t)->Name("BM_Mpmc_Invalidate_L1")->UseRealTime();
BENCHMARK_TEMPLATE(BM_Conduit_InvalidateByPtr_Latency, mpmc_conduit<uint64_t, 4096, 8>, uint64_t)->Name("BM_Mpmc_Invalidate_L8")->UseRealTime();

// MPMC Matrix Conduit Benchmarks
BENCHMARK(BM_MatrixConduit_Latency)->Name("BM_MpmcMatrix_Latency")->UseRealTime();
BENCHMARK(BM_MatrixConduit_BatchThroughput_Variable) // Corrected to use BENCHMARK directly
    ->Name("BM_MpmcMatrix_BatchThroughput_Variable")
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)
    ->UseRealTime();
BENCHMARK(BM_MatrixConduit_CrossCore_Throughput)->UseRealTime()->Name("BM_MpmcMatrix_CrossCore_Throughput");

// Multithreaded Contention Scaling: 1 Lane (Atomic Bottleneck) vs 8 Lanes (Distributed Matrix)
BENCHMARK_TEMPLATE(BM_MpmcConduit_ContentionThroughput, 1)->ThreadRange(2, 16)->Name("BM_MpmcContention_L1")->UseRealTime();
BENCHMARK_TEMPLATE(BM_MpmcConduit_ContentionThroughput, 8)->ThreadRange(2, 16)->Name("BM_MpmcContention_L8")->UseRealTime();

// MPMC Sharded Matrix Scaling Analysis
BENCHMARK_TEMPLATE(BM_Conduit_Latency, mpmc_sharded_conduit<uint64_t, 4096, 8>, uint64_t)->Name("BM_MpmcSharded_Latency_L8")->UseRealTime();
BENCHMARK_TEMPLATE(BM_Conduit_BatchThroughput_AVX512, mpmc_sharded_conduit<uint64_t, 4096, 8>, uint64_t)->Name("BM_MpmcSharded_BatchThroughput_L8")->UseRealTime();
BENCHMARK_TEMPLATE(BM_Conduit_InvalidateByPtr_Latency, mpmc_sharded_conduit<uint64_t, 4096, 8>, uint64_t)->Name("BM_MpmcSharded_Invalidate_L8")->UseRealTime();

// Multithreaded Contention Scaling: Distributed Sharded Matrix
BENCHMARK_TEMPLATE(BM_MpmcShardedConduit_ContentionThroughput, 8)->ThreadRange(2, 16)->UseRealTime()->Name("BM_MpmcShardedContention_L8");
BENCHMARK_TEMPLATE(BM_MpmcShardedConduit_ContentionThroughput, 16)->ThreadRange(2, 16)->UseRealTime()->Name("BM_MpmcShardedContention_L16");

BENCHMARK_MAIN();
