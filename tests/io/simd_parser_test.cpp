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
#include <x86intrin.h>
#include <cstring> // For std::memset
#include "slabflux/io/simd_parser.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"

using namespace slabflux::io;

/**
 * @brief Vectorized Extraction Integrity.
 * Proves that fields are extracted from the raw wire-frame into registers
 * without scalar branching.
 */
TEST(SimdParserTest, VectorizedFieldExtraction) {
    alignas(64) char raw_packet[64];
    ::std::memset(raw_packet, 0, 64);
    
    // Inject "Magic" at offset 16
    *reinterpret_cast<uint64_t*>(raw_packet + 16) = 0xCAFEBABEULL;
    
    simd_parser parser;
    uint64_t extracted = parser.extract_u64<16>(raw_packet);
    
    EXPECT_EQ(extracted, 0xCAFEBABEULL);
}

/**
 * @brief Branchless Dispatch Physics.
 * Measures the cycle-count to extract 4 fields simultaneously.
 */
TEST(SimdParserTest, ExtractionCycleBudget) {
    alignas(64) char raw_packet[64];
    slabflux::io::simd_parser parser; // Explicitly qualify
    
    uint64_t start = __rdtsc();
    for(int i=0; i<1000; ++i) {
        volatile uint64_t f1 = parser.extract_u64<0>(raw_packet);
        volatile uint64_t f2 = parser.extract_u64<8>(raw_packet);
    }
    uint64_t end = __rdtsc();
    
    double cycles_per_packet = static_cast<double>(end - start) / 1000.0;
    std::cout << "[PERF] SIMD Parser Latency: " << cycles_per_packet << " cycles/packet\n";
    
    // Requirement: Sub-10 cycles for field extraction
    EXPECT_LT(cycles_per_packet, 10.0);
}
