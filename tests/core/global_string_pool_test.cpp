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
 * ============================================================================* @brief SLABFLUX - Global String Pool Matrix Audit
 */

#include <gtest/gtest.h>
#include "slabflux/core/global_string_pool.hpp"

using namespace slabflux::core;

TEST(GlobalStringPoolTest, MatrixInitializationAndAllocation) {
    // Matrix capacity 1024 chunks
    string_matrix_pool<1024, string_concurrency::mpmc> pool;

    // Requirement: Thread-safe lazy or explicit initialization
    pool.initialize(1024);

    // Allocation: O(1) index-to-pointer math
    string_chunk* c1 = pool.make_raw();
    ASSERT_NE(c1, nullptr);

    // Reclamation: Return chunk to the versioned stack
    pool.release(c1);

    // Verify LIFO property (hot cache line reuse)
    string_chunk* c2 = pool.make_raw();
    EXPECT_EQ(c1, c2);

    pool.release(c2);
}

TEST(GlobalStringPoolTest, BatchSymmetry) {
    string_matrix_pool<128, string_concurrency::mpmc> pool;
    pool.initialize(128);

    string_chunk* batch[16];
    size_t count = pool.make_batch(batch, 16);
    EXPECT_EQ(count, 16);

    pool.release_batch(batch, 16);
}

TEST(GlobalStringPoolTest, MpmcModeInvariants) {
    mpmc_matrix_pool<128> pool;
    pool.initialize();

    auto* c1 = pool.make_raw();
    ASSERT_NE(c1, nullptr);
    
    string_chunk* batch[10];
    size_t n = pool.make_batch(batch, 10);
    EXPECT_EQ(n, 10);

    pool.release(c1);
    pool.release_batch(batch, 10);
}

TEST(GlobalStringPoolTest, SpscModeEfficiency) {
    spsc_matrix_pool<64> pool;
    pool.initialize();

    // In SPSC mode, indices should be returned in strict LIFO order
    auto* c1 = pool.make_raw();
    uint32_t idx1 = pool.get_index(c1);
    
    auto* c2 = pool.make_raw();
    uint32_t idx2 = pool.get_index(c2);
    
    EXPECT_NE(idx1, idx2);

    pool.release(c1);
    auto* c3 = pool.make_raw();
    EXPECT_EQ(c1, c3); // Immediate reuse
}

TEST(GlobalStringPoolTest, LazyInitSafety) {
    default_string_pool pool;
    // We skip explicit .initialize()
    
    auto* chunk = pool.make_raw();
    EXPECT_NE(chunk, nullptr);
    pool.release(chunk);
}
