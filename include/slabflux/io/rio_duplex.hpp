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
#include <cstring>

#include "slabflux/core/memory.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/core/memory.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace slabflux::io {

    template <typename InboundConduit, typename OutboundConduit>
    class alignas(64) rio_duplex {
    public:
        static constexpr std::size_t BUFFER_SIZE = 2048; // Cache line aligned stride
        static constexpr std::size_t MAX_SLOTS   = 64;

    private:
        SOCKET             socket_fd_;
        RIO_CQ             ingress_cq_;
        RIO_CQ             egress_cq_;
        RIO_RQ             request_queue_;
        RIO_BUFFERID       buffer_pool_id_;

        InboundConduit&    ingress_conduit_;
        OutboundConduit&   egress_conduit_;
        std::atomic<bool>& running_sentinel_;

        char* buffer_pool_{nullptr};

        // Atomic lease tracking for Ingress and Egress ring synchronization.
        alignas(64) std::atomic<uint32_t> ingress_leases_[MAX_SLOTS];

        static constexpr std::size_t RX_POOL_OFFSET = 0;
        static constexpr std::size_t TX_POOL_OFFSET = BUFFER_SIZE * MAX_SLOTS;
        static constexpr std::size_t TOTAL_POOL_SIZE = BUFFER_SIZE * MAX_SLOTS * 2;

    public:
        explicit rio_duplex(
            SOCKET sock,
            RIO_EXTENSION_FUNCTION_TABLE& rio_table,
            InboundConduit& in_conduit,
            OutboundConduit& out_conduit,
            std::atomic<bool>& running
        ) noexcept
        : socket_fd_(sock)
        , ingress_conduit_(in_conduit)
        , egress_conduit_(out_conduit)
        , running_sentinel_(running)
        {
            buffer_pool_ = reinterpret_cast<char*>(::VirtualAlloc(
                nullptr, TOTAL_POOL_SIZE, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE
            ));
            if (!buffer_pool_) {
                buffer_pool_ = reinterpret_cast<char*>(::VirtualAlloc(
                    nullptr, TOTAL_POOL_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
                ));
            }

            buffer_pool_id_ = rio_table.RIORegisterBuffer(buffer_pool_, static_cast<DWORD>(TOTAL_POOL_SIZE));
            ingress_cq_ = rio_table.RIOCreateCompletionQueue(MAX_SLOTS, nullptr);
            egress_cq_  = rio_table.RIOCreateCompletionQueue(MAX_SLOTS, nullptr);

            request_queue_ = rio_table.RIOCreateRequestQueue(
                socket_fd_, MAX_SLOTS, 1, MAX_SLOTS, 1, ingress_cq_, egress_cq_, nullptr
            );

            for (DWORD i = 0; i < MAX_SLOTS; ++i) {
                ingress_leases_[i].store(0, std::memory_order_relaxed);
                RIO_BUF buf;
                buf.BufferId = buffer_pool_id_;
                buf.Offset   = static_cast<DWORD>(RX_POOL_OFFSET + (i * BUFFER_SIZE));
                buf.Length   = BUFFER_SIZE;
                rio_table.RIOReceive(request_queue_, &buf, 1, 0, reinterpret_cast<void*>(static_cast<ULONG_PTR>(i)));
            }
        }

        ~rio_duplex() noexcept {
            if (buffer_pool_) {
                ::VirtualFree(buffer_pool_, 0, MEM_RELEASE);
            }
        }

        rio_duplex(const rio_duplex&) = delete;
        rio_duplex& operator=(const rio_duplex&) = delete;

        inline void poll_ingress(RIO_EXTENSION_FUNCTION_TABLE& rio_table) noexcept {
            RIORESULT results[MAX_SLOTS];

            while (__builtin_expect(running_sentinel_.load(std::memory_order_relaxed), 1)) {

                // Asynchronous refill phase: if the processing core has released the slot (state: 2).
                for (DWORD i = 0; i < MAX_SLOTS; ++i) {
                    if (ingress_leases_[i].load(std::memory_order_relaxed) == 2) {
                        ingress_leases_[i].store(0, std::memory_order_relaxed);
                        RIO_BUF buf;
                        buf.BufferId = buffer_pool_id_;
                        buf.Offset   = static_cast<DWORD>(RX_POOL_OFFSET + (i * BUFFER_SIZE));
                        buf.Length   = BUFFER_SIZE;
                        rio_table.RIOReceive(request_queue_, &buf, 1, 0, reinterpret_cast<void*>(static_cast<ULONG_PTR>(i)));
                    }
                }

                ULONG dequeue_count = rio_table.RIODequeueCompletion(ingress_cq_, results, MAX_SLOTS);
                if (__builtin_expect(dequeue_count == 0, 1)) {
                    #if defined(__x86_64__) || defined(_M_X64)
                    _mm_pause();
                    #endif
                    continue;
                }

                alignas(64) core::tagged_pointer token_batch[MAX_SLOTS];
                uint32_t valid_tokens = 0;

                for (ULONG i = 0; i < dequeue_count; ++i) {
                    ULONG_PTR slot_idx = reinterpret_cast<ULONG_PTR>(results[i].RequestContext);
                    std::size_t bytes_transferred = results[i].BytesTransferred;

                    if (__builtin_expect(bytes_transferred > 0, 1)) {
                        char* data_ptr = buffer_pool_ + RX_POOL_OFFSET + (slot_idx * BUFFER_SIZE);

                        // Optimized: Zero-copy frame skeleton establishment within the existing buffer pool.
                        auto* frame = reinterpret_cast<transport::raw_tcp_frame*>(data_ptr);
                        frame->payload_length = static_cast<uint16_t>(bytes_transferred);
                        frame->connection_id  = static_cast<uint32_t>(socket_fd_);

                        ingress_leases_[slot_idx].store(1, std::memory_order_relaxed); // Locked for processing.

                        // Store slot_idx in the tag field to enable asynchronous reclamation by the consumer core.
                        token_batch[valid_tokens++] = core::tagged_pointer::pack(static_cast<uint16_t>(slot_idx), frame);
                    } else {
                        ingress_leases_[slot_idx].store(2, std::memory_order_relaxed); // Faulty frame, immediate re-arm.
                    }
                }

                if (valid_tokens > 0) {
                    std::size_t pushed = ingress_conduit_.push_batch(token_batch, valid_tokens);
                    if (__builtin_expect(pushed < valid_tokens, 0)) {
                        for (std::size_t i = pushed; i < valid_tokens; ++i) {
                            uint16_t s_idx = static_cast<uint16_t>(token_batch[i].tag());
                            ingress_leases_[s_idx].store(2, std::memory_order_relaxed);
                        }
                    }
                }
            }
        }

        inline void poll_egress(RIO_EXTENSION_FUNCTION_TABLE& rio_table) noexcept {
            alignas(64) core::tagged_pointer batch[MAX_SLOTS];
            RIORESULT results[MAX_SLOTS];
            DWORD active_inflight_slots = 0;

            while (__builtin_expect(running_sentinel_.load(std::memory_order_relaxed), 1)) {
                if (active_inflight_slots > 0) {
                    ULONG comp_count = rio_table.RIODequeueCompletion(egress_cq_, results, active_inflight_slots);
                    active_inflight_slots -= comp_count;
                }

                if (active_inflight_slots >= MAX_SLOTS) {
                    #if defined(__x86_64__) || defined(_M_X64)
                    _mm_pause();
                    #endif
                    continue;
                }

                std::size_t max_fill_allowance = MAX_SLOTS - active_inflight_slots;
                std::size_t read_count = egress_conduit_.pop_batch(batch, max_fill_allowance);

                if (__builtin_expect(read_count == 0, 1)) {
                    if (active_inflight_slots == 0) {
                        #if defined(__x86_64__) || defined(_M_X64)
                        _mm_pause();
                        #endif
                    }
                    continue;
                }

                for (std::size_t i = 0; i < read_count; ++i) {
                    auto* raw_packet = reinterpret_cast<const transport::raw_tcp_frame*>(batch[i].get_address());
                    DWORD current_slot = (active_inflight_slots + i) % MAX_SLOTS;
                    char* target_buffer_slot = buffer_pool_ + TX_POOL_OFFSET + (current_slot * BUFFER_SIZE);

                    std::memcpy(target_buffer_slot, raw_packet->data, raw_packet->payload_length);

                    RIO_BUF buf;
                    buf.BufferId = buffer_pool_id_;
                    buf.Offset   = static_cast<DWORD>(TX_POOL_OFFSET + (current_slot * BUFFER_SIZE));
                    buf.Length   = static_cast<ULONG>(raw_packet->payload_length);

                    // Amortized doorbell signaling: utilization of RIO_MSG_DEFER until the burst tail.
                    DWORD flags = RIO_MSG_DONT_NOTIFY;
                    if (i < read_count - 1) {
                        flags |= RIO_MSG_DEFER;
                    }

                    rio_table.RIOSend(
                        request_queue_,
                        &buf, 1,
                        flags,
                        reinterpret_cast<void*>(static_cast<ULONG_PTR>(current_slot))
                    );
                }

                active_inflight_slots += static_cast<DWORD>(read_count);
            }
        }
    };
}
#endif
