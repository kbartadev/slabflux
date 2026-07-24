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

#include <liburing.h>
#include <stdexcept>
#include <cstdint>

namespace slabflux::io::uring_shim {

    /**
     * @brief High-performance ring initialization.
     */
    inline void ring_init(unsigned entries, io_uring* ring, uint32_t flags, uint32_t sq_idle) {
        io_uring_params params{};
        params.flags = flags;
        params.sq_thread_idle = sq_idle;

        int ret = ::io_uring_queue_init_params(entries, ring, &params);
        if (ret < 0) {
            throw std::runtime_error("io_uring_queue_init_params failed");
        }
    }

    inline void ring_exit(io_uring* ring) noexcept {
        ::io_uring_queue_exit(ring);
    }

    inline void unregister_buffer_ring(io_uring* ring, int bgid) noexcept {
        ::io_uring_unregister_buf_ring(ring, bgid);
    }

    inline io_uring_sqe* get_sqe(io_uring* ring) noexcept {
        return ::io_uring_get_sqe(ring);
    }

    inline void prep_recv_multishot(io_uring_sqe* sqe, int fd, void* buf, size_t len, int flags) noexcept {
        ::io_uring_prep_recv_multishot(sqe, fd, buf, len, flags);
    }

    /**
     * @brief Zero-syscall submission under SQPOLL.
     */
    inline int submit(io_uring* ring) noexcept {
        return ::io_uring_submit(ring);
    }

    inline unsigned peek_batch_cqe(io_uring* ring, io_uring_cqe** cqes, unsigned count) noexcept {
        return ::io_uring_peek_batch_cqe(ring, cqes, count);
    }

    /**
     * @brief Zero-Copy Send (ZC) - Requires Linux 6.0+
     */
    inline void prep_send_zc(io_uring_sqe* sqe, int fd, const void* buf, size_t len, int flags, unsigned zc_flags) noexcept {
        ::io_uring_prep_send_zc(sqe, fd, buf, len, flags, zc_flags);
    }

    // ========================================================================
    // BUFFER RING HELPERS
    // ========================================================================
    
    inline void buf_ring_add(io_uring_buf_ring* br, void* addr, unsigned int len, 
                             uint16_t bid, int mask, int buf_offset) noexcept {
        ::io_uring_buf_ring_add(br, addr, len, bid, mask, buf_offset);
    }

    inline void buf_ring_advance(io_uring_buf_ring* br, int count) noexcept {
        ::io_uring_buf_ring_advance(br, count);
    }

} // namespace slabflux::io::uring_shim