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
 * @file af_xdp_ingress.hpp
 * @brief Physical Zero-copy via AF_XDP.
 * @details Maps NIC hardware descriptors directly to the Slab Allocator. 
 * Eliminates kernel overhead entirely.
 */

#pragma once

#include <sys/socket.h>
#include <immintrin.h>
#include <memory>
#include "slabflux/io/xdp_shim.hpp"
#include <concepts>
#include <cstdint>
#include <cstddef>

#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    class af_xdp_ingress;

    /**
     * @brief UMEM Frame Deleter.
     * @details Implements the C++20 deleter contract to automatically recycle 
     * buffers back to the NIC Fill Ring upon destruction.
     */
    struct xdp_frame_deleter {
        af_xdp_ingress* parent{ nullptr };
        uint64_t umem_addr{ 0 };

        inline void operator()(char* /*ptr*/) noexcept;
    };

    /** @brief Type-safe Frame handle. */
    using xdp_frame_ptr = std::unique_ptr<char, xdp_frame_deleter>;

    // Sovereign UMEM Provider Invariant.
    // Ensures the memory resource satisfies physical residency
    // requirements and exposes O(1) topology for XDP descriptor mapping.
    template <typename Pool>
    concept SovereignUmemProvider = requires(Pool& p) {
        { p.get_raw_ptr() } -> std::same_as<void*>;
        { p.get_raw_ptr_size() } -> std::convertible_to<std::size_t>; // Total size in bytes
        { p.get_raw_ptr_by_id(uint32_t{}) } -> std::same_as<typename Pool::value_type*>;
        { p.capacity() } -> std::convertible_to<std::size_t>; // Number of elements
    };

    /** @brief Validates that a type satisfies the Frame requirements. */
    template <typename T>
    concept XdpFrame = std::same_as<T, xdp_frame_ptr>;

    class alignas(64) af_xdp_ingress {
    private:
        friend struct xdp_frame_deleter;
        ::xsk_socket* xsk_{nullptr};
        ::xsk_umem* umem_{nullptr};
        void* buffer_area_{nullptr};

        ::xsk_ring_prod fill_ring_{};
        ::xsk_ring_cons comp_ring_{};
        ::xsk_ring_cons rx_ring_{};
        ::xsk_ring_prod tx_ring_{};

        static constexpr uint32_t BATCH_SIZE = 32;

    public:
        af_xdp_ingress() noexcept = default;

        ~af_xdp_ingress() noexcept {
            if (xsk_) xdp_shim::socket_delete(xsk_);
            if (umem_) xdp_shim::umem_delete(umem_);
        }

        af_xdp_ingress(const af_xdp_ingress&) = delete;
        af_xdp_ingress& operator=(const af_xdp_ingress&) = delete;

        /**
         * @brief Concept-Verified UMEM Registration Engine.
         * @details Statically validates the memory provider and maps it directly 
         * to the hardware ingress pipeline.
         */
        template <SovereignUmemProvider Pool>
        [[nodiscard]] bool bind_to_nic(const char* ifname, uint32_t queue_id, Pool& pool) noexcept {
            buffer_area_ = pool.get_raw_ptr();
            const size_t pool_bytes = pool.get_raw_ptr_size(); // Use the pool's actual allocated size in bytes

            xdp_shim::umem_params umem_p{};
            umem_p.fill_size = 2048; // Diverged from default num descs
            umem_p.comp_size = 2048; 
            umem_p.frame_size = 4096;
            umem_p.headroom = 0;
            umem_p.flags = 0;

            if (xdp_shim::umem_create(&umem_, buffer_area_, pool_bytes, &fill_ring_, &comp_ring_, umem_p) < 0) {
                return false;
            }

            xdp_shim::socket_params sock_p{};
            sock_p.rx_size = 2048;
            sock_p.tx_size = 2048;
            sock_p.xdp_flags = 0;
            sock_p.bind_flags = 0;

            if (xdp_shim::socket_create(&xsk_, ifname, queue_id, umem_, &rx_ring_, &tx_ring_, sock_p) < 0) {
                return false;
            }

            return true;
        }

        /**
         * @brief Physical Recycling.
         * @details O(1) wait-free injection into the hardware Fill Ring.
         * Replaces standard imperative loops with atomic commit sequences.
         */
        SLAB_FORCE_INLINE void recycle_frame(uint64_t addr) noexcept {
            uint32_t fill_idx = 0;
            if (SL_EXPECT_TRUE(xdp_shim::fill_reserve(&fill_ring_, 1, &fill_idx) == 1)) {
                *xdp_shim::fill_addr(&fill_ring_, fill_idx) = addr;
                xdp_shim::fill_submit(&fill_ring_, 1);

                if (xdp_shim::fill_needs_wakeup(&fill_ring_)) [[unlikely]] {
                    ::recvmsg(xdp_shim::socket_fd(xsk_), nullptr, MSG_DONTWAIT);
                }
            }
        }

        /**
         * @brief Physical Reaper.
         * @details Optimized RX poll utilizing a batch rematerialization 
         * strategy, wrapping UMEM offsets in C++20 smart handles.
         */
        SLAB_FORCE_INLINE uint32_t poll_rx(xdp_frame_ptr* out_frames) noexcept {
            if (SL_UNLIKELY(!xsk_)) return 0;
            uint32_t idx = 0;
            uint32_t rcvd = xdp_shim::rx_peek(&rx_ring_, BATCH_SIZE, &idx);

            if (__builtin_expect(rcvd == 0, 1)) {
                return 0;
            }

            // VECTOR PATH: Batch address rematerialization
            alignas(64) char* resolved_ptrs[BATCH_SIZE];
            const uintptr_t base_val = reinterpret_cast<uintptr_t>(buffer_area_);

            for (uint32_t i = 0; i < rcvd; ++i) {
                const xdp_desc* desc = xdp_shim::rx_desc(&rx_ring_, idx + i);
                // Re-calculate virtual address from UMEM offset in registers
                resolved_ptrs[i] = reinterpret_cast<char*>(base_val + desc->addr);
                
                // Structural Isolation: Bind hardware offset to smart handle.
                // The unique_ptr logic remains, but its input is now vectorized.
                out_frames[i] = xdp_frame_ptr(resolved_ptrs[i], xdp_frame_deleter{this, desc->addr});

                _mm_prefetch(resolved_ptrs[i], _MM_HINT_T0);
            }

            xdp_shim::rx_release(&rx_ring_, rcvd);
            return rcvd;
        }
    };

    // Inline definition of deleter to satisfy SLAB_FORCE_INLINE requirements
    inline void xdp_frame_deleter::operator()(char*) noexcept {
        if (SL_EXPECT_TRUE(parent)) {
            parent->recycle_frame(umem_addr);
        }
    }
} // namespace slabflux::io
