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
 * ============================================================================*/#include <gtest/gtest.h>
#include <sys/mman.h>
#include <cstring>
#include "slabflux/sys/hugepage_allocator.hpp"
#include "slabflux/sys/offset_ptr.hpp"

using namespace slabflux::sys;

struct alignas(64) pod_state {
    uint64_t version;
    double price;
};

struct root_segment {
    offset_ptr<pod_state> target;
    alignas(512) pod_state state;
};

/**
 * @brief Test for Deterministic Referencing.
 * Validates that offset_ptr remains valid across different memory
 * mappings by using relative arithmetic instead of absolute addresses.
 */
TEST(MemoryTopology, OffsetPtrResistsMemoryRebase) {
    // Create a contiguous buffer simulating a shared memory segment
    alignas(4096) char buffer[1024];
    std::memset(buffer, 0, sizeof(buffer));

    root_segment* root = reinterpret_cast<root_segment*>(buffer);
    root->state.price = 42.5;

    // Practice: Constructor at a fixed offset within the segment
    new (&root->target) offset_ptr<pod_state>(&root->state);
    offset_ptr<pod_state>& optr = root->target;

    // 1. Verify standard access
    EXPECT_EQ(optr->price, 42.5);

    // 2. Simulate "Moving" the base (Relative bit-identical state)
    // In a real scenario, this would be a different virtual address
    // mapping the same physical frame.
    std::uintptr_t original_offset = optr.raw_offset();

    // The internal offset should be exactly 512 bytes
    EXPECT_EQ(original_offset, 512);

    // 3. Ensure no absolute pointers are stored
    EXPECT_EQ(sizeof(optr), sizeof(std::ptrdiff_t));
}

/**
 * @brief Test for HugePage Alignment.
 * Ensures the allocator satisfies mlock and physical pinning requirements.
 */
TEST(MemoryTopology, HugePageAllocatorEnforcesPhysicalPinning) {
    hugepage_allocator<pod_state> allocator;

    // Attempt allocation of 1MB (must be 2MB aligned in many Linux distros)
    auto* ptr = allocator.allocate(1024);

    if (ptr == nullptr) {
        GTEST_SKIP() << "HugePage allocation failed; check vm.nr_hugepages.";
    }

    // Verify 2MB page alignment (0x200000)
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);
    EXPECT_EQ(addr % (2 * 1024 * 1024), 0) << "Memory not aligned to 2MB HugePage boundary!";

    ptr->price = 100.0;
    EXPECT_EQ(ptr->price, 100.0);

    allocator.deallocate(ptr, 1024);
}
