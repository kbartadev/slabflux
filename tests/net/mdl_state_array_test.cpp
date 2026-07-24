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
#include <cstdint>
#include <cstring>

// Include the MDL State Array (The 5th Pillar of the Quintipartite Defense)
#include "slabflux/compute/mdl_state_array.hpp"

namespace slabflux::test {

    // =====================================================================
    // Minkowski Data Lattice (MDL) Tests
    // =====================================================================

    class MdlStateArrayTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // MDL relies on AVX-512VL instructions (e.g., _mm256_maskz_mov_epi32)
            if (!__builtin_cpu_supports("avx512vl")) {
                GTEST_SKIP() << "Skipping MDL tests: AVX-512VL not supported by host silicon.";
            }
        }
    };

    TEST_F(MdlStateArrayTest, IntactLightCone) {
        slabflux::compute::mdl_state_array<float, 16> state;
        uint64_t active_lsn = 8888;

        // Write data, permanently anchoring it to the temporal clock
        state.write_sealed(0, 1.234f, active_lsn);
        state.write_sealed(7, 5.678f, active_lsn);

        // Valid reads directly on the Light-Cone should extract exact values
        EXPECT_FLOAT_EQ(state.read_subsumed(0, active_lsn), 1.234f);
        EXPECT_FLOAT_EQ(state.read_subsumed(7, active_lsn), 5.678f);
    }

    TEST_F(MdlStateArrayTest, SpatialCorruptionSubsumption) {
        slabflux::compute::mdl_state_array<float, 16> state;
        uint64_t active_lsn = 8888;

        state.write_sealed(3, 99.99f, active_lsn);

        // Simulate a physical DIMM bit-flip (Rowhammer or Cosmic Ray)
        auto* raw_mem = reinterpret_cast<uint8_t*>(&state);
        size_t target_offset = 3 * 64; // 3rd lattice envelope (64 bytes each)
        raw_mem[target_offset + 1] ^= 0x40; // Force a bit-flip in the payload

        // The geometric interval s^2 != 0. The hardware zeroes the lane.
        EXPECT_FLOAT_EQ(state.read_subsumed(3, active_lsn), 0.0f);
    }

    TEST_F(MdlStateArrayTest, TemporalCorruptionSubsumption) {
        slabflux::compute::mdl_state_array<float, 16> state;
        uint64_t active_lsn = 8888;

        state.write_sealed(4, 42.42f, active_lsn);

        // Simulate an asynchronous UAF thread reading with a desynchronized clock
        uint64_t desynced_lsn = 8889;

        // The temporal coordinate fractures the interval. The hardware zeroes the lane.
        EXPECT_FLOAT_EQ(state.read_subsumed(4, desynced_lsn), 0.0f);
    }
}
