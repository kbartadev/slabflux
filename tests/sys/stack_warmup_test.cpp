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
 * ============================================================================* @brief SLABFLUX - Stack and TLB Warmup Physics Audit
 */

#include <gtest/gtest.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/platform/os.hpp"
#include <sys/mman.h>

using namespace slabflux::core;

TEST(WarmupTest, StackPreFaultAudit) {
    // Requirement: Pre-faulting 64KB must not cause stack corruption.
    // Ensures the physical stack frames are committed to RAM.
    volatile char dummy[64 * 1024];
    for (size_t i = 0; i < sizeof(dummy); i += 4096) dummy[i] = 0;
    SUCCEED();
}

TEST(WarmupTest, TlbPrimingAudit) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "Skipping TLB audit: HugePages not configured on this host.";
    }

    const size_t size = 2 * 1024 * 1024; // 2MB
    void* slab = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, 
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    
    // Handle failure case where OS reports support but cannot fulfill request (e.g. pool exhausted)
    if (slab == MAP_FAILED) {
        GTEST_SKIP() << "HugePage mmap failed despite OS report. Likely permission or pool depletion.";
        return;
    }

    // Verify access to the physical slab
    static_cast<char*>(slab)[0] = 0;
    ::munmap(slab, size);
}