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
 */
#pragma once
#include <sys/mman.h>
#include <numaif.h>
#include <cstddef>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/sys/tier_guard.hpp"

namespace slabflux::sys {

/**
 * @brief HugePage Allocator.
 * @details Enforces 2MB alignment and physical pinning (mlock).
 */
template<typename T>
class hugepage_allocator {
public:
    static constexpr size_t HUGEPAGE_SIZE = 2 * 1024 * 1024; // 2MB Standard

    T* allocate(size_t count, int numa_node = -1) {
        if (count == 0) [[unlikely]] return nullptr;

        size_t bytes = count * sizeof(T);
        // Align to 2MB HugePage boundary
        size_t aligned_size = (bytes + HUGEPAGE_SIZE - 1) & ~(HUGEPAGE_SIZE - 1);

        void* ptr = ::mmap(nullptr, aligned_size, 
                           PROT_READ | PROT_WRITE, 
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_LOCKED, 
                           -1, 0);

        if (ptr == MAP_FAILED) return nullptr;

        // Hardening: Strict NUMA binding
        if (numa_node >= 0) {
            unsigned long nodemask = (1UL << numa_node);
            if (::mbind(ptr, aligned_size, MPOL_BIND, &nodemask, sizeof(nodemask) * 8, MPOL_MF_STRICT) != 0) {
                ::munmap(ptr, aligned_size);
                slabflux::core::handle_critical_error("Physical NUMA binding violation: Residency required but unavailable.");
            }

            // Proactively enforce DRAM-only residency (prevents silent spilling 
            // into far-memory/CXL pools during unexpected memory pressure).
            tier_guard::enforce_dram_locality(ptr, aligned_size);
        }

        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, size_t count = 0) {
        if (!ptr) return;
        const size_t bytes = count * sizeof(T);
        const size_t aligned_size = (bytes + HUGEPAGE_SIZE - 1) & ~(HUGEPAGE_SIZE - 1);
        ::munmap(ptr, aligned_size);
    }
};

} // namespace slabflux::sys
