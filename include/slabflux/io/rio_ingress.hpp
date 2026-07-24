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
#include <new>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace slabflux::io {

    /**
     * @brief True Zero-Copy Windows Registered I/O (RIO) Ingress Boundary.
     * @details Structures the kernel-bypass page block directly as a matrix of frame slots,
     * allowing absolute zero-copy user-space ingestion via vectorized batching paths.
     * @tparam TargetConduit Interconnect queue layer for received tokens (e.g., sharded core SPSC conduit).
     * @tparam TotalSlots Capacity tracking ceiling of the hardware ring descriptor layer. Must be power-of-two.
     */
    template <typename TargetConduit, std::size_t TotalSlots = 128>
    class alignas(64) rio_ingress {
        static_assert((TotalSlots & (TotalSlots - 1)) == 0, "RIO Ring Slots must be an exact power of 2");

    public:
        // Size of every descriptor block. Statically padded to achieve strict cache separation.
        static constexpr std::size_t SLOT_STRIDE = 2048;
        static constexpr std::size_t MASK = TotalSlots - 1;

        /**
         * @brief Unified Memory Layout for a Single Hardware Ingestion Slot.
         * @details Aligned to a 64-byte boundary to prevent L3 cache-line splits.
         */
        struct alignas(64) ingress_slot {
            transport::raw_tcp_frame frame;
            // Private state bit-flags to prevent the hardware ring from re-arming
            // a memory segment while it is actively leased by downstream parser threads.
            alignas(8) std::atomic<uint32_t> leased{0};
            char                             padding[SLOT_STRIDE - sizeof(transport::raw_tcp_frame) - sizeof(std::atomic<uint32_t>)];
        };

    private:
        SOCKET             socket_fd_;
        RIO_CQ             completion_queue_;
        RIO_RQ             request_queue_;
        RIO_BUFFERID       registered_buffer_id_;

        TargetConduit&     interconnect_conduit_;
        std::atomic<bool>& running_sentinel_;

        ingress_slot* rio_buffer_pool_{nullptr};

        // Local tracking registers positioned inside the core's private cache lines
        alignas(64) std::size_t next_arm_index_{0};
        alignas(64) uint64_t total_ingressed_{0};
        static constexpr std::size_t MAX_BURST_SIZE = 32;

    public:
        explicit rio_ingress(
            SOCKET sock,
            RIO_EXTENSION_FUNCTION_TABLE& rio_table,
            TargetConduit& conduit,
            std::atomic<bool>& running
        ) noexcept
        : socket_fd_(sock)
        , interconnect_conduit_(conduit)
        , running_sentinel_(running)
        {
            const std::size_t total_allocation_size = sizeof(ingress_slot) * TotalSlots;

            // Allocate physically locked memory regions to eliminate soft page faults on network inputs
            rio_buffer_pool_ = reinterpret_cast<ingress_slot*>(::VirtualAlloc(
                nullptr,
                total_allocation_size,
                MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
                PAGE_READWRITE
            ));

            if (!rio_buffer_pool_) {
                rio_buffer_pool_ = reinterpret_cast<ingress_slot*>(::VirtualAlloc(
                    nullptr,
                    total_allocation_size,
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE
                ));
            }

            // Register the structured memory matrix straight with the Windows network sub-layer
            registered_buffer_id_ = rio_table.RIORegisterBuffer(
                reinterpret_cast<PVOID>(rio_buffer_pool_),
                                                                static_cast<DWORD>(total_allocation_size)
            );

            completion_queue_ = rio_table.RIOCreateCompletionQueue(static_cast<DWORD>(TotalSlots), nullptr);
            request_queue_ = rio_table.RIOCreateRequestQueue(
                socket_fd_,
                1, 1, // Max Inbound descriptors
                static_cast<DWORD>(TotalSlots), 1, // Max Outbound descriptors
                                                             completion_queue_, completion_queue_, nullptr
            );

            // Queue the initial hardware receive descriptor block ring
            for (std::size_t i = 0; i < TotalSlots; ++i) {
                new (&rio_buffer_pool_[i]) ingress_slot();

                RIO_BUF buf;
                buf.BufferId = registered_buffer_id_;
                // Direct DMA Point: Map offset to point straight to the inner data vector buffer rows
                buf.Offset   = static_cast<DWORD>((i * SLOT_STRIDE) + offsetof(ingress_slot, frame.data));
                buf.Length   = static_cast<LONG>(sizeof(transport::raw_tcp_frame::data));

                rio_table.RIOReceive(request_queue_, &buf, 1, 0, reinterpret_cast<PVOID>(static_cast<ULONG_PTR>(i)));
            }
        }

        ~rio_ingress() noexcept {
            if (rio_buffer_pool_) {
                for (std::size_t i = 0; i < TotalSlots; ++i) {
                    rio_buffer_pool_[i].~ingress_slot();
                }
                ::VirtualFree(rio_buffer_pool_, 0, MEM_RELEASE);
            }
        }

        rio_ingress(const rio_ingress&) = delete;
        rio_ingress& operator=(const rio_ingress&) = delete;

        /**
         * @brief Synchronous true zero-copy ingress harvesting loop.
         * @note Must be assigned exclusively to your execution-pinned Core 1 network thread.
         */
        inline void poll_ingress(RIO_EXTENSION_FUNCTION_TABLE& rio_table) noexcept {
            RIORESULT results[MAX_BURST_SIZE];

            while (__builtin_expect(running_sentinel_.load(std::memory_order_relaxed), 1)) {

                // 1. RE-ARM UNLEASED HARDWARE SLOTS (Asynchronous Refill Loop)
                std::size_t scan_idx = next_arm_index_;
                for (std::size_t k = 0; k < MAX_BURST_SIZE; ++k) {
                    std::size_t slot_to_check = (scan_idx + k) & MASK;
                    ingress_slot& check_slot = rio_buffer_pool_[slot_to_check];

                    // Check if the downstream consumer core has un-locked and recycled this specific slot view
                    if (check_slot.leased.load(std::memory_order_relaxed) == 2) {
                        check_slot.leased.store(0, std::memory_order_relaxed);

                        RIO_BUF buf;
                        buf.BufferId = registered_buffer_id_;
                        buf.Offset   = static_cast<DWORD>((slot_to_check * SLOT_STRIDE) + offsetof(ingress_slot, frame.data));
                        buf.Length   = static_cast<LONG>(sizeof(transport::raw_tcp_frame::data));

                        rio_table.RIOReceive(request_queue_, &buf, 1, 0, reinterpret_cast<PVOID>(static_cast<ULONG_PTR>(slot_to_check)));
                        next_arm_index_ = (slot_to_check + 1) & MASK;
                    }
                }

                // 2. HARVEST PENDING HARDWARE COMPLETIONS FROM USER SPACE
                ULONG dequeue_count = rio_table.RIODequeueCompletion(completion_queue_, results, MAX_BURST_SIZE);

                if (__builtin_expect(dequeue_count == 0, 1)) {
                    #if defined(__x86_64__) || defined(_M_X64)
                    _mm_pause();
                    #endif
                    continue;
                }

                alignas(64) typename TargetConduit::value_type token_batch[MAX_BURST_SIZE];

                // 3. PROCESS COMPLETED DMA SEGMENTS
                for (ULONG i = 0; i < dequeue_count; ++i) {
                    ULONG_PTR buffer_index = reinterpret_cast<ULONG_PTR>(results[i].RequestContext);
                    std::size_t bytes_transferred = results[i].BytesTransferred;

                    ingress_slot& active_slot = rio_buffer_pool_[buffer_index];

                    if (__builtin_expect(bytes_transferred > 0, 1)) {
                        // Setup the header variables directly inside the pre-mapped structural memory section
                        active_slot.frame.payload_length = static_cast<uint16_t>(bytes_transferred);
                        active_slot.frame.connection_id  = static_cast<uint32_t>(socket_fd_);

                        // Lock the slot to protect its contents from the re-arm cycle
                        active_slot.leased.store(1, std::memory_order_relaxed);

                        auto packed = core::tagged_pointer::pack(transport::raw_tcp_frame::ID, &active_slot.frame);
                        if constexpr (requires { token_batch[i].embed_symmetry(0); }) {
                            token_batch[i] = typename TargetConduit::value_type(transport::raw_tcp_frame::ID, packed);
                            token_batch[i].embed_symmetry(static_cast<uint32_t>(total_ingressed_ + i));
                        } else if constexpr (requires { token_batch[i].anchor_to_lightcone(0); }) {
                            token_batch[i] = typename TargetConduit::value_type(packed);
                            token_batch[i].anchor_to_lightcone(total_ingressed_ + i);
                        } else {
                            token_batch[i] = packed;
                        }
                    } else {
                        // Handle disconnects or faulty frames by priming the slot for immediate re-arm
                        active_slot.leased.store(2, std::memory_order_relaxed);
                        auto packed = core::tagged_pointer::pack(0, nullptr);
                        if constexpr (requires { token_batch[i].embed_symmetry(0); }) {
                            token_batch[i] = typename TargetConduit::value_type(0, packed);
                            token_batch[i].embed_symmetry(static_cast<uint32_t>(total_ingressed_ + i));
                        } else if constexpr (requires { token_batch[i].anchor_to_lightcone(0); }) {
                            token_batch[i] = typename TargetConduit::value_type(packed);
                            token_batch[i].anchor_to_lightcone(total_ingressed_ + i);
                        } else {
                            token_batch[i] = packed;
                        }
                    }
                }
                total_ingressed_ += dequeue_count;

                // 4. VECTORIZED BURST PUSH
                // Transmit the complete batch over the lock-free conduit boundary in one single operation,
                // minimizing atomic synchronization overhead across the cross-core boundary.
                std::size_t pushed_count = interconnect_conduit_.push_batch(token_batch, dequeue_count);

                if (__builtin_expect(pushed_count < dequeue_count, 0)) {
                    // Drop Recovery Safely un-lock slots that failed to clear the conduit barrier
                    for (std::size_t i = pushed_count; i < dequeue_count; ++i) {
                        auto* failed_frame = reinterpret_cast<transport::raw_tcp_frame*>(token_batch[i].get_address());
                        if (failed_frame) {
                            auto* slot_ptr = reinterpret_cast<ingress_slot*>(reinterpret_cast<char*>(failed_frame) - offsetof(ingress_slot, frame));
                            slot_ptr->leased.store(2, std::memory_order_relaxed); // Recycle right back to the hardware refill loop
                        }
                    }
                }
            }
        }
    };

} // namespace slabflux::transport
#endif
