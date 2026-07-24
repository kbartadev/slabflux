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

// Include the Autotelic Chrysalis hardware envelope
#include "slabflux/net/autotelic_chrysalis.hpp"

namespace slabflux::test {

    // Standard 64-byte bounded POD payload (Somatic Strand)
    struct alignas(64) ChrysalisTestEvent {
        uint64_t trade_id;
        double price;
        uint64_t size;
        uint64_t padding[5]; // Explicitly pad to fill the 512-bit register limit
    };

    // =====================================================================
    // Autotelic Chrysalis (BITALG Silicon Shearing) Tests
    // =====================================================================

    class AutotelicChrysalisTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // The Chrysalis relies on the specialized VPSHUFBITQMB instruction.
            // This requires the AVX-512 BITALG instruction set extension.
            if (!__builtin_cpu_supports("avx512bitalg")) {
                GTEST_SKIP() << "Skipping Autotelic Chrysalis tests: AVX-512 BITALG not supported by host silicon.";
            }
        }
    };

    TEST_F(AutotelicChrysalisTest, PerfectOblivion) {
        ChrysalisTestEvent ev{1001, 150.25, 50, {0}};
        slabflux::net::autotelic_chrysalis<ChrysalisTestEvent> chrysalis(ev);

        // Weave the payload against the temporal heartbeat
        chrysalis.weave(9999);

        // Extract the Fray using the exact same temporal heartbeat
        uint8_t fray = chrysalis.execute_silicon_shearing(9999);
        
        // If the structure is fully intact, BITALG Silicon Shearing must yield absolute zero (0x00)
        EXPECT_EQ(fray, 0x00);
        
        // Payload remains perfectly intact and accessible
        EXPECT_EQ(chrysalis.raw_strand().trade_id, 1001);
        EXPECT_DOUBLE_EQ(chrysalis.raw_strand().price, 150.25);
    }

    TEST_F(AutotelicChrysalisTest, TemporalFray) {
        ChrysalisTestEvent ev{1001, 150.25, 50, {0}};
        slabflux::net::autotelic_chrysalis<ChrysalisTestEvent> chrysalis(ev);

        chrysalis.weave(9999);

        // Attempt to validate with a desynchronized heartbeat (e.g., LSN progressed without updating queue)
        uint8_t fray = chrysalis.execute_silicon_shearing(10000); 
        
        // Fray must be non-zero, shifting the instruction pointer downstream
        EXPECT_NE(fray, 0x00);
    }

    TEST_F(AutotelicChrysalisTest, SpatialFray) {
        ChrysalisTestEvent ev{1001, 150.25, 50, {0}};
        slabflux::net::autotelic_chrysalis<ChrysalisTestEvent> chrysalis(ev);

        chrysalis.weave(9999);

        // Simulate a cosmic ray bit-flip or UAF overwrite in the shared memory
        auto* raw_memory = reinterpret_cast<uint8_t*>(&chrysalis);
        raw_memory[14] ^= 0xFF; 

        // The indexical map is broken; BITALG will pull wrong bits from the anchor
        uint8_t fray = chrysalis.execute_silicon_shearing(9999);
        EXPECT_NE(fray, 0x00);
    }
}