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
 * ============================================================================* @file orthogonal_arbiter_test.cpp
 * @brief Unit tests for the Gödel-numbered Orthogonal Error Arbiter.
 */

#include <gtest/gtest.h>
#include "slabflux/rte/orthogonal_error_arbiter.hpp"

using namespace slabflux::rte;

TEST(OrthogonalArbiterTest, TopologyMathIsomorphism) {
    // Net Timeout (3 * 11) is a subset of Base Network (3)
    EXPECT_TRUE(error_topology::is_a(error_topology::net_timeout, error_topology::base_network));
    
    // Net Timeout is NOT a subset of Base Hardware (2)
    EXPECT_FALSE(error_topology::is_a(error_topology::net_timeout, error_topology::base_hardware));
}

TEST(OrthogonalArbiterTest, RecordAndDestructiveHarvest) {
    orthogonal_error_arbiter<1024> arbiter;
    uint32_t magnitude = 0;
    
    // Initially empty
    EXPECT_FALSE(arbiter.try_harvest(error_topology::hw_thermal, magnitude));
    
    // Wait-free record
    arbiter.record(error_topology::hw_thermal, 85);
    
    // Destructive Harvest (Consume to Zero)
    EXPECT_TRUE(arbiter.try_harvest(error_topology::hw_thermal, magnitude));
    EXPECT_EQ(magnitude, 85);
    
    // Second read should be empty
    EXPECT_FALSE(arbiter.try_harvest(error_topology::hw_thermal, magnitude));
}

TEST(OrthogonalArbiterTest, OOMImmuneSubsumption) {
    orthogonal_error_arbiter<1024> arbiter;
    
    // Simulate extreme error storm
    for(int i = 1; i <= 10000; ++i) {
        arbiter.record(error_topology::logic_nan, i);
    }
    
    uint32_t magnitude = 0;
    EXPECT_TRUE(arbiter.try_harvest(error_topology::logic_nan, magnitude));
    
    // Subsumption property guarantees the latest magnitude overwrote the previous ones without backpressure
    EXPECT_EQ(magnitude, 10000); 
}