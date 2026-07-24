/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file vector_lane_512_test.cpp
 * @brief Unit tests for AVX-512 fixed-point vector lanes and DIW-E compliance.
 */

#include <gtest/gtest.h>
#include "slabflux/compute/vector_lane_512.hpp"
#include "slabflux/compute/simd_invariant_guard.hpp"
#include <cstring>

using namespace slabflux::compute;

TEST(VectorLane512Test, PhysicalAlignmentInvariants) {
    vector_lane_512<64> engine;
    
    // Requirement: Must be 64-byte aligned for zero-penalty AVX-512 loads
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&engine) % 64, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(engine.states) % 64, 0);
}

TEST(VectorLane512Test, StructuralInvariants) {
    EXPECT_EQ(alignof(vector_lane_512<16>), 64);
    EXPECT_EQ(alignof(vector_lane_512<64>), 64);
    
    // Validate DIW-E Concept Boundaries for ZMM (32 max registers)
    static_assert(DIWEnforceable<64, 16, 4, 32>, "4-way unroll should fit in 32 ZMMs");
    static_assert(DIWEnforceable<32, 16, 2, 32>, "2-way unroll should fit in 32 ZMMs");
    static_assert(DIWEnforceable<16, 16, 1, 32>, "1-way unroll should fit in 32 ZMMs");
}

TEST(VectorLane512Test, OutOfBoundsMemoryIntegrity) {
    // Sandbox structure to detect SIMD over-writes and spillage
    struct alignas(64) Sandbox {
        uint32_t guard_prefix[16];
        vector_lane_512<16, true> engine;
        uint32_t guard_suffix[16];
    } sandbox;

    auto calc_expected = [](int32_t init, int32_t sig) {
        return (init * 922 + sig * 102 + 512) >> 10;
    };

    for (int i = 0; i < 16; ++i) {
        sandbox.guard_prefix[i] = 0xDEADBEEF;
        sandbox.guard_suffix[i] = 0xCAFEBABE;
        sandbox.engine.states[i] = i * 1000; // Unique value per lane
    }

    sandbox.engine.propagate(10, 1);

    for (int i = 0; i < 16; ++i) {
        // Ensure strict physical containment
        EXPECT_EQ(sandbox.guard_prefix[i], 0xDEADBEEF) << "Prefix guard corrupted at index " << i;
        EXPECT_EQ(sandbox.guard_suffix[i], 0xCAFEBABE) << "Suffix guard corrupted at index " << i;
        
        int32_t expected = calc_expected(i * 1000, 10);
        EXPECT_EQ(sandbox.engine.states[i], expected) << "Math corrupted at lane " << i;
    }
}

TEST(VectorLane512Test, PropagateStrictDIWE_16) {
    // 16 lanes is the absolute minimum boundary for AVX-512 strict DIW-E
    vector_lane_512<16, true> lane;
    for (int i = 0; i < 16; ++i) lane.states[i] = i * 1000;
    
    lane.propagate(10, 1);
    
    for (int i = 0; i < 16; ++i) {
        int32_t expected = (i * 1000 * 922 + 10 * 102 + 512) >> 10;
        EXPECT_EQ(lane.states[i], expected);
    }
}

TEST(VectorLane512Test, PropagateStrictDIWE) {
    // Test 64-lane full DIW-E unrolled block
    vector_lane_512<64, true> lane;
    for (int i = 0; i < 64; ++i) lane.states[i] = i * 1000; // Unique lane values
    
    lane.propagate(10, 1);
    
    for (int i = 0; i < 64; ++i) {
        int32_t expected = (i * 1000 * 922 + 10 * 102 + 512) >> 10;
        EXPECT_EQ(lane.states[i], expected);
    }
}

TEST(VectorLane512Test, PropagateNonStrict) {
    // Test fallback loop
    vector_lane_512<64, false> lane;
    for (int i = 0; i < 64; ++i) lane.states[i] = i * 1000;
    
    lane.propagate(10, 1);
    
    for (int i = 0; i < 64; ++i) {
        int32_t expected = (i * 1000 * 922 + 10 * 102 + 512) >> 10;
        EXPECT_EQ(lane.states[i], expected);
    }
}

TEST(VectorLane512Test, SyncState) {
    vector_lane_512<32> lane;
    alignas(64) int32_t new_states[32];
    
    for (int i = 0; i < 32; ++i) {
        new_states[i] = i * 10;
    }
    
    lane.sync_state(new_states, 42);
    
    EXPECT_EQ(lane.last_lsn, 42);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(lane.states[i], i * 10);
    }
}

TEST(VectorLane512Test, BitExactPropagation) {
    vector_lane_512<64> engine_a;
    vector_lane_512<64> engine_b;

    std::memset(engine_a.states, 0, sizeof(engine_a.states));
    std::memset(engine_b.states, 0, sizeof(engine_b.states));

    const int32_t signal = 10;
    const uint64_t lsn = 12345;

    // Execution must be identical across separate instances on the same silicon
    engine_a.propagate(signal, lsn);
    engine_b.propagate(signal, lsn);

    for (int i = 0; i < 64; ++i) {
        uint32_t raw_a, raw_b;
        std::memcpy(&raw_a, &engine_a.states[i], 4);
        std::memcpy(&raw_b, &engine_b.states[i], 4);
        
        // Binary equality check to ensure no floating point entropy/drift
        ASSERT_EQ(raw_a, raw_b) << "Divergence detected at lane " << i;
    }
}
