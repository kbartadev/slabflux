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

#include <atomic>
#include <cstdint>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <iostream>
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#include <liburing.h> // For io_uring
#include <stdexcept> // For std::runtime_error

#include "../core/hot_path_alignment.hpp"
#include "slabflux/hw/spin_backoff.hpp"
#include "slabflux/io/durable_journal.hpp" // For journal_stream_layout

namespace slabflux::io {

    /**
     * @brief Batch-Aggregated SQE Metaprogramming Engine.
     * @details Statically resolves hardware submission sequences, eliminating 
     * manual liburing preparation loops. Provides O(1) SQE synthesis 
     * via template-specialized pulses.
     */
    template <typename T>
    struct uring_sqe_aggregator {
        static SLAB_FORCE_INLINE void write_pulse(io_uring_sqe* sqe, int fd, T* addr, uint64_t offset) noexcept {
            io_uring_prep_write(sqe, fd, addr, sizeof(T), offset);
            // Bind the physical pointer as the completion token
            io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(addr));
        }

        static SLAB_FORCE_INLINE void sync_pulse(io_uring_sqe* sqe, int fd) noexcept {
            io_uring_prep_fsync(sqe, fd, 0);
            io_uring_sqe_set_data64(sqe, 0xFFFFFFFFFFFFFFFFULL);
        }
    };

    template<typename EventType, size_t JournalSizeBytes = 1073741824> // 1GB default
    class alignas(64) io_uring_durable_journal {
        using Aggregator = uring_sqe_aggregator<EventType>;
        static constexpr size_t STRIDE = journal_stream_layout<EventType>::stride;
        io_uring ring_;
        int sq_core_id_{-1};
        int fd_{-1};

        static constexpr size_t SECTOR_SIZE = 512;

        // Global Stream Watermarks: Scalar tracking for single-producer line-rate speed
        // Eliminates the `lock xadd` 8-cycle penalty from the hot path.
        alignas(64) uint64_t write_cursor_{0};
        alignas(64) uint64_t persistent_horizon_{0}; 
        alignas(64) uint64_t submission_pulse_{0}; 
        alignas(64) uint64_t completion_pulse_{0}; 

        uint8_t* base_ptr_{nullptr};

    public:
        // io_uring with SQ_AFF (pinned kernel poller) and robust memory allocation.
        explicit io_uring_durable_journal(const char* filepath, int sq_core_id)
            : sq_core_id_(sq_core_id) {
            // O_DIRECT ensures kernel bypass and direct writes to NVMe
            fd_ = ::open(filepath, O_CREAT | O_RDWR | O_DIRECT, 0666);
            if (fd_ == -1) {
                throw std::runtime_error("Failed to open durable journal file with O_DIRECT");
            }
            static_assert(JournalSizeBytes % SECTOR_SIZE == 0, "JournalSizeBytes must be a multiple of SECTOR_SIZE for O_DIRECT compatibility.");
            ::ftruncate(fd_, JournalSizeBytes);

            void* mem = MAP_FAILED;
            int current_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;

            // Attempt 1: HugePages + Locked
            #ifdef MAP_HUGETLB
            current_flags |= MAP_HUGETLB | MAP_HUGE_2MB | MAP_LOCKED;
            mem = ::mmap(nullptr, JournalSizeBytes, PROT_READ | PROT_WRITE, current_flags, -1, 0);
            if (mem == MAP_FAILED) {
                // Fallback 1.1: Standard Locked Pages (remove HugePage flags)
                current_flags &= ~(MAP_HUGETLB | MAP_HUGE_2MB);
                mem = ::mmap(nullptr, JournalSizeBytes, PROT_READ | PROT_WRITE, current_flags, -1, 0);
            }
            #endif

            if (mem == MAP_FAILED) {
                // Fallback 2: Standard Locked Pages (if HugePage attempt failed or not defined)
                current_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
                mem = ::mmap(nullptr, JournalSizeBytes, PROT_READ | PROT_WRITE, current_flags, -1, 0);
            }

            if (mem == MAP_FAILED) {
                // Fallback 3: Unlocked, non-HugePage memory (remove MAP_LOCKED)
                current_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;
                mem = ::mmap(nullptr, JournalSizeBytes, PROT_READ | PROT_WRITE, current_flags, -1, 0);
            }

            if (mem == MAP_FAILED) {
                ::close(fd_);
                fd_ = -1;
                throw std::runtime_error("Failed to mmap durable journal memory");
            }
            base_ptr_ = static_cast<uint8_t*>(mem);

            // If mmap succeeded but MAP_LOCKED was requested, try to mlock explicitly.
            // This handles cases where mmap with MAP_LOCKED fails, but mlock after mmap succeeds.
            if ((current_flags & MAP_LOCKED)) {
                ::mlock(base_ptr_, JournalSizeBytes);
            }

            // Initialize io_uring with SQPOLL and SQ_AFF
            io_uring_params params{};
            params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
            
            // Guard: Ensure CPU affinity doesn't exceed hardware bounds to prevent EINVAL
            const int max_cpu = static_cast<int>(std::thread::hardware_concurrency()) - 1;
            params.sq_thread_cpu = (sq_core_id_ <= max_cpu && sq_core_id_ >= 0) ? sq_core_id_ : 0;

            if (io_uring_queue_init_params(256, &ring_, &params) < 0) {
                // Fallback: Try baseline SQPOLL if pinning fails or is restricted (e.g., cgroups)
                params.flags = IORING_SETUP_SQPOLL;
                if (io_uring_queue_init_params(256, &ring_, &params) < 0) {
                    ::munmap(base_ptr_, JournalSizeBytes);
                    ::close(fd_);
                    fd_ = -1;
                    base_ptr_ = nullptr;
                    throw std::runtime_error("io_uring initialization failed");
                }
            }
        }

        ~io_uring_durable_journal() noexcept {
            if (base_ptr_ && fd_ != -1) {
                // Ensure all pending writes are flushed before unmapping
                try {
                    force_flush();
                } catch (...) {
                    // Destructor must remain noexcept
                }

                // Finalize physical file size to match committed progress
                ::ftruncate(fd_, persistent_horizon_);

                ::munmap(base_ptr_, JournalSizeBytes);
            }
            if (fd_ != -1) ::close(fd_);
            if (ring_.ring_fd > 0) io_uring_queue_exit(&ring_);
        }

        // No copy/move constructors or assignment operators
        io_uring_durable_journal(const io_uring_durable_journal&) = delete;
        io_uring_durable_journal& operator=(const io_uring_durable_journal&) = delete;

        SLAB_FORCE_INLINE void reset() noexcept {
            write_cursor_ = 0;
            persistent_horizon_ = 0;
            submission_pulse_ = 0;
            completion_pulse_ = 0;
        }
        
        [[nodiscard]] inline uint8_t* get_arena_base() const noexcept {
            return base_ptr_;
        }

        [[nodiscard]] SLAB_FORCE_INLINE EventType* reserve_slot() noexcept {
            const uint64_t offset = write_cursor_;
            write_cursor_ += STRIDE;

            // Safety Guard: Detect arena exhaustion or initialization failure
            if (__builtin_expect(offset >= JournalSizeBytes || base_ptr_ == nullptr, 0)) [[unlikely]] {
                return nullptr;
            }

            return reinterpret_cast<EventType*>(base_ptr_ + offset);
        }

        /**
         * @brief Sovereign Commitment.
         * @details Dispatches a synthesized write pulse to the hardware ring.
         * @param slot_ptr The address returned by reserve_slot.
         */
        SLAB_FORCE_INLINE void commit_slot(EventType* slot_ptr) noexcept {
            io_uring_sqe* sqe = uring_shim::get_sqe(&ring_);

            if (SL_EXPECT_TRUE(sqe)) {
                const uint64_t offset = reinterpret_cast<uint8_t*>(slot_ptr) - base_ptr_;
                Aggregator::write_pulse(sqe, fd_, slot_ptr, offset);

                if (SL_UNLIKELY(::io_uring_smp_load_acquire(ring_.sq.kflags) & IORING_SQ_NEED_WAKEUP)) {
                    uring_shim::submit(&ring_);
                }
                submission_pulse_++;
            }
            persistent_horizon_ += STRIDE;
        }

        /** @brief Sovereign Commitment Pulse. */
        SLAB_FORCE_INLINE void commit_slot() noexcept {
            // Optimization: Derive the last reserved slot for single-threaded flows
            const uint64_t off = write_cursor_ - STRIDE;
            commit_slot(reinterpret_cast<EventType*>(base_ptr_ + off));
        }

        /** @brief Legacy alias for industrial parity. */
        SLAB_FORCE_INLINE void commit() noexcept { commit_slot(); }

        void force_flush() {
            uint32_t yield_count = 0;
            // Wait for all submitted writes to complete
            while (completion_pulse_ < submission_pulse_) {
                poll_completions();
                if (SL_UNLIKELY(::io_uring_smp_load_acquire(ring_.sq.kflags) & IORING_SQ_NEED_WAKEUP)) {
                    uring_shim::submit(&ring_);
                }
                slabflux::hw::spin_backoff(yield_count);
            }

            io_uring_sqe* sqe = uring_shim::get_sqe(&ring_);
            yield_count = 0;
            while (SL_UNLIKELY(!sqe)) {
                poll_completions();
                if (SL_UNLIKELY(::io_uring_smp_load_acquire(ring_.sq.kflags) & IORING_SQ_NEED_WAKEUP)) {
                    uring_shim::submit(&ring_);
                }
                slabflux::hw::spin_backoff(yield_count);
                sqe = uring_shim::get_sqe(&ring_);
            }

            Aggregator::sync_pulse(sqe, fd_);
            uring_shim::submit(&ring_);

            io_uring_cqe* cqe;
            int ret = io_uring_wait_cqe(&ring_, &cqe);
            if (ret == 0) {
                if (cqe->res < 0) {
                    throw std::runtime_error("io_uring fsync failed");
                }
                io_uring_cqe_seen(&ring_, cqe);
            } else {
                throw std::runtime_error("io_uring wait_cqe for fsync failed");
            }
        }

        SLAB_FORCE_INLINE void poll_completions() noexcept {
            io_uring_cqe* cqes[32];
            const unsigned count = uring_shim::peek_batch_cqe(&ring_, cqes, 32);
            if (SL_EXPECT_TRUE(count > 0)) {
                ::io_uring_cq_advance(&ring_, count);
                completion_pulse_ += count;
            }
        }

        [[nodiscard]] SLAB_FORCE_INLINE uint64_t get_sync_watermark() const noexcept {
            return persistent_horizon_;
        }
    };
}