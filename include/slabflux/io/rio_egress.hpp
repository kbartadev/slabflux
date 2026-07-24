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

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/security/autologous_isomorphism.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace slabflux::io {

    /**
     * @brief Zero-Copy Windows Registered I/O (RIO) Egress Boundary.
     * @details Employs hardware doorbell batching and local inflight slot tracking
     * to eliminate PCIe bus thrashing and guarantee request queue safety.
     * @tparam TargetConduit The core storage queue layer (SPSC conduit or specialized MPMC mesh slice).
     * @tparam PoolType The backing memory allocator pool managing the registered memory fabric.
     * @tparam MaxSlots Maximum hardware descriptor capacity allocated to the underlying RIO ring. Must be power-of-two.
     */
    template <typename TargetConduit, typename PoolType, std::size_t MaxSlots = 64>
    class alignas(64) rio_egress {
        static_assert((MaxSlots & (MaxSlots - 1)) == 0, "RIO Ring Slots must be an exact power of 2");

    private:
        SOCKET             socket_fd_;
        RIO_CQ             completion_queue_;
        RIO_RQ             request_queue_;
        RIO_BUFFERID       registered_buffer_id_;

        TargetConduit&     interconnect_conduit_;
        PoolType&          mem_pool_;
        std::atomic<bool>& running_sentinel_;

        // ====================================================================
        // MICROARCHITECTURAL CORE-LOCAL REGISTERS
        // ====================================================================
        // Private tracking variables kept strictly within the local core's L1D
        // allocation lines to eradicate shared memory bus traffic during steady state.
        alignas(64) std::size_t inflight_count_{0};
        char* cached_pool_base_{nullptr};

        static constexpr std::size_t MAX_BURST_SIZE = 32;

    public:
        explicit rio_egress(
            SOCKET sock,
            RIO_EXTENSION_FUNCTION_TABLE& rio_table,
            TargetConduit& conduit,
            PoolType& pool,
            std::atomic<bool>& running
        ) noexcept
        : socket_fd_(sock)
        , interconnect_conduit_(conduit)
        , mem_pool_(pool)
        , running_sentinel_(running)
        , cached_pool_base_(static_cast<char*>(mem_pool_.data()))
        {
            // Register the external memory pool storage block directly with the Windows kernel network fabric
            registered_buffer_id_ = rio_table.RIORegisterBuffer(
                cached_pool_base_,
                static_cast<DWORD>(mem_pool_.size_bytes())
            );

            completion_queue_ = rio_table.RIOCreateCompletionQueue(static_cast<DWORD>(MaxSlots), nullptr);
            request_queue_ = rio_table.RIOCreateRequestQueue(
                socket_fd_,
                static_cast<DWORD>(MaxSlots), 1,
                                                             static_cast<DWORD>(MaxSlots), 1,
                                                             completion_queue_, completion_queue_, nullptr
            );
        }

        ~rio_egress() noexcept = default;

        rio_egress(const rio_egress&) = delete;
        rio_egress& operator=(const rio_egress&) = delete;

        /**
         * @brief Synchronous true zero-copy outbound transmission loop with batch flushing.
         * @note Must be execution-pinned to your outbound kernel-bypass worker thread.
         */
        inline void poll_egress(RIO_EXTENSION_FUNCTION_TABLE& rio_table) noexcept {
            RIORESULT results[MaxSlots];

            // 1. HARVEST COMPLETIONS & RECLAIM SLOTS (Wait-Free CQE Scan)
            if (inflight_count_ > 0) {
                ULONG comp_count = rio_table.RIODequeueCompletion(completion_queue_, results, static_cast<ULONG>(MaxSlots));
                if (comp_count > 0) {
                    inflight_count_ -= comp_count;
                    for (ULONG i = 0; i < comp_count; ++i) {
                        void* completed_frame_address = reinterpret_cast<void*>(results[i].RequestContext);
                        if (__builtin_expect(completed_frame_address != nullptr, 1)) {
                            mem_pool_.release(completed_frame_address); // Safely recycle memory back to the pool
                        }
                    }
                }
            }

            // 2. CHECK RING CAPACITY CEILING (Prevent Hardware Descriptor Satiation)
            if (__builtin_expect(inflight_count_ >= MaxSlots, 0)) {
                #if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
                #endif
                return;
            }

            // 3. INGEST VECTOR BATCH FROM INTERCONNECT CONDUIT
            std::size_t max_allowed_allocation = MaxSlots - inflight_count_;
            std::size_t desired_burst = (max_allowed_allocation < MAX_BURST_SIZE) ? max_allowed_allocation : MAX_BURST_SIZE;

            alignas(64) typename TargetConduit::value_type batch[MAX_BURST_SIZE];
            std::size_t count = interconnect_conduit_.pop_batch(batch, desired_burst);

            if (__builtin_expect(count == 0, 1)) {
                return;
            }

            // Determine the current temporal epoch to validate the spacetime sequence (if ACI is active)
            uint32_t current_temporal_clock = static_cast<uint32_t>(inflight_count_);

            // 4. VECTORIZED TRANSMISSION WITH HARDWARE DOORBELL BATCHING
            transport::raw_tcp_frame* valid_packets[MAX_BURST_SIZE];
            std::size_t valid_count = 0;

            for (std::size_t i = 0; i < count; ++i) {
                void* raw_address = nullptr;

                // Compile-time structural resolution: Is Quintipartite Defense active on this conduit?
                if constexpr (requires(uint32_t safe_type_id) { batch[i].extract_and_decouple(current_temporal_clock, safe_type_id); }) {
                    uint32_t safe_type_id = 0;
                    auto safe_ptr = batch[i].extract_and_decouple(current_temporal_clock, safe_type_id);
                    
                    if constexpr (std::is_pointer_v<decltype(safe_ptr)>) raw_address = static_cast<void*>(safe_ptr);
                    else raw_address = safe_ptr.get_address();

                    // TELEOLOGICAL AGNOSIA: Silently vaporize payloads whose Collision Graph fractured
                    if (__builtin_expect(safe_type_id == 0, 0)) [[unlikely]] {
                        // CRITICAL: Prevent memory leaks and queue starvation. Corrupted frames 
                        // must be returned to the pool immediately since they bypass hardware submission.
                        if (raw_address) mem_pool_.release(raw_address);
                        continue;
                    }
                } else if constexpr (requires { batch[i].extract_via_subsumption(0); }) {
                    auto safe_payload = batch[i].extract_via_subsumption(current_temporal_clock);
                    
                    if constexpr (std::is_pointer_v<decltype(safe_payload)>) raw_address = static_cast<void*>(safe_payload);
                    else raw_address = safe_payload.get_address();

                    // LORENTZ SUBSUMPTION: Natively vaporized payload interception
                    if (__builtin_expect(!raw_address, 0)) [[unlikely]] {
                        // Recover the original memory pointer bypassing privacy rules via offset 0 cast
                        auto orig_payload = *reinterpret_cast<const decltype(safe_payload)*>(&batch[i]);
                        void* orig_addr = nullptr;
                        if constexpr (std::is_pointer_v<decltype(orig_payload)>) orig_addr = static_cast<void*>(orig_payload);
                        else orig_addr = orig_payload.get_address();
                        if (orig_addr) mem_pool_.release(orig_addr);
                        continue;
                    }
                } else {
                    // Unprotected standard execution path (Zero Cost)
                    if constexpr (std::is_pointer_v<typename TargetConduit::value_type>) raw_address = static_cast<void*>(batch[i]);
                    else raw_address = batch[i].get_address();
                }
                
                valid_packets[valid_count++] = reinterpret_cast<transport::raw_tcp_frame*>(raw_address);
            }

            for (std::size_t i = 0; i < valid_count; ++i) {
                auto* raw_packet = valid_packets[i];
                // Bit-perfect offset lookup executing entirely in a single-cycle CPU register step
                std::size_t buffer_offset = reinterpret_cast<char*>(raw_packet) - cached_pool_base_;

                RIO_BUF buf;
                buf.BufferId = registered_buffer_id_;
                buf.Offset   = static_cast<DWORD>(buffer_offset);
                buf.Length   = static_cast<ULONG>(raw_packet->payload_length);

                // Microarchitectural Optimization: Apply RIO_MSG_DEFER to append descriptors to the
                // hardware ring without notifying the NIC. Omit the flag on the final element of the
                // valid batch loop to strike the PCIe doorbell exactly once for the entire vector.
                DWORD flags = RIO_MSG_DONT_NOTIFY;
                if (i < valid_count - 1) {
                    flags |= RIO_MSG_DEFER; 
                } else {
                    // Ultimate Optimization: Ensure the last packet in a burst 
                    // always forces a hardware interrupt/doorbell strike.
                    flags &= ~RIO_MSG_DEFER;
                }

                BOOL success = rio_table.RIOSend(
                    request_queue_,
                    &buf, 1,
                    flags,
                    reinterpret_cast<void*>(raw_packet)
                );

                if (__builtin_expect(!success, 0)) {
                    // Critical: if we fail here, we must not increment inflight_count_ 
                    // for this specific frame, or the completion harvester will drift.
                    mem_pool_.release(reinterpret_cast<void*>(raw_packet));
                    
                    // Release any subsequent valid packets that failed to dispatch due to the break
                    for (std::size_t j = i + 1; j < valid_count; ++j) {
                        mem_pool_.release(reinterpret_cast<void*>(valid_packets[j]));
                    }
                    
                    valid_count = i; // Terminate burst and only count successful submissions
                    break;
                }
            }

            inflight_count_ += valid_count;
        }
    };

} // namespace slabflux::transport
#endif
