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
     * @brief Legacy Syscall-based Ingress.
     * @details WARNING: This is 1000x slower than uring_ingress. 
     * Used only for non-critical management traffic or legacy bridging.
     */
    template <typename EventType, typename PipelineLogic, typename PoolType>
    class alignas(64) socket_ingress {
        int fd_;
        PipelineLogic& logic_;
        PoolType& pool_;
        alignas(64) uint64_t drop_count_{0};

        struct mmsghdr msgs_[32];
        struct iovec iovs_[32];

    public:
        socket_ingress(int fd, PipelineLogic& logic, PoolType& pool) 
            : fd_(fd), logic_(logic), pool_(pool) {
            std::memset(msgs_, 0, sizeof(msgs_));
            for (int i = 0; i < 32; ++i) {
                msgs_[i].msg_hdr.msg_iov = &iovs_[i];
                msgs_[i].msg_hdr.msg_iovlen = 1;
            }
        }

        /**
         * @brief Standard POSIX poll.
         * @note This incurs a mandatory Kernel-to-User memory copy.
         */
        SLAB_HOT void poll() noexcept {
            EventType* batch[32];
            
            size_t allocated = 0;
            if constexpr (requires { pool_.make_batch(batch, 32); }) {
                allocated = pool_.make_batch(batch, 32);
            } else {
                for (; allocated < 32; ++allocated) {
                    EventType* p = pool_.make_raw();
                    if (SL_EXPECT_FALSE(!p)) break;
                    batch[allocated] = p;
                }
            }
            if (SL_EXPECT_FALSE(allocated == 0)) return;

            for (size_t i = 0; i < allocated; ++i) {
                iovs_[i].iov_base = batch[i];
                iovs_[i].iov_len  = sizeof(EventType);
            }

            // 2. VECTOR SYSCALL: Harvest multiple packets from the kernel buffer
            int received = ::recvmmsg(fd_, msgs_, static_cast<unsigned int>(allocated), MSG_DONTWAIT, nullptr);

            if (SL_EXPECT_TRUE(received > 0)) {
                for (int i = 0; i < received; ++i) {
                    // 3. Zero-Copy Dispatch (Pointer Passing)
                    if (SL_EXPECT_FALSE(!logic_.on_raw_frame(batch[i], msgs_[i].msg_len))) {
                        drop_count_++;
                    }
                }
            }

            if constexpr (requires { pool_.release_batch(batch, allocated); }) {
                pool_.release_batch(batch, allocated);
            } else {
                for (size_t i = 0; i < allocated; ++i) pool_.release(batch[i]);
            }
        }

        [[nodiscard]] uint64_t get_drops() const noexcept { return drop_count_; }
    };

} // namespace slabflux::io