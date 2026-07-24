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
 * @file uring_egress_xdp.hpp
 * @brief Zero-Copy AF_XDP Network Egress Nexus for Pinned Slab Pools.
 */

#pragma once

#include <sys/socket.h>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "slabflux/io/xdp_shim.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"

namespace slabflux::io {

    /**
     * @brief Pure Zero-Copy AF_XDP Egress Boundary Engine.
     * @details Synchronizes the core-local SPSC memory pipeline directly with native driver TX descriptor rings.
     * @tparam TargetConduit Interconnect queue layer providing ready-to-transmit packet envelopes.
     * @tparam PoolType The underlying page-locked UMEM manager block pool layout.
     * @tparam RingEntries Total descriptor slot density. Must match hardware driver capacity boundaries.
     */
    template <typename TargetConduit, typename PoolType, std::size_t RingEntries = 256>
    class alignas(64) uring_egress_xdp {
        static_assert((RingEntries & (RingEntries - 1)) == 0, "XSK Ring Slots must be an exact power of 2");

    private:
        ::xsk_socket* xsk_{nullptr};
        ::xsk_ring_prod    tx_ring_{};
        ::xsk_ring_cons    comp_ring_{};

        TargetConduit&     interconnect_conduit_;
        PoolType&          mem_pool_;
        std::atomic<bool>& running_sentinel_;

        // Core-local tracking cache structures kept inside the local core's L1D lines
        alignas(64) std::size_t inflight_count_{0};
        char* cached_umem_base_{nullptr};

        static constexpr std::size_t MAX_BURST_SIZE = 32;

    public:
        explicit uring_egress_xdp(
            ::xsk_socket* xsk,
            ::xsk_ring_prod tx,
            ::xsk_ring_cons comp,
            TargetConduit& conduit,
            PoolType& pool,
            std::atomic<bool>& running
        ) noexcept
        : xsk_(xsk)
        , tx_ring_(tx)
        , comp_ring_(comp)
        , interconnect_conduit_(conduit)
        , mem_pool_(pool)
        , running_sentinel_(running)
        , cached_umem_base_(reinterpret_cast<char*>(mem_pool_.get_ptr(0)))
        {}

        ~uring_egress_xdp() noexcept = default;

        uring_egress_xdp(const uring_egress_xdp&) = delete;
        uring_egress_xdp& operator=(const uring_egress_xdp&) = delete;

        /**
         * @brief Synchronous true zero-copy outbound transmission loop with amortized doorbells.
         * @note Must be assigned exclusively to your execution-pinned network core.
         */
        inline void poll_egress() noexcept {
            if (SL_UNLIKELY(!xsk_)) return;

            // 1. RECLAIM TRANSMIT COMPLETIONS (Batch release back to the shared UMEM block allocator)
            if (inflight_count_ > 0) {
                uint32_t comp_idx = 0;
                uint32_t comp_count = xdp_shim::rx_peek(&comp_ring_, MAX_BURST_SIZE, &comp_idx);

                if (comp_count > 0) {
                    using FramePtr = decltype(mem_pool_.make_raw());
                    FramePtr release_batch[MAX_BURST_SIZE];
                    
                    #if defined(__GNUC__) && !defined(__clang__)
                    #pragma GCC unroll 32
                    #endif
                    for (uint32_t i = 0; i < comp_count; ++i) {
                        uint64_t addr = *xdp_shim::comp_addr(&comp_ring_, comp_idx + i);
                        release_batch[i] = reinterpret_cast<FramePtr>(cached_umem_base_ + addr);
                    }
                    
                    if constexpr (requires { mem_pool_.release_batch(release_batch, comp_count); }) {
                        mem_pool_.release_batch(release_batch, comp_count);
                    } else {
                        for (uint32_t i = 0; i < comp_count; ++i) mem_pool_.release(release_batch[i]);
                    }
                    
                    xdp_shim::rx_release(&comp_ring_, comp_count);
                    inflight_count_ -= comp_count;
                }
            }

            // 2. CHECK HARDWARE ACCESSIBILITY THRESHOLD
            if (__builtin_expect(inflight_count_ >= RingEntries, 0)) {
                #if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
                #endif
                return;
            }

            // 3. INGEST VECTOR BURST FROM INTERCONNECT CONDUIT
            std::size_t max_allowed_slots = RingEntries - inflight_count_;
            std::size_t desired_burst = (max_allowed_slots < MAX_BURST_SIZE) ? max_allowed_slots : MAX_BURST_SIZE;

            alignas(64) typename TargetConduit::value_type batch[MAX_BURST_SIZE];
            std::size_t count = interconnect_conduit_.pop_batch(batch, desired_burst);

            if (__builtin_expect(count == 0, 1)) {
                return;
            }

            // 4. RESERVE DRIVER DESCRIPTOR SLOTS IN BULK
            uint32_t tx_idx = 0;
            uint32_t reserved_slots = xdp_shim::fill_reserve(&tx_ring_, static_cast<uint32_t>(count), &tx_idx);

            if (__builtin_expect(reserved_slots < count, 0)) {
                if (reserved_slots == 0) {
                    interconnect_conduit_.revert_batch(batch, count);
                    return;
                }
                interconnect_conduit_.revert_batch(batch + reserved_slots, count - reserved_slots);
                count = reserved_slots;
            }

            // 5. MAP DIRECT IN-PLACE UMEM SLOTS STRAIGHT TO HARDWARE DMA CONTROLLERS
            #if defined(__GNUC__) && !defined(__clang__)
            #pragma GCC unroll 32
            #endif
            for (std::size_t i = 0; i < count; ++i) {
                void* addr = nullptr;
                
                if constexpr (requires(uint32_t safe_type_id) { batch[i].extract_and_decouple(0, safe_type_id); }) {
                    uint32_t safe_type_id = 0;
                    auto safe_ptr = batch[i].extract_and_decouple(static_cast<uint32_t>(inflight_count_), safe_type_id);
                    if constexpr (std::is_pointer_v<decltype(safe_ptr)>) addr = static_cast<void*>(safe_ptr);
                    else addr = safe_ptr.get_address();
                    
                    // TELEOLOGICAL AGNOSIA
                    if (__builtin_expect(safe_type_id == 0, 0)) [[unlikely]] {
                        if (addr) mem_pool_.release(static_cast<decltype(mem_pool_.make_raw())>(addr));
                        ::xdp_desc* tx_desc = xdp_shim::tx_desc(&tx_ring_, tx_idx + i);
                        tx_desc->len = 0;
                        continue;
                    }
                } else if constexpr (requires { batch[i].extract_via_subsumption(0); }) {
                    auto safe_payload = batch[i].extract_via_subsumption(static_cast<uint32_t>(inflight_count_));
                    if constexpr (std::is_pointer_v<decltype(safe_payload)>) addr = static_cast<void*>(safe_payload);
                    else addr = safe_payload.get_address();
                    
                    // LORENTZ SUBSUMPTION
                    if (__builtin_expect(!addr, 0)) [[unlikely]] {
                        auto orig_payload = *reinterpret_cast<const decltype(safe_payload)*>(&batch[i]);
                        void* orig_addr = nullptr;
                        if constexpr (std::is_pointer_v<decltype(orig_payload)>) orig_addr = static_cast<void*>(orig_payload);
                        else orig_addr = orig_payload.get_address();
                        if (orig_addr) mem_pool_.release(static_cast<decltype(mem_pool_.make_raw())>(orig_addr));
                        ::xdp_desc* tx_desc = xdp_shim::tx_desc(&tx_ring_, tx_idx + i);
                        tx_desc->len = 0;
                        continue;
                    }
                } else if constexpr (std::is_pointer_v<typename TargetConduit::value_type>) {
                    addr = static_cast<void*>(batch[i]);
                } else {
                    addr = batch[i].get_address();
                }

                if (SL_EXPECT_FALSE(!addr)) {
                    // Hardware Invariant: Must still fill reserved descriptors
                    ::xdp_desc* tx_desc = xdp_shim::tx_desc(&tx_ring_, tx_idx + i);
                    tx_desc->len = 0;
                    continue;
                }

                using FrameType = std::remove_pointer_t<typename PoolType::value_type>;

                // PILLAR IV: AUTOTELIC CHRYSALIS
                if constexpr (requires { reinterpret_cast<FrameType*>(addr)->validate_chrysalis(); }) {
                    auto* user_frame = reinterpret_cast<FrameType*>(addr);
                    if (__builtin_expect(!user_frame->validate_chrysalis(), 0)) [[unlikely]] {
                        mem_pool_.release(static_cast<decltype(mem_pool_.make_raw())>(addr));
                        ::xdp_desc* tx_desc = xdp_shim::tx_desc(&tx_ring_, tx_idx + i);
                        tx_desc->len = 0;
                        continue;
                    }
                }

                // Resolution: Determine data pointer and length via structural introspection
                void* data_ptr = addr;
                uint32_t payload_len = sizeof(FrameType);

                if constexpr (requires { static_cast<FrameType*>(nullptr)->data; }) {
                    auto* frame = static_cast<FrameType*>(addr);
                data_ptr = static_cast<void*>(frame->data);
                    payload_len = frame->payload_length;
                } else if constexpr (requires { static_cast<FrameType*>(nullptr)->payload; }) {
                    auto* frame = static_cast<FrameType*>(addr);
                data_ptr = static_cast<void*>(&frame->payload);
                    if constexpr (requires { frame->length; }) payload_len = frame->length;
                }

                ::xdp_desc* tx_desc = xdp_shim::tx_desc(&tx_ring_, tx_idx + i);
                tx_desc->addr = static_cast<char*>(data_ptr) - cached_umem_base_;
                tx_desc->len  = payload_len;
            }

            // Publish the descriptor batch down to the network adapter with a single atomic memory fence
            xdp_shim::fill_submit(&tx_ring_, static_cast<uint32_t>(count));
            inflight_count_ += count;

            // 6. DOORBELL: Signal the hardware only when explicitly requested
            if (xdp_shim::fill_needs_wakeup(&tx_ring_)) {
                xdp_shim::socket_kick(xsk_);
            }
        }
    };

} // namespace slabflux::io
