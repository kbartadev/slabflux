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
#include <cstring>
#include "slabflux/io/socket_ingress.hpp"
#include "slabflux/io/socket_egress.hpp"

namespace slabflux::io {

    /**
     * @brief Legacy Syscall-based Duplex Engine.
     * @details Orchestrates synchronous ingress and egress over a standard POSIX file descriptor.
     */
    template <typename EventType, typename PipelineLogic, typename InboundPool, 
              typename OutboundConduit, typename OutboundPool>
    class alignas(64) socket_duplex {
    private:
        int               fd_;
        PipelineLogic&    logic_;
        InboundPool&      in_pool_;
        OutboundConduit&  out_conduit_;
        OutboundPool&     out_pool_;
        
        alignas(64) uint64_t ingress_drops_{0};
        static constexpr uint32_t BATCH_SIZE = 32;

        struct mmsghdr rx_msgs_[BATCH_SIZE];
        struct iovec rx_iovs_[BATCH_SIZE];
        struct mmsghdr tx_msgs_[BATCH_SIZE];
        struct iovec tx_iovs_[BATCH_SIZE];

    public:
        socket_duplex(int fd, PipelineLogic& logic, InboundPool& in_pool, 
                      OutboundConduit& out_q, OutboundPool& out_pool)
            : fd_(fd), logic_(logic), in_pool_(in_pool), 
              out_conduit_(out_q), out_pool_(out_pool) {
            std::memset(rx_msgs_, 0, sizeof(rx_msgs_));
            std::memset(tx_msgs_, 0, sizeof(tx_msgs_));
            for (uint32_t i = 0; i < BATCH_SIZE; ++i) {
                rx_msgs_[i].msg_hdr.msg_iov = &rx_iovs_[i];
                rx_msgs_[i].msg_hdr.msg_iovlen = 1;
                tx_msgs_[i].msg_hdr.msg_iov = &tx_iovs_[i];
                tx_msgs_[i].msg_hdr.msg_iovlen = 1;
            }
        }

        /**
         * @brief Fused Vectorized Runtime.
         * @details Orchestrates recvmmsg and sendmmsg in a single pass to 
         * minimize context-switch overhead and maximize instruction locality.
         */
        SLAB_HOT void poll() noexcept {
            // 1. INGRESS: Vectorized Harvest
            EventType* rx_batch[BATCH_SIZE];
            size_t allocated = 0;
            if constexpr (requires { in_pool_.make_batch(rx_batch, BATCH_SIZE); }) {
                allocated = in_pool_.make_batch(rx_batch, BATCH_SIZE);
            } else {
                for (; allocated < BATCH_SIZE; ++allocated) {
                    EventType* p = in_pool_.make_raw();
                    if (SL_EXPECT_FALSE(!p)) break;
                    rx_batch[allocated] = p;
                }
            }
            
            if (SL_EXPECT_TRUE(allocated > 0)) {
                for (size_t i = 0; i < allocated; ++i) {
                    rx_iovs_[i].iov_base = rx_batch[i];
                    rx_iovs_[i].iov_len  = sizeof(EventType);
                }

                int received = ::recvmmsg(fd_, rx_msgs_, static_cast<unsigned int>(allocated), MSG_DONTWAIT, nullptr);
                if (received > 0) {
                    for (int i = 0; i < received; ++i) {
                        if (SL_EXPECT_FALSE(!logic_.on_raw_frame(rx_batch[i], rx_msgs_[i].msg_len))) {
                            ingress_drops_++;
                        }
                    }
                }
                if constexpr (requires { in_pool_.release_batch(rx_batch, allocated); }) {
                    in_pool_.release_batch(rx_batch, allocated);
                } else {
                    for (size_t i = 0; i < allocated; ++i) in_pool_.release(rx_batch[i]);
                }
            }

            // 2. EGRESS: Vectorized Dispatch
            EventType* tx_batch[BATCH_SIZE];
            int tx_count = static_cast<int>(out_conduit_.pop_batch(tx_batch, BATCH_SIZE));
            if (tx_count > 0) {
                for (int i = 0; i < tx_count; ++i) {
                    tx_iovs_[i].iov_base = tx_batch[i];
                    tx_iovs_[i].iov_len  = sizeof(EventType);
                }
                ::sendmmsg(fd_, tx_msgs_, tx_count, MSG_DONTWAIT);
                if constexpr (requires { out_pool_.release_batch(tx_batch, tx_count); }) {
                    out_pool_.release_batch(tx_batch, tx_count);
                } else {
                    for (int i = 0; i < tx_count; ++i) out_pool_.release(tx_batch[i]);
                }
            }
        }

        /** @brief Returns the underlying socket handle. */
        [[nodiscard]] int get_fd() const noexcept { return fd_; }

        /** @brief Accessor for ingress telemetry. */
        [[nodiscard]] uint64_t get_ingress_drops() const noexcept { 
            return ingress_drops_; 
        }
    };

} // namespace slabflux::io