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
 * ============================================================================* @brief SLABFLUX - HugePage Allocator Physical Residency
 */

#include <gtest/gtest.h>
#include "slabflux/core/hugepage_allocator.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/platform/os.hpp"
#include <fstream>
#include <numa.h>
#include <sys/mman.h>

using namespace slabflux::core;

TEST(HugepageAllocatorTest, PhysicalAlignmentAndBinding) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Run: sudo sysctl -w vm.nr_hugepages=512";
    }

    struct alignas(64) huge_blob {
        uint64_t data[1024];
    };

    // Attempt allocation on Node 0 (if available, otherwise fallback)
    int target_node = (numa_available() >= 0) ? 0 : -1;
    
    huge_blob* ptr = allocate_huge_pinned<huge_blob, 1024>(target_node);
    
    ASSERT_NE(ptr, nullptr);

    // Verify 2MB page alignment
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(addr % (2 * 1024 * 1024), 0);

    // Cleanup (Since allocate_huge_pinned uses mmap, we munmap it)
    size_t size = 1024 * sizeof(huge_blob);
    size = (size + (2 * 1024 * 1024) - 1) & ~((2 * 1024 * 1024) - 1);
    ::munmap(ptr, size);
    ::munmap(ptr, size); // Qualified
}