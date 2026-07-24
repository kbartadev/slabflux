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
 * @file baremetal_egress.hpp
 * @brief Zero-copy network egress using io_uring.
 * @details Implements Streams processed events back to the wire.
 */

#pragma once

#include <liburing.h>
#include <system_error>
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    template<typename EventType, size_t Capacity>
    class baremetal_egress {
        io_uring ring_;
        int fd_{-1};
        bool valid_{false};

    public:
        baremetal_egress(int socket_fd, int sq_core_id) : fd_(socket_fd) {
            io_uring_params params{};
            params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
            params.sq_thread_cpu = sq_core_id;

            int ret = io_uring_queue_init_params(Capacity, &ring_, &params);
            if (ret < 0) {
                params.flags = IORING_SETUP_SQPOLL;
                if (io_uring_queue_init_params(Capacity, &ring_, &params) < 0) {
                    if (io_uring_queue_init(Capacity, &ring_, 0) < 0)
                        throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init_params failed");
                }
            }
            valid_ = true;
        }

        ~baremetal_egress() {
            if (valid_) {
                io_uring_queue_exit(&ring_);
            }
        }

        /**
         * @brief Non-blocking write submission.
         * @details Uses the same pinned memory from the pool for zero-copy transmission.
         */
        inline void send(const EventType* ev) noexcept {
            if (SL_EXPECT_FALSE(!valid_)) return;
            io_uring_sqe* sqe = uring_shim::get_sqe(&ring_);
            if (sqe) [[likely]] {
                io_uring_prep_write(sqe, fd_, ev, sizeof(EventType), 0);
                // We don't wait for completion here; SQPOLL handles it.
                uring_shim::submit(&ring_);
            }
        }
    };

} // namespace slabflux::io
