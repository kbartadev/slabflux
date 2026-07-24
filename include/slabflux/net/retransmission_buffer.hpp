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
#include <atomic>
#include <cstring>
#include "slabflux/mesh/causal_mesh.hpp"
#include "slabflux/hw/intrinsics.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

/**
 * @brief High-performance LSN frame cache.
 * @details Stores a sliding window of recent LSNs to fulfill NACK requests.
 */
template<typename FrameType, size_t WindowSize>
class retransmission_buffer {
    static_assert((WindowSize & (WindowSize - 1)) == 0, 
        "LSN wrap-around safety requires Power-of-Two WindowSize to ensure index continuity at 2^64 boundary");
    static constexpr size_t MASK = WindowSize - 1;
    static constexpr uint64_t BUSY_SENTINEL = 0xFFFFFFFFFFFFFFFE;
    
    // Safety: Wrapper to allow atomic sequence tracking alongside POD payload
    struct retrans_slot {
        FrameType frame;
        // Published LSN: Act as a version/lock (Seqlock-style)
        std::atomic<uint64_t> published_lsn{0xFFFFFFFFFFFFFFFF};
        alignas(64) uint64_t last_sent_tsc{0};
    };

    // Fixed-size array to ensure O(1) access and zero allocations
    alignas(64) retrans_slot slots_[WindowSize];
    
public:
    retransmission_buffer() noexcept {
        // Memory is zeroed via member initialization list / default values
    }

    // Metadata access for the NACK handler
    uint64_t& last_sent(uint64_t lsn) noexcept { return slots_[lsn & MASK].last_sent_tsc; }

    /**
     * @brief Adds a frame to the buffer.
     * @details Overwrites the oldest frame in the circular window.
     */
    inline void insert(const FrameType& frame) noexcept {
        // O(1) Bitmask indexing. 
        // Monotonicity Guard: In large clusters, recovery paths might attempt to 
        // back-fill historical data. We must not overwrite newer frames.
        auto& slot = slots_[frame.sequence_id & MASK];
        uint64_t existing = slot.published_lsn.load(std::memory_order_relaxed);
        if (SL_EXPECT_FALSE(frame.sequence_id < existing && existing != 0xFFFFFFFFFFFFFFFF)) return;

        // Seqlock: Mark slot as BUSY before writing to prevent torn reads 
        // of larger FrameTypes by the asynchronous NACK thread.
        slot.published_lsn.store(BUSY_SENTINEL, std::memory_order_release);
        
        // 2. Perform the copy (potentially non-atomic for large frames)
        slot.frame = frame;

        // 3. Atomic store with release publishes the stable frame
        slot.published_lsn.store(frame.sequence_id, std::memory_order_release);
    }

    /**
     * @brief Retrieves a frame by its LSN.
     * @return Pointer to frame if valid, nullptr if evicted from window.
     */
    inline const FrameType* get(uint64_t lsn) const noexcept {
        const auto& slot = slots_[lsn & MASK];
        
        // Acquire: If the LSN matches and isn't BUSY, the data is stable.
        uint64_t v1 = slot.published_lsn.load(std::memory_order_acquire);
        if (v1 == lsn) [[likely]] {
            const FrameType* ptr = &slot.frame;
            // Double-check to ensure no wrap-around occurred during the pointer generation
            uint64_t v2 = slot.published_lsn.load(std::memory_order_acquire);
            if (SL_EXPECT_TRUE(v1 == v2)) {
                return ptr;
            }
        }
        return nullptr; // LSN was already overwritten (too old)
    }

    /**
     * @brief Accelerated Burst Extraction.
     * @details Uses a software-pipelined bulk extractor with hardware prefetching 
     * to hide memory latency during massive NACK (Negative Acknowledgment) storms.
     */
    SLAB_FORCE_INLINE size_t extract_burst(const uint64_t* missing_lsns, size_t count, const FrameType** out_frames) const noexcept {
        size_t found = 0;
        for (size_t i = 0; i < count; ++i) {
            // Software Pipelining: Instruct the CPU to fetch memory 2 iterations ahead
            if (i + 2 < count) {
                _mm_prefetch(reinterpret_cast<const char*>(&slots_[missing_lsns[i+2] & MASK]), _MM_HINT_T0);
            }
            const auto& slot = slots_[missing_lsns[i] & MASK];
            if (slot.published_lsn.load(std::memory_order_acquire) == missing_lsns[i]) {
                out_frames[found++] = &slot.frame;
            }
        }
        return found;
    }
};

} // namespace slabflux::net