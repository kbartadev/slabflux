/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <benchmark/benchmark.h>
#include <array>
#include "slabflux/compute/mdl_state_array.hpp"

using namespace slabflux::compute;

constexpr size_t CAPACITY = 4096;

/**
 * @brief Baseline: Standard std::array read/write latency
 */
static void BM_StateArray_Baseline_StdArray(benchmark::State& state) {
    std::array<float, CAPACITY> arr{};
    float sum = 0.0f;
    
    for (auto _ : state) {
        // Write
        arr[512] = 42.0f;
        // Read
        sum += arr[512];
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StateArray_Baseline_StdArray)->UseRealTime();

/**
 * @brief SlabFlux: Minkowski Data Lattice (MDL) State Array
 * Measures the cost of accessing data through the Spacetime Envelope 
 * with automatic hardware subsumption.
 */
static void BM_StateArray_MDL_Subsumption(benchmark::State& state) {
    mdl_state_array<float, CAPACITY> mdl;
    uint64_t lsn = 0;
    float sum = 0.0f;

    for (auto _ : state) {
        ++lsn;
        // Write Sealed
        mdl.write_sealed(512, 42.0f, lsn);
        
        // Read Subsumed (Validates against active lightcone)
        sum += mdl.read_subsumed(512, lsn);
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StateArray_MDL_Subsumption)->UseRealTime();

BENCHMARK_MAIN();