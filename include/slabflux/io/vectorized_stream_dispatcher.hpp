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

#include <cstdint>
#include <cstddef>
#include <algorithm>

#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    /**
     * @brief True, zero-internal-buffer, high-speed event dispatcher.
     * @details Eliminates store-forwarding stalls and amortizes the cost of atomic operations
     * by pushing data into lanes in batches.
     * @tparam EventType The type of flowing events (e.g. transport::raw_tcp_frame).
     * @tparam LanesCount The number of lanes in the target channel (e.g. 8, 16). Must be a power of two.
     */
    template <typename EventType, std::size_t BatchSize = 16, typename Engine = void>
    class alignas(64) vectorized_stream_dispatcher {
    private:
        Engine&     engine_;
        EventType*  batch_[BatchSize];
        std::size_t count_{0};

    public:
        explicit vectorized_stream_dispatcher(Engine& engine) noexcept : engine_(engine) {}

        SLAB_FORCE_INLINE void push(EventType* ev) noexcept {
            batch_[count_++] = ev;
            if (SL_EXPECT_FALSE(count_ == BatchSize)) {
                flush();
            }
        }

        SLAB_FORCE_INLINE void flush() noexcept {
            if (SL_EXPECT_TRUE(count_ > 0)) {
                engine_.on_vector_batch(const_cast<const EventType**>(batch_), count_);
                count_ = 0;
            }
        }
    };

    /** @brief Specialized branchless sharding dispatcher for MPMC matrices. */
    template <typename EventType, std::size_t LanesCount>
    class alignas(64) vectorized_stream_dispatcher<EventType, LanesCount, void> {
        static_assert((LanesCount & (LanesCount - 1)) == 0, "LanesCount must be power-of-two");
        static constexpr uint64_t LANE_MASK = LanesCount - 1;
    public:
        explicit vectorized_stream_dispatcher() noexcept = default;
        ~vectorized_stream_dispatcher() noexcept = default;

        vectorized_stream_dispatcher(const vectorized_stream_dispatcher&) = delete;
        vectorized_stream_dispatcher& operator=(const vectorized_stream_dispatcher&) = delete;

        /**
         * @brief Practically proven, 8-wide high-speed distribution pass.
         * @details Reorders packets in-place into temporary arrays, then submits them in batches.
         * Zero AVX-to-stack transition penalty.
         * @param raw_batch The freshly read event array from the network layer (e.g. AF_XDP/DPDK).
         * @param sharded_conduit The multi-lane, lock-free ring buffer interconnect.
         */
        SLAB_HOT void dispatch_8_wide(EventType** raw_batch, auto& sharded_conduit) noexcept {
            // Per-lane local collector arrays on the stack, strictly for temporary pointers only.
            // Since the loop is fixed-size (8), the compiler fully unrolls it into registers.
            alignas(64) EventType* lane_buckets[LanesCount][8];
            alignas(64) std::size_t lane_counts[LanesCount] = {0};

            // 1st PHASE: Grouping (Sharding) in pure scalar, software-prefetched fashion
            #pragma unroll
            for (std::size_t i = 0; i < 8; ++i) {
                // Compute pointer-based hash/lane index (e.g. from lower bits of the memory address)
                uint64_t target_lane_id = (reinterpret_cast<uint64_t>(raw_batch[i]) >> 6) & LANE_MASK;

                std::size_t bucket_idx = lane_counts[target_lane_id]++;
                lane_buckets[target_lane_id][bucket_idx] = raw_batch[i];
            }

            // 2nd PHASE: Amortized batched push (Vector Burst Push)
            // We only touch those lanes atomically that actually receive data.
            // This radically reduces inter-core cache-line invalidation (RFO).
            for (std::size_t lane = 0; lane < LanesCount; ++lane) {
                std::size_t count_to_push = lane_counts[lane];
                if (__builtin_expect(count_to_push > 0, 1)) {

                    // Push the entire batch into the lock-free lane in a single atomic step
                    std::size_t pushed = sharded_conduit.get_lane(lane).push_batch(lane_buckets[lane], count_to_push);

                    // Drop recovery: if the target lane is full, handle software backpressure locally
                    if (__builtin_expect(pushed < count_to_push, 0)) {
                        for (std::size_t k = pushed; k < count_to_push; ++k) {
                            // Emergency drop or redirect into an alternative buffer if the causal mesh is stalled
                            if constexpr (requires { sharded_conduit.on_lane_overflow(lane_buckets[lane][k]); }) {
                                sharded_conduit.on_lane_overflow(lane_buckets[lane][k]);
                            }
                        }
                    }
                }
            }
        }
    };
} // namespace slabflux::io
