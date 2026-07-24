/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#pragma once

#ifdef __linux__
#include <liburing.h>
#endif
#include <system_error>
#include <iostream>
#include <x86intrin.h>
#include <stdexcept>

#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/net/server_ingress.hpp"

namespace slabflux::core {

    /**
     * @brief The Fused Nexus Node handling kernel-bypass network ingress.
     * @details Composes `server_ingress` for lifecycle management while deploying 
     * custom Multishot Buffer Rings for true zero-copy, zero-syscall network ingestion.
     */
    template<typename EventType, size_t Capacity, typename PipelineLogic>
    class alignas(64) fused_nexus_node {

        // The pristine hardware-pinned memory pool
        pinned_allocator_spsc<EventType, Capacity>& mem_pool_;

        // The target domain logic (e.g. NetBus)
        PipelineLogic& logic_;
        int sock_fd_;

        // Composition: Server Ingress acts as the io_uring lifecycle engine
        net::server_ingress ingress_;

#ifdef __linux__
        // Buffer Ring for Multishot Recv
        struct io_uring_buf_ring* buf_ring_{ nullptr };
        uint32_t buf_ring_tail_{ 0 };
        static constexpr uint16_t BUFFER_GROUP_ID = 1;

        // Bare-metal io_uring state for ultra-fast polling
        unsigned* cq_khead_;
        unsigned* cq_ktail_;
        io_uring_cqe* cq_cqes_;
        unsigned cq_ring_mask_;
#endif

        // Isolated telemetry to prevent RFO stalls on the hot poll loop
        alignas(64) uint64_t full_drop_count_{ 0 };

    public:
        fused_nexus_node(pinned_allocator_spsc<EventType, Capacity>& pool, PipelineLogic& logic,
                         int sock_fd, int sq_core_id, void* aux_buf = nullptr, size_t aux_len = 0)
        : mem_pool_(pool), logic_(logic), sock_fd_(sock_fd),
          // Initialize the server_ingress component with CQSIZE overrides
          ingress_(Capacity, sq_core_id, -1, Capacity * 2) {

#ifdef __linux__
            io_uring& ring_ = ingress_.get_raw_ring();

            cq_khead_ = ring_.cq.khead;
            cq_ktail_ = ring_.cq.ktail;
            cq_cqes_  = ring_.cq.cqes;
            cq_ring_mask_ = *ring_.cq.kring_mask;

            int ret;
            buf_ring_ = io_uring_setup_buf_ring(&ring_, Capacity, BUFFER_GROUP_ID, 0, &ret);
            if (!buf_ring_) {
                throw std::system_error(ret, std::generic_category(), "Nexus Node: Buffer ring setup failed");
            }

            // Populate the ring with all buffers from the pool
            for (uint32_t i = 0; i < Capacity; ++i) {
                io_uring_buf_ring_add(buf_ring_, mem_pool_.get_ptr(i), sizeof(EventType), i,
                                      io_uring_buf_ring_mask(Capacity), i);
            }
            io_uring_buf_ring_advance(buf_ring_, Capacity);

            if (sock_fd_ >= 0) {
                if (io_uring_register_files(&ring_, &sock_fd_, 1) < 0) {
                    throw std::runtime_error("Nexus Node: Failed to register fixed socket FD.");
                }
            }

            const int active_iov_count = aux_buf ? 2 : 1;
            iovec iovs[2];
            iovs[0] = { .iov_base = mem_pool_.data(), .iov_len = mem_pool_.size_bytes() };
            if (aux_buf) {
                iovs[1] = { .iov_base = aux_buf, .iov_len = aux_len };
            }

            if (io_uring_register_buffers(&ring_, iovs, active_iov_count) < 0) {
                throw std::runtime_error("Nexus Node: Failed to register fixed buffers (check memlock limit)");
            }

            std::cout << "[IGNITION] Fused Nexus Node engaged via Server Ingress Core " << sq_core_id << ".\n";

            arm_multishot_recv();
#endif
        }

        ~fused_nexus_node() {
#ifdef __linux__
            if (buf_ring_) io_uring_free_buf_ring(&ingress_.get_raw_ring(), buf_ring_, Capacity, BUFFER_GROUP_ID);
#endif
        }

        fused_nexus_node(const fused_nexus_node&) = delete;
        fused_nexus_node& operator=(const fused_nexus_node&) = delete;

        [[nodiscard]] uint64_t get_full_drop_count() const noexcept { return full_drop_count_; }
        
#ifdef __linux__
        io_uring& get_ring() noexcept { return ingress_.get_raw_ring(); }
#endif
        int get_ring_fd() const noexcept { return ingress_.get_ring_fd(); }

        inline void poll() noexcept {
#ifdef __linux__
            unsigned head = io_uring_smp_load_acquire(cq_khead_);
            unsigned tail = io_uring_smp_load_acquire(cq_ktail_);
            io_uring& ring_ = ingress_.get_raw_ring();

            if (head == tail) [[likely]] {
                if (SL_EXPECT_FALSE(io_uring_sq_ready(&ring_) > 0)) {
                    io_uring_submit(&ring_);
                }
                return; 
            }

            while (head != tail) {
                io_uring_cqe* cqe = &cq_cqes_[head & cq_ring_mask_];

                if (SL_LIKELY(head + 1 != tail)) {
                    _mm_prefetch(reinterpret_cast<const char*>(&cq_cqes_[(head + 1) & cq_ring_mask_]), _MM_HINT_T0);
                }

                int res = cqe->res;
                uint16_t bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
                EventType* ev = mem_pool_.get_ptr(bid);

                if (res > 0) [[likely]] {
                    // Admission Control: Seamlessly falls back to Aphasic Horizon via logic_ if saturated
                    if (SL_EXPECT_FALSE(!logic_.on_raw_frame(ev, res))) {
                        full_drop_count_++;
                        if constexpr (requires { logic_.on_conduit_full_drop(); }) {
                            logic_.on_conduit_full_drop();
                        }
                        mem_pool_.free(ev);
                    }
                } else {
                    mem_pool_.free(ev);
                }

                io_uring_buf_ring_add(buf_ring_, ev, sizeof(EventType), bid,
                                      io_uring_buf_ring_mask(Capacity), buf_ring_tail_++);

                head++;
            }

            io_uring_smp_store_release(cq_khead_, head);

            replenish_rx_buffers();
            io_uring_buf_ring_advance(buf_ring_, buf_ring_tail_);
            buf_ring_tail_ = 0;
#endif
        }

    private:
#ifdef __linux__
        inline void replenish_rx_buffers() noexcept {
            io_uring& ring_ = ingress_.get_raw_ring();
            while (EventType* ev = mem_pool_.allocate()) {
                io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
                if (!sqe) {
                    mem_pool_.free(ev);
                    break;
                }
                io_uring_prep_read_fixed(sqe, sock_fd_, ev, sizeof(EventType), 0, 0);
                io_uring_sqe_set_data(sqe, ev);
            }
            std::atomic_thread_fence(std::memory_order_release);
            io_uring_submit(&ring_);
        }

        inline void arm_multishot_recv() noexcept {
            if (sock_fd_ < 0) return;
            io_uring& ring_ = ingress_.get_raw_ring();
            io_uring_sqe* sqe = io_uring_get_sqe(&ring_);

            io_uring_prep_recv_multishot(sqe, sock_fd_, nullptr, 0, 0);
            sqe->buf_group = BUFFER_GROUP_ID;
            sqe->flags |= IOSQE_BUFFER_SELECT | IOSQE_FIXED_FILE;
            io_uring_submit(&ring_);
        }
#endif
    };

} // namespace slabflux::core