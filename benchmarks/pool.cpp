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
#include "slabflux/core/local_pool.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/spsc_pool.hpp"
#include "slabflux/core/spsc_ring_pool.hpp"
#include "slabflux/core/mpmc_conduit.hpp"
#include "slabflux/core/mpsc_hybrid_pool.hpp"
#include "slabflux/core/mpsc_pool.hpp"
#include "slabflux/core/slab_allocator.hpp"
#include "slabflux/core/pinned_allocator_mpmc.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/pinned_allocator_isolated.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::core;

/**
 * @brief Benchmark Payload.
 * Aligned to 64 bytes to ensure each object occupies exactly one cache line,
 * mirroring real-world HFT event structures.
 */
struct alignas(64) bench_event { 
    uint64_t data[8]; 
};

/**
 * @brief MPMC Pool Local Mode: The hardware latency floor.
 * Measures the absolute cycle cost of index-to-pointer resolution and LIFO stack movement
 * with zero atomic overhead.
 */
static void BM_Pool_Local_RoundTrip(benchmark::State& state) {
    local_pool<bench_event, 65536> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        benchmark::DoNotOptimize(ptr);
        pool.release(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Local_RoundTrip)->UseRealTime();

/**
 * @brief SPSC Pool Latency: The shadow-pointer ceiling.
 * Measures the absolute cycle cost of the shadow-pointer based allocation and release.
 * This validates the overhead of the SPSC ring-backed free-list compared to pure LIFO.
 */
static void BM_Pool_Spsc_RoundTrip(benchmark::State& state) {
    spsc_pool<bench_event, 65536> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        benchmark::DoNotOptimize(ptr);
        pool.release(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Spsc_RoundTrip)->UseRealTime();

/**
 * @brief SPSC Pool Batch Throughput: The Vectorized Ceiling.
 * Measures the throughput when allocating and releasing in batches of 64.
 * This should match the 17GB/s conduit performance.
 */
static void BM_Pool_Spsc_Batch_Throughput(benchmark::State& state) {
    spsc_pool<bench_event, 65536> pool;
    bench_event* batch[64];
    for (auto _ : state) {
        size_t n = pool.make_batch(batch, 64);
        if (SL_EXPECT_FALSE(n == 0)) {
            state.SkipWithError("Pool exhausted");
            break;
        }
        benchmark::DoNotOptimize(batch);
        pool.release_batch(batch, n);
    }
    state.SetItemsProcessed(state.iterations() * 64);
    state.counters["throughput_GBps"] = benchmark::Counter(
        static_cast<double>(64 * sizeof(bench_event)), 
        benchmark::Counter::kIsIterationInvariantRate
    );
}
BENCHMARK(BM_Pool_Spsc_Batch_Throughput)->UseRealTime();

/**
 * @brief SPSC Ring Pool: Shadow-pointer based wire pool.
 */
static void BM_Pool_SpscRing_RoundTrip(benchmark::State& state) {
    spsc_ring_pool<bench_event, 65536> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        benchmark::DoNotOptimize(ptr);
        pool.release(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_SpscRing_RoundTrip)->UseRealTime();

static void BM_Pool_SpscRing_Batch_Throughput(benchmark::State& state) {
    spsc_ring_pool<bench_event, 65536> pool;
    bench_event* batch[64];
    for (auto _ : state) {
        size_t n = pool.make_batch(batch, 64);
        pool.release_batch(batch, n);
    }
    state.SetItemsProcessed(state.iterations() * 64);
}
BENCHMARK(BM_Pool_SpscRing_Batch_Throughput)->UseRealTime();

/**
 * @brief MPMC Pool Shared Mode: Atomic Synchronization Tax.
 * Measures the cost of an ABA-protected CAS allocation on a warm L1-D cache.
 */
static void BM_Pool_Shared_Scalar_Latency(benchmark::State& state) {
    mpmc_pool<bench_event, 65536, 8> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        benchmark::DoNotOptimize(ptr);
        pool.release(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Shared_Scalar_Latency)->UseRealTime();

/**
 * @brief Batch Amortization: O(1) Atomic Tax.
 * Validates the efficiency of grabbing 64 nodes in a single atomic transaction.
 * This should demonstrate significantly higher throughput than scalar allocation.
 */
static void BM_Pool_Shared_Batch_Throughput(benchmark::State& state) {
    mpmc_pool<bench_event, 65536, 8> pool;
    bench_event* batch[64];
    for (auto _ : state) {
        size_t n = pool.make_batch(batch, 64);
        if (SL_EXPECT_FALSE(n == 0)) {
            state.SkipWithError("Pool exhausted");
            break;
        }
        benchmark::DoNotOptimize(batch);
        pool.release_batch(batch, n);
    }
    state.SetItemsProcessed(state.iterations() * 64);
    state.counters["throughput_GBps"] = benchmark::Counter(
        static_cast<double>(64 * sizeof(bench_event)), 
        benchmark::Counter::kIsIterationInvariantRate
    );
}
BENCHMARK(BM_Pool_Shared_Batch_Throughput)->UseRealTime();

/**
 * @brief Hybrid Pool Latency: Asymmetric Handoff.
 * Measures the cost of allocation from the primary stack and asynchronous release
 * via the return ring using the automatic reclamation strategy.
 */
static void BM_Pool_Hybrid_RoundTrip(benchmark::State& state) {
    mpsc_hybrid_pool<bench_event, 65536, reclaim_strategy::automatic> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        if (SL_EXPECT_FALSE(!ptr)) {
            state.SkipWithError("Hybrid pool exhausted");
            break;
        }
        benchmark::DoNotOptimize(ptr);
        pool.release(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Hybrid_RoundTrip)->UseRealTime();

/**
 * @brief Hybrid Pool Batch Throughput.
 * Validates the efficiency of grabbing and releasing 64 nodes in a single cycle.
 */
static void BM_Pool_Hybrid_Batch_Throughput(benchmark::State& state) {
    mpsc_hybrid_pool<bench_event, 65536, reclaim_strategy::automatic> pool;
    bench_event* batch[64];
    for (auto _ : state) {
        size_t n = pool.make_batch(batch, 64);
        if (SL_EXPECT_FALSE(n == 0)) {
            state.SkipWithError("Hybrid pool exhausted");
            break;
        }
        benchmark::DoNotOptimize(batch);
        pool.release_batch(batch, n);
    }
    state.SetItemsProcessed(state.iterations() * 64);
    state.counters["throughput_GBps"] = benchmark::Counter(
        static_cast<double>(64 * sizeof(bench_event)), 
        benchmark::Counter::kIsIterationInvariantRate
    );
}
BENCHMARK(BM_Pool_Hybrid_Batch_Throughput)->UseRealTime();

/**
 * @brief Hybrid Pool Contention: Asymmetric Pressure.
 * Measures the stack-head contention when multiple threads allocate/release.
 */
static void BM_Pool_Hybrid_Contention_Scaling(benchmark::State& state) {
    static mpsc_hybrid_pool<bench_event, 262144, reclaim_strategy::automatic> pool;
    hardware_topology::pin_thread(state.thread_index());

    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        while (SL_UNLIKELY(!ptr)) {
            _mm_pause();
            ptr = pool.make_raw();
        }
        benchmark::DoNotOptimize(ptr);
        pool.release(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Hybrid_Contention_Scaling)->ThreadRange(1, 8)->UseRealTime();

/**
 * @brief Slab Allocator: ABA-protected SHM Allocator.
 */
static void BM_Pool_SlabAllocator_Latency(benchmark::State& state) {
    slab_allocator<bench_event, 65536> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        benchmark::DoNotOptimize(ptr);
        pool.free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_SlabAllocator_Latency)->UseRealTime();

/**
 * @brief Pinned MPMC: Detached Treiber Stack.
 */
static void BM_Pool_Pinned_MPMC_Latency(benchmark::State& state) {
    pinned_allocator_mpmc<bench_event, 65536> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        benchmark::DoNotOptimize(ptr);
        pool.free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Pinned_MPMC_Latency)->UseRealTime();

/**
 * @brief Pinned SPSC: Shadow-Pointer SHM.
 */
static void BM_Pool_Pinned_SPSC_Latency(benchmark::State& state) {
    pinned_allocator_spsc<bench_event, 65536> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        benchmark::DoNotOptimize(ptr);
        pool.free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Pinned_SPSC_Latency)->UseRealTime();

/**
 * @brief Pinned Isolated: Cache-Isolated MPMC.
 */
static void BM_Pool_Pinned_Isolated_Latency(benchmark::State& state) {
    pinned_slab_allocator<bench_event, 65536> pool;
    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        benchmark::DoNotOptimize(ptr);
        pool.free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Pinned_Isolated_Latency)->UseRealTime();

/**
 * @brief Sharded MPMC Scaling (The Slabflux Advantage).
 * Demonstrates how sharding eliminates the 'Interconnect Wall'.
 * We compare a single-lane MPMC (Contention) vs 8-lane MPMC (Slabflux Default).
 */
template <size_t Lanes>
static void BM_Pool_MPMC_Contention_Scaling(benchmark::State& state) {
    // Static pool shared across all benchmark threads
    static mpmc_pool<bench_event, 262144, Lanes> pool;
    
    // Ensure threads are pinned to avoid OS scheduler interference
    hardware_topology::pin_thread(state.thread_index());

    for (auto _ : state) {
        auto* ptr = pool.make_raw();
        while (SL_UNLIKELY(!ptr)) {
            _mm_pause(); // Stabilize L3 bus traffic
            ptr = pool.make_raw();
        }
        benchmark::DoNotOptimize(ptr);
        pool.release(ptr); // Ensure items processed is set
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_TEMPLATE(BM_Pool_MPMC_Contention_Scaling, 1)
    ->Name("BM_Pool_Contention_SingleLane")->ThreadRange(1, 8)->UseRealTime();
BENCHMARK_TEMPLATE(BM_Pool_MPMC_Contention_Scaling, 8)
    ->Name("BM_Pool_Contention_Sharded_L8")->ThreadRange(1, 8)->UseRealTime();

/**
 * @brief MPSC (Asymmetric) handoff.
 * Measures the latency of allocating on the Producer's LIFO stack (0 atomic ops)
 * and releasing via the cross-thread return ring.
 */
static void BM_Pool_Asymmetric_Release_Latency(benchmark::State& state) {
    auto pool_ptr = std::make_unique<mpsc_pool<bench_event, 65536, reclaim_strategy::manual>>();
    auto& pool = *pool_ptr;
    std::vector<bench_event*> ptrs;
    ptrs.reserve(4096);
    
    for(size_t i = 0; i < 4096; ++i) {
        if (auto* ev = pool.make_raw()) ptrs.push_back(ev);
    }

    for (auto _ : state) {
        // We measure the cost of the release (atomic return-ring push)
        if (!ptrs.empty()) {
            auto* p = ptrs.back();
            ptrs.pop_back();
            if (p) pool.release(p);
        } else {
            state.PauseTiming();
            pool.reclaim_returns();
            for(size_t i = 0; i < 4096; ++i) {
                if (auto* ev = pool.make_raw()) ptrs.push_back(ev);
            }
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Pool_Asymmetric_Release_Latency)->UseRealTime();

/**
 * @brief MPSC Pool Batch Throughput.
 * Validates the efficiency of the release_batch mechanism for the MPSC return ring.
 * Measures the full lifecycle: scalar allocation, batch release, and reclamation.
 */
static void BM_Pool_MPSC_Batch_Throughput(benchmark::State& state) {
    mpsc_pool<bench_event, 65536, reclaim_strategy::manual> pool;
    bench_event* batch[64];
    for (auto _ : state) {
        for (size_t i = 0; i < 64; ++i) {
            batch[i] = pool.make_raw();
        }
        benchmark::DoNotOptimize(batch);
        pool.release_batch(batch, 64);
        
        // Return memory to the LIFO stack for the next iteration
        pool.reclaim_returns();
    }
    state.SetItemsProcessed(state.iterations() * 64);
    state.counters["throughput_GBps"] = benchmark::Counter(
        static_cast<double>(64 * sizeof(bench_event)), 
        benchmark::Counter::kIsIterationInvariantRate
    );
}
BENCHMARK(BM_Pool_MPSC_Batch_Throughput)->UseRealTime();
/**
 * @brief Atomically safe SlabFlux MPSC Contention and Reclaim Benchmark.
 * Eliminates race conditions during the shutdown phase. 100% thread-safe.
 */
static void BM_Pool_MPSC_Contention_Scaling(benchmark::State& state) {
    using PoolType = slabflux::core::mpsc_pool<bench_event, 1048576, slabflux::core::reclaim_strategy::manual>;
    using HandoffType = slabflux::core::mpmc_conduit<bench_event*, 1048576, 8>;
    
    // Global unique pointer safely managed across threads
    static std::unique_ptr<PoolType> shared_pool = nullptr;
    static std::unique_ptr<HandoffType> handoff = nullptr;
    static std::atomic<int> active_threads{0};

    // 1. Thread registration for lifetime tracking
    if (state.thread_index() == 0) {
        shared_pool = std::make_unique<PoolType>();
        handoff = std::make_unique<HandoffType>();
    }
    active_threads.fetch_add(1, std::memory_order_release);

    // Wait until the pool is fully constructed on all threads
    while (handoff == nullptr) { _mm_pause(); }

    // Lock hardware affinity
    slabflux::core::hardware_topology::pin_thread(state.thread_index());

    // --- HOT PATH MEASUREMENT LOOP ---
    if (state.thread_index() == 0) {
        // OWNER: Allocates from the MPSC pool and distributes to workers via the MPMC conduit
        for (auto _ : state) {
            shared_pool->reclaim_returns();
            
            bench_event* local_batch[32];
            size_t allocated = shared_pool->make_batch(local_batch, 32);
            if (allocated > 0) {
                size_t pushed = handoff->push_batch(local_batch, allocated);
                if (pushed < allocated) {
                    // Return items that didn't fit into the channel back to the pool
                    shared_pool->release_batch(local_batch + pushed, allocated - pushed);
                }
            } else {
                _mm_pause();
            }
        }
    } else {
        // WORKER: Takes pointers from the conduit and returns them to the MPSC pool
        size_t items_processed = 0;
        for (auto _ : state) {
            bench_event* local_batch[32];
            size_t popped = handoff->pop_batch(local_batch, 32);
            
            if (popped > 0) {
                shared_pool->release_batch(local_batch, popped);
                items_processed += popped;
            } else {
                _mm_pause();
            }
        }
        state.SetItemsProcessed(items_processed);
    }
    // --- END HOT PATH ---

    // 2. Safe teardown protocol
    // Signal that this thread has finished
    active_threads.fetch_sub(1, std::memory_order_acq_rel);

    if (state.thread_index() == 0) {
        // The main thread waits until ALL producer threads have fully stopped and exited the loop
        while (active_threads.load(std::memory_order_acquire) > 0) { _mm_pause(); }
        
        // Drain any remaining items from the channel and pool for cleanliness
        bench_event* trash[32];
        while (handoff->pop_batch(trash, 32) > 0) {}
        shared_pool->reclaim_returns();

        // Once all threads are dead and guaranteed not to touch memory anymore,
        // the main thread safely destroys the pool before the program's official exit phase.
        handoff.reset();
        shared_pool.reset();
    }
}

BENCHMARK(BM_Pool_MPSC_Contention_Scaling)->ThreadRange(2, 16)->UseRealTime();

BENCHMARK_MAIN();
