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
#include <atomic>
#include "slabflux/io/uring_shim.hpp"
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/pool.hpp"
#include <type_traits>
#include <utility>

namespace slabflux::io {

    template <typename PoolType>
    struct alignas(64) uring_egress_stream {
        using EventType = std::remove_pointer_t<decltype(std::declval<PoolType&>().make_raw())>;

        io_uring ring{};
        PoolType& memory_pool;
        int socket_fd_{-1};
        
        alignas(64) std::atomic<std::size_t> inflight_{0};

    public:
        uring_egress_stream(PoolType& p, int sock_fd, unsigned kernel_cpu_core = 3)
        : memory_pool(p)
        , socket_fd_(sock_fd) {
            io_uring_params params = {};
            params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
            params.sq_thread_cpu = kernel_cpu_core;
            params.cq_entries = 8192;
            if (::io_uring_queue_init_params(4096, &ring, &params) != 0) {
                params.flags = IORING_SETUP_SQPOLL;
                if (::io_uring_queue_init_params(4096, &ring, &params) != 0) {
                    if (::io_uring_queue_init(4096, &ring, 0) != 0) 
                        throw std::runtime_error("io_uring init failed");
                }
            }
        }

        ~uring_egress_stream() noexcept { 
            ::io_uring_queue_exit(&ring); 
        }

        /** @brief Queries whether the kernel is actively polling the submission queue. */
        [[nodiscard]] inline bool is_sqpoll_active() const noexcept {
            return (ring.flags & IORING_SETUP_SQPOLL) != 0;
        }

        SLAB_FORCE_INLINE bool dispatch(EventType* req, std::size_t len) noexcept {
            io_uring_sqe* sqe = uring_shim::get_sqe(&ring);
            if (SL_EXPECT_TRUE(sqe)) {
                void* buf_ptr = req;
                if constexpr (requires { req->raw_buffer; }) {
                    buf_ptr = req->raw_buffer;
                } else if constexpr (requires { req->data; }) {
                    buf_ptr = req->data;
                }
                io_uring_prep_send(sqe, socket_fd_, buf_ptr, len, MSG_DONTWAIT);
                sqe->user_data = reinterpret_cast<uint64_t>(req);
                inflight_.fetch_add(1, std::memory_order_relaxed);
                return true;
            } else {
                // Fallback: Drop directly back to the HTTP pool to prevent memory exhaustion
                memory_pool.release(req);
                return false;
            }
        }

        SLAB_FORCE_INLINE void flush_doorbell() noexcept {
            uring_shim::submit(&ring);
        }

        SLAB_FORCE_INLINE void poll_completions() noexcept {
            // INDIRECTION HOISTING: Cache references in local registers
            auto& r = ring;
            auto& pool = memory_pool;
            
            // ARCHITECTURAL MIRROR: Drain until dry to prevent CQ saturation.
            while (true) [[likely]] {
                io_uring_cqe* cqes[32];
                unsigned count = uring_shim::peek_batch_cqe(&r, cqes, 32);
                if (SL_EXPECT_TRUE(count == 0)) break;
                
                EventType* release_batch[32];
                unsigned valid_releases = 0;
                unsigned finished_sqes = 0;
                
                #if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 32
                #endif
                for (unsigned i = 0; i < count; ++i) {
                    io_uring_cqe* cqe = cqes[i];
                    
                    release_batch[valid_releases] = reinterpret_cast<EventType*>(cqe->user_data);
                    valid_releases += ((cqe->flags & IORING_CQE_F_NOTIF) == 0);
                    finished_sqes += ((cqe->flags & IORING_CQE_F_MORE) == 0);
                }

                ::io_uring_cq_advance(&r, count);

                // Reclaim resources decoupling struct drops from immediate ring ops
                if (valid_releases > 0) {
                    if constexpr (requires { pool.release_batch(release_batch, valid_releases); }) {
                        pool.release_batch(release_batch, valid_releases);
                    } else {
                        for (unsigned i = 0; i < valid_releases; ++i) pool.release(release_batch[i]);
                    }
                }
                if (finished_sqes > 0) {
                    inflight_.fetch_sub(finished_sqes, std::memory_order_relaxed);
                }
            }

            // DOORBELL FUSION: Pulse check
            if (SL_UNLIKELY((r.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(r.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
                uring_shim::submit(&r);
            }
        }
    };
}