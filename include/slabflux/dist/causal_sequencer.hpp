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
 * @file causal_sequencer.hpp
 * @brief Distributed Event Ordering.
 * @details Reorders out-of-order network packets using a slot-based matrix.
 */

#pragma once

#include <array>
#include <cstdint>
#include <algorithm>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::dist {

    class causal_sequencer {
        static constexpr size_t WINDOW_SIZE = 2048;
        static constexpr size_t MASK = WINDOW_SIZE - 1;
        static constexpr size_t BITMASK_COUNT = WINDOW_SIZE / 64;

        // Slot Matrix: Replaces the non-functional FIFO with a zero-allocation window.
        alignas(64) std::array<network_packet*, WINDOW_SIZE> slots_{};
        
        // Hardware-accelerated horizon bitmask
        alignas(64) std::array<uint64_t, BITMASK_COUNT> presence_masks_{};
        uint64_t next_expected_lsn_{0};

    public:
        causal_sequencer() noexcept { 
            slots_.fill(nullptr); 
            presence_masks_.fill(0);
        }

        /**
         * @brief Injects a packet and returns the next contiguous event.
         * @return network_packet* if the gap is closed, otherwise buffers and returns nullptr.
         */
        SLAB_FORCE_INLINE network_packet* push_and_align(network_packet* p) noexcept {
            const uint64_t lsn = p->lsn;

            // 1. Direct path: Packet matches the expected sequence exactly.
            if (SL_EXPECT_TRUE(lsn == next_expected_lsn_)) {
                next_expected_lsn_++;
                return p;
            }

            // 2. Future path: Store out-of-order packets in the alignment window.
            if (lsn > next_expected_lsn_ && lsn < next_expected_lsn_ + WINDOW_SIZE) {
                const size_t idx = lsn & MASK;
                slots_[idx] = p;
                presence_masks_[idx / 64] |= (1ULL << (idx % 64));
            }

            return nullptr;
        }

        /**
         * @brief Drains aligned packets from the window using Hardware Bit-Scan.
         * @details Replaces textbook sequential looping with an O(1) bitwise horizon fast-forward.
         */
        SLAB_FORCE_INLINE network_packet* poll_ready() noexcept {
            const size_t idx = next_expected_lsn_ & MASK;
            const size_t mask_idx = idx / 64;
            const size_t bit_idx = idx % 64;

            // Fast-path: Check presence mask directly without touching the wider slot array
            if (SL_EXPECT_FALSE((presence_masks_[mask_idx] & (1ULL << bit_idx)) != 0)) {
                network_packet* p = slots_[idx];
                slots_[idx] = nullptr;
                presence_masks_[mask_idx] &= ~(1ULL << bit_idx);
                next_expected_lsn_++;
                return p;
            }
            return nullptr;
        }

        /**
         * @brief Vectorized Horizon Batching.
         * @details Uses hardware trailing-zero counts (TZCNT) to discover contiguous blocks 
         * of out-of-order packets in a single CPU cycle, completely bypassing sequential logic.
         */
        SLAB_FORCE_INLINE size_t poll_batch(network_packet** out_batch, size_t max_count) noexcept {
            size_t count = 0;
            while (count < max_count) {
                const size_t idx = next_expected_lsn_ & MASK;
                const size_t mask_idx = idx / 64;
                const size_t bit_idx = idx % 64;

                uint64_t current_mask = presence_masks_[mask_idx] >> bit_idx;
                if ((current_mask & 1) == 0) break; // Next expected LSN is still missing

                // Integer Math: Find the exact length of contiguous 1s in the bitmask
                uint64_t inv_mask = ~current_mask;
                size_t contiguous = (inv_mask == 0) ? (64 - bit_idx) : __builtin_ctzll(inv_mask);
                size_t to_fetch = std::min(contiguous, max_count - count);

                for (size_t i = 0; i < to_fetch; ++i) {
                    out_batch[count++] = slots_[(idx + i) & MASK];
                    slots_[(idx + i) & MASK] = nullptr;
                }

                uint64_t clear_mask = (to_fetch == 64) ? ~0ULL : (((1ULL << to_fetch) - 1) << bit_idx);
                presence_masks_[mask_idx] &= ~clear_mask;
                next_expected_lsn_ += to_fetch;
            }
            return count;
        }
    };
}