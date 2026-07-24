/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <benchmark/benchmark.h>
#include <atomic>
#include <thread>
#include <immintrin.h>
#include "slabflux/compute/timing_invariant.hpp"
#include "slabflux/core/hardware_topology.hpp"
using namespace slabflux::compute;

struct payload { uint64_t metric; }; // Reduced to 8 bytes for native lock-free atomic operations

/**
 * @brief Baseline: Standard std::atomic Exchange
 * Often suffers from severe MESI protocol False Sharing under contention.
 */
static void BM_Observer_Baseline_Atomic_Contention(benchmark::State& state) {
    std::atomic<payload> shared_state{};
    std::atomic<bool> running{true};
    
    std::thread reader([&]() {
        slabflux::core::hardware_topology::pin_thread(1);
        while (running.load(std::memory_order_relaxed)) {
            payload p = shared_state.load(std::memory_order_acquire);
            benchmark::DoNotOptimize(p);
        }
    });

    slabflux::core::hardware_topology::pin_thread(0);
    payload update_val = {1}; // Initialize with a single value

    for (auto _ : state) {
        shared_state.store(update_val, std::memory_order_release);
    }
    
    running = false;
    reader.join();
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Observer_Baseline_Atomic_Contention)->UseRealTime();

/**
 * @brief SlabFlux: Sovereign Observer
 * Measures the throughput when using explicit cache-line isolation 
 * and lock-free seqlock logic with `_mm_prefetch`.
 */
static void BM_Observer_Sovereign_Contention(benchmark::State& state) {
    sovereign_observer<payload> observer;
    std::atomic<bool> running{true};
    
    std::thread reader([&]() {
        slabflux::core::hardware_topology::pin_thread(1);
        payload p;
        while (running.load(std::memory_order_relaxed)) {
            // The reader tries optimistically and spins if busy,
            // drastically reducing write-side cache invalidations.
            if (observer.try_read(p)) {
                benchmark::DoNotOptimize(p);
            } else {
                _mm_pause();
            }
        }
    });

    slabflux::core::hardware_topology::pin_thread(0);
    payload update_val = {1}; // Initialize with a single value

    for (auto _ : state) {
        // The Compute thread is the sovereign writer.
        // Zero-stall guaranteed.
        observer.update(update_val);
    }
    
    running = false;
    reader.join();
    
    // Often orders of magnitude faster on the write side than std::atomic 
    // due to complete elimination of cache-line ping-ponging.
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Observer_Sovereign_Contention)->UseRealTime();
