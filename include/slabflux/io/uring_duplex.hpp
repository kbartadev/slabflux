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
#include <cstring>
#include <stdexcept>
#include <vector>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/uring_shim.hpp" // New shim layer for liburing calls
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"

namespace slabflux::io {

    /**
     * @brief Linux io_uring Zero-Copy Duplex Runtime Engine.
     * @details Implements true zero-copy kernel bypass interfaces via pre-registered buffer matrices.
     * @tparam InboundConduit Interconnect queue layer for received tokens (e.g., sharded core SPSC conduit).
     * @tparam OutboundConduit Interconnect queue layer for pending egress packets (e.g., MPMC mesh slice).
     * @tparam PoolType Backing memory allocator pool managing the registered network memory fabric.
     * @tparam RingEntries Total hardware descriptor slots allocated to the submission queue. Must be power-of-two.
     */
    template <typename InboundConduit, typename OutboundConduit, typename PoolType, std::size_t RingEntries = 256>
    class alignas(64) uring_duplex {
        static_assert((RingEntries & (RingEntries - 1)) == 0, "io_uring ring density must be an exact power of 2");

    public:
        static constexpr std::size_t BUFFER_STRIDE = 2048;
        static constexpr uint16_t    BGID_INGRESS  = 1; // Inbound Provided Buffer Group Identifier

    private:
        int                socket_fd_{-1};
        io_uring           ring_{};
        InboundConduit&    ingress_conduit_;
        OutboundConduit&   egress_conduit_;
        PoolType&          mem_pool_;
        std::atomic<bool>& running_sentinel_;

        // User-space provided buffer ring management structures
        io_uring_buf_ring* ibr_{nullptr};
        char* ingress_buffer_pool_{nullptr};
        std::size_t        br_mask_{0};

        alignas(64) std::size_t tx_inflight_{0};
        alignas(64) uint64_t rx_total_ingressed_{0};
        static constexpr std::size_t MAX_BURST_SIZE = 32;

        /**
         * @brief Registers the hardware-accessible user-space buffer pools.
         */
        void setup_provided_buffer_ring() {
            const std::size_t ring_size_bytes = RingEntries * sizeof(io_uring_buf);
            const std::size_t pool_size_bytes = RingEntries * BUFFER_STRIDE;

            // Allocate page-aligned memory for the buffer ring descriptor table
            if (::posix_memalign(reinterpret_cast<void**>(&ibr_), 4096, ring_size_bytes) != 0) {
                throw std::runtime_error("Failed to allocate page-aligned io_uring buffer ring tracking matrices");
            }

            // Allocate page-aligned memory for the raw payload data slots
            ingress_buffer_pool_ = static_cast<char*>(::mmap(
                nullptr, pool_size_bytes, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0
            ));

            if (ingress_buffer_pool_ == MAP_FAILED) {
                throw std::runtime_error("Failed to mmap memory for the network receive buffer pool");
            }

            io_uring_buf_reg reg{};
            reg.ring_addr = reinterpret_cast<uint64_t>(ibr_);
            reg.ring_entries = RingEntries;
            reg.bgid = BGID_INGRESS;

            if (::io_uring_register_buf_ring(&ring_, &reg, 0) != 0) {
                throw std::runtime_error("Kernel registration failed for the provided buffer ring context");
            }

            // Initialize descriptors and link them into the tracking structures
            io_uring_buf_ring_init(ibr_);
            br_mask_ = io_uring_buf_ring_mask(RingEntries);

            for (std::size_t i = 0; i < RingEntries; ++i) {
                char* buffer_base = ingress_buffer_pool_ + (i * BUFFER_STRIDE);
                // Offset the hardware target past the meta-header to guarantee clean zero-copy placements
                char* dma_target = buffer_base + offsetof(transport::raw_tcp_frame, data);
                std::size_t stride_len = BUFFER_STRIDE - offsetof(transport::raw_tcp_frame, data);
                
                uring_shim::buf_ring_add(ibr_, dma_target, stride_len, static_cast<uint16_t>(i), br_mask_, static_cast<int>(i));
            }

            uring_shim::buf_ring_advance(ibr_, static_cast<int>(RingEntries));
        }

    public:
        explicit uring_duplex(
            int sock,
            InboundConduit& in_conduit,
            OutboundConduit& out_conduit,
            PoolType& pool,
            std::atomic<bool>& running
        )
        : socket_fd_(sock)
        , ingress_conduit_(in_conduit)
        , egress_conduit_(out_conduit)
        , mem_pool_(pool)
        , running_sentinel_(running)
        {
            // Fallback Chain: Try SQPOLL (Linux 5.10+) -> Try baseline io_uring
            try {
                uring_shim::ring_init(RingEntries, &ring_, IORING_SETUP_SQPOLL | IORING_SETUP_COOP_TASKRUN | IORING_SETUP_SINGLE_ISSUER, 2000);
            } catch (...) {
                try {
                    uring_shim::ring_init(RingEntries, &ring_, IORING_SETUP_SQPOLL, 2000);
                } catch (...) {
                    if (::io_uring_queue_init(RingEntries, &ring_, 0) != 0) {
                        throw std::runtime_error("Failed to initialize any supported io_uring variant");
                    }
                }
            }

            setup_provided_buffer_ring();

            // Prime the kernel submission ring with initial non-blocking multi-shot receive operations
            prime_multishot_receive();
        }

        ~uring_duplex() noexcept {
            if (ibr_) {
                uring_shim::unregister_buffer_ring(&ring_, BGID_INGRESS);
                ::free(ibr_);
            }
            if (ingress_buffer_pool_ && ingress_buffer_pool_ != MAP_FAILED) {
                ::munmap(ingress_buffer_pool_, RingEntries * BUFFER_STRIDE);
            }
            uring_shim::ring_exit(&ring_);
        }

        uring_duplex(const uring_duplex&) = delete;
        uring_duplex& operator=(const uring_duplex&) = delete;

        /** @brief Queries whether the kernel is actively polling the submission queue. */
        [[nodiscard]] inline bool is_sqpoll_active() const noexcept {
            return (ring_.flags & IORING_SETUP_SQPOLL) != 0;
        }

        /**
         * @brief Enqueues a non-blocking multi-shot receive request to harvest incoming data streams.
         */
        void prime_multishot_receive() noexcept {
            io_uring_sqe* sqe = uring_shim::get_sqe(&ring_);
            if (__builtin_expect(sqe != nullptr, 1)) {
                // Multi-shot recv instructs the kernel to keep the poll loop active,
                // automatically consuming slots from our provided buffer ring on each burst.
                uring_shim::prep_recv_multishot(sqe, socket_fd_, nullptr, 0, 0);
                sqe->flags |= IOSQE_BUFFER_SELECT;
                sqe->buf_group = BGID_INGRESS;
                // Encode identifier tag to isolate completion signatures
                sqe->user_data = 0xFFFFFFFFFFFFFFFFULL;
                uring_shim::submit(&ring_);
            }
        }

        /**
         * @brief Vectorized core processing loop. Drives asynchronous I/O loops across the ring.
         * @note Must be execution-pinned to your isolated network worker core.
         */
        SLAB_FORCE_INLINE void poll_runtime() noexcept {
            // INDIRECTION HOISTING: Cache all member references in local registers
            auto& ring        = ring_;
            auto& pool        = mem_pool_;
            auto& in_conduit  = ingress_conduit_;
            auto& out_conduit = egress_conduit_;
            const int sock    = socket_fd_;
            const char* in_pool = ingress_buffer_pool_;
            auto& running     = running_sentinel_;
            std::size_t inflight = tx_inflight_;
            bool need_submit  = false;

            // 1. PROCESS COMPLETION QUEUE ENTRIES (CQEs) FIRST
            // This instantly frees up inflight SQE slots, drastically increasing TX batch density.
            bool multishot_terminated = false;
            
            // ARCHITECTURAL MIRROR: Drain until dry to prevent CQ saturation.
            while (true) [[likely]] {
                io_uring_cqe* cqes[MAX_BURST_SIZE];
                unsigned cqe_count = uring_shim::peek_batch_cqe(&ring, cqes, MAX_BURST_SIZE);
                if (SL_EXPECT_TRUE(cqe_count == 0)) break;

                alignas(64) typename InboundConduit::value_type rx_token_batch[MAX_BURST_SIZE];
                std::size_t rx_tokens_collected = 0;
                void* tx_release_batch[MAX_BURST_SIZE];
                unsigned tx_finished_sqes = 0;
                unsigned tx_release_count = 0;

                #if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 32
                #endif
                for (unsigned i = 0; i < cqe_count; ++i) {
                    const io_uring_cqe* cqe = cqes[i];
            
                    // SOFTWARE PREFETCH: Parallelize L1 cache fetch
                    if (i + 1 < cqe_count) [[likely]] {
                        const uint32_t nf = cqes[i+1]->flags;
                        if (nf & IORING_CQE_F_BUFFER) {
                            _mm_prefetch(in_pool + (static_cast<size_t>(nf >> IORING_CQE_BUFFER_SHIFT) << 11), _MM_HINT_T0);
                        }
                    }

                    const uint64_t user_data = cqe->user_data;
                    const int32_t res = cqe->res;
                    const uint32_t flags = cqe->flags;

                    if (user_data == 0xFFFFFFFFFFFFFFFFULL) {
                        // INBOUND DATA EVENT: Extracted from our provided buffer ring
                        if (SL_EXPECT_TRUE(res > 0)) {
                            if (SL_EXPECT_TRUE(flags & IORING_CQE_F_BUFFER)) {
                                uint16_t buffer_id = static_cast<uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
                                char* raw_buffer_ptr = const_cast<char*>(in_pool) + (static_cast<size_t>(buffer_id) << 11);

                                auto* frame = reinterpret_cast<transport::raw_tcp_frame*>(raw_buffer_ptr);
                                frame->payload_length = static_cast<uint16_t>(res);
                                frame->connection_id  = static_cast<uint32_t>(sock);

                                auto packed = core::tagged_pointer::pack(buffer_id, frame);
                                if constexpr (requires { rx_token_batch[0].embed_symmetry(0); }) {
                                    rx_token_batch[rx_tokens_collected] = typename InboundConduit::value_type(transport::raw_tcp_frame::ID, packed);
                                    rx_token_batch[rx_tokens_collected].embed_symmetry(static_cast<uint32_t>(rx_total_ingressed_ + rx_tokens_collected));
                                } else if constexpr (requires { rx_token_batch[0].anchor_to_lightcone(0); }) {
                                    rx_token_batch[rx_tokens_collected] = typename InboundConduit::value_type(packed);
                                    rx_token_batch[rx_tokens_collected].anchor_to_lightcone(rx_total_ingressed_ + rx_tokens_collected);
                                } else {
                                    rx_token_batch[rx_tokens_collected] = packed;
                                }
                                rx_tokens_collected++;
                            }
                        } else if (SL_UNLIKELY(res <= 0 && res != -EAGAIN)) {
                            multishot_terminated = true;
                            running.store(false, std::memory_order_relaxed);
                        }
                    } else {
                        tx_release_batch[tx_release_count] = reinterpret_cast<void*>(user_data);
                        tx_release_count += ((flags & IORING_CQE_F_NOTIF) == 0);
                        tx_finished_sqes += ((flags & IORING_CQE_F_MORE) == 0);
                    }
                }

                rx_total_ingressed_ += rx_tokens_collected;
                ::io_uring_cq_advance(&ring, cqe_count);
                inflight -= tx_finished_sqes;

                // AMORTIZED TX RELEASE: Bit-perfect mirror of egress optimization
                if (tx_release_count > 0) {
                    using FramePtr = decltype(pool.make_raw());
                    if constexpr (requires { pool.release_batch(reinterpret_cast<FramePtr*>(tx_release_batch), tx_release_count); }) {
                        pool.release_batch(reinterpret_cast<FramePtr*>(tx_release_batch), tx_release_count);
                    } else {
                        for (unsigned j = 0; j < tx_release_count; ++j) pool.release(static_cast<FramePtr>(tx_release_batch[j]));
                    }
                }

                if (rx_tokens_collected > 0) {
                    std::size_t pushed = in_conduit.push_batch(rx_token_batch, rx_tokens_collected);
                    if (SL_EXPECT_FALSE(pushed < rx_tokens_collected)) {
                        release_ingress_buffers_internal(rx_token_batch + pushed, rx_tokens_collected - pushed);
                    }
                }
            }

            // Auto-recovery: If multishot receive was dropped, re-prime it immediately
            if (SL_UNLIKELY(multishot_terminated && running.load(std::memory_order_relaxed))) {
                prime_multishot_receive();
                need_submit = true;
            }

            // 2. INGEST OUTBOUND BURSTS FROM THE FRAMEWORK INTERCONNECT CONDUIT
            alignas(64) typename OutboundConduit::value_type tx_batch[MAX_BURST_SIZE];
            
            // Prevent SQ saturation - calculate slots AFTER CQEs have cleared space
            const std::size_t available_slots = RingEntries > inflight ? RingEntries - inflight : 0;
            const std::size_t desired_burst = std::min(available_slots, MAX_BURST_SIZE);
            
            if (SL_EXPECT_TRUE(desired_burst > 0)) {
                std::size_t tx_count = out_conduit.pop_batch(tx_batch, desired_burst);
                std::size_t submissions = 0;

                if (tx_count > 0) {
                    #if defined(__GNUC__) && !defined(__clang__)
                    #pragma GCC unroll 32
                    #endif
                    for (std::size_t i = 0; i < tx_count; ++i) {
                        void* addr = nullptr;
                        if constexpr (std::is_pointer_v<typename OutboundConduit::value_type>) addr = static_cast<void*>(tx_batch[i]);
                        else addr = tx_batch[i].get_address();

                        auto* raw = reinterpret_cast<transport::raw_tcp_frame*>(addr);
                        void* data_ptr = addr;
                        uint32_t data_len = sizeof(typename OutboundConduit::value_type_pod);
                        
                        if constexpr (requires { static_cast<transport::raw_tcp_frame*>(nullptr)->data; }) {
                            data_ptr = raw->data;
                            data_len = raw->payload_length;
                        }

                        io_uring_sqe* sqe = uring_shim::get_sqe(&ring);
                        if (SL_EXPECT_FALSE(sqe == nullptr)) {
                            if constexpr (requires { out_conduit.revert_batch(tx_batch + i, tx_count - i); }) {
                                out_conduit.revert_batch(tx_batch + i, tx_count - i);
                            } else {
                                using FramePtr = decltype(pool.make_raw());
                                for (std::size_t j = i; j < tx_count; ++j) {
                                    void* a = nullptr;
                                    if constexpr (std::is_pointer_v<typename OutboundConduit::value_type>) a = static_cast<void*>(tx_batch[j]);
                                    else a = tx_batch[j].get_address();
                                    pool.release(static_cast<FramePtr>(a));
                                }
                            }
                            break;
                        }

                        // Native Kernel Zero-Copy Transmission Link
                        io_uring_prep_send(sqe, sock, data_ptr, data_len, MSG_DONTWAIT);
                        sqe->user_data = reinterpret_cast<uint64_t>(addr);
                        submissions++;
                    }
                    if (submissions > 0) {
                        inflight += submissions;
                        need_submit = true;
                    }
                }
            }

            tx_inflight_ = inflight;

            // DOORBELL FUSION: One kick per cycle max.
            if (need_submit) {
                uring_shim::submit(&ring);
            } else if (inflight > 0 && SL_UNLIKELY((ring.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(ring.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
                uring_shim::submit(&ring);
            }
        }

        /** @brief Internal vectorized release to avoid redundant doorbell triggers during poll loop. */
        SLAB_FORCE_INLINE void release_ingress_buffers_internal(const core::tagged_pointer* tokens, std::size_t count) noexcept {
            auto* ibr = ibr_;
            const int mask = static_cast<int>(br_mask_);
            const char* pool = ingress_buffer_pool_;
            constexpr int data_off = offsetof(transport::raw_tcp_frame, data);
            constexpr int stride_len = BUFFER_STRIDE - data_off;

            #if defined(__GNUC__) && !defined(__clang__)
            #pragma GCC unroll 32
            #endif
            for (std::size_t i = 0; i < count; ++i) {
                const uint16_t bid = static_cast<uint16_t>(tokens[i].tag());
                char* dma = const_cast<char*>(pool) + (static_cast<size_t>(bid) << 11) + data_off;
                uring_shim::buf_ring_add(ibr, dma, stride_len, bid, mask, static_cast<int>(i));
            }
            uring_shim::buf_ring_advance(ibr, static_cast<int>(count));
        }

        inline void release_ingress_buffers_batch(const core::tagged_pointer* tokens, std::size_t count) noexcept {
            if (SL_UNLIKELY(count == 0)) return;
            release_ingress_buffers_internal(tokens, count);
            if (!(ring_.flags & IORING_SETUP_SQPOLL) || SL_UNLIKELY(::io_uring_smp_load_acquire(ring_.sq.kflags) & IORING_SQ_NEED_WAKEUP)) {
                uring_shim::submit(&ring_);
            }
        }

        /**
         * @brief Returns a processed buffer back to the kernel tracking rings.
         * @param buffer_id Direct tracking index mapped inside the token's tag allocation field.
         */
        inline void release_ingress_buffer(uint16_t buffer_id) noexcept {
            char* struct_base = ingress_buffer_pool_ + (static_cast<size_t>(buffer_id) << 11);
            char* dma_target = struct_base + offsetof(transport::raw_tcp_frame, data);
            std::size_t stride_len = BUFFER_STRIDE - offsetof(transport::raw_tcp_frame, data);

            uring_shim::buf_ring_add(ibr_, dma_target, stride_len, buffer_id, br_mask_, 0);
            uring_shim::buf_ring_advance(ibr_, 1);

            // SQPOLL Optimization: Ensure the kernel thread is awake to see the returned buffers
            if (SL_UNLIKELY(::io_uring_smp_load_acquire(ring_.sq.kflags) & IORING_SQ_NEED_WAKEUP)) {
                uring_shim::submit(&ring_);
            }
        }
    };

} // namespace slabflux::transport
