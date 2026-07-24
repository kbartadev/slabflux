/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE - ONTOLOGICAL COMPUTE SUBSYSTEM
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <benchmark/benchmark.h>
#include "slabflux/compute/axiomatic_vector.hpp"

using namespace slabflux::compute::axiomatic;

constexpr size_t DIMENSION = 64;

/**
 * @brief Baseline: Safe Array Addition
 * @details Hagyományos megoldás, ahol minden iterációnál "if" ágakkal 
 * ellenőrizzük a topológiát és az állapotot. Jellemzően elrontja a 
 * Branch Predictort és megtöri az ILP-t.
 */
static void BM_Baseline_SafeArray_Add(benchmark::State& state) {
    int v1_data[DIMENSION], v2_data[DIMENSION], v3_data[DIMENSION];
    bool v1_mask[DIMENSION], v2_mask[DIMENSION], v3_mask[DIMENSION];

    // Inicializálás folytonos szubsztanciával
    for (size_t i = 0; i < DIMENSION; ++i) {
        v1_data[i] = v2_data[i] = static_cast<int>(i);
        v1_mask[i] = v2_mask[i] = true;
    }

    for (auto _ : state) {
        bool error = false;
        bool vacuum_detected = false;

        for (size_t i = 0; i < DIMENSION; ++i) {
            v3_mask[i] = v1_mask[i] && v2_mask[i];
            v3_data[i] = v3_mask[i] ? v1_data[i] + v2_data[i] : 0;

            // Hagyományos struktúra ellenőrzés
            if (!v3_mask[i]) {
                vacuum_detected = true;
            } else if (vacuum_detected) {
                error = true; // Topológiai szakadás
            }

            if (!v3_mask[i] && v3_data[i] != 0) {
                error = true; // Tisztátalan nulla
            }
        }
        benchmark::DoNotOptimize(v3_data);
        benchmark::DoNotOptimize(v3_mask);
        benchmark::DoNotOptimize(error);
    }
    state.SetItemsProcessed(state.iterations() * DIMENSION);
}
BENCHMARK(BM_Baseline_SafeArray_Add)->UseRealTime();

/**
 * @brief SlabFlux: Axiomatikus Vektor Inicializálás (Gödel-háló)
 * @details A szubsztancia-mező validálásának és a Monád (Validated<T>)
 * becsomagolásának költsége.
 */
static void BM_AxiomaticVector_Construct(benchmark::State& state) {
    int vals[DIMENSION] = {0};
    bool masks[DIMENSION] = {false};
    for (size_t i = 0; i < DIMENSION; ++i) {
        vals[i] = static_cast<int>(i);
        masks[i] = true;
    }

    for (auto _ : state) {
        auto v = VectorLane<int, DIMENSION>::construct(vals, masks, DIMENSION);
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations() * DIMENSION);
}
BENCHMARK(BM_AxiomaticVector_Construct)->UseRealTime();

/**
 * @brief SlabFlux: Axiomatikus Vektor Algebrai Összeadás
 * @details Két validált sokaság algebrai fúziója és a Gödel-rács
 * újra-értékelése. Bizonyítja, hogy az axiomatikus struktúra 
 * azonos vagy jobb teljesítményt nyújt a naiv Baseline-nál.
 */
static void BM_AxiomaticVector_Add(benchmark::State& state) {
    int vals[DIMENSION] = {0};
    bool masks[DIMENSION] = {false};
    for (size_t i = 0; i < DIMENSION; ++i) {
        vals[i] = static_cast<int>(i);
        masks[i] = true;
    }

    // Kinyerjük a garantáltan tiszta sokaságokat
    auto v1 = VectorLane<int, DIMENSION>::construct(vals, masks, DIMENSION).extract_or_panic();
    auto v2 = VectorLane<int, DIMENSION>::construct(vals, masks, DIMENSION).extract_or_panic();

    for (auto _ : state) {
        // Algebrai művelet, amely be is csomagolja az eredményt a Monádba
        auto v3 = v1.add(v2);
        benchmark::DoNotOptimize(v3);
    }
    state.SetItemsProcessed(state.iterations() * DIMENSION);
}
BENCHMARK(BM_AxiomaticVector_Add)->UseRealTime();

BENCHMARK_MAIN();