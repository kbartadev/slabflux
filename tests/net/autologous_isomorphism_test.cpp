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
#include "slabflux/net/autologous_isomorphism.hpp"

using namespace slabflux::net;

class AutologousIsomorphismTest : public ::testing::Test {
protected:
    void SetUp() override {
        // ACI requires AVX-512 Conflict Detection Instructions (CD)
        if (!__builtin_cpu_supports("avx512f") || !__builtin_cpu_supports("avx512cd")) {
            GTEST_SKIP() << "Skipping ACI tests: AVX-512CD (Conflict Detection) not supported by host silicon.";
        }
    }
};

TEST_F(AutologousIsomorphismTest, ValidIdentity) {
    uint64_t dummy_payload = 0xDEADBEEF;
    uint32_t expected_type_id = 5;
    uint32_t sequence_clock = 1024;

    autologous_isomorphism<uint64_t*> aci(expected_type_id, &dummy_payload);
    aci.embed_symmetry(sequence_clock);

    auto [type_id, payload] = aci.extract_and_decouple(sequence_clock);
    
    EXPECT_EQ(type_id, expected_type_id) << "Valid identity was decoupled!";
    EXPECT_EQ(payload, &dummy_payload) << "Payload pointer corrupted!";
}

TEST_F(AutologousIsomorphismTest, OntologicalDecoupling) {
    uint64_t dummy_payload = 0xCAFEBABE;
    autologous_isomorphism<uint64_t*> aci(3, &dummy_payload);
    aci.embed_symmetry(2048);

    // Simulate sequence mismatch (e.g., memory was overwritten by another thread)
    auto [type_id, payload] = aci.extract_and_decouple(2049);
    
    EXPECT_EQ(type_id, 0) << "Corrupted identity bypassed Ontological Decoupling (VPCONFLICTD failed)!";
}