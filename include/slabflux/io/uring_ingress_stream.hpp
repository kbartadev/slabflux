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
 * ============================================================================* @file uring_ingress_stream.hpp
 * @brief Zero-syscall network ingestion using kernel-bypass io_uring.
 */

#pragma once

#include <liburing.h>
#include <atomic>
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/pool.hpp"
#include <type_traits>
#include <utility>

namespace slabflux::io {

    template <typename PoolType, typename PipelineType, std::size_t MaxPayloadSize = 2048>
    struct alignas(64) uring_ingress_stream {
        using EventType = std::remove_pointer_t<decltype(std::declval<PoolType&>().make_raw())>;

        io_uring ring{};
        PoolType& memory_pool;
        PipelineType& app_pipeline;
        static constexpr uint16_t BGID_INGRESS = 1;
        
        io_uring_buf_ring* ibr_{nullptr};
        std::size_t br_mask_{0};
        EventType** dma_map_{nullptr};

    public:
        uring_ingress_stream(PoolType& p, PipelineType& pipe, unsigned kernel_cpu_core = 3)
        : memory_pool(p)
        , app_pipeline(pipe) {
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

            const std::size_t ring_size_bytes = 4096 * sizeof(io_uring_buf);
            if (::posix_memalign(reinterpret_cast<void**>(&ibr_), 4096, ring_size_bytes) != 0)
                throw std::runtime_error("Buffer ring allocation failed");

            io_uring_buf_reg reg{.ring_addr = reinterpret_cast<uint64_t>(ibr_), .ring_entries = 4096, .bgid = BGID_INGRESS};
            if (::io_uring_register_buf_ring(&ring, &reg, 0) < 0) throw std::runtime_error("Buf ring reg failed");

            io_uring_buf_ring_init(ibr_);
            br_mask_ = io_uring_buf_ring_mask(4096);
            
            dma_map_ = new EventType*[memory_pool.capacity()];

            for (std::size_t i = 0; i < memory_pool.capacity(); ++i) {
                void* dma = nullptr;
                if constexpr (requires { memory_pool.get_raw_ptr_by_id(i); }) {
                    dma = memory_pool.get_raw_ptr_by_id(i);
                } else if constexpr (requires { memory_pool.get_ptr(i); }) {
                    dma = memory_pool.get_ptr(i);
                }
                dma_map_[i] = reinterpret_cast<EventType*>(dma);
                uring_shim::buf_ring_add(ibr_, dma, MaxPayloadSize, static_cast<uint16_t>(i), br_mask_, static_cast<int>(i));
            }
            uring_shim::buf_ring_advance(ibr_, static_cast<int>(memory_pool.capacity()));
        }

        ~uring_ingress_stream() noexcept { 
            if (ibr_) { ::io_uring_unregister_buf_ring(&ring, BGID_INGRESS); ::free(ibr_); }
            if (dma_map_) delete[] dma_map_;
            ::io_uring_queue_exit(&ring); 
        }

        /** @brief Queries whether the kernel is actively polling the submission queue. */
        [[nodiscard]] inline bool is_sqpoll_active() const noexcept {
            return (ring.flags & IORING_SETUP_SQPOLL) != 0;
        }

        void arm_socket(int socket_fd) noexcept {
            io_uring_sqe* sqe = uring_shim::get_sqe(&ring);
            uring_shim::prep_recv_multishot(sqe, socket_fd, nullptr, 0, 0);
            sqe->buf_group = BGID_INGRESS;
            sqe->flags |= IOSQE_BUFFER_SELECT;
            uring_shim::submit(&ring);
        }

        SLAB_FORCE_INLINE void poll_hot_path() noexcept {
            // INDIRECTION HOISTING: Cache all member references in local registers
            auto& r = ring;
            auto& pipe = app_pipeline;
            auto* ibr = ibr_;
            const int mask = static_cast<int>(br_mask_);
            EventType** dma_map = dma_map_;

            // ARCHITECTURAL MIRROR: Drain until dry to prevent CQ saturation.
            while (true) [[likely]] {
                io_uring_cqe* cqes[32];
                unsigned count = uring_shim::peek_batch_cqe(&r, cqes, 32);
                
                if (SL_EXPECT_TRUE(count == 0)) break;
                
                EventType* req_batch[32];
                uint16_t bid_batch[32];
                unsigned valid_requests = 0;
                unsigned provided_count = 0;

                for (unsigned i = 0; i < count; ++i) {
                    io_uring_cqe* cqe = cqes[i];
                    
                    if (i + 1 < count) [[likely]] {
                        const uint32_t nf = cqes[i+1]->flags;
                        if (nf & IORING_CQE_F_BUFFER) {
                            _mm_prefetch(reinterpret_cast<const char*>(dma_map[nf >> IORING_CQE_BUFFER_SHIFT]), _MM_HINT_T0);
                        }
                    }

                    if (cqe->flags & IORING_CQE_F_BUFFER) [[likely]] {
                        uint16_t bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
                        EventType* req = dma_map[bid];
                        if (cqe->res > 0) {
                            if constexpr (requires { req->buffer_length; }) {
                                req->buffer_length = cqe->res;
                            } else if constexpr (requires { req->payload_length; }) {
                                req->payload_length = cqe->res;
                            } else if constexpr (requires { req->length; }) {
                                req->length = cqe->res;
                            }
                            
                            // PILLAR I: Symplectic Resonance Fencing (Topological Vaporization)
                            if constexpr (requires { req->validate_resonance(); }) {
                                if (__builtin_expect(!req->validate_resonance(), 0)) [[unlikely]] {
                                    // Recycle buffer immediately, never alerting downstream logic
                                    uring_shim::buf_ring_add(ibr, req, MaxPayloadSize, bid, mask, provided_count++);
                                    continue;
                                }
                            }

                            req_batch[valid_requests] = req;
                            bid_batch[valid_requests] = bid;
                            valid_requests++;
                        } else {
                            uring_shim::buf_ring_add(ibr, req, MaxPayloadSize, bid, mask, provided_count++);
                        }
                    }
                }
                
                ::io_uring_cq_advance(&r, count);

                if (valid_requests > 0) {
                    // Decouple business logic execution from hardware ring tracking
                    for (unsigned i = 0; i < valid_requests; ++i) {
                        pipe.dispatch(req_batch[i]);
                        uring_shim::buf_ring_add(ibr, req_batch[i], MaxPayloadSize, bid_batch[i], mask, provided_count++);
                    }
                }

                if (provided_count > 0) {
                    uring_shim::buf_ring_advance(ibr, provided_count);
                }

                // DOORBELL FUSION: One kick per cycle max.
                if (provided_count > 0) {
                    uring_shim::submit(&r);
                } else if (SL_UNLIKELY((r.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(r.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
                    uring_shim::submit(&r);
                }
            }
        }
    };
}
