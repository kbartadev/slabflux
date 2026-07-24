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
 * @file       conduit.hpp
 * @brief      Zero-Overhead Conduit Primitives.
 * @details    Core infrastructure for deterministic O(1) dataflow.
 * Provides pipeline composition, signal multiplexing, and SIMD ring buffers.
 */

#pragma once
#include <concepts>
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/core/mpmc_conduit.hpp"

namespace slabflux::core {

    /**
     * @brief Transport Abstraction Invariant.
     * @details Defines the structural requirements for SlabFlux data conduits 
     * to ensure deterministic, hardware-aligned state propagation.
     */
    template <typename C, typename T>
    concept Conduit = requires(C& c, T item, T& out, T* batch, size_t count, int res) {
        // Ingress
        { c.push(item) } -> std::convertible_to<bool>;
        { c.try_push(item) } -> std::convertible_to<bool>;
        { c.on_raw_frame(item, res) } -> std::convertible_to<bool>;

        // Egress
        { c.pop() } -> std::same_as<T>;
        { c.try_pop(out) } -> std::convertible_to<bool>;
        { c.pop_batch(batch, count) } -> std::convertible_to<size_t>;
    };

    /**
     * ========================================================================
     * THE CONDUIT API CONTRACT
     * ========================================================================
     * All communication conduits in SLABFLUX (SPSC, MPMC, Ring) adhere to 
     * a unified behavioral interface for deterministic, high-frequency dataflow:
     * 
     * 1. INGRESS (Producer Path):
     *    - push(...)      : Blocking spin-wait. Guaranteed delivery or critical error.
     *    - try_push(...)  : Non-blocking attempt. Returns bool status.
     *    - on_raw_frame() : Direct PipelineLogic integration for ingress engines.
     *    - DETERMINISM: All ingress paths utilize anticipatory L1-D pre-warming.
     * 
     * 2. EGRESS (Consumer Path):
     *    - pop(...)       : Blocking retrieval via CPU pause loops.
     *    - try_pop(...)   : Unambiguous non-blocking retrieval (Empty vs NULL).
     *    - pop_batch(...) : SIMD-accelerated (AVX-512) multi-pointer drain.
     * 
     * 3. MICRO-ARCHITECTURAL ALIGNMENT:
     *    - Isolation      : Strict 64-byte alignment to eliminate MESI thrashing.
     *    - Pipelining     : Software prefetch hints to hide memory indirection latency.
     *    - Vectorization  : AVX-512 mask generation for recovery-path invalidation.
     */

    /* Note: individual conduit types (spsc_conduit, mpmc_conduit, spsc_ring_conduit)
     * are now defined directly in their respective headers to ensure 
     * filename and class name parity.
     */

    // ========================================================================
    // CONDUIT ALIAS
    // ========================================================================
    /** @brief Primary template alias for the default MPMC (developer safety first). */
    template <typename T, std::size_t Capacity, std::size_t NumLanes = 1>
    using conduit = mpmc_conduit<T, Capacity, NumLanes>;

} // namespace slabflux::core
