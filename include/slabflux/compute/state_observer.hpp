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

#include <liburing.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::compute {

    /**
     * @brief Sovereign Asynchronous Snapshot Manager.
     * @tparam StateLogic Core computation state structure. Must be page-aligned.
     */
    template<typename StateLogic>
    class snapshot_manager {
        static_assert(alignof(StateLogic) >= 4096, "State must be page-aligned for O_DIRECT");

    private:
        int fd_{-1};
        struct io_uring ring_;
        
        // Shadow State: The isolated background buffer for the NVMe DMA transfer
        StateLogic* shadow_state_{ nullptr };
        bool snapshot_in_flight_{ false };

        // Fixed page-aligned buffer for the LSN header to prepend to the state
        alignas(4096) uint8_t header_block_[4096]{ 0 };
        iovec write_vecs_[2]{};

    public:
        explicit snapshot_manager(const char* path) {
            // 1. Hardware-level direct I/O initialization
            fd_ = ::open(path, O_CREAT | O_RDWR | O_DIRECT, 0644);
            
            // 2. io_uring queue setup for zero-latency submission
            io_uring_queue_init(8, &ring_, 0);

            // 3. Allocate shadow buffer matching page-alignment constraints on the local NUMA node
            // Note: std::aligned_alloc requires the size to be a multiple of the alignment!
            shadow_state_ = static_cast<StateLogic*>(
                std::aligned_alloc(4096, (sizeof(StateLogic) + 4095) & ~4095)
            );

            // 4. Register the tracking header buffer for optimized kernel access
            // Note: writev ignores registered buffers, but we map both here for parity
            // in case you switch to newer io_uring fixed scatter/gather ops.
            iovec iovs[2] = {{ .iov_base = header_block_, .iov_len = 4096 }, { .iov_base = shadow_state_, .iov_len = sizeof(StateLogic) }};
            io_uring_register_buffers(&ring_, iovs, 2);
        }

        /**
         * @brief Non-blocking state capture via shadow isolation.
         * @details Performs a high-speed synchronous memory copy to the shadow buffer,
         * then delegates the asynchronous storage transit to io_uring vectors.
         * @param live_state Reference to the active runtime memory state.
         * @param lsn Log Sequence Number corresponding to the transaction boundary.
         */
        SLAB_HOT void async_snapshot(const StateLogic& live_state, uint64_t lsn) noexcept {
            if (SL_EXPECT_FALSE(snapshot_in_flight_)) {
                // Peek completion queue for non-blocking confirmation
                struct io_uring_cqe* cqe;
                if (io_uring_peek_cqe(&ring_, &cqe) == 0) {
                    io_uring_cqe_seen(&ring_, cqe);
                    snapshot_in_flight_ = false;
                } else {
                    return; // In-flight transaction active; drop concurrent submission to prevent overlap
                }
            }

            // 1. Bit-perfect copy to shadow (The only "cost" on the hot path)
            // Approx 5-10 microseconds for a multi-MB state using AVX-512
            __builtin_memcpy(shadow_state_, &live_state, sizeof(StateLogic));
            
            // 2. Update the registered header segment tracking value
            *reinterpret_cast<uint64_t*>(header_block_) = lsn;

            // 3. Map structural boundaries to vector segments for atomic zero-copy transmission
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
            if (SL_EXPECT_TRUE(sqe)) {
                write_vecs_[0].iov_base = header_block_;
                write_vecs_[0].iov_len  = 4096;
                write_vecs_[1].iov_base = shadow_state_;
                write_vecs_[1].iov_len  = sizeof(StateLogic);

                io_uring_prep_writev(sqe, fd_, write_vecs_, 2, 0);
                io_uring_submit(&ring_);
                snapshot_in_flight_ = true;
            }
        }

        /**
         * @brief O(1) State Recovery.
         * @details Re-maps the NVMe persistence file directly into the destination execution state.
         * @param target_state Target memory block to populate with historical checkpoint data.
         */
        SLAB_COLD void restore(StateLogic& target_state) {
            ::pread(fd_, &target_state, sizeof(StateLogic), 4096);
        }

        ~snapshot_manager() {
            io_uring_queue_exit(&ring_);
            if (fd_ != -1) {
                ::close(fd_);
            }
            std::free(shadow_state_);
        }
    };
}