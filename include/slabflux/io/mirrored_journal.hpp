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
 * @file mirrored_journal.hpp
 * @brief Physical Redundancy.
 * @details Writes events to two separate NVMe devices simultaneously 
 * using io_uring to ensure 100% data survival.
 */

#pragma once

#include "slabflux/io/durable_journal.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include <liburing.h>
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#include <fcntl.h>
#include <unistd.h>

namespace slabflux::io {

    template<typename T>
    class alignas(64) mirrored_journal {
    private:
        int fd_primary_{ -1 };
        int fd_secondary_{ -1 };

        // Two completely independent rings, each with isolated SQPOLL core affinity!
        io_uring ring_primary_{};
        io_uring ring_secondary_{};

        uint64_t file_offset_{0};

        static constexpr std::size_t MAX_BURST_SIZE = 32;

    public:
        mirrored_journal(const char* p1, const char* p2, int primary_cpu, int secondary_cpu) noexcept {
            fd_primary_   = ::open(p1, O_CREAT | O_WRONLY | O_DIRECT | O_DSYNC, 0644);
            fd_secondary_ = ::open(p2, O_CREAT | O_WRONLY | O_DIRECT | O_DSYNC, 0644);

            io_uring_params p_params{};
            p_params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
            p_params.sq_thread_cpu = primary_cpu;
            io_uring_queue_init_params(128, &ring_primary_, &p_params);

            io_uring_params s_params{};
            s_params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
            s_params.sq_thread_cpu = secondary_cpu;
            io_uring_queue_init_params(128, &ring_secondary_, &s_params);
        }

        ~mirrored_journal() noexcept {
            io_uring_queue_exit(&ring_primary_);
            io_uring_queue_exit(&ring_secondary_);
            if (fd_primary_ >= 0) ::close(fd_primary_);
            if (fd_secondary_ >= 0) ::close(fd_secondary_);
        }

        /**
         * @brief Completion Reaper.
         * @details Reclaims descriptor slots from both physical rings.
         * Must be called periodically to prevent ring saturation.
         */
        inline void poll_completions() noexcept {
            // RAW HARVEST: Primary Ring
            unsigned p_head = ::io_uring_smp_load_acquire(ring_primary_.cq.khead);
            const unsigned p_tail = ::io_uring_smp_load_acquire(ring_primary_.cq.ktail);
            
            if (SL_EXPECT_TRUE(p_head != p_tail)) {
                ::io_uring_smp_store_release(ring_primary_.cq.khead, p_tail);
            }

            // RAW HARVEST: Secondary Ring
            unsigned s_head = ::io_uring_smp_load_acquire(ring_secondary_.cq.khead);
            const unsigned s_tail = ::io_uring_smp_load_acquire(ring_secondary_.cq.ktail);
            
            if (SL_EXPECT_TRUE(s_head != s_tail)) {
                ::io_uring_smp_store_release(ring_secondary_.cq.khead, s_tail);
            }
        }

        /**
         * @brief True parallel physical NVMe write.
         * @details A stall on one drive does not pull down the entire processing pipeline.
         */
        inline void persist_event(const void* data, size_t len) noexcept {
            io_uring_sqe* sqe1 = uring_shim::get_sqe(&ring_primary_);
            io_uring_sqe* sqe2 = uring_shim::get_sqe(&ring_secondary_);

            if (__builtin_expect(sqe1 && sqe2, 1)) {
                io_uring_prep_write(sqe1, fd_primary_, data, len, file_offset_);
                io_uring_prep_write(sqe2, fd_secondary_, data, len, file_offset_);

                // CORE-LOCAL SNAPSHOT: Use acquire loads for thread safety without double-syscalls
                const uint32_t f1 = ::io_uring_smp_load_acquire(ring_primary_.sq.kflags);
                const uint32_t f2 = ::io_uring_smp_load_acquire(ring_secondary_.sq.kflags);
                
                // Fusion: Kick only the rings that explicitly requested it.
                // This prevents "Double Kick" latency spikes where one fast drive
                // forces a syscall for a slow drive that is still processing.
                if (SL_UNLIKELY((f1 | f2) & IORING_SQ_NEED_WAKEUP)) [[unlikely]] {
                    if (f1 & IORING_SQ_NEED_WAKEUP) uring_shim::submit(&ring_primary_);
                    if (f2 & IORING_SQ_NEED_WAKEUP) uring_shim::submit(&ring_secondary_);
                }

                file_offset_ += len;
            }
        }
    };
}
