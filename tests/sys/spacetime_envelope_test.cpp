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
#include <cstring>
#include <cstdint>
#include "slabflux/sys/spacetime_envelope.hpp"

using namespace slabflux::sys;

TEST(SpacetimeEnvelopeTest, LorentzSubsumption) {
    uint32_t original_data = 42;
    uint64_t current_lsn = 100;
    
    spacetime_envelope<uint32_t> lattice(original_data);

    // Seal data into the lattice across the active sequence horizon
    lattice.anchor_to_lightcone(current_lsn);

    // Read with the correct temporal coordinate
    uint32_t extracted = lattice.extract_via_subsumption(current_lsn);
    EXPECT_EQ(extracted, original_data) << "Data corrupted under valid spacetime coordinates!";
}

TEST(SpacetimeEnvelopeTest, TemporalParadox) {
    spacetime_envelope<uint32_t> lattice(0xDEADBEEF);
    lattice.anchor_to_lightcone(100);

    // Attempt to read with a future sequence number
    uint32_t extracted = lattice.extract_via_subsumption(101);
    EXPECT_EQ(extracted, 0) << "Algebraic Atony failed: Invalid temporal extraction did not yield absolute zero!";
}

TEST(SpacetimeEnvelopeTest, SpatialCorruption) {
    spacetime_envelope<uint32_t> lattice(0xCAFEBABE);
    lattice.anchor_to_lightcone(200);

    // Simulate physical RAM corruption by directly overwriting the entangled object footprint
    uint8_t* raw_mem = reinterpret_cast<uint8_t*>(&lattice);
    raw_mem[0] ^= 0xFF; // Flip bits in the entangled spatial dimension

    uint32_t extracted = lattice.extract_via_subsumption(200);
    EXPECT_EQ(extracted, 0) << "Geometric Entanglement failed: Corrupted spatial data bypassed the Light-Cone boundary!";
}

TEST(SpacetimeEnvelopeTest, Sub32ByteStackSafety) {
    uint64_t small_data = 0x123456789ABCDEF0;
    
    spacetime_envelope<uint64_t> lattice(small_data);
    lattice.anchor_to_lightcone(300);
    
    uint64_t extracted = lattice.extract_via_subsumption(300);
    EXPECT_EQ(extracted, small_data) << "8-byte payload corrupted or stack smashed during extraction!";
}