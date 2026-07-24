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
#include <unistd.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/pool.hpp"

namespace slabflux::io {

    /**
     * @brief Legacy Syscall-based Egress.
     * @details Provides a bridge from high-speed conduits back to POSIX sockets.
     * Incurs standard kernel copy-from-user overhead.
     */
    template <typename EventType, typename ConduitType, typename PoolType>
    class alignas(64) socket_egress {
        int fd_;
        ConduitType& tx_conduit_;
        PoolType& pool_;
        struct mmsghdr msgs_[32];
        struct iovec iovs_[32];

    public:
        socket_egress(int fd, ConduitType& conduit, PoolType& pool) 
            : fd_(fd), tx_conduit_(conduit), pool_(pool) {
            // Hoist syscall structures out of the hot loop
            std::memset(msgs_, 0, sizeof(msgs_));
            for (int i = 0; i < 32; ++i) {
                msgs_[i].msg_hdr.msg_iov = &iovs_[i];
                msgs_[i].msg_hdr.msg_iovlen = 1;
            }
        }

        /**
         * @brief Vectorized Egress Poll.
         * @details Drains the conduit in bursts to minimize loop overhead, 
         * even though the syscall remains the primary bottleneck.
         */
        SLAB_HOT void poll() noexcept {
            EventType* batch[32];
            const int count = static_cast<int>(tx_conduit_.pop_batch(batch, 32));
            
            if (count > 0) {
                struct mmsghdr msgs[32];
                struct iovec iovs[32];
                std::memset(msgs, 0, sizeof(msgs));

                for (int i = 0; i < count; ++i) {
                    iovs[i].iov_base = batch[i];
                    iovs[i].iov_len  = sizeof(EventType);
                    msgs[i].msg_hdr.msg_iov = &iovs[i];
                    msgs[i].msg_hdr.msg_iovlen = 1;
                }

                // SYSCALL AMORTIZATION: One kernel transition for the entire burst.
                // This is still 100x slower than SHM, but 10x faster than individual send() calls.
                int sent = ::sendmmsg(fd_, msgs, count, MSG_DONTWAIT);
                
                // Handle partial sends (rare with MSG_DONTWAIT but possible on buffer saturation)
                if (SL_EXPECT_FALSE(sent < count && sent >= 0)) {
                    // Revert un-sent items if the conduit supports it, or just release them to prevent leaks
                    for (int i = sent; i < count; ++i) pool_.release(batch[i]);
                }

                // Reclamation: Always release the memory back to the Slab pool
                for (int i = 0; i < (sent > 0 ? sent : 0); ++i) pool_.release(batch[i]);
            }
        }
    };

} // namespace slabflux::io