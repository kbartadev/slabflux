/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <benchmark/benchmark.h>
#include <immintrin.h>
#include "slabflux/compute/ilp_shadowed_executor.hpp"
#include "slabflux/compute/numerical_sanitizer.hpp"

using namespace slabflux::compute;

constexpr size_t CAPACITY = 1024;
alignas(64) float state_matrix[CAPACITY];
alignas(64) float weight_matrix[CAPACITY];
alignas(64) uint32_t identity_matrix[CAPACITY];
alignas(64) uint8_t aphasic_horizon[256];

/**
 * @brief Baseline: Standard FMA AVX-512 execution without any security validation.
 */
static void BM_Compute_Baseline_FMA_Only(benchmark::State& state) {
    const __m512 v_decay = _mm512_set1_ps(0.9f);
    const __m512 v_conf  = _mm512_set1_ps(0.1f);
    
    for (auto _ : state) {
        #pragma GCC unroll 4
        for (size_t i = 0; i < CAPACITY / 16; ++i) {
            __m512 v_state = _mm512_load_ps(&state_matrix[i * 16]);
            __m512 v_weight = _mm512_load_ps(&weight_matrix[i * 16]);
            v_state = _mm512_fmadd_ps(v_state, v_decay, _mm512_mul_ps(v_weight, v_conf));
            _mm512_store_ps(&state_matrix[i * 16], v_state);
        }
        benchmark::DoNotOptimize(state_matrix);
    }
    state.SetItemsProcessed(state.iterations() * CAPACITY);
}
BENCHMARK(BM_Compute_Baseline_FMA_Only)->UseRealTime();

/**
 * @brief SlabFlux: ILP Shadowed Executor
 * Measures the execution time of running business logic (FMA) AND
 * security validation (Conflict Detection & Masking) concurrently.
 */
static void BM_Compute_ILP_Shadowed_Executor(benchmark::State& state) {
    for (auto _ : state) {
        ilp_shadowed_executor::execute_shadowed_loop(
            state_matrix, weight_matrix, identity_matrix, 
            CAPACITY / 16, 0.9f, 0.1f, 0, aphasic_horizon
        );
        benchmark::DoNotOptimize(state_matrix);
    }
    state.SetItemsProcessed(state.iterations() * CAPACITY);
}
BENCHMARK(BM_Compute_ILP_Shadowed_Executor)->UseRealTime();

/**
 * @brief SlabFlux: Numerical Sanitizer (Baseline Stabilizer)
 */
static void BM_NumericalSanitizer_Baseline(benchmark::State& state) {
    for (auto _ : state) {
        uint64_t mask = numerical_sanitizer<float, CAPACITY>::template sanitize<baseline_stabilizer>(
            state_matrix, 0.0f, false, nullptr, 0.0f
        );
        benchmark::DoNotOptimize(mask);
    }
    state.SetItemsProcessed(state.iterations() * CAPACITY);
}
BENCHMARK(BM_NumericalSanitizer_Baseline)->UseRealTime();

/**
 * @brief SlabFlux: Numerical Sanitizer (Neighbor-Weighted Interpolation)
 * Hardcore hardware-mapped recovery logic benchmark.
 */
static void BM_NumericalSanitizer_NeighborWeighted(benchmark::State& state) {
    // Introduce a NaN to trigger the recovery path
    state_matrix[16] = __builtin_nanf("");
    
    for (auto _ : state) {
        uint64_t mask = numerical_sanitizer<float, CAPACITY>::template sanitize<neighbor_weighted_stabilizer>(
            state_matrix, 0.0f, true, nullptr, 0.0f
        );
        benchmark::DoNotOptimize(mask);
    }
    state.SetItemsProcessed(state.iterations() * CAPACITY);
}
BENCHMARK(BM_NumericalSanitizer_NeighborWeighted)->UseRealTime();

BENCHMARK_MAIN();