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
#include <string_view>
#include <cstdint>
#include <atomic>
#include "../hw/intrinsics.hpp"
#include "core.hpp"

namespace slabflux::hft {

    using namespace core;

    template<typename BusinessLogic>
    struct alignas(64) stall_free_nexus {
        io_uring ring;
        io_uring_buf_ring* buf_ring_{ nullptr };
        pool<char>& memory_pool;
        BusinessLogic& logic;

        alignas(64) uint64_t idle_polls_{ 0 };
        unsigned buf_ring_tail_{ 0 };
        static constexpr uint64_t SPIN_THRESHOLD = 1024;

        static constexpr uint16_t BUFFER_GROUP_ID = 1;
        static constexpr unsigned BATCH_LIMIT = 8;

        stall_free_nexus(pool<char>& p, BusinessLogic& l, unsigned cpu_core)
            : memory_pool(p), logic(l) {
            io_uring_params params{};
            params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF | IORING_SETUP_SINGLE_ISSUER;
            params.sq_thread_cpu = cpu_core;
            params.cq_entries = 4096;

            io_uring_queue_init_params(2048, &ring, &params);

            int ret;
            buf_ring_ = io_uring_setup_buf_ring(&ring, memory_pool.capacity(), BUFFER_GROUP_ID, 0, &ret);
            
            const uint32_t b_mask = io_uring_buf_ring_mask(memory_pool.capacity());
            for (uint16_t i = 0; i < memory_pool.capacity(); ++i) {
                io_uring_buf_ring_add(buf_ring_, memory_pool.get_raw_ptr_by_id(i),
                                      1024, i, b_mask, i);
            }
            io_uring_buf_ring_advance(buf_ring_, memory_pool.capacity());

            // Initial Arm
            io_uring_sqe* sqe = io_uring_get_sqe(&ring);
            io_uring_prep_recv_multishot(sqe, 0, nullptr, 0, 0);
            sqe->buf_group = BUFFER_GROUP_ID;
            sqe->flags |= IOSQE_BUFFER_SELECT;
            io_uring_submit(&ring);
        }

        /**
         * @brief Arms a socket for zero-copy multishot reception.
         */
        void arm_socket(int fd) noexcept {
            io_uring_sqe* sqe = io_uring_get_sqe(&ring);
            io_uring_prep_recv_multishot(sqe, fd, nullptr, 0, 0);
            sqe->buf_group = BUFFER_GROUP_ID;
            sqe->flags |= IOSQE_BUFFER_SELECT;
            io_uring_submit(&ring);
        }

    private:
        /**
         * @brief CPU yield backoff.
         */
        SLAB_FORCE_INLINE void apply_backoff(const unsigned* ktail, unsigned current_val) noexcept {
            if (++idle_polls_ < SPIN_THRESHOLD) { _mm_pause(); return; }

            // C++20 wait primitive for power-efficient adaptive backoff
            // Removed std::atomic_ref as requested. Reverting to busy-spin for longer waits.
            // Note: This might increase CPU usage during prolonged idleness compared to std::atomic::wait.
            while (io_uring_smp_load_acquire(ktail) == current_val) { _mm_pause(); }
        }

    public:
        [[gnu::always_inline]] inline void poll_and_execute() noexcept {
            const unsigned* ktail = ring.cq.ktail;
            const unsigned tail = io_uring_smp_load_acquire(ktail);
            const unsigned head = *ring.cq.khead;

            if (head == tail) [[likely]] {
                apply_backoff(ktail, tail);
                return;
            }

            // Reset heuristics on successful ingestion
            idle_polls_ = 0;

            const unsigned mask = *ring.cq.kring_mask;
            const unsigned available = tail - head;
            const unsigned batch_count = available < BATCH_LIMIT ? available : BATCH_LIMIT;

            uint16_t consumed_bids[BATCH_LIMIT];
            const char* method_ptrs[BATCH_LIMIT];
            uint32_t method_lens[BATCH_LIMIT];
            const char* payload_ptrs[BATCH_LIMIT];
            uint32_t payload_lens[BATCH_LIMIT];

            uint64_t valid_mask = 0;

            const __m256i v_space = _mm256_set1_epi8(' ');

#pragma GCC unroll 8
            for (unsigned i = 0; i < batch_count; ++i) {
                io_uring_cqe* cqe = &ring.cq.cqes[(head + i) & mask];
                const uint16_t bid = static_cast<uint16_t>(cqe->flags >> IORING_CQE_BUFFER_SHIFT);
                consumed_bids[i] = bid;
                
                const char* ptr = static_cast<const char*>(memory_pool.get_raw_ptr_by_id(bid));

                if (i + 1 < batch_count) [[likely]] {
                    const uint16_t next_bid = static_cast<uint16_t>(ring.cq.cqes[(head + i + 1) & mask].flags >> IORING_CQE_BUFFER_SHIFT);
                    _mm_prefetch(memory_pool.get_raw_ptr_by_id(next_bid), _MM_HINT_T0);
                }

                __m256i chunk = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr));
                uint32_t space_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_space));

                if (SL_EXPECT_TRUE(space_mask != 0)) {
                    uint32_t space1_idx = slabflux::hw::tzcnt_32(space_mask);

                    // HFT Protocol Invariant: Methods are short verbs (e.g., GET, PUB, NEW).
                    // If the first word exceeds 4 characters, it is part of the payload.
                    if (space1_idx <= 4) {
                        method_ptrs[i] = ptr;
                        method_lens[i] = space1_idx;
                        
                        const char* p_start = ptr + space1_idx + 1;
                        const char* p_end = ptr + cqe->res;
                        while (p_start < p_end && *p_start == ' ') p_start++;
                        
                        payload_ptrs[i] = p_start;
                        payload_lens[i] = static_cast<uint32_t>(p_end - p_start);
                    } else {
                        // Content Guard: Treat the entire block as payload (e.g. for "Sovereign Intelligence")
                        method_ptrs[i] = nullptr;
                        method_lens[i] = 0;
                        payload_ptrs[i] = ptr;
                        payload_lens[i] = cqe->res;
                    }
                    valid_mask |= (1ULL << i);
                } else {
                    // Fallback: No space found, treat the entire block as payload
                    method_ptrs[i] = nullptr;
                    method_lens[i] = 0;
                    payload_ptrs[i] = ptr;
                    payload_lens[i] = cqe->res;
                    valid_mask |= (1ULL << i);
                }
            }

            uint64_t active_bits = valid_mask;
            while (active_bits) {
                uint32_t idx = static_cast<uint32_t>(slabflux::hw::tzcnt_64(active_bits));

                logic.on_fast_path(
                    std::string_view(method_ptrs[idx], method_lens[idx]),
                    std::string_view(payload_ptrs[idx], payload_lens[idx])
                );

                active_bits &= (active_bits - 1);
            }

            const uint32_t b_mask = io_uring_buf_ring_mask(memory_pool.capacity());
            for (unsigned i = 0; i < batch_count; ++i) {
                io_uring_buf_ring_add(buf_ring_, 
                                      memory_pool.get_raw_ptr_by_id(consumed_bids[i]),
                                      1024, consumed_bids[i], b_mask, buf_ring_tail_++);
            }

            io_uring_buf_ring_advance(buf_ring_, batch_count);
            io_uring_smp_store_release(ring.cq.khead, head + batch_count);

            if (SL_EXPECT_FALSE(io_uring_smp_load_acquire(ring.sq.kflags) & IORING_SQ_NEED_WAKEUP)) {
                ::io_uring_enter(ring.ring_fd, batch_count, 0, IORING_ENTER_SQ_WAKEUP, nullptr);
            }
        }
    };

} // namespace slabflux::hft
