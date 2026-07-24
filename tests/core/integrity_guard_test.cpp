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
 * ============================================================================* @brief SLABFLUX - Integrity and Ownership Audit
 */

#include <gtest/gtest.h>
#include "slabflux/core/integrity_guard.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/mpmc_pool.hpp"

using namespace slabflux::core;

TEST(IntegrityGuardTest, CanaryVerification) {
    integrity_wrapper<uint64_t> wrapper;
    wrapper.head_canary = 0xCAFEBABECAFEBABEULL;
    wrapper.payload = 12345;
    wrapper.tail_canary = 0xDEADBEEFDEADBEEFULL;

    EXPECT_TRUE(verify_canary(&wrapper.head_canary, 0xCAFEBABECAFEBABEULL));
    EXPECT_TRUE(verify_canary(&wrapper.tail_canary, 0xDEADBEEFDEADBEEFULL));
    
    wrapper.head_canary = 0;
    EXPECT_FALSE(verify_canary(&wrapper.head_canary, 0xCAFEBABECAFEBABEULL));
}

TEST(ManagedDataTest, MoveAndAutoRelease) {
    mpmc_pool<uint64_t, 16> pool;
    uint64_t* raw_ptr = nullptr;

    {
        auto managed = pool.make(1337ULL);
        raw_ptr = managed.get();
        ASSERT_NE(raw_ptr, nullptr);
        EXPECT_EQ(*managed, 1337ULL);

        auto moved = std::move(managed);
        EXPECT_EQ(managed.get(), nullptr);
        EXPECT_EQ(moved.get(), raw_ptr);
    } // 'moved' goes out of scope, item returned to pool

    // Re-allocate should succeed
    auto realloc = pool.make(1ULL);
    EXPECT_TRUE(realloc);
}