/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file orthogonal_arbiter_bench.cpp
 * @brief Hot-path execution cost of the Subsumption Field Arbiter.
 */

#include <benchmark/benchmark.h>
#include "slabflux/rte/orthogonal_error_arbiter.hpp"

using namespace slabflux::rte;

static void BM_OrthogonalArbiter_RecordOnly(benchmark::State& state) {
    orthogonal_error_arbiter<1024> arbiter;
    
    // Measures strictly the O(1) wait-free injection tax on the hot-path
    for (auto _ : state) {
        arbiter.record(error_topology::logic_nan, 1);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrthogonalArbiter_RecordOnly)->UseRealTime();

static void BM_OrthogonalArbiter_RecordAndHarvest(benchmark::State& state) {
    orthogonal_error_arbiter<1024> arbiter;
    uint32_t mag = 0;
    
    // Measures a full Produce/Consume cycle simulating Telemetry sweeps
    for (auto _ : state) {
        arbiter.record(error_topology::hw_thermal, 90);
        arbiter.try_harvest(error_topology::hw_thermal, mag);
        benchmark::DoNotOptimize(mag);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrthogonalArbiter_RecordAndHarvest)->UseRealTime();

BENCHMARK_MAIN();