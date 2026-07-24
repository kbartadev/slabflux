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
#include <type_traits>
#include <immintrin.h>
#include "slabflux/io/uring_shim.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

template <typename PoolType, typename PipelineType, std::size_t MaxPayload = 2048>
struct alignas(64) uring_duplex_stream {
    using Event = std::remove_pointer_t<decltype(std::declval<PoolType&>().make_raw())>;

    io_uring ring{};
    PoolType& pool;
    PipelineType& pipe;
    int fd{-1};

    io_uring_buf_ring* br{nullptr};
    std::size_t br_mask{0};

    Event** dma_map{nullptr};
    bool pending_submit{false};
    bool rx_active{false};

    static constexpr uint16_t BGID = 1;

public:
    uring_duplex_stream(PoolType& p, PipelineType& pl, int sock, unsigned cpu = 3)
        : pool(p), pipe(pl), fd(sock)
    {
        io_uring_params prm{};
        prm.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF | IORING_SETUP_SINGLE_ISSUER;
        prm.sq_thread_cpu = cpu;
        prm.cq_entries = 8192;

        if (::io_uring_queue_init_params(4096, &ring, &prm) != 0) {
            prm.flags = IORING_SETUP_SQPOLL;
            if (::io_uring_queue_init_params(4096, &ring, &prm) != 0) {
                if (::io_uring_queue_init(4096, &ring, 0) != 0) 
                    throw std::runtime_error("io_uring init failed");
            }
        }

        // Provided Buffer Ring size = half of pool capacity (rounded down to nearest power of 2)
        std::size_t target_br = pool.capacity() / 2;
        std::size_t b_count = 1;
        while (b_count <= target_br) b_count <<= 1;
        b_count >>= 1;
        if (b_count == 0) b_count = 1;

        const size_t bytes = b_count * sizeof(io_uring_buf);
        if (posix_memalign((void**)&br, 4096, bytes) != 0)
            throw std::runtime_error("buf ring alloc failed");

        io_uring_buf_reg reg{
            .ring_addr = (uint64_t)br,
            .ring_entries = (uint32_t)b_count,
            .bgid = BGID
        };
        if (io_uring_register_buf_ring(&ring, &reg, 0) < 0)
            throw std::runtime_error("buf ring reg failed");

        io_uring_buf_ring_init(br);
        br_mask = io_uring_buf_ring_mask((uint32_t)b_count);

        dma_map = new Event*[b_count];

        // Register raw buffers from pool into the ring
        for (std::size_t i = 0; i < b_count; ++i) {
            Event* ev = pool.make_raw();
            if (!ev) throw std::runtime_error("pool too small");

            dma_map[i] = ev;

            void* ptr = ev;
            if constexpr (requires { ev->raw_buffer; }) ptr = ev->raw_buffer;
            else if constexpr (requires { ev->data; }) ptr = ev->data;

            uring_shim::buf_ring_add(br, ptr, MaxPayload, (uint16_t)i, br_mask, (int)i);
        }
        uring_shim::buf_ring_advance(br, (int)b_count);

        prime_recv();
        uring_shim::submit(&ring);
        pending_submit = false;
    }

    ~uring_duplex_stream() {
        if (br) io_uring_unregister_buf_ring(&ring, BGID);
        if (br) free(br);
        if (dma_map) {
            // Safely return captive kernel buffers to the pool to prevent leaks
            std::size_t b_count = br_mask + 1;
            for (std::size_t i = 0; i < b_count; ++i) {
                if (dma_map[i]) pool.release(dma_map[i]);
            }
            delete[] dma_map;
        }
        io_uring_queue_exit(&ring);
    }

    /** @brief Queries whether the kernel is actively polling the submission queue. */
    [[nodiscard]] inline bool is_sqpoll_active() const noexcept {
        return (ring.flags & IORING_SETUP_SQPOLL) != 0;
    }

private:
    void prime_recv() noexcept {
        if (rx_active) return;
        io_uring_sqe* sqe = uring_shim::get_sqe(&ring);
        if (SL_EXPECT_TRUE(sqe != nullptr)) {
            uring_shim::prep_recv_multishot(sqe, fd, nullptr, 0, 0);
            sqe->buf_group = BGID;
            sqe->flags |= IOSQE_BUFFER_SELECT;
            sqe->user_data = 0xFFFFFFFFFFFFFFFFULL;
            pending_submit = true;
            rx_active = true;
        }
    }

public:
    SLAB_FORCE_INLINE bool dispatch(Event* ev, std::size_t len) noexcept {
        io_uring_sqe* sqe = uring_shim::get_sqe(&ring);
        if (SL_EXPECT_FALSE(!sqe)) {
            pool.release(ev);
            return false;
        }

        void* ptr = ev;
        if constexpr (requires { ev->raw_buffer; }) ptr = ev->raw_buffer;
        else if constexpr (requires { ev->data; }) ptr = ev->data;

        io_uring_prep_send(sqe, fd, ptr, len, MSG_DONTWAIT);
        sqe->user_data = reinterpret_cast<uint64_t>(ev);

        // Flag asynchronous hardware awakening
        pending_submit = true;
        return true;
    }

    SLAB_FORCE_INLINE void poll_runtime() noexcept {
        bool need_submit = false;

        // INDIRECTION HOISTING: Cache all member references in local registers
        auto& r = ring;
        auto* b = br;
        const int mask = static_cast<int>(br_mask);
        Event** dmap = dma_map;
        auto& p = pipe;

        // ARCHITECTURAL MIRROR: Drain until dry to prevent CQ saturation.
        while (true) [[likely]] {
            io_uring_cqe* cqes[32];
            unsigned count = uring_shim::peek_batch_cqe(&r, cqes, 32);
            if (SL_EXPECT_TRUE(count == 0)) break;

            Event* rx_batch[32];
            unsigned rx_count = 0;
            unsigned provided_count = 0;

            Event* tx_rel[32];
            unsigned tx_rel_count = 0;

            #if defined(__GNUC__) && !defined(__clang__)
            #pragma GCC unroll 32
            #endif
            for (unsigned i = 0; i < count; ++i) {
                const io_uring_cqe* cqe = cqes[i];
                const uint64_t ud = cqe->user_data;
                const int32_t res = cqe->res;
                const uint32_t fl = cqe->flags;

                if (ud == 0xFFFFFFFFFFFFFFFFULL) {
                    // Safely track if the kernel disarmed our multishot listener
                    if ((fl & IORING_CQE_F_MORE) == 0) rx_active = false;

                    // SOFTWARE PREFETCH: Parallelize L1 cache fetch with iteration logic
                    if (i + 1 < count) [[likely]] {
                        const uint32_t nf = cqes[i+1]->flags;
                        if (nf & IORING_CQE_F_BUFFER) {
                            _mm_prefetch(reinterpret_cast<const char*>(dmap[nf >> IORING_CQE_BUFFER_SHIFT]), _MM_HINT_T0);
                        }
                    }

                    if (SL_EXPECT_TRUE(fl & IORING_CQE_F_BUFFER)) {
                        uint16_t bid = static_cast<uint16_t>(fl >> IORING_CQE_BUFFER_SHIFT);
                        Event* ev = dmap[bid];

                        if (SL_EXPECT_TRUE(res > 0)) {
                            // DYNAMIC REPLENISHMENT: Extract the filled buffer and restock the kernel ring
                            Event* new_ev = pool.make_raw();
                            if (SL_EXPECT_TRUE(new_ev != nullptr)) {
                                dmap[bid] = new_ev;
                                void* ptr = new_ev;
                                if constexpr (requires { new_ev->raw_buffer; }) ptr = new_ev->raw_buffer;
                                else if constexpr (requires { new_ev->data; }) ptr = new_ev->data;
                                
                                uring_shim::buf_ring_add(b, ptr, MaxPayload, bid, mask, provided_count++);

                                if constexpr (requires { ev->payload_length; }) ev->payload_length = res;
                                else if constexpr (requires { ev->length; }) ev->length = res;
                                else if constexpr (requires { ev->buffer_length; }) ev->buffer_length = res;

                                rx_batch[rx_count++] = ev;
                            } else {
                                // BACKPRESSURE: Pool exhausted. Drop the packet and recycle the buffer.
                                void* ptr = ev;
                                if constexpr (requires { ev->raw_buffer; }) ptr = ev->raw_buffer;
                                else if constexpr (requires { ev->data; }) ptr = ev->data;
                                uring_shim::buf_ring_add(b, ptr, MaxPayload, bid, mask, provided_count++);
                            }
                        } else {
                            // Error or empty read: recycle buffer
                            void* ptr = ev;
                            if constexpr (requires { ev->raw_buffer; }) ptr = ev->raw_buffer;
                            else if constexpr (requires { ev->data; }) ptr = ev->data;

                            uring_shim::buf_ring_add(b, ptr, MaxPayload, bid, mask, provided_count++);
                        }
                    }
                } else {
                    // Branchless TCP-Bypass: Track exact lifecycle state via CQE flags
                    tx_rel[tx_rel_count] = reinterpret_cast<Event*>(ud);
                    // Safe release condition for Zero-Copy: Wait for the NOTIF CQE (F_MORE == 0)
                    tx_rel_count += ((fl & IORING_CQE_F_MORE) == 0);
                }
            }

            ::io_uring_cq_advance(&r, count);

            // Process RX batch
            for (unsigned i = 0; i < rx_count; ++i) {
                // Decouple business logic execution from hardware ring tracking
                p.dispatch(rx_batch[i]);
            }
            
            // STRICT OWNERSHIP: Reclaim RX memory after the stream pipeline finishes executing
            if (rx_count > 0) {
                if constexpr (requires { pool.release_batch(rx_batch, rx_count); })
                    pool.release_batch(rx_batch, rx_count);
                else
                    for (unsigned i = 0; i < rx_count; ++i) pool.release(rx_batch[i]);
            }
            
            // Batch increment the provided buffer ring cursor
            if (provided_count > 0) {
                uring_shim::buf_ring_advance(b, provided_count);
                need_submit = true;
            }

            // Amortized TX Release
            if (tx_rel_count > 0) {
                if constexpr (requires { pool.release_batch(tx_rel, tx_rel_count); })
                    pool.release_batch(tx_rel, tx_rel_count);
                else
                    for (unsigned i = 0; i < tx_rel_count; ++i) pool.release(tx_rel[i]);
            }
        }

        // Auto-rearm if the kernel dropped the multishot listener
        if (SL_UNLIKELY(!rx_active)) {
            prime_recv();
        }

        // FOLD LINGERING SUBMITS: Capture events enqueued during the pipeline dispatch
        need_submit |= pending_submit;
        pending_submit = false;

        // DOORBELL FUSION: Pulse check for hardware submission queue awakening
        if (need_submit) {
            uring_shim::submit(&r);
        } else if (SL_UNLIKELY((r.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(r.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
            uring_shim::submit(&r);
        }
    }
};

} // namespace slabflux::io