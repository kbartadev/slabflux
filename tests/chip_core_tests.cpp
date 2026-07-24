/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * SOURCE-AVAILABLE CODEBASE
 *
 * This source file is distributed under the conditions of the SLABFLUX 
 * SOURCE-AVAILABLE AND ECOSYSTEM LICENSE (the "License").
 *
 * ----------------------------------------------------------------------------
 * CRITICAL WARNING
 * ----------------------------------------------------------------------------
 * This module may execute outside standard OS mediation layers. Incorrect 
 * integration, misconfiguration, or unsafe deployment can result in:
 *
 *   • irreversible data corruption
 *   • kernel instability or panics
 *   • NIC or PCIe bus desynchronization
 *   • undefined hardware state transitions
 *   • permanent loss of system integrity
 *
 * Use only in controlled environments with full understanding of the 
 * architectural constraints and hardware implications.
 *
 * ----------------------------------------------------------------------------
 * USAGE GUIDELINES
 * ----------------------------------------------------------------------------
 * Execution, integration, and deployment by developers is permitted strictly 
 * subject to the conditional grants and structural limitations defined within 
 * the License. Please refer to the License for full terms regarding corporate 
 * deployment and replication.
 *
 * ----------------------------------------------------------------------------
 * LIMITATION OF LIABILITY
 * ----------------------------------------------------------------------------
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL THE AUTHOR OR 
 * COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, 
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 * DISCLAIMER OF WARRANTY
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * See accompanying LICENSE and NOTICE files for the integrated terms of use.
 * ============================================================================*/
 
#include <gtest/gtest.h>
#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/core/hole_puncher.hpp"
#include "slabflux/compute/vector_lane_256.hpp"

using namespace slabflux::core;

// ============================================================
// 1. STRUCTURAL TESTS (Structural Honesty)
// ============================================================
TEST(ChipCore, WireFrameIntegrity) {
    struct dummy_payload { float a; uint32_t b; };
    using frame_t = wire_frame_lsn<dummy_payload>;

    // Verify that the frame is aligned and padded to 64 bytes for cache isolation
    EXPECT_EQ(sizeof(frame_t), 64);

    frame_t frame{};
    frame.cluster_id = 0x4653;
    EXPECT_EQ(frame.cluster_id, 0x4653); // 'SF' Cluster Routing ID
}

TEST(ChipCore, CacheAlignment) {
    // Critical elements must be aligned to cache lines
    EXPECT_EQ(alignof(sf_node_ctx), 64);
    EXPECT_EQ(alignof(slabflux::compute::vector_lane_256<64>), 64);
}

// ============================================================
// 2. LSN ENGINE TESTS (The Global Clock)
// ============================================================
TEST(ChipCore, LSNSequencing) {
    sf_node_ctx ctx;

    // O(1) sequence generation
    EXPECT_EQ(ctx.reserve_next(), 0);
    EXPECT_EQ(ctx.reserve_next(), 1);

    // Commit horizon check
    ctx.commit(100);
    EXPECT_EQ(ctx.horizon(), 100);
}

// ============================================================
// 3. HOLE PUNCHER TESTS (Gap-Engine)
// ============================================================
TEST(ChipCore, HolePuncherReordering) {
    hole_puncher<uint32_t, 16> hp;
    uint32_t result = 0;
    auto reducer = [&](uint32_t val, uint64_t lsn) { result += val; };

    // 1. LSN 1 arrives (a hole is created at LSN 0)
    EXPECT_TRUE(hp.insert(1, 100));
    hp.flush_ready(reducer);
    EXPECT_EQ(result, 0); // Cannot run yet because LSN 0 is missing

    // 2. LSN 0 arrives (hole is filled)
    EXPECT_TRUE(hp.insert(0, 50));
    hp.flush_ready(reducer);
    EXPECT_EQ(result, 150); // Now both run in order
}

TEST(ChipCore, HolePuncherLateArrival) {
    hole_puncher<uint32_t, 16> hp;

    hp.insert(0, 10);
    hp.flush_ready([](auto, auto) {}); // expected_lsn is now 1

    // The past cannot be overwritten.
    // LSN 0 has already been committed; a new LSN 0 must be rejected.
    EXPECT_FALSE(hp.insert(0, 20));
}

// ============================================================
// 4. VECTOR LANE TESTS (SIMD Determinism)
// ============================================================
TEST(ChipCore, DeterministicVectorFlow) {
    slabflux::compute::vector_lane_256<64> engine1;
    slabflux::compute::vector_lane_256<64> engine2;

    // Initialize with zero
    for (int i = 0; i < 64; ++i) {
        engine1.states[i] = 0.0f;
        engine2.states[i] = 0.0f;
    }

    // Same stimulus (Essence)
    float signal = 42.0f;
    uint64_t lsn = 100;

    engine1.propagate(signal, lsn);
    engine2.propagate(signal, lsn);

    // Absolute determinism at the hardware level.
    // The state of the two independent engines must match bit‑for‑bit.
    for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(engine1.states[i], engine2.states[i]);
    }
}
