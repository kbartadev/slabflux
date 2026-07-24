/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file novel_queues_bench.cpp
 * @brief Benchmarks for the new structural queue primitives.
 */

#include <benchmark/benchmark.h>
#include "slabflux/core/orthogonal_manifold.hpp"
#include "slabflux/core/cross_orthogonal_queue.hpp"
#include "slabflux/core/asymmetric_dispersion_queue.hpp"
#include "slabflux/core/pendulum_spsc_conduit.hpp"

using namespace slabflux::core;

static void BM_OrthogonalManifold_PushPop(benchmark::State& state) {
    orthogonal_manifold<uint64_t, 256> q;
    uint64_t data = 42;
    for (auto _ : state) {
        q.push(&data);
        auto* res = q.pop();
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrthogonalManifold_PushPop)->UseRealTime();

static void BM_CrossOrthogonal_PushPop(benchmark::State& state) {
    cross_orthogonal_queue<uint64_t, 256> q;
    uint64_t data = 42;
    for (auto _ : state) {
        q.push(&data);
        auto* res = q.pop();
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CrossOrthogonal_PushPop)->UseRealTime();

static void BM_AsymmetricDispersion_PushPop(benchmark::State& state) {
    asymmetric_dispersion_queue<uint64_t, 256> q;
    uint64_t data = 42;
    for (auto _ : state) {
        q.push(&data);
        auto* res = q.pop();
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AsymmetricDispersion_PushPop)->UseRealTime();

static void BM_PendulumSPSC_PushPop(benchmark::State& state) {
    pendulum_spsc_conduit<uint64_t, 256> q;
    uint64_t data = 42;
    for (auto _ : state) {
        (void)q.try_push(&data);
        uint64_t* res = nullptr;
        (void)q.try_pop(res);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PendulumSPSC_PushPop)->UseRealTime();

BENCHMARK_MAIN();