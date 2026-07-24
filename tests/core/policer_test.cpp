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
 * ============================================================================* @brief SLABFLUX - Ingress Policer Audit
 */

#include <gtest/gtest.h>
#include "slabflux/core/deterministic_policer.hpp"

using namespace slabflux::core;

TEST(PolicerTest, TokenBucketPhysics) {
    // Rate: 10 tokens per tick, Burst: 20
    ingress_policer policer(10, 20);
    
    // Requirement: Starts at 0
    EXPECT_FALSE(policer.allow(1));
    
    // Replenish 1 tick
    policer.replenish();
    
    // Verify consumption
    EXPECT_TRUE(policer.allow(5));
    EXPECT_TRUE(policer.allow(5));
    EXPECT_FALSE(policer.allow(1)); // Exhausted
    
    // Verify burst limit
    for(int i=0; i<5; ++i) policer.replenish();
    
    // Should be capped at 20 despite 5 replenishments
    EXPECT_TRUE(policer.allow(20));
    EXPECT_FALSE(policer.allow(1));
}

TEST(PolicerTest, ContentionStability) {
    ingress_policer policer(1000, 1000);
    policer.replenish();
    
    // Rapid scalar consumption
    for(int i=0; i<1000; ++i) EXPECT_TRUE(policer.allow(1));
    EXPECT_FALSE(policer.allow(1));
}