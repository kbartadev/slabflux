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
 * ============================================================================*/

#pragma once

#include <cstddef>
#include <new>

namespace slabflux::core {

// ============================================================
// PHYSICAL CONSTANTS
// Fixing hardware limits for the sake of determinism.
// ============================================================

/** @brief L1 Cache Line Size. Hardcoded to 64 bytes to ensure ABI stability 
 *  across different compiler tuning flags and to prevent MESI thrashing. */
constexpr std::size_t CACHE_LINE_SIZE = 64;

/** @brief Cache-line padding wrapper
 *  Wraps any data so it occupies its own cache line. */
template <typename T>
struct alignas(CACHE_LINE_SIZE) padded_wrapper {
    T data;
    // The compiler automatically pads the rest to 64 bytes.
};

/** @brief NUMA-aware allocator (principle)
 *  SLABFLUX Core performs all memory allocations on the physical CPU socket
 *  where the given thread is running. */
struct numa_allocator {
    static void* allocate(size_t size, int node_id) {
        // OS-specific calls go here:
        // Linux: numa_alloc_onnode()
        // Windows: VirtualAllocExNuma()
        // This ensures the “Practical Limit” is not violated due to memory latency.
        return ::operator new(size);  // Current fallback until libnuma is used
    }

    static void deallocate(void* ptr, size_t size) { ::operator delete(ptr); }
};

}  // namespace slabflux::core
