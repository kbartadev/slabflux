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
 * @file       conduit_primitives.hpp
 * @brief      Zero-Overhead Signal Routing and Memory Primitives.
 * @details    Core infrastructure for deterministic O(1) dataflow.
 * Provides pipeline composition, signal multiplexing, and SIMD ring buffers.
 */

#pragma once
#include <tuple>
#include <optional>
#include <atomic>
#include <utility>
#include <concepts>
#include <iterator>
#include <array>

#include "../hw/intrinsics.hpp"
#include "signal_backbone.hpp"

namespace slabflux::conduit {

    using namespace bridge;

    // ============================================================================
    // 1. ZERO-OVERHEAD PIPELINE COMPOSITION (Dataflow Pipe)
    // ============================================================================

    template <typename T, typename Transform>
    SLAB_FORCE_INLINE constexpr auto operator|(std::optional<T> input, const Transform& op) noexcept {
        return input ? std::make_optional(op(*input)) : std::nullopt;
    }

    template <typename T, typename Transform>
    SLAB_FORCE_INLINE constexpr auto operator|(const T& input, const Transform& op) noexcept {
        return op(input);
    }

    // ============================================================================
    // 2. HARDWARE SIGNAL MULTIPLEXER (Formerly "conduit_manifold")
    // ============================================================================

    template <typename... Subsystems>
    class signal_multiplexer {
        std::tuple<Subsystems&...> subsystems_;

    public:
        constexpr explicit signal_multiplexer(Subsystems&... systems) noexcept
            : subsystems_(systems...) {
        }

        /**
         * @brief O(1) Compile-Time Broadcast
         * Distributes an incoming signal to all connected subsystems.
         * The compiler collapses this into a single inlined assembly block.
         */
        template <typename Signal>
        SLAB_FORCE_INLINE void broadcast(const Signal& signal) noexcept {
            // std::apply expands the tuple, and the fold expression (..., expr)
            // invokes the methods at compile time without iteration.
            std::apply([&](auto&... system) {
                (..., system.process_signal(signal));
                }, subsystems_);
        }
    };

    // ============================================================================
    // 3. ZERO-ALLOCATION SIMD RING BUFFER (Formerly "avx2_kv_cache_pebble")
    // ============================================================================
    // A fixed memory region that continuously overwrites the oldest data.
    // Perfect for HFT tick history, DSP signal processing, or LLM context windows.

    template <size_t Dim, size_t ContextSize>
    struct alignas(64) simd_ring_buffer {
        static_assert((ContextSize & (ContextSize - 1)) == 0, "ContextSize must be a power of 2 for O(1) masking.");

        // Physical memory: aligned to L1/L2 cache for extreme speed.
        alignas(64) mutable std::array<simd_vector<Dim>, ContextSize> memory_ring;

        // Coordination Gate: [32-bit Version | 32-bit Index]
        // Prevents ABA and coordinates multi-threaded signal ingestion.
        alignas(64) mutable std::atomic<uint64_t> coordination_gate{0};
        alignas(64) mutable std::atomic<size_t> active_elements{0};

        // The Multiplexer (above) calls this when a signal arrives
        SLAB_FORCE_INLINE std::optional<simd_vector<Dim>> process_signal(const simd_vector<Dim>& input) const noexcept {
            // 1. SLOT ACQUISITION (CAS Matrix)
            uint64_t gate_val = coordination_gate.load(std::memory_order_acquire);
            uint32_t index;
            
            for (uint32_t retries = 0; ; ++retries) {
                index = static_cast<uint32_t>(gate_val & 0xFFFFFFFF);
                uint32_t version = static_cast<uint32_t>(gate_val >> 32);
                uint32_t next_index = (index + 1) & (ContextSize - 1);
                uint64_t new_gate = (static_cast<uint64_t>(version + 1) << 32) | next_index;

                if (SL_EXPECT_TRUE(coordination_gate.compare_exchange_strong(gate_val, new_gate, 
                    std::memory_order_release, std::memory_order_acquire))) {
                    break;
                }

                // Interconnect Stabilization: Restructured CAS fallback.
                // Exponential backoff prevents RFO thrashing on the coordination gate.
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }

            // 2. WRITE (O(1))
            memory_ring[index] = input;

            // 3. TRACK OCCUPANCY
            size_t elements = active_elements.load(std::memory_order_relaxed);
            if (elements < ContextSize) {
                active_elements.fetch_add(1, std::memory_order_relaxed);
            }

            // If the ring is full, drip the oldest signal back into the dataflow
            if (elements >= ContextSize) [[unlikely]] {
                return memory_ring[(index + 1) & (ContextSize - 1)];
            }

            return std::nullopt;
        }

        // Test-Time Compute (Backtracking) for temporal rewinding
        SLAB_FORCE_INLINE const simd_vector<Dim>& peek_historical(size_t steps_back) const noexcept {
            size_t index = (static_cast<uint32_t>(coordination_gate.load(std::memory_order_relaxed)) - 1 - steps_back + ContextSize) & (ContextSize - 1);
            return memory_ring[index];
        }
    };

} // namespace slabflux::conduit
