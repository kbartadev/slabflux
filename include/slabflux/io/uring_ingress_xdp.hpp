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
#include <sys/socket.h>
#include <immintrin.h>
#include <cstdint>
#include <cstddef>
#include "slabflux/io/xdp_shim.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core.hpp"

namespace slabflux::io {

    /**
     * @brief BPF-Mapped EBPF Concept Generator.
     * @details Statically derives the hardware interaction trace for 
     * XDP-to-UserSpace mapping. Synthesizing the relationship between
     * the UMEM matrix, the BPF maps, and the polling engine.
     */
    template <typename Pool, typename FrameT>
    concept SovereignXdpResource = requires(Pool& p) {
        { p.get_raw_ptr() } -> std::same_as<void*>;
        { p.get_raw_ptr_size() } -> std::convertible_to<std::size_t>;
        { p.capacity() } -> std::convertible_to<std::size_t>;
    };

    enum class io_backend {
        io_uring_kernel,
        af_xdp_bypass
    };

    template <typename T, std::size_t RingEntries, typename Logic, io_backend Backend = io_backend::af_xdp_bypass>
    class alignas(64) uring_ingress_xdp {
        static_assert(core::POD<T>, "XDP Ingress requires bit-perfect POD payloads.");

    private:
        ::xsk_socket* xsk_{nullptr};
        ::xsk_umem* umem_{nullptr};
        void* buffer_area_{nullptr};
        std::size_t buffer_size_{0};

        ::xsk_ring_prod fill_ring{};
        ::xsk_ring_cons comp_ring{};
        ::xsk_ring_cons rx_ring{};
        ::xsk_ring_prod tx_ring{};

        Logic& logic_;
        static constexpr uint32_t BATCH_SIZE = 32; // Boosted to maximize cache-line prefetch density

    public:
        /**
         * @brief Sovereign Nexus Initialization.
         * @details Replaces generic backend selection with a statically-pinned 
         * hardware ingress pipeline.
         */
        explicit uring_ingress_xdp(Logic& logic) noexcept 
        : logic_(logic) {}

        static constexpr size_t DATA_OFFSET = [](){
            using FrameType = T;
            if constexpr (requires { static_cast<FrameType*>(nullptr)->data; }) {
                return offsetof(FrameType, data);
            } else if constexpr (requires { static_cast<FrameType*>(nullptr)->payload; }) {
                return offsetof(FrameType, payload);
            }
            return size_t{0};
        }();

        /**
         * @brief Unified polling interface matching the test suite.
         * @details FUSED VECTORIZED PATH: Replaces the double-loop traversal with 
         * a single-pass address rematerializer and logic dispatcher.
         */
        template<typename PoolType>
        SLAB_HOT void poll(PoolType& /*unused*/) noexcept {
            if (SL_UNLIKELY(!xsk_)) return;

            // 1. PEEK RX RING (Collect available descriptors)
            uint32_t rx_idx = 0;
            const uint32_t rcvd = xdp_shim::rx_peek(&rx_ring, BATCH_SIZE, &rx_idx);
            if (SL_EXPECT_TRUE(rcvd == 0)) return;

            // 2. INDIRECTION HOISTING: Cache base pointers in registers
            const char* __restrict__ umem_base = static_cast<const char*>(buffer_area_);
            const size_t offset_const = DATA_OFFSET;

            alignas(64) uint64_t recycled_addrs[BATCH_SIZE];

            // VECTORIZED DISPATCH: If logic supports it, pass the entire batch to avoid call overhead
            if constexpr (requires { logic_.on_vector_batch(static_cast<T**>(nullptr), 0); }) {
                alignas(64) T* batch[BATCH_SIZE];
                uint32_t valid_count = 0;
                #if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 32
                #endif
                for (uint32_t i = 0; i < rcvd; ++i) {
                    const ::xdp_desc* desc = xdp_shim::rx_desc(&rx_ring, rx_idx + i);
                    recycled_addrs[i] = desc->addr;
                    T* frame = reinterpret_cast<T*>(const_cast<char*>(umem_base + desc->addr - offset_const));
                    
                    // PILLAR I: Symplectic Resonance Fencing (Topological Vaporization)
                    if constexpr (requires { frame->validate_resonance(); }) {
                        if (__builtin_expect(!frame->validate_resonance(), 0)) [[unlikely]] continue;
                    }
                    batch[valid_count++] = frame;
                }
                if (SL_EXPECT_TRUE(valid_count > 0)) logic_.on_vector_batch(batch, valid_count);
            } else {
                // Scalar Fallback
                #if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 32
                #endif
            for (uint32_t i = 0; i < rcvd; ++i) {
                const ::xdp_desc* desc = xdp_shim::rx_desc(&rx_ring, rx_idx + i);
                    recycled_addrs[i] = desc->addr;

                // Register-Local Resolution: Rematerialize virtual address in 1 cycle.
                    T* frame_ptr = reinterpret_cast<T*>(const_cast<char*>(umem_base + desc->addr - offset_const));

                    // PILLAR I: Symplectic Resonance Fencing
                    if constexpr (requires { frame_ptr->validate_resonance(); }) {
                        if (__builtin_expect(!frame_ptr->validate_resonance(), 0)) [[unlikely]] continue;
                    }

                logic_.on_raw_frame(frame_ptr, desc->len);
            }
            }
            
            // 3. ATOMIC COMMITMENT
            xdp_shim::rx_release(&rx_ring, rcvd);

            // 4. REFILL RESERVATION
            uint32_t fill_idx = 0;
            const uint32_t reserved = xdp_shim::fill_reserve(&fill_ring, rcvd, &fill_idx);
            if (SL_EXPECT_TRUE(reserved > 0)) {
                #if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 32
                #endif
                for (uint32_t i = 0; i < reserved; ++i) {
                    *xdp_shim::fill_addr(&fill_ring, fill_idx + i) = recycled_addrs[i];
                }
                xdp_shim::fill_submit(&fill_ring, reserved);
            }

            if (xdp_shim::fill_needs_wakeup(&fill_ring)) {
                ::recvmsg(xdp_shim::socket_fd(xsk_), nullptr, MSG_DONTWAIT);
            }
        }

        ~uring_ingress_xdp() noexcept {
            if (xsk_) xdp_shim::socket_delete(xsk_);
            if (umem_) xdp_shim::umem_delete(umem_);
        }

        uring_ingress_xdp(const uring_ingress_xdp&) = delete;
        uring_ingress_xdp& operator=(const uring_ingress_xdp&) = delete;

        /**
         * @brief Concept-Verified NIC Synchronization.
         * @details Statically validates the memory resource and maps it directly 
         * to the hardware ingress pipeline.
         */
        template <SovereignXdpResource<T> Pool>
        [[nodiscard]] bool bind_to_nic(const char* ifname, uint32_t queue_id, Pool& pool) noexcept {
            buffer_area_ = pool.get_raw_ptr();
            buffer_size_ = pool.get_raw_ptr_size();

            xdp_shim::umem_params umem_p{};
            umem_p.fill_size = 2048; 
            umem_p.comp_size = 2048;
            umem_p.frame_size = 4096;
            umem_p.headroom = 0;
            umem_p.flags = 0;

            if (xdp_shim::umem_create(&umem_, buffer_area_, buffer_size_, &fill_ring, &comp_ring, umem_p) < 0) {
                return false;
            }

            xdp_shim::socket_params sock_p{};
            sock_p.rx_size = 2048;
            sock_p.tx_size = 2048;
            sock_p.xdp_flags = 0;
            sock_p.bind_flags = 0;

            if (xdp_shim::socket_create(&xsk_, ifname, queue_id, umem_, &rx_ring, &tx_ring, sock_p) < 0) {
                return false;
            }

            return true;
        }

        /**
         * @brief Physical Address Rematerializer.
         * @details Optimized RX poll utilizing a batch rematerialization 
         * strategy to bypass the kernel-user boundary tax.
         */
        inline uint32_t poll_rx(void** out_frames) noexcept {
            if (SL_UNLIKELY(!xsk_)) return 0;
            uint32_t idx = 0;
            uint32_t rcvd = xdp_shim::rx_peek(&rx_ring, BATCH_SIZE, &idx);
            if (__builtin_expect(rcvd == 0, 1)) {
                return 0;
            }

            alignas(64) uint64_t recycled_addrs[BATCH_SIZE];

            #if defined(__GNUC__) && !defined(__clang__)
            #pragma GCC unroll 32
            #endif
            for (uint32_t i = 0; i < rcvd; ++i) {
                const ::xdp_desc* desc = xdp_shim::rx_desc(&rx_ring, idx + i);
                recycled_addrs[i] = desc->addr;

                // Register-Local Resolution: Derives virtual address from UMEM offset
                out_frames[i] = static_cast<char*>(buffer_area_) + desc->addr;
            }

            xdp_shim::rx_release(&rx_ring, rcvd);

            // Resilient Batch Refill: Handles partial allocations to protect against descriptor starvation
            uint32_t fill_idx = 0;
            uint32_t reserved = xdp_shim::fill_reserve(&fill_ring, rcvd, &fill_idx);

            if (__builtin_expect(reserved > 0, 1)) {
                #if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 32
                #endif
                for (uint32_t i = 0; i < reserved; ++i) {
                    *xdp_shim::fill_addr(&fill_ring, fill_idx + i) = recycled_addrs[i];
                }
                xdp_shim::fill_submit(&fill_ring, reserved);
            }

            // Fallback wakeups triggered exclusively when the driver core falls into an idle state
            if (xdp_shim::fill_needs_wakeup(&fill_ring)) {
                ::recvmsg(xdp_shim::socket_fd(xsk_), nullptr, MSG_DONTWAIT);
            }

            return rcvd;
        }

        /**
         * @brief Clears the completion ring to recycle outbound transmitted frames.
         */
        inline void drain_tx_completion() noexcept {
            uint32_t idx = 0;
            alignas(64) uint64_t addrs[BATCH_SIZE];
            uint32_t completed = xdp_shim::rx_peek(&comp_ring, BATCH_SIZE, &idx);

            if (completed > 0) {
                #if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 32
                #endif
                for (uint32_t i = 0; i < completed; ++i) {
                    addrs[i] = *xdp_shim::comp_addr(&comp_ring, idx + i);
                }
                xdp_shim::rx_release(&comp_ring, completed);

                uint32_t fill_idx = 0;
                uint32_t reserved = xdp_shim::fill_reserve(&fill_ring, completed, &fill_idx);
                if (reserved > 0) {
                    #if defined(__GNUC__) && !defined(__clang__)
                    #pragma GCC unroll 32
                    #endif
                    for (uint32_t i = 0; i < reserved; ++i) {
                        *xdp_shim::fill_addr(&fill_ring, fill_idx + i) = addrs[i];
                    }
                    xdp_shim::fill_submit(&fill_ring, reserved);
                }
            }
        }
    };
} // namespace slabflux::io
