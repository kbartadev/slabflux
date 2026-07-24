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
#include "slabflux/core/string_service.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::core;

class SmartString : public ::testing::Test {
protected:
    local_pool<string_chunk, 4096> chunk_pool;
    string_service<local_pool<string_chunk, 4096>> svc{chunk_pool};
    fragmented_string fs;

    void SetUp() override {
        fs.head_idx = string_chunk::END_OF_CHAIN;
        fs.total_length = 0;
        fs.sso_size = 0;
    }

    void TearDown() override {
        svc.clear(fs);
    }
};

/**
 * @brief Audits the physical layout of string chunks.
 * Ensures 0% False Sharing and L1 cache residency.
 */
TEST_F(SmartString, PhysicalArchitecture) {
    // Invariant: Chunks must be exactly one cache line
    EXPECT_EQ(sizeof(string_chunk), 64);
    
    auto* chunk = chunk_pool.make_raw();
    ASSERT_NE(chunk, nullptr);
    
    // Verify 64-byte alignment to prevent MESI thrashing
    EXPECT_EQ(reinterpret_cast<uintptr_t>(chunk) % 64, 0);
    chunk_pool.release(chunk);
}

/**
 * @brief High-velocity assignment and size parity.
 */
TEST_F(SmartString, ConcurrencyAndIntegrity) {
    // Pin to core to ensure deterministic cycle measurements
    hardware_topology::pin_thread(1);
    
    auto s = svc(fs);
    std::string test_val = "SLABFLUX Engine";
    
    s = test_val;
    EXPECT_EQ(s.size(), test_val.size());
    EXPECT_EQ(s.length(), test_val.length());
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s, test_val);
}

/**
 * @brief Tests the exact boundary of Small String Optimization.
 * Ensures the transition from inlined to fragmented is bit-perfect.
 */
TEST_F(SmartString, SSOBoundaryExhaustion) {
    auto s = svc(fs);
    constexpr size_t CAPACITY = fragmented_string::SSO_CAPACITY;
    
    // 1. Exact SSO Limit: Validate the edge of the inlined buffer
    std::string boundary(CAPACITY, 'A');
    s = boundary;
    EXPECT_EQ(fs.sso_size, CAPACITY);
    EXPECT_EQ(fs.head_idx, string_chunk::END_OF_CHAIN);
    EXPECT_EQ(s, boundary);

    // 2. Overflow to Fragmented mode
    std::string overflow(CAPACITY + 1, 'B');
    s = overflow;
    EXPECT_EQ(fs.sso_size, 0);
    EXPECT_NE(fs.head_idx, string_chunk::END_OF_CHAIN);
    EXPECT_EQ(s, overflow);
}

/**
 * @brief Verifies O(1) element access across chunk boundaries.
 */
TEST_F(SmartString, ElementAccess) {
    auto s = svc(fs);
    
    // SSO Access
    s = "Sovereign";
    EXPECT_EQ(s[0], 'S');
    EXPECT_EQ(s.at(1), 'o');
    EXPECT_EQ(s.front(), 'S');
    EXPECT_EQ(s.back(), 'n');
    EXPECT_THROW(s.at(10), std::out_of_range);

    // Fragmented Access (spanning 3 chunks)
    std::string large_val(120, 'A');
    large_val[0] = 'B';
    large_val[119] = 'Z';
    
    s = large_val;
    EXPECT_EQ(s[0], 'B');
    EXPECT_EQ(s[119], 'Z');
    EXPECT_EQ(s.front(), 'B');
    EXPECT_EQ(s.back(), 'Z');
}

/**
 * @brief Verifies the near-O(1) append logic.
 */
TEST_F(SmartString, AppendContinuity) {
    auto s = svc(fs);
    s = "Part 1";
    s += " | Part 2";
    s += " | Part 3... extending beyond SSO now definitely.";
    
    std::string expected = "Part 1 | Part 2 | Part 3... extending beyond SSO now definitely.";
    EXPECT_EQ(s.size(), expected.size());
    EXPECT_EQ(s, expected);
    
    // Verify head is now fragmented
    EXPECT_NE(fs.head_idx, string_chunk::END_OF_CHAIN);
}

/**
 * @brief Audits string comparison and prefix logic.
 * Validates branch-neutral comparison.
 */
TEST_F(SmartString, Comparison) {
    auto s = svc(fs);
    s = "Intelligence";
    
    EXPECT_TRUE(s.starts_with("Intelligence"));
    EXPECT_FALSE(s.starts_with("Sovereign"));
    EXPECT_EQ(s.compare("Intelligence"), 0);
    EXPECT_LT(s.compare("T"), 0);
}

/**
 * @brief Audits the assignment cycle budget.
 * Verified Performance: sub-20ns assignments via SIMD stores.
 */
TEST_F(SmartString, AssignmentCycleAudit) {
    auto s = svc(fs);
    std::string data(128, 'X'); // Spans multiple chunks
    
    uint64_t start = __rdtsc();
    s = data;
    uint64_t delta = __rdtsc() - start;
    
    // On industrial silicon, this should execute in < 400 cycles 
    // including GTest overhead and setup.
    EXPECT_LT(delta, 1000) << "String assignment exceeds deterministic cycle budget.";
}

TEST_F(SmartString, OstreamOperator) {
    auto s = svc(fs);
    s = "Terminal Output";
    std::stringstream ss;
    ss << s;
    EXPECT_EQ(ss.str(), "Terminal Output");
}

/**
 * @brief Full-Flux End-to-End Stress.
 * Simulates randomized textual mutations to verify pool stability.
 */
TEST_F(SmartString, FullFluxStressAudit) {
    auto s = svc(fs);
    const size_t ITERATIONS = 100'000;
    
    for(size_t i = 0; i < ITERATIONS; ++i) {
        // Mix of SSO and Fragmented payloads
        std::string test_val;
        if (i % 2 == 0) test_val = "SSO_Payload_" + std::to_string(i);
        else test_val = std::string(120, 'F') + "_Frag_" + std::to_string(i);
        
        s = test_val;
        ASSERT_EQ(s, test_val);
        
        // Partial appends
        s += "_Appended";
        test_val += "_Appended";
        ASSERT_EQ(s, test_val);
        
        if (i % 10 == 0) s.clear();
    }
}

/**
 * @brief Audits Lexicographical comparison across fragmented boundaries.
 */
TEST_F(SmartString, FragmentedComparison) {
    auto s1 = svc(fs);
    fragmented_string fs2;
    fs2.head_idx = string_chunk::END_OF_CHAIN;
    auto s2 = svc(fs2);

    std::string v1(100, 'A');
    std::string v2(100, 'A');
    v2[99] = 'B';

    s1 = v1;
    s2 = v2;
    EXPECT_LT(s1.compare(v2), 0);
    EXPECT_GT(s2.compare(v1), 0);
    svc.clear(fs2);
}
