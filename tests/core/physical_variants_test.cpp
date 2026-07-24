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
 * ============================================================================*
 * @file physical_variants.cpp
 * @brief Audits the physical memory layout of core structures to ensure
 * register-level determinism and cache-line residency.
 */

#include <gtest/gtest.h>
#include "slabflux/compute/vector_lane_512.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::compute;

// tests/core/test_physical_invariants.cpp
TEST(PhysicalLayer, CacheLineIntegrity) {
    // Every critical structure must be a multiple of 64 bytes
    static_assert(sizeof(vector_lane_512<64>) % 64 == 0, "Cache line split detected!"); 
    static_assert(alignof(vector_lane_512<64>) == 64, "Alignment violation!");
}

TEST(PhysicalLayer, NUMA_Affinity_Verification) {
    auto* ptr = slabflux::core::hardware_topology::allocate_on_local_node(1024 * 1024);
    int cpu = sched_getcpu();
    int expected_node = numa_node_of_cpu(cpu);
    
    int actual_node = -1;
    get_mempolicy(&actual_node, NULL, 0, ptr, MPOL_F_ADDR | MPOL_F_NODE);
    EXPECT_EQ(actual_node, expected_node) << "Memory allocated on wrong NUMA node!";
}