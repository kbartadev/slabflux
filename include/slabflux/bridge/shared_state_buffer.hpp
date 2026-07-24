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
#include <numa.h>
#include <sched.h>
#include <atomic>

namespace slabflux::bridge {

/**
 * @brief Zero-copy state container shared between the Core and the Engine.
 */
template<typename T, size_t LaneCount>
struct alignas(64) shared_state_slab {
    static_assert((LaneCount & (LaneCount - 1)) == 0, "LaneCount must be a power of 2 for masked index computations");
    static constexpr size_t MASK = LaneCount - 1;

    // The "Truth" calculated by the Chip (AVX-512 lanes)
    T states[LaneCount];
    
    // The last LSN that was successfully retired to this slab
    std::atomic<uint64_t> published_lsn{0};

    /**
     * @brief Invariant ring buffer index resolution.
     * @details Replaces standard modulo logic with constexpr masked arithmetic 
     * to ensure O(1) index projection without hardware division overhead.
     */
    [[nodiscard]] static constexpr size_t index_of(uint64_t sequence) noexcept {
        return static_cast<size_t>(sequence & MASK);
    }

    static shared_state_slab* create() {
        int node = numa_node_of_cpu(sched_getcpu());
        void* ptr = numa_alloc_onnode(sizeof(shared_state_slab), node);
        return new (ptr) shared_state_slab();
    }
};

} // namespace slabflux::bridge
