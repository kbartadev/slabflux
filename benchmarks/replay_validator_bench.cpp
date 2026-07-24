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
#include "slabflux/compute/replay_validator.hpp"

using namespace slabflux::compute;

struct alignas(64) large_state {
    uint64_t data[8192]; // 64 KB State Block
};

static large_state test_state_block;

/**
 * @brief Baseline: Standard Scalar CRC32 Accumulation
 */
static void BM_ReplayValidator_Baseline_CRC32(benchmark::State& state) {
    for (auto _ : state) {
        uint64_t hash = 0x12345678;
        for (size_t i = 0; i < 8192; ++i) {
            hash = _mm_crc32_u64(hash, test_state_block.data[i]);
        }
        benchmark::DoNotOptimize(hash);
    }
    state.SetBytesProcessed(state.iterations() * sizeof(large_state));
}
BENCHMARK(BM_ReplayValidator_Baseline_CRC32)->UseRealTime();

/**
 * @brief SlabFlux: 3-Way Interleaved Superscalar CRC32
 * Exposes exactly how much throughput is gained by saturating 
 * multiple hardware ALUs concurrently.
 */
static void BM_ReplayValidator_SlabFlux_Interleaved(benchmark::State& state) {
    for (auto _ : state) {
        uint64_t hash = compute_state_hash(test_state_block);
        benchmark::DoNotOptimize(hash);
    }
    state.SetBytesProcessed(state.iterations() * sizeof(large_state));
}
// Often yields 2.5x to 2.9x improvement on modern Intel architectures
BENCHMARK(BM_ReplayValidator_SlabFlux_Interleaved)->UseRealTime();

BENCHMARK_MAIN();