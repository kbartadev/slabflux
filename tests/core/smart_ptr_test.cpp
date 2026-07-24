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
 * ============================================================================* @brief SLABFLUX - Smart Pointer Ownership Audit
 */

#include <gtest/gtest.h>
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/core/mpmc_pool.hpp"

using namespace slabflux::core;

struct pod_event { int x; };

TEST(SmartPtrTest, ScopedPtrLifecycle) {
    int release_called = 0;
    auto deleter = [](void* ctx, void*) { (*static_cast<int*>(ctx))++; };
    
    pod_event ev{42};
    {
        scoped_ptr<pod_event> ptr(&ev, deleter, &release_called);
        EXPECT_EQ(ptr->x, 42);
        
        // Move semantics
        auto ptr2 = std::move(ptr);
        EXPECT_FALSE(ptr);
        EXPECT_TRUE(ptr2);
        EXPECT_EQ(release_called, 0);
    }
    
    // Destructor must trigger release
    EXPECT_EQ(release_called, 1);
}

TEST(SmartPtrTest, ManagedDataExhaustionRecovery) {
    // Use 1 Lane to ensure strict capacity enforcement for exhaustion tests.
    mpmc_pool<pod_event, 2, 1> pool;
    
    {
        auto m1 = pool.make();
        auto m2 = pool.make();
        
        // Pool is now empty
        auto m3 = pool.make();
        EXPECT_FALSE(m3);
    } 
    
    // RAII returned items, allocation should work again
    EXPECT_TRUE(pool.make());
}