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

#include <array>
#include <bit>
#include <ranges>
#include <algorithm>
#include <cstdint>

namespace slabflux::core {

    /**
     * @struct timing_node
     * @brief Intrusive Timer Metadata.
     * @details Embedded directly in event payloads to ensure O(1) scheduling.
     */
    struct timing_node {
        uint64_t expires_tick{ 0 };
        uint32_t timer_next{ 0xFFFFFFFF };
        uint32_t timer_prev{ 0xFFFFFFFF };
        uint32_t wheel_bucket{ 0xFFFFFFFF };
        uint32_t _pad{ 0 }; // Explicit padding for 24-byte alignment
    };

    /**
     * @brief O(1) Timing Wheel.
     * @details Re-architected using C++20 std::ranges and bit manipulation
     * to mirror high-performance kernel scheduling patterns.
     * @tparam SlabAllocator Authority for node index resolution.
     * @tparam WheelSlots Must be a power of two.
     */
    template <typename SlabAllocator, uint32_t WheelSlots = 512>
    class timing_wheel {
        // Hierarchical Tier configuration (Linux Kernel style TVN/TVR)
        static constexpr uint32_t TVR_BITS = 8;
        static constexpr uint32_t TVN_BITS = 6;
        static constexpr uint32_t TVR_SIZE = 1 << TVR_BITS;
        static constexpr uint32_t TVN_SIZE = 1 << TVN_BITS;
        static constexpr uint32_t TVR_MASK = TVR_SIZE - 1;
        static constexpr uint32_t TVN_MASK = TVN_SIZE - 1;
        static constexpr uint32_t LVL_COUNT = 5;
        static constexpr uint32_t TOTAL_BUCKETS = TVR_SIZE + (TVN_SIZE * (LVL_COUNT - 1));

    public:
        static constexpr uint32_t END_OF_LIST = 0xFFFFFFFF;

    private:
        /** @brief Provides a C++20 range view over the intrusive linked list in a bucket. */
        struct intrusive_view : std::ranges::view_interface<intrusive_view> {
            struct iterator {
                using iterator_category = std::forward_iterator_tag;
                using value_type = uint32_t;
                using difference_type = std::ptrdiff_t;

                uint32_t curr;
                SlabAllocator& pool;

                uint32_t operator*() const noexcept { return curr; }
                iterator& operator++() noexcept {
                    curr = pool.get_by_index(curr)->timer_next;
                    return *this;
                }
                iterator operator++(int) noexcept { iterator tmp = *this; ++(*this); return tmp; }
                bool operator==(const iterator& other) const noexcept { return curr == other.curr; }
            };
            uint32_t head; SlabAllocator& pool;
            iterator begin() const noexcept { return { head, pool }; }
            iterator end() const noexcept { return { END_OF_LIST, pool }; }
            
            // C++20 view requirements
            intrusive_view() = default;
            intrusive_view(uint32_t h, SlabAllocator& p) : head(h), pool(p) {}
        };

        // Non-linear slot matrix
        std::array<uint32_t, TOTAL_BUCKETS> buckets_;
        
        // Global temporal state
        uint64_t current_tick_{ 0 };
        SlabAllocator& memory_pool_;

    public:
        explicit timing_wheel(SlabAllocator& pool) : memory_pool_(pool) {
            std::ranges::fill(buckets_, END_OF_LIST);
        }

        // ============================================================
        // O(1) TIMER CANCELLATION
        // ============================================================
        void cancel(uint32_t node_index) noexcept {
            auto* node = memory_pool_.get_by_index(node_index);
            if (!node || node->wheel_bucket == END_OF_LIST) return; // Not scheduled

            uint32_t bucket = node->wheel_bucket;

            // O(1) list unlink
            if (node->timer_prev != END_OF_LIST) {
                memory_pool_.get_by_index(node->timer_prev)->timer_next = node->timer_next;
            }
            else {
                buckets_[bucket] = node->timer_next; // We were the head
            }

            if (node->timer_next != END_OF_LIST) {
                memory_pool_.get_by_index(node->timer_next)->timer_prev = node->timer_prev;
            }

            // Cleanup node state
            node->wheel_bucket = END_OF_LIST;
            node->timer_next = END_OF_LIST;
            node->timer_prev = END_OF_LIST;
        }

        // ============================================================
        // O(1) TIMER INSERTION
        // ============================================================
        void schedule(uint32_t node_index, uint64_t expires) noexcept {
            auto* node = memory_pool_.get_by_index(node_index);
            if (!node) return;

            if (node->wheel_bucket != END_OF_LIST) {
                cancel(node_index);
            }

            const uint64_t diff = expires - current_tick_;
            
            // C++20 Optimization: Use <bit> for hierarchical tier selection.
            // Replaces legacy if-else chains with constant-time bit manipulation.
            uint32_t final_bucket = 0;

            if (diff < TVR_SIZE) {
                final_bucket = expires & TVR_MASK;
            } else {
                uint32_t shift = 64 - std::countl_zero(diff);
                uint32_t level = (shift - TVR_BITS + TVN_BITS - 1) / TVN_BITS;
                level = std::min(level, LVL_COUNT - 1);
                
                uint32_t bucket_offset = TVR_SIZE + (level - 1) * TVN_SIZE;
                uint32_t idx = (expires >> (TVR_BITS + (level - 1) * TVN_BITS)) & TVN_MASK;
                final_bucket = bucket_offset + idx;
            }

            // Intrusive Head Insertion
            node->timer_next = buckets_[final_bucket];
            node->timer_prev = END_OF_LIST;
            node->wheel_bucket = final_bucket;
            node->expires_tick = expires;

            if (buckets_[final_bucket] != END_OF_LIST) {
                auto* old_head = memory_pool_.get_by_index(buckets_[final_bucket]);
                old_head->timer_prev = node_index;
            }
            buckets_[final_bucket] = node_index;
        }

        // ============================================================
        // HIERARCHICAL CLOCK ADVANCE (Cascade)
        // ============================================================
        uint32_t cascade(uint32_t bucket_offset, uint32_t index) noexcept {
            const uint32_t bucket = bucket_offset + index;
            uint32_t curr = buckets_[bucket];
            buckets_[bucket] = END_OF_LIST;

            while (curr != END_OF_LIST) {
                auto* node = memory_pool_.get_by_index(curr);
                uint32_t next = node->timer_next;
                
                // Re-scheduling the node naturally places it into a lower, more precise tier
                node->wheel_bucket = END_OF_LIST; 
                schedule(curr, node->expires_tick);

                curr = next;
            }
            return buckets_[bucket];
        }

        uint32_t tick() noexcept {
            const uint32_t current_bucket = current_tick_ & TVR_MASK;
            const uint32_t expired_head = buckets_[current_bucket];
            
            if (expired_head != END_OF_LIST) {
                buckets_[current_bucket] = END_OF_LIST; // Clear bucket

                // Clean markers using C++20 ranges to ensure nodes can be rescheduled cleanly
                for (uint32_t idx : intrusive_view{ expired_head, memory_pool_ }) {
                    memory_pool_.get_by_index(idx)->wheel_bucket = END_OF_LIST;
                }
            }

            current_tick_++;
            const uint32_t idx = current_tick_ & TVR_MASK;
            
            // Tier 0 roll-over triggers Hierarchical Cascade
            if (!idx) {
                uint32_t n = 1;
                do {
                    uint32_t l_idx = (current_tick_ >> (TVR_BITS + (n - 1) * TVN_BITS)) & TVN_MASK;
                    cascade(TVR_SIZE + (n - 1) * TVN_SIZE, l_idx);
                    if (l_idx != 0) break; // Stop if the level didn't wrap
                } while (++n < LVL_COUNT);
            }

            return expired_head;
        }
    };
}