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
#include <sys/socket.h>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <stdexcept>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/uring_shim.hpp"
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"

namespace slabflux::io {

    /**
     * @brief High-Velocity Linux io_uring Ingress Boundary (Architecture Mirror).
     * @details Strictly follows the uring_egress architecture to achieve 85ns parity.
     * Eliminates manual head/tail management in favor of cached liburing descriptors.
     */
    template <typename TargetConduit, std::size_t TotalEntries = 512>
    class alignas(64) uring_ingress {
        static_assert((TotalEntries & (TotalEntries - 1)) == 0, "io_uring ring density must be power of 2");

    public:
        static constexpr std::size_t BUFFER_STRIDE = 2048;
        static constexpr uint16_t    BGID_INGRESS  = 1;

    private:
        int                socket_fd_{-1};
        io_uring           ring_{};
        TargetConduit&     conduit_;
        std::atomic<bool>& running_sentinel_;

        io_uring_buf_ring* ibr_{nullptr};
        char*              cached_pool_base_{nullptr};
        std::size_t        br_mask_{0};
        bool               valid_{false};
        alignas(64) uint64_t total_ingressed_{0};

        static constexpr std::size_t MAX_BURST_SIZE = 32;

        void setup_provided_buffer_ring() {
            const std::size_t ring_size_bytes = TotalEntries * sizeof(io_uring_buf);
            const std::size_t pool_size_bytes = TotalEntries * BUFFER_STRIDE;

            if (::posix_memalign(reinterpret_cast<void**>(&ibr_), 4096, ring_size_bytes) != 0)
                throw std::runtime_error("Buffer ring allocation failed");

            cached_pool_base_ = static_cast<char*>(::mmap(nullptr, pool_size_bytes, PROT_READ | PROT_WRITE, 
                                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0));
            if (cached_pool_base_ == MAP_FAILED) throw std::runtime_error("Buffer pool mmap failed");

            io_uring_buf_reg reg{.ring_addr = reinterpret_cast<uint64_t>(ibr_), .ring_entries = TotalEntries, .bgid = BGID_INGRESS};
            if (::io_uring_register_buf_ring(&ring_, &reg, 0) < 0) throw std::runtime_error("Buf ring reg failed");

            io_uring_buf_ring_init(ibr_);
            br_mask_ = io_uring_buf_ring_mask(TotalEntries);

            for (std::size_t i = 0; i < TotalEntries; ++i) {
                char* base = cached_pool_base_ + (i * BUFFER_STRIDE);
                char* dma  = base + offsetof(transport::raw_tcp_frame, data);
                uring_shim::buf_ring_add(ibr_, dma, BUFFER_STRIDE - offsetof(transport::raw_tcp_frame, data), static_cast<uint16_t>(i), br_mask_, static_cast<int>(i));
            }
            uring_shim::buf_ring_advance(ibr_, static_cast<int>(TotalEntries));
        }

        void prime_multishot_receive() noexcept {
            io_uring_sqe* sqe = uring_shim::get_sqe(&ring_);
            if (sqe) {
                uring_shim::prep_recv_multishot(sqe, socket_fd_, nullptr, 0, 0);
                sqe->flags |= IOSQE_BUFFER_SELECT;
                sqe->buf_group = BGID_INGRESS;
                sqe->user_data = 0xFFFFFFFFFFFFFFFFULL;
                uring_shim::submit(&ring_);
            }
        }

    public:
        explicit uring_ingress(int sock, TargetConduit& conduit, std::atomic<bool>& running)
        : socket_fd_(sock), conduit_(conduit), running_sentinel_(running) {
            io_uring_params params{};
            params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_COOP_TASKRUN | IORING_SETUP_SINGLE_ISSUER;
            params.sq_thread_idle = 2000;

            if (::io_uring_queue_init_params(TotalEntries, &ring_, &params) == 0) {
                valid_ = true;
                setup_provided_buffer_ring();
                prime_multishot_receive();
            }
        }

        ~uring_ingress() noexcept {
            if (ibr_) { ::io_uring_unregister_buf_ring(&ring_, BGID_INGRESS); ::free(ibr_); }
            if (cached_pool_base_ && cached_pool_base_ != MAP_FAILED) ::munmap(cached_pool_base_, TotalEntries * BUFFER_STRIDE);
            if (valid_) ::io_uring_queue_exit(&ring_);
        }

        /** @brief Queries whether the kernel is actively polling the submission queue. */
        [[nodiscard]] inline bool is_sqpoll_active() const noexcept {
            return (ring_.flags & IORING_SETUP_SQPOLL) != 0;
        }

        /**
         * @brief Synchronous Ingress Poll.
         * @details Mirrored from poll_egress: One burst per poll, zero manual head/tail snapshots.
         */
        inline void poll_ingress() noexcept {
            if (SL_EXPECT_FALSE(!valid_)) return;

            // INDIRECTION HOISTING: Cache all member references in local registers
            auto& ring    = ring_;
            auto& conduit = conduit_;
            const uint32_t conn_id = static_cast<uint32_t>(socket_fd_);
            const char* pool_ptr   = cached_pool_base_;
            bool terminated = false;
            bool need_submit = false;

            // ARCHITECTURE MIRROR: Drain until dry to prevent CQ saturation.
            // This keeps hardware descriptors hot and matches the egress "perfect combo".
            while (true) [[likely]] {
                io_uring_cqe* cqes[MAX_BURST_SIZE];
                const unsigned count = uring_shim::peek_batch_cqe(&ring, cqes, MAX_BURST_SIZE);
                if (SL_EXPECT_TRUE(count == 0)) break;

                alignas(64) typename TargetConduit::value_type token_batch[MAX_BURST_SIZE];

                for (unsigned i = 0; i < count; ++i) {
                    // SOFTWARE PREFETCH: Hoisted to the top of the loop to parallelize header fetching
                    // with register-packing logic, eliminating RFO stalls entirely.
                    if (i + 1 < count) [[likely]] {
                        const uint32_t nf = cqes[i+1]->flags;
                        _mm_prefetch(pool_ptr + (static_cast<size_t>(nf >> IORING_CQE_BUFFER_SHIFT) << 11), _MM_HINT_T0);
                    }

                    const uint32_t flags = cqes[i]->flags;
                    const int32_t res = cqes[i]->res;
                    const uint16_t bid = static_cast<uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
                    
                    auto* frame = reinterpret_cast<transport::raw_tcp_frame*>(const_cast<char*>(pool_ptr) + (static_cast<size_t>(bid) << 11));
                    
                    frame->payload_length = static_cast<uint16_t>(res);
                    frame->connection_id  = conn_id;

                    auto packed = core::tagged_pointer::pack(bid, frame);
                    if constexpr (requires { token_batch[i].embed_symmetry(0); }) {
                        token_batch[i] = typename TargetConduit::value_type(transport::raw_tcp_frame::ID, packed);
                        token_batch[i].embed_symmetry(static_cast<uint32_t>(total_ingressed_ + i));
                    } else if constexpr (requires { token_batch[i].anchor_to_lightcone(0); }) {
                        token_batch[i] = typename TargetConduit::value_type(packed);
                        token_batch[i].anchor_to_lightcone(total_ingressed_ + i);
                    } else {
                        token_batch[i] = packed;
                    }

                    if (SL_UNLIKELY(res <= 0 && res != -EAGAIN)) terminated = true;
                }
                total_ingressed_ += count;
                ::io_uring_cq_advance(&ring, count);

                std::size_t pushed = conduit.push_batch(token_batch, count);
                if (SL_EXPECT_FALSE(pushed < count)) {
                    // Internal vectorized release to avoid redundant doorbell triggers
                    release_ingress_buffers_internal(token_batch + pushed, count - pushed);
                // Releasing buffers doesn't require a submit unless SQPOLL needs wakeup
                if (SL_UNLIKELY((ring.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(ring.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
                    uring_shim::submit(&ring);
                }
                }
            }

            // Auto-reprime: Ensure the engine doesn't go dark during the benchmark
            if (SL_UNLIKELY(terminated && running_sentinel_.load(std::memory_order_relaxed))) {
                prime_multishot_receive();
            need_submit = true;
            }

        if (need_submit) {
            uring_shim::submit(&ring);
        } else if (SL_UNLIKELY((ring.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(ring.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
            uring_shim::submit(&ring);
        }
        }

        /** @brief Internal vectorized release to avoid redundant doorbell triggers during poll loop. */
        SLAB_FORCE_INLINE void release_ingress_buffers_internal(const core::tagged_pointer* tokens, std::size_t count) noexcept {
            // INDIRECTION HOISTING: Cache ring descriptors to bypass `get_address()` masking tax
            auto* ibr = ibr_;
            const int mask = static_cast<int>(br_mask_);
            const char* pool = cached_pool_base_;
            constexpr int data_off = offsetof(transport::raw_tcp_frame, data);
            constexpr int stride_len = BUFFER_STRIDE - data_off;

            for (std::size_t i = 0; i < count; ++i) {
                const uint16_t bid = static_cast<uint16_t>(tokens[i].tag());
                char* dma = const_cast<char*>(pool) + (static_cast<size_t>(bid) << 11) + data_off;
                uring_shim::buf_ring_add(ibr, dma, stride_len, bid, mask, static_cast<int>(i));
            }
            uring_shim::buf_ring_advance(ibr, static_cast<int>(count));
        }

        /**
         * @brief Vectorized buffer recycling.
         * @details Reduces memory bus pressure by batching updates to the provided buffer ring.
         */
        inline void release_ingress_buffers_batch(const core::tagged_pointer* tokens, std::size_t count) noexcept {
            if (SL_UNLIKELY(count == 0)) return;
            release_ingress_buffers_internal(tokens, count);
            if (SL_UNLIKELY((ring_.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(ring_.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
                uring_shim::submit(&ring_);
            }
        }

        inline void release_ingress_buffer(uint16_t bid) noexcept {
            char* base = cached_pool_base_ + (static_cast<size_t>(bid) << 11);
            char* dma  = base + offsetof(transport::raw_tcp_frame, data);
            uring_shim::buf_ring_add(ibr_, dma, BUFFER_STRIDE - offsetof(transport::raw_tcp_frame, data), bid, br_mask_, 0);
            uring_shim::buf_ring_advance(ibr_, 1);

            if (SL_UNLIKELY((ring_.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(ring_.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
                uring_shim::submit(&ring_);
            }
        }
    };
}