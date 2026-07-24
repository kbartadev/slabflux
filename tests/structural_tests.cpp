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
 * @file structural_tests.cpp
 * @brief tatic and dynamic alignment verification.
 */
#include <gtest/gtest.h>
#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/compute/vector_lane_256.hpp"

#if defined(_WIN32) || defined(_WIN64)
#include <stdlib.h>
 // Windows NUMA mocks, so the tests can compile and run on any platform without NUMA support.
#define MPOL_MF_MOVE 0
inline int sched_getcpu() { return 0; }
inline int numa_node_of_cpu(int) { return 0; }
inline long move_pages(int, unsigned long, void**, const int*, int*, int) { return 0; }

namespace hardware_topology {
    inline void* allocate_on_local_node(size_t size) { return malloc(size); }
}
#else
    #include <numa.h>
    #include <numaif.h>
    #include <sched.h>
    #include "slabflux/core/hardware_topology.hpp"
    using hardware_topology = slabflux::core::hardware_topology;
#endif

using namespace slabflux::core;

TEST(Structural, MemoryAlignmentInvariants) {
    // Structural Honesty.
    // Data must map 1:1 to cache lines (64 bytes).
    EXPECT_EQ(alignof(slabflux::compute::vector_lane_256<64>), 64);
    EXPECT_EQ(sizeof(slabflux::compute::vector_lane_256<64>) % 64, 0);

    // Verify wire frames align seamlessly to L1 cache lines.
    // Padded to 64 bytes for strict cache-line isolation.
    struct payload { float a; float b; };
    // Header(2+2+8) + Payload(4+4) = 20 bytes -> padded to 64.
    EXPECT_EQ(sizeof(wire_frame_lsn<payload>), 64);
}

TEST(Structural, NUMALocalityVerification) {
    const size_t PAGE_SIZE = 4096;
    void* ptr = hardware_topology::allocate_on_local_node(PAGE_SIZE);
    ASSERT_NE(ptr, nullptr);

    // 1. Forced touch (First-Touch Policy)
    volatile char* vptr = static_cast<volatile char*>(ptr);
    *vptr = 0xAA;

    // 2. CRITICAL: Page alignment!
    // move_pages only accepts pointers aligned to page boundaries.
    // Mask out the lower 12 bits (for 4KB pages) to get the exact page start.
    uintptr_t aligned_addr = reinterpret_cast<uintptr_t>(ptr) & ~(PAGE_SIZE - 1);
    void* page_aligned_ptr = reinterpret_cast<void*>(aligned_addr);

    int node = -1;
    void* ptr_array[1] = { page_aligned_ptr };
    int status_array[1] = { -1 };

    // 3. System call
    long res = move_pages(0, 1, ptr_array, NULL, status_array, MPOL_MF_MOVE);
    ASSERT_EQ(res, 0) << "move_pages system call failed!";

    node = status_array[0];
    int current_cpu_node = numa_node_of_cpu(sched_getcpu());

    // 4. Windows polyfill protection
    // If MSVC is compiling and our move_pages wrapper is a no-op stub that leaves -1,
    // skip the strict validation instead of failing the test.
#ifdef _MSC_VER
    if (node == -1) {
        GTEST_SKIP() << "move_pages polyfill returned -1 on Windows native. Skipping strict NUMA check.";
    }
#endif

    // On Linux or with a correct wrapper, the values must match
    EXPECT_EQ(node, current_cpu_node)
        << "Memory is on node " << node
        << " but CPU is on node " << current_cpu_node;
}
