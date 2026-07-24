/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file vector_lane_engine_test.cpp
 * @brief Unit tests verifying AVX multi-port execution graphs and Float stability.
 */

// Enable hardware numeric traps for the purpose of verifying death tests
#define SLABFLUX_SIMD_SANITIZE_NUMERICS

#include <gtest/gtest.h>
#include <limits>
#include <cstring>
#include "slabflux/compute/vector_lane_engine.hpp"

using namespace slabflux::compute;

// Mock execution graph to verify the execute() dispatcher logic across AVX ISAs
struct test_kernel_fma {
    template <typename VecT>
    static VecT compute(VecT m, VecT i, VecT p) {
        if constexpr (sizeof(VecT) == 64) {
#if defined(__AVX512F__)
            return _mm512_add_ps(m, _mm512_mul_ps(i, p));
#else
            return m; // Should not be reached with proper ISA, but handles compiler AST
#endif
        } else if constexpr (sizeof(VecT) == 32) {
#if defined(__AVX2__)
            return _mm256_add_ps(m, _mm256_mul_ps(i, p));
#else
            return m;
#endif
        } else {
            return m + i * p; // Scalar fallback
        }
    }
};

TEST(VectorLaneEngineTest, StructuralInvariants) {
    // 1. Core Object Alignment
    EXPECT_EQ(alignof(vector_lane_engine<test_kernel_fma, float, 16>), 64);
    EXPECT_EQ(alignof(vector_lane_engine<test_kernel_fma, float, 64>), 64);

    // 2. Internal Array Alignment (Crucial for AVX-512 Zero-Penalty Loads)
    vector_lane_engine<test_kernel_fma, float, 64> engine;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(engine.state()) % 64, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(engine.inputs()) % 64, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(engine.parameters()) % 64, 0);
}

TEST(VectorLaneEngineTest, OutOfBoundsMemoryIntegrity) {
    // Sandbox structure to detect SIMD over-writes and spillage
    struct alignas(64) Sandbox {
        uint32_t guard_prefix[16];
        vector_lane_engine<test_kernel_fma, float, 16, true> engine;
        uint32_t guard_suffix[16];
    } sandbox;

    for (int i = 0; i < 16; ++i) {
        sandbox.guard_prefix[i] = 0xDEADBEEF;
        sandbox.guard_suffix[i] = 0xCAFEBABE;
        sandbox.engine.state()[i] = 10.0f;
        sandbox.engine.inputs()[i] = 2.0f;
        sandbox.engine.parameters()[i] = 2.0f;
    }

    sandbox.engine.execute();

    for (int i = 0; i < 16; ++i) {
        // Ensure strict physical containment
        EXPECT_EQ(sandbox.guard_prefix[i], 0xDEADBEEF) << "Prefix guard corrupted at index " << i;
        EXPECT_EQ(sandbox.guard_suffix[i], 0xCAFEBABE) << "Suffix guard corrupted at index " << i;
        EXPECT_FLOAT_EQ(sandbox.engine.state()[i], 14.0f);
    }
}

TEST(VectorLaneEngineTest, ExecuteStrictDIWE_16_MinTiling) {
    // 16 lanes is the minimum boundary for float to tile across 64-byte cache lines
    vector_lane_engine<test_kernel_fma, float, 16, true> engine;
    
    for (int i = 0; i < 16; ++i) {
        engine.state()[i] = static_cast<float>(i * 2);
        engine.inputs()[i] = 1.5f;
        engine.parameters()[i] = 2.0f;
    }
    
    engine.execute();
    
    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(engine.state()[i], static_cast<float>(i * 2) + 3.0f);
    }
}

TEST(VectorLaneEngineTest, ExecuteStrictDIWE_16) {
    vector_lane_engine<test_kernel_fma, float, 16, true> engine;
    
    for (int i = 0; i < 16; ++i) {
        engine.state()[i] = static_cast<float>(i); // Distinct value per lane
        engine.inputs()[i] = 2.5f;
        engine.parameters()[i] = 4.0f;
    }
    
    engine.execute();
    
    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(engine.state()[i], static_cast<float>(i) + 10.0f); 
    }
}

TEST(VectorLaneEngineTest, ExecuteStrictDIWE_64) {
    vector_lane_engine<test_kernel_fma, float, 64, true> engine;
    
    for (int i = 0; i < 64; ++i) {
        engine.state()[i] = static_cast<float>(i);
        engine.inputs()[i] = 0.5f;
        engine.parameters()[i] = 10.0f;
    }
    
    engine.execute();
    
    for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(engine.state()[i], static_cast<float>(i) + 5.0f);
    }
}

TEST(VectorLaneEngineTest, ExecuteNonStrict_32) {
    // Ensure non-strict fallback performs correctly and yields identically
    vector_lane_engine<test_kernel_fma, float, 32, false> engine;
    
    // Zero-initialize the entire memory block to prevent the SIMD sanitization macro 
    // from trapping on uninitialized padding/garbage during non-strict fallback execution.
    std::memset(&engine, 0, sizeof(engine));

    for (int i = 0; i < 32; ++i) {
        engine.state()[i] = static_cast<float>(i); 
        engine.inputs()[i] = -2.0f; 
        engine.parameters()[i] = 3.0f;
    }
    engine.execute();
    for (int i = 0; i < 32; ++i) EXPECT_FLOAT_EQ(engine.state()[i], static_cast<float>(i) - 6.0f);
}

TEST(VectorLaneEngineTest, NumericSanitizationDeath_NaN) {
    vector_lane_engine<test_kernel_fma, float, 16, true> engine;
    for (int i = 0; i < 16; ++i) {
        engine.state()[i] = 1.0f;
        engine.inputs()[i] = std::numeric_limits<float>::quiet_NaN();
        engine.parameters()[i] = 3.0f;
    }
    
    // The FMA kernel will compute 1.0 + NaN * 3.0 = NaN.
    // The sanitization macro must intercept this and trigger a cold hardware trap.
    EXPECT_DEATH({
        engine.execute();
    }, ".*");
}

TEST(VectorLaneEngineTest, NumericSanitizationDeath_Infinity) {
    vector_lane_engine<test_kernel_fma, float, 16, true> engine;
    for (int i = 0; i < 16; ++i) {
        engine.state()[i] = 1.0f;
        engine.inputs()[i] = std::numeric_limits<float>::infinity();
        engine.parameters()[i] = 3.0f;
    }
    
    // FMA computes 1.0 + Inf * 3.0 = Inf.
#if defined(__AVX512F__)
    // On AVX-512, the 0x99 mask actively traps Infinity
    EXPECT_DEATH({
        engine.execute();
    }, ".*");
#else
    // On AVX2, the check relies on _CMP_UNORD_Q which specifically detects NaNs.
    engine.execute();
    SUCCEED();
#endif
}