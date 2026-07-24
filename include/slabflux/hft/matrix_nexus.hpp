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
#include <immintrin.h>
#include <new>
#include <cstdint>
#include <string_view>
#include "slabflux/core/pool.hpp"
#include "slabflux/transport/http_frame.hpp"
#include "slabflux/transport/baremetal_parser.hpp"
#include "slabflux/transport/tcp_stream_defragmenter.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::hft {

template<typename BusinessLogic, size_t Capacity = 1024>
struct alignas(slabflux::core::CACHE_LINE_SIZE) matrix_nexus {
    io_uring ring;
    io_uring_buf_ring* buf_ring; // The new, ultra-lightweight memory ring
    slabflux::core::pool<char, Capacity>& memory_pool;
    BusinessLogic& logic;

    struct inbound_frame {
        const char* data;
        std::size_t payload_length;
        std::size_t connection_id;
    };

    slabflux::transport::session_storage_registry<slabflux::transport::http_frame, 1024> registry;
    slabflux::transport::tcp_stream_defragmenter<
        slabflux::transport::baremetal_parser, 
        slabflux::transport::http_frame, 
        BusinessLogic, 
        inbound_frame
    > defragmenter;

    static constexpr uint16_t BUFFER_GROUP_ID = 1;
    static constexpr unsigned BATCH_LIMIT = 8;
    unsigned buf_ring_tail = 0; // Local follower for replenishment

    matrix_nexus(slabflux::core::pool<char, Capacity>& p, BusinessLogic& bl)
        : memory_pool(p), logic(bl), defragmenter(bl, registry) {

        io_uring_params params{};
        params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
        params.sq_thread_cpu = 3;
        io_uring_queue_init_params(4096, &ring, &params);

        // Register a dedicated Buffer Ring with the Kernel
        int ret;
        buf_ring = io_uring_setup_buf_ring(&ring, memory_pool.capacity(), BUFFER_GROUP_ID, 0, &ret);

        // Initial fill (Only 16 bytes per buffer)
        for(uint16_t i = 0; i < memory_pool.capacity(); ++i) {
            io_uring_buf_ring_add(buf_ring, memory_pool.get_raw_ptr_by_id(i),
                                  4096, i,
                                  io_uring_buf_ring_mask(memory_pool.capacity()), i);
        }
        io_uring_buf_ring_advance(buf_ring, memory_pool.capacity());
        buf_ring_tail = 0;

        // Arm socket (Multishot)
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_recv_multishot(sqe, 0, nullptr, 0, 0);
        sqe->buf_group = BUFFER_GROUP_ID;
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->flags |= IOSQE_FIXED_FILE;
        io_uring_submit(&ring);
    }

    [[gnu::always_inline]] inline void poll_and_execute() noexcept {
        const unsigned tail = io_uring_smp_load_acquire(ring.cq.ktail);
        const unsigned head = *ring.cq.khead;
        if (head == tail) [[likely]] return;

        const unsigned mask = *ring.cq.kring_mask;
        const unsigned batch_count = (tail - head) < BATCH_LIMIT ? (tail - head) : BATCH_LIMIT;
        const uint32_t buf_mask = io_uring_buf_ring_mask(memory_pool.capacity());
        unsigned local_buf_tail = buf_ring_tail;

        #pragma GCC unroll 8
        for (unsigned i = 0; i < batch_count; ++i) {
            io_uring_cqe* cqe = &ring.cq.cqes[(head + i) & mask];
            uint16_t bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
            const char* ptr = static_cast<const char*>(memory_pool.get_raw_ptr_by_id(bid));

            __builtin_prefetch(ptr, 0, 3); // Pull into L1
            
            if (SL_EXPECT_TRUE(cqe->res > 0)) {
                inbound_frame frame{ptr, static_cast<std::size_t>(cqe->res), static_cast<std::size_t>(cqe->user_data)};
                defragmenter.on(frame);
            }

            io_uring_buf_ring_add(buf_ring, const_cast<char*>(ptr), 4096, bid, buf_mask, local_buf_tail++);
        }

        io_uring_smp_store_release(ring.cq.khead, head + batch_count);
        // Update Buffer Ring (Kernel sees it immediately)
        io_uring_buf_ring_advance(buf_ring, local_buf_tail - buf_ring_tail);
        buf_ring_tail = local_buf_tail;
    }
};

} // namespace slabflux::hft
