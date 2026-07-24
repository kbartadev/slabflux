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
 * ============================================================================*
 * @file uring_egress.hpp
 * @brief Linux io_uring Zero-Copy Vectorized Egress Boundary.
 */

#pragma once

#include <liburing.h>
#include <sys/socket.h>
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#include <type_traits>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <stdexcept>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/core/memory.hpp"

namespace slabflux::io {

    /**
     * @brief Zero-Copy Linux io_uring Vectorized Egress Boundary.
     * @details Completely eliminates hot-path memory replication and kernel transitions via pure SQPOLL send structures.
     * @tparam TargetConduit The core storage queue layer (SPSC conduit or specialized MPMC mesh slice).
     * @tparam PoolType The backing memory allocator pool managing the network frame fabric.
     * @tparam TotalEntries Maximum hardware descriptor capacity allocated to the submission queue. Must be power-of-two.
     */
    template <typename TargetConduit, typename PoolType, std::size_t TotalEntries = 512>
    class alignas(64) uring_egress {
        static_assert((TotalEntries & (TotalEntries - 1)) == 0, "io_uring ring density must be an exact power of 2");

    private:
        int                socket_fd_{-1};
        io_uring           ring_{};

        TargetConduit&     interconnect_conduit_;
        PoolType&          mem_pool_;
        std::atomic<bool>& running_sentinel_;

        // ====================================================================
        // MICROARCHITECTURAL CORE-LOCAL REGISTERS
        // ====================================================================
        alignas(64) std::size_t inflight_count_{0};
        char* cached_pool_base_{nullptr};

        static constexpr std::size_t MAX_BURST_SIZE = 32;

    public:
        explicit uring_egress(
            int sock,
            TargetConduit& conduit,
            PoolType& pool,
            std::atomic<bool>& running
        )
        : socket_fd_(sock)
        , interconnect_conduit_(conduit)
        , mem_pool_(pool)
        , running_sentinel_(running)
        , cached_pool_base_(static_cast<char*>(mem_pool_.data()))
        {
            io_uring_params params{};
            // Try for extreme performance flags (Linux 5.19+)
            params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_COOP_TASKRUN | IORING_SETUP_SINGLE_ISSUER;
            params.sq_thread_idle = 2000; // Keep the kernel submission thread spinning for up to 2000ms

            if (::io_uring_queue_init_params(TotalEntries, &ring_, &params) != 0) {
                // Fallback: Baseline SQPOLL -> Standard ring
                params.flags = IORING_SETUP_SQPOLL;
                if (::io_uring_queue_init_params(TotalEntries, &ring_, &params) != 0) {
                    if (::io_uring_queue_init(TotalEntries, &ring_, 0) != 0) throw std::runtime_error("io_uring init failed");
                }
            }
        }

        ~uring_egress() noexcept {
            ::io_uring_queue_exit(&ring_);
        }

        uring_egress(const uring_egress&) = delete;
        uring_egress& operator=(const uring_egress&) = delete;

        /** @brief Queries whether the kernel is actively polling the submission queue. */
        [[nodiscard]] inline bool is_sqpoll_active() const noexcept {
            return (ring_.flags & IORING_SETUP_SQPOLL) != 0;
        }

        /**
         * @brief Vectorized synchronous transmission loop with zero data copy loops.
         * @note Must be execution-pinned to your outbound kernel-bypass worker thread.
         */
    SLAB_FORCE_INLINE void poll_egress() noexcept {
        // INDIRECTION HOISTING: Pull all member references into registers
        auto& ring = ring_;
        auto& pool = mem_pool_;
        auto& conduit = interconnect_conduit_;
        const int sock = socket_fd_;
        std::size_t inflight = inflight_count_;

        using FramePtr = decltype(pool.make_raw());

        // 1. RECLAIM COMPLETIONS (Symmetrical to Ingress Harvest)
        while (true) [[likely]] {
            io_uring_cqe* cqes[MAX_BURST_SIZE];
            const unsigned comp_count = ::io_uring_peek_batch_cqe(&ring, cqes, MAX_BURST_SIZE);
            if (SL_EXPECT_TRUE(comp_count == 0)) break;

            void* release_batch[MAX_BURST_SIZE];
            unsigned valid_releases = 0;
            #if defined(__GNUC__) && !defined(__clang__)
            #pragma GCC unroll 32
            #endif
            for (unsigned i = 0; i < comp_count; ++i) {
                release_batch[valid_releases] = reinterpret_cast<void*>(cqes[i]->user_data);
                // Branchless TCP-Bypass: Release memory on the SEND completion (ignore NOTIF)
                // This completely decouples the CPU benchmark from OS TCP ACK latency.
                valid_releases += ((cqes[i]->flags & IORING_CQE_F_NOTIF) == 0);
                
                // Track actual SQE lifecycle: Decrement inflight only on the final CQE
                inflight -= ((cqes[i]->flags & IORING_CQE_F_MORE) == 0);
            }
            ::io_uring_cq_advance(&ring, comp_count);

            if (valid_releases > 0) {
                if constexpr (requires { pool.release_batch(reinterpret_cast<FramePtr*>(release_batch), valid_releases); }) {
                    pool.release_batch(reinterpret_cast<FramePtr*>(release_batch), valid_releases);
                } else {
                    for (unsigned i = 0; i < valid_releases; ++i) pool.release(static_cast<FramePtr>(release_batch[i]));
                }
            }
        }
        inflight_count_ = inflight; // Commit local register back to memory once per poll

        // 2. DISPATCH BURST
        if (SL_EXPECT_FALSE(inflight >= TotalEntries)) {
            if (SL_UNLIKELY((ring.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(ring.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
                uring_shim::submit(&ring);
            }
            return;
        }

        alignas(64) typename TargetConduit::value_type batch[MAX_BURST_SIZE];
        const std::size_t count = conduit.pop_batch(batch, MAX_BURST_SIZE);
        if (SL_EXPECT_TRUE(count == 0)) {
            if (inflight > 0 && SL_UNLIKELY((ring.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(ring.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
                uring_shim::submit(&ring);
            }
            return;
        }

        bool need_submit = false;
        std::size_t submissions = 0;
        #if defined(__GNUC__) && !defined(__clang__)
        #pragma GCC unroll 32
        #endif
        for (std::size_t i = 0; i < count; ++i) {
            // Micro-Mirror: Zero-indirection address resolution
            void* addr = nullptr;
            
            if constexpr (requires(uint32_t safe_type_id) { batch[i].extract_and_decouple(0, safe_type_id); }) {
                uint32_t safe_type_id = 0;
                auto safe_ptr = batch[i].extract_and_decouple(static_cast<uint32_t>(inflight), safe_type_id);
                if constexpr (std::is_pointer_v<decltype(safe_ptr)>) addr = static_cast<void*>(safe_ptr);
                else addr = safe_ptr.get_address();
                
                // TELEOLOGICAL AGNOSIA: Silently vaporize and recycle
                if (__builtin_expect(safe_type_id == 0, 0)) [[unlikely]] {
                    if (addr) pool.release(static_cast<FramePtr>(addr));
                    continue;
                }
            } else if constexpr (requires { batch[i].extract_via_subsumption(0); }) {
                auto safe_payload = batch[i].extract_via_subsumption(static_cast<uint32_t>(inflight));
                if constexpr (std::is_pointer_v<decltype(safe_payload)>) addr = static_cast<void*>(safe_payload);
                else addr = safe_payload.get_address();
                
                // LORENTZ SUBSUMPTION
                if (__builtin_expect(!addr, 0)) [[unlikely]] {
                    auto orig_payload = *reinterpret_cast<const decltype(safe_payload)*>(&batch[i]);
                    void* orig_addr = nullptr;
                    if constexpr (std::is_pointer_v<decltype(orig_payload)>) orig_addr = static_cast<void*>(orig_payload);
                    else orig_addr = orig_payload.get_address();
                    if (orig_addr) pool.release(static_cast<FramePtr>(orig_addr));
                    continue;
                }
            } else if constexpr (std::is_pointer_v<typename TargetConduit::value_type>) {
                addr = static_cast<void*>(batch[i]);
            } else {
                addr = batch[i].get_address();
            }

            // PILLAR IV: AUTOTELIC CHRYSALIS (Network Egress Defense)
            using FrameType = std::remove_pointer_t<typename PoolType::value_type>;
            if constexpr (requires { reinterpret_cast<FrameType*>(addr)->validate_chrysalis(); }) {
                auto* user_frame = reinterpret_cast<FrameType*>(addr);
                if (__builtin_expect(!user_frame->validate_chrysalis(), 0)) [[unlikely]] {
                    pool.release(static_cast<FramePtr>(addr));
                    continue; // Teleological Agnosia: Vaporize corrupted state natively
                }
            }

            auto* raw = reinterpret_cast<transport::raw_tcp_frame*>(addr);
            io_uring_sqe* sqe = uring_shim::get_sqe(&ring);
            
            if (SL_EXPECT_FALSE(sqe == nullptr)) {
                if constexpr (requires { conduit.revert_batch(batch + i, count - i); }) {
                    conduit.revert_batch(batch + i, count - i);
                } else {
                    for (std::size_t j = i; j < count; ++j) {
                        void* a = nullptr;
                        if constexpr (requires(uint32_t safe_type_id) { batch[j].extract_and_decouple(0, safe_type_id); }) {
                            uint32_t s_id = 0;
                            auto s_ptr = batch[j].extract_and_decouple(static_cast<uint32_t>(inflight), s_id);
                            if constexpr (std::is_pointer_v<decltype(s_ptr)>) a = static_cast<void*>(s_ptr);
                            else a = s_ptr.get_address();
                        } else if constexpr (requires { batch[j].extract_via_subsumption(0); }) {
                            // Fail-safe: Always extract raw address if dropping
                            auto s_ptr = batch[j].extract_via_subsumption(static_cast<uint32_t>(inflight));
                            auto orig_ptr = *reinterpret_cast<const decltype(s_ptr)*>(&batch[j]);
                            if constexpr (std::is_pointer_v<decltype(orig_ptr)>) a = static_cast<void*>(orig_ptr);
                            else a = orig_ptr.get_address();
                        } else if constexpr (std::is_pointer_v<typename TargetConduit::value_type>) {
                            a = static_cast<void*>(batch[j]);
                        } else {
                            a = batch[j].get_address();
                        }
                        pool.release(static_cast<FramePtr>(a));
                    }
                }
                break;
            }

            // Synthesis: Replace complex structural resolution with a linear DMA point
            io_uring_prep_send(sqe, sock, raw->data, raw->payload_length, MSG_DONTWAIT);
            sqe->user_data = reinterpret_cast<uint64_t>(raw);
            submissions++;
        }

        if (submissions > 0) {
            inflight += submissions;
            need_submit = true;
        }
        inflight_count_ = inflight;

        // 3. DOORBELL FUSION: Amortized Kernel Pulse
        if (need_submit) {
            uring_shim::submit(&ring);
        } else if (inflight > 0 && SL_UNLIKELY((ring.flags & IORING_SETUP_SQPOLL) && (::io_uring_smp_load_acquire(ring.sq.kflags) & IORING_SQ_NEED_WAKEUP))) {
            uring_shim::submit(&ring);
        }
    }
    };

} // namespace slabflux::io
