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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Absolute Liability Limitation & Full Terms: See DISCLAIMER, NOTICE, LICENSE.
 *
 * @file egress.hpp
 * @brief High-Speed Lock-Free Egress.
 * @details A Single-Producer-Single-Consumer (SPSC) ring buffer designed
 * for zero-copy handoff between the Compute Core and the Network Core.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <new>
#include <immintrin.h>

#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    template<typename T, uint32_t Capacity>
    class alignas(64) egress_ring {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    private:
        // Cache-line separation to completely eliminate false sharing
        alignas(64) std::atomic<uint64_t> tail_{ 0 };
        alignas(64) std::atomic<uint64_t> head_{ 0 };

        T* ring_;
        static constexpr uint64_t MASK = Capacity - 1;

        // ====================================================================
        // MICROARCHITECTURAL PRIVATE CORE REGISTERS
        // ====================================================================
        // These variables live exclusively in the local core’s private L1D cache,
        // preventing bus traffic on every push.
        alignas(64) uint64_t cached_head_{0};
        alignas(64) uint64_t cached_tail_{0};

    public:
        egress_ring(T* slab_memory) noexcept
        : ring_(slab_memory) {}

        ~egress_ring() noexcept = default;

        /**
         * @brief Pushes a result to the ring utilizing local counter optimization.
         * @details Only touches the remote atomic head_ variable when the local cache view is exhausted.
         */
        [[nodiscard]] [[gnu::always_inline]] inline bool try_push(const T& data) noexcept {
            const uint64_t current_tail = tail_.load(std::memory_order_relaxed);

            // STEP 1: Check the local, non-invalidated L1 cache register first
            if (SL_EXPECT_FALSE(current_tail - cached_head_ >= Capacity)) {

                // STEP 2: Only fetch from the bus if the local view indicates full capacity
                cached_head_ = head_.load(std::memory_order_acquire);

                if (SL_EXPECT_FALSE(current_tail - cached_head_ >= Capacity)) {
                    return false; // Actual congestion in the fabric
                }
            }

            ring_[current_tail & MASK] = data;

            // After writing the data, a single release-store makes it visible to the consumer
            tail_.store(current_tail + 1, std::memory_order_release);
            return true;
        }

        /**
         * @brief Non-blocking poll for the Consumer (Network Core).
         */
        [[nodiscard]] [[gnu::always_inline]] inline T* poll_next() noexcept {
            const uint64_t current_head = head_.load(std::memory_order_relaxed);

            if (current_head == cached_tail_) {
                cached_tail_ = tail_.load(std::memory_order_acquire);
                if (current_head == cached_tail_) return nullptr;
            }

            return &ring_[current_head & MASK];
        }

        [[gnu::always_inline]] inline void commit_pop() noexcept {
            head_.store(head_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
        }
    };
}
