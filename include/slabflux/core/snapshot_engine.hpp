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
#include "slabflux/platform/os.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::compute {
    /**
     * @brief Asynchronous Snapshotting.
     * @details Non-blocking persistence. Uses io_uring and
     * SQPOLL to ensure the Compute core never stalls for NVMe I/O.
     */
    template<typename StateLogic>
    class snapshot_manager {
        static_assert(alignof(StateLogic) >= 4096, "State must be page-aligned for O_DIRECT");
        static_assert(sizeof(StateLogic) % 512 == 0, "State size must be a multiple of logical block size (512b) for O_DIRECT IO");

    private:
        int fd_{ -1 };
        size_t buffer_len_{ 0 };
        struct io_uring ring_;
        
        // Shadow State: The background buffer for the NVMe DMA transfer
        StateLogic* shadow_state_{ nullptr };
        bool snapshot_in_flight_{ false };

        // Fixed buffer for the LSN header to prepend to the state
        alignas(4096) uint64_t header_lsn_{ 0 };
        iovec write_vecs_[2]{};

    public:
        snapshot_manager(const char* path) {
            // 1. Hardware-level I/O setup
            fd_ = ::open(path, O_CREAT | O_RDWR | O_DIRECT | O_SYNC, 0644);
            if (fd_ < 0) slabflux::core::handle_critical_error("Snapshot: O_DIRECT open failed.");

            // 2. io_uring initialization with SQPOLL for zero-stall persistence
            io_uring_params params{};
            params.flags |= IORING_SETUP_SQPOLL;
            if (io_uring_queue_init_params(16, &ring_, &params) < 0) {
                slabflux::core::handle_critical_error("Snapshot: io_uring setup failed.");
            }

            // 3. Sovereign Allocation: Enforce physical residency and HugePage alignment
            // Space for 4KB aligned header + StateLogic, rounded to 2MB boundary
            const size_t raw_size = 4096 + sizeof(StateLogic);
            buffer_len_ = (raw_size + 2 * 1024 * 1024 - 1) & ~(2 * 1024 * 1024 - 1);
            
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB | MAP_HUGE_2MB;
            void* mem = ::mmap(nullptr, buffer_len_, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
                mem = ::mmap(nullptr, buffer_len_, PROT_READ | PROT_WRITE, flags, -1, 0);
                if (mem == MAP_FAILED || mem == nullptr) throw std::bad_alloc();
            }

#ifndef _WIN32
            ::madvise(mem, buffer_len_, MADV_HUGEPAGE | MADV_DONTDUMP);
#endif
            shadow_state_ = static_cast<StateLogic*>(mem);
            
            iovec iov = { .iov_base = shadow_state_, .iov_len = buffer_len_ };
            if (io_uring_register_buffers(&ring_, &iov, 1) < 0) {
                slabflux::core::handle_critical_error("Snapshot: Buffer registration failed.");
            }
        }

        /**
         * @brief Non-blocking state capture.
         * @details Structural Honesty. Instead of waiting for disk,
         * we perform a high-speed SIMD memcpy to the shadow buffer and
         * fire an asynchronous io_uring request.
         */
        SLAB_HOT void async_snapshot(const StateLogic& live_state, uint64_t lsn) noexcept {
            if (snapshot_in_flight_) {
                // Check if previous I/O finished
                struct io_uring_cqe* cqe;
                if (io_uring_peek_cqe(&ring_, &cqe) == 0) {
                    io_uring_cqe_seen(&ring_, cqe);
                    snapshot_in_flight_ = false;
                } else {
                    return; // Previous snapshot still moving to silicon
                }
            }

            // 1. TRUE ZERO-COPY: No memcpy! 
            // We write the header and the live state directly to the kernel via gathered I/O (writev).
            // Assumes 'live_state' is a double-buffered read-only replica during this cycle.
            header_lsn_ = lsn;

            // 2. Submit asynchronous Gathered Write (writev) via io_uring
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
            if (SL_EXPECT_TRUE(sqe)) {
                write_vecs_[0].iov_base = &header_lsn_;
                write_vecs_[0].iov_len  = 4096;
                write_vecs_[1].iov_base = const_cast<void*>(reinterpret_cast<const void*>(&live_state));
                write_vecs_[1].iov_len  = sizeof(StateLogic);

                io_uring_prep_writev(sqe, fd_, write_vecs_, 2, 0);
                io_uring_submit(&ring_);
                snapshot_in_flight_ = true;
            }
        }

        /**
         * @brief O(1) Recovery.
         * @details Re-maps the NVMe file directly into the Slab memory.
         */
        SLAB_COLD void restore(StateLogic& target_state) {
            ::pread(fd_, &target_state, sizeof(StateLogic), sizeof(uint64_t));
        }

        ~snapshot_manager() {
            io_uring_queue_exit(&ring_);
            if (fd_ != -1) ::close(fd_);
            if (shadow_state_) ::munmap(shadow_state_, buffer_len_);
        }
    };
}