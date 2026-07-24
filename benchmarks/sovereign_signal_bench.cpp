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
#include "slabflux/compute/sovereign_signal.hpp"

using namespace slabflux::compute;

struct alignas(32) payload {
    uint64_t data[4];
};

/**
 * @brief Baseline: Standard CRC32 Integrity Seal
 */
static void BM_Integrity_Baseline_CRC32(benchmark::State& state) {
    payload p = {1, 2, 3, 4};
    uint64_t lsn = 0;
    uint32_t checksum = 0;

    for (auto _ : state) {
        checksum = _mm_crc32_u64(0, p.data[0]);
        checksum = _mm_crc32_u64(checksum, p.data[1]);
        checksum = _mm_crc32_u64(checksum, p.data[2]);
        checksum = _mm_crc32_u64(checksum, p.data[3]);
        checksum = _mm_crc32_u64(checksum, ++lsn);
        
        // Validate
        bool valid = (checksum != 0);
        benchmark::DoNotOptimize(valid);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Integrity_Baseline_CRC32)->UseRealTime();

/**
 * @brief SlabFlux: Sovereign Signal (Symplectic Resonance Fencing)
 */
static void BM_SovereignSignal_Seal_And_Validate(benchmark::State& state) {
    payload p = {1, 2, 3, 4};
    sovereign_signal<payload> signal(p);
    uint64_t lsn = 0;

    for (auto _ : state) {
        // Entangle
        signal.seal(++lsn);
        
        // Evaluate Geometric Tension
        bool valid = signal.validate_and_vaporize();
        benchmark::DoNotOptimize(valid);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SovereignSignal_Seal_And_Validate)->UseRealTime();

/**
 * @brief SlabFlux: Topological Vaporization (Corruption Path)
 * Measures the cost of destroying corrupted data in the same cycle.
 */
static void BM_SovereignSignal_Vaporization_Path(benchmark::State& state) {
    payload p = {1, 2, 3, 4};
    sovereign_signal<payload> signal(p);
    uint64_t lsn = 0;

    for (auto _ : state) {
        state.PauseTiming();
        signal.seal(++lsn);
        // Induce structural corruption (Cosmic Ray / UAF Simulation)
        reinterpret_cast<uint8_t*>(&signal)[10] ^= 0xFF; 
        state.ResumeTiming();
        
        bool valid = signal.validate_and_vaporize();
        benchmark::DoNotOptimize(valid);
        
        if (valid) {
            state.SkipWithError("Geometric fencing failed to catch corruption!");
            break;
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SovereignSignal_Vaporization_Path)->UseRealTime();

BENCHMARK_MAIN();