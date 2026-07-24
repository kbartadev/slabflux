/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <gtest/gtest.h>
#include <immintrin.h>
#include <x86intrin.h>
#include <vector>
#include <iostream>

// Hard barrier to prevent the C++ compiler from optimizing away the test loops
#define DO_NOT_OPTIMIZE(x) asm volatile("" : "+v"(x))

class ILPShadowingTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!__builtin_cpu_supports("avx512f") || !__builtin_cpu_supports("avx512cd")) {
            GTEST_SKIP() << "Skipping ILP Proof: AVX-512/CD not supported by host silicon.";
        }
    }
};

TEST_F(ILPShadowingTest, ProveShadowingWithCachePressure) {
    // 2048 vectors * 64 bytes = 128 KB per array.
    // This intentionally exceeds the standard 32KB/48KB L1 Data Cache to prove
    // that the hardware prefetcher and L2 cache handle the memory loads correctly.
    constexpr size_t NUM_VECTORS = 2048;
    constexpr size_t ITERATIONS = 10000; // Loop over the array 10,000 times
    
    alignas(64) float state_array[NUM_VECTORS * 16];
    alignas(64) uint32_t pointer_array[NUM_VECTORS * 16];
    
    for (size_t i = 0; i < NUM_VECTORS * 16; ++i) {
        state_array[i] = 1.0f;
        pointer_array[i] = 0xDEADBEEF;
    }

    __m512 v_mul = _mm512_set1_ps(1.0001f);
    __m512 v_add = _mm512_set1_ps(0.00001f);
    __m512i v_conflict_mask = _mm512_setzero_si512();

    // 1. WARMUP (Wake up the CPU from C-States, prime the caches)
    volatile float sink = 0;
    for(size_t i = 0; i < 1000; ++i) sink += state_array[0];

    // 2. BASELINE: Memory Load + Math Only
    uint64_t start_math = __rdtsc();
    for (size_t iter = 0; iter < ITERATIONS; ++iter) {
        #pragma GCC unroll 4
        for (size_t i = 0; i < NUM_VECTORS; ++i) {
            __m512 v_state = _mm512_load_ps(&state_array[i * 16]); // Uses Load Port
            v_state = _mm512_fmadd_ps(v_state, v_mul, v_add);      // Uses ALU Port 0/1
            _mm512_store_ps(&state_array[i * 16], v_state);        // Uses Store Port
        }
        DO_NOT_OPTIMIZE(state_array[0]);
    }
    uint64_t math_cycles = __rdtsc() - start_math;

    // 3. SHADOWED: Memory Load + Math + Parallel Integrity Load + Security Validation
    // This simulates real life: reading pointers from memory while computing.
    uint64_t start_shadow = __rdtsc();
    for (size_t iter = 0; iter < ITERATIONS; ++iter) {
        #pragma GCC unroll 4
        for (size_t i = 0; i < NUM_VECTORS; ++i) {
            // Math Path
            __m512 v_state = _mm512_load_ps(&state_array[i * 16]); 
            v_state = _mm512_fmadd_ps(v_state, v_mul, v_add);      
            _mm512_store_ps(&state_array[i * 16], v_state);        
            
            // Security Path (Shadowed Parallel Execution)
            __m512i v_ptr = _mm512_load_si512(&pointer_array[i * 16]); // Parallel Load Port
            v_conflict_mask = _mm512_conflict_epi32(v_ptr);            // ALU Port 5
        }
        DO_NOT_OPTIMIZE(state_array[0]);
        DO_NOT_OPTIMIZE(v_conflict_mask);
    }
    uint64_t shadow_cycles = __rdtsc() - start_shadow;

    double ratio = static_cast<double>(shadow_cycles) / static_cast<double>(math_cycles);
    
    std::cout << "[ILP CACHE PROOF] Math + L1/L2 Load Cycles  : " << math_cycles << "\n";
    std::cout << "[ILP CACHE PROOF] Math + Sec + Dual Loads   : " << shadow_cycles << "\n";
    std::cout << "[ILP CACHE PROOF] Overhead Ratio            : " << ratio << "x\n";

    // We are executing DOUBLE the memory loads, yet the total latency should barely move 
    // because the dual Load AGUs (Address Generation Units) and ALU ports process it in parallel.
    // We allow a slightly higher margin (15%) for L2 cache line collision jitter.
    EXPECT_LT(ratio, 1.15) << "ILP Cache Shadowing failed! The memory fetches stalled the pipeline.";
}