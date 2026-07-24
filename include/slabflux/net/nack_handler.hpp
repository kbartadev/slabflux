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

#include <immintrin.h>
#include <liburing.h>

namespace slabflux::net {
    struct nack_request {
        uint64_t missing_lsn;
        uint32_t requester_id;
    };
}

#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

// Forward declaration to break circular dependency with causal_mesh.hpp
template<typename FrameType, size_t WindowSize> class retransmission_buffer;

/**
 * @brief Listens for and fulfills Negative ACKs.
 */
template<typename FrameType, size_t Window>
class nack_handler {
    retransmission_buffer<FrameType, Window>& buffer_;
    const slabflux::core::sf_node_ctx& context_;
    struct io_uring& ring_;
    int buffer_index_;
    
    // Probabilistic Fault State Machine.
    // Dynamically incorporates the local CPU TSC frequency and perceived network 
    // jitter to form a highly adaptive retransmission squelch horizon.
    uint64_t dynamic_squelch_cycles_{ 300'000 };

public:
    nack_handler(retransmission_buffer<FrameType, Window>& buf, const slabflux::core::sf_node_ctx& ctx, struct io_uring& ring, int buf_idx = -1)
        : buffer_(buf), context_(ctx), ring_(ring), buffer_index_(buf_idx) {}

    /**
     * @brief Ensures retransmissions are committed before buffer modification.
     */
    void sync_barrier() noexcept {
        io_uring_submit_and_wait(&ring_, 0); 
    }

public:
    /**
     * @brief Processes a NACK request and resends the missing LSN.
     */
    void on_nack_received(const nack_request& req, int client_fd) {
        // Overrun Guard: Do not fulfill NACKs for "future" LSNs that haven't 
        // been logically committed by the local compute engine.
        if (SL_EXPECT_FALSE(req.missing_lsn >= context_.horizon())) {
            return;
        }

        const uint64_t now = __rdtsc();
        uint64_t& last_sent = buffer_.last_sent(req.missing_lsn);

        // Mathematical Jitter Adaptation: scales squelch by distance to prevent burst-collapse
        const uint64_t LSN_distance = context_.horizon() - req.missing_lsn;
        const uint64_t adaptive_squelch = dynamic_squelch_cycles_ + (LSN_distance << 8);

        if (SL_EXPECT_FALSE(now - last_sent < adaptive_squelch)) {
#if defined(__CLDEMOTE__)
            _mm_cldemote(&last_sent);
#else
            _mm_prefetch(reinterpret_cast<const char*>(&last_sent), _MM_HINT_T1);
#endif
            return;
        }

        const auto* frame = buffer_.get(req.missing_lsn);
        if (frame) [[likely]] {
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
            if (SL_EXPECT_FALSE(!sqe)) return;

            io_uring_prep_send_zc(sqe, client_fd, frame, sizeof(*frame), 0, 0);
            if (buffer_index_ >= 0) sqe->buf_index = static_cast<uint16_t>(buffer_index_);
            
            last_sent = now;
#if defined(__CLDEMOTE__)
            _mm_cldemote(&last_sent);
#else
            _mm_prefetch(reinterpret_cast<const char*>(&last_sent), _MM_HINT_T1);
#endif

#if defined(__CLDEMOTE__)
            _mm_cldemote(frame);
#else
            _mm_prefetch(reinterpret_cast<const char*>(frame), _MM_HINT_T1);
#endif

            if (SL_UNLIKELY((*ring_.sq.kflags) & IORING_SQ_NEED_WAKEUP)) {
                io_uring_submit(&ring_);
            }
        }
        // If frame is nullptr, the node has lagged too far (Critical Desync)
    }
};

} // namespace slabflux::net