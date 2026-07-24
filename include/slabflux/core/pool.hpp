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
#include <concepts>
#include "mpmc_pool.hpp"
#include "mpsc_pool.hpp"
#include "mpsc_hybrid_pool.hpp"
#include "spsc_ring_pool.hpp"
#include "local_pool.hpp"
#include "spsc_pool.hpp"

namespace slabflux::core {

    /**
     * @brief Memory Resource Invariant.
     * @details Defines the structural requirements for SlabFlux memory pools 
     * to ensure deterministic, O(1) resource lifecycle management.
     */
    template <typename P, typename T>
    concept MemoryPool = requires(P& p, T* ptr, T** batch, size_t count, uint32_t id) {
        // Allocation
        { p.make_raw() } -> std::same_as<T*>;
        { p.make_batch(batch, count) } -> std::convertible_to<std::size_t>;

        // Reclamation
        { p.release(ptr) } -> std::same_as<void>;
        { p.release_batch(batch, count) } -> std::same_as<void>;

        // Addressing & Topology
        { p.capacity() } -> std::convertible_to<std::size_t>;
        { p.get_raw_ptr_by_id(id) } -> std::same_as<T*>;
    };

    /**
     * ========================================================================
     * THE POOL API CONTRACT
     * ========================================================================
     * All memory pools in SLABFLUX (Local, MPMC, MPSC, SPSC) adhere to a 
     * unified behavioral interface for high-frequency resource management:
     * 
     * 1. ALLOCATION (Hot Path):
     *    - make_raw(...Args): Direct placement-new into pinned memory. Returns T*.
     *    - make(...Args)    : Returns managed_data<T> for automated reclamation.
     *    - DETERMINISM: All allocations are O(1), wait-free, and utilize dual-step prefetching.
     * 
     * 2. RECLAMATION (Lifecycle):
     *    - release(T*)      : Returns memory to the pool via amortized return rings (MAX 32/cycle).
     *    - release(scoped_ptr<T>&): Safe ownership collapse.
     * 
     * 3. MICRO-ARCHITECTURAL ALIGNMENT:
     *    - All pools utilize cache-line isolation (64-byte alignment) to prevent 
     *      false sharing during concurrent cross-thread returns.
     */

    /* Note: individual pool types (local_pool, spsc_pool, mpmc_pool, mpsc_pool)
     * are now defined directly in their respective headers to ensure 
     * filename and class name parity.
     */

    // ========================================================================
    // POOL ALIAS
    // ========================================================================
    template <typename Event, std::size_t Capacity>
    using pool = mpmc_pool<Event, Capacity>;

} // namespace slabflux::core
