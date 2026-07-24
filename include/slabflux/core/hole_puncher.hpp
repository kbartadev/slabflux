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
#include <optional>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief Deterministic order restorer.
     * @details Freezes time for the Chip if there is a gap in the sequence.
     * @tparam WindowSize O(1) memory window for reorder buffering.
     */
    template<typename T, size_t WindowSize>
    class hole_puncher {
        static_assert((WindowSize & (WindowSize - 1)) == 0, "WindowSize must be a power-of-2 for LSN wrap safety");
        static constexpr size_t MASK = WindowSize - 1;

        struct slot {
            T data;
            bool occupied : 1;
            bool skipped : 1;
        };

        std::array<slot, WindowSize> ring_buffer_{};
        uint64_t expected_lsn_{ 0 };
        uint64_t peak_jitter_{ 0 };
        size_t effective_window_{ 64 }; // Adaptive starting depth

    public:
        // O(1) insertion into the barrier
        inline bool insert(uint64_t lsn, const T& data) noexcept {
            // Serial Number Arithmetic: handles 2^64 wrap-around via cast to signed diff
            int64_t diff = static_cast<int64_t>(lsn - expected_lsn_);

            // Hard Cap: Reject if outside physical buffer bounds.
            if (diff < 0 || diff >= static_cast<int64_t>(WindowSize)) [[unlikely]] return false;

            // Dynamic Reorder Window: Expand effective depth based on observed jitter peaks.
            if (SL_EXPECT_FALSE(diff >= static_cast<int64_t>(effective_window_))) {
                effective_window_ = (static_cast<size_t>(diff + 1) * 2 < WindowSize) ? 
                                    (static_cast<size_t>(diff + 1) * 2) : WindowSize;
            }

            if (static_cast<uint64_t>(diff) > peak_jitter_) peak_jitter_ = static_cast<uint64_t>(diff);

            jitter_recovery_policy();

            auto& s = ring_buffer_[lsn & MASK];
            s.data = data;
            s.occupied = true;
            s.skipped = false;
            return true;
        }

        /** @brief Marks an LSN as skipped to prevent pipeline stalls (Tombstone) */
        inline void mark_skipped(uint64_t lsn) noexcept {
            int64_t diff = static_cast<int64_t>(lsn - expected_lsn_);
            if (diff < 0 || diff >= static_cast<int64_t>(WindowSize)) [[unlikely]] return;
            
            auto& s = ring_buffer_[lsn & MASK];
            s.occupied = true;
            s.skipped = true;
        }

        /** @brief Jitter Recovery: Mark oldest gaps as skipped to slide the window. */
        inline void jitter_recovery_policy() noexcept {
            if (SL_EXPECT_FALSE(peak_jitter_ > (WindowSize * 9 / 10))) {
                // Congestion recovery: skip the oldest 5% of the window
                const size_t skip_count = (WindowSize / 20) > 0 ? (WindowSize / 20) : 1;
                for (size_t i = 0; i < skip_count; ++i) {
                    mark_skipped(expected_lsn_ + i);
                }
                peak_jitter_ = 0; // Cool-down to stabilize sequence baseline
            }
        }

        /** @brief Decay Window: Shrink the reorder depth during stable conditions. */
        inline void decay_reorder_window() noexcept {
            // If jitter is consistently low, slowly shrink the effective window 
            // to minimize future traversal overhead during Segment B unblocking.
            if (SL_EXPECT_FALSE(peak_jitter_ < (effective_window_ >> 2) && effective_window_ > 64)) {
                effective_window_ -= 1;
                if (peak_jitter_ > 0) peak_jitter_ -= 1;
            }
        }

        /** @brief Checks if the next expected LSN is available for processing. */
        inline bool has_ready() const noexcept {
            return ring_buffer_[expected_lsn_ & MASK].occupied;
        }

        /** @brief Purges the reorder window and resets the sequence horizon. */
        inline void reset(uint64_t next_expected_lsn = 0) noexcept {
            for (auto& s : ring_buffer_) {
                s.occupied = false;
                s.skipped = false;
            }
            expected_lsn_ = next_expected_lsn;
        }

        // Continuous prefix handoff to the reduction stage
        template<typename Func>
        inline void flush_ready(Func&& processor) noexcept {
            while (ring_buffer_[expected_lsn_ & MASK].occupied) {
                auto& s = ring_buffer_[expected_lsn_ & MASK];
                if (!s.skipped) {
                    processor(s.data, expected_lsn_);
                    decay_reorder_window();
                }
                s.occupied = false;
                expected_lsn_++;
            }
        }
    };

} // namespace slabflux::core
