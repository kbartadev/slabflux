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
 * Absolute Liability Limitation & Full Terms: See LICENSE and NOTICE.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "../core/hot_path_alignment.hpp"

namespace slabflux::io {

    /**
     * @brief Constexpr Stream Topology Layer.
     * @details Statically derives physical stride and alignment for the 
     * persistent medium, ensuring bit-perfect append-only semantics.
     * Enforces SECTOR_SIZE alignment to satisfy O_DIRECT invariants.
     */
    template <typename T>
    struct journal_stream_layout {
        // Physical Requirement: Stride must be multiple of SECTOR_SIZE (512) for O_DIRECT efficiency.
        static constexpr std::size_t stride = (sizeof(T) + 511) & ~std::size_t(511);
    };

    /**
     * @brief Append-Only Persistent Constexpr Streamer.
     * @details Replaces standard WAL synchronization with a hardware-aligned 
     * persistent stream. Maps the durable log directly to the NVMe storage 
     * controller to eliminate kernel-managed buffer jitter.
     */
    template<core::POD EventType, size_t JournalSizeBytes = 1073741824>
    class alignas(64) durable_journal {
        static_assert(journal_stream_layout<EventType>::stride >= sizeof(EventType), "Structural Breach: Stride underflow");

        int fd_{-1};

        // Global Stream Watermarks: Isolated to separate 64-byte windows
        alignas(64) std::atomic<uint64_t> write_cursor_{0};
        alignas(64) std::atomic<uint64_t> persistent_horizon_{0};

        uint8_t* base_ptr_{nullptr};
        static constexpr size_t STRIDE = journal_stream_layout<EventType>::stride;

    public:
        explicit durable_journal(const char* filepath) noexcept {
            fd_ = ::open(filepath, O_CREAT | O_RDWR, 0666);
            if (fd_ == -1) return;

            // Pre-allocate NVMe blocks to prevent hot-path page faults
            ::ftruncate(fd_, JournalSizeBytes);

            // MAP_SHARED makes this memory visible to other processes and backed by the file
            void* mem = ::mmap(nullptr, JournalSizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd_, 0);
            if (mem != MAP_FAILED) {
                base_ptr_ = static_cast<uint8_t*>(mem);
            }
        }

        ~durable_journal() noexcept {
            if (base_ptr_) {
                // Horizon-Aware Sync: Only force persistence for the committed pulse range.
                // Ensures closing the journal doesn't stall on zero-filled pre-allocated blocks.
                const uint64_t horizon = persistent_horizon_.load(std::memory_order_acquire);
                if (horizon > 0) [[likely]] 
                    ::msync(base_ptr_, (horizon + 4095) & ~4095UL, MS_SYNC);

                // Structural Finalization: Lock the physical file size to the 
                // persistent horizon.
                if (fd_ != -1) [[likely]] {
                    ::ftruncate(fd_, persistent_horizon_.load(std::memory_order_relaxed));
                }

                ::munmap(base_ptr_, JournalSizeBytes);
            }
            if (fd_ != -1) ::close(fd_);
        }

        /**
         * @brief Physical synchronization boundary.
         * Forces the kernel to flush mmap dirty pages to the underlying NVMe storage.
         */
        SLAB_FORCE_INLINE void force_flush() noexcept {
            if (base_ptr_) [[likely]] {
                const uint64_t horizon = persistent_horizon_.load(std::memory_order_acquire);
                if (horizon > 0)
                    ::msync(base_ptr_, (horizon + 4095) & ~4095UL, MS_SYNC);
            }
        }

        [[nodiscard]] inline uint8_t* get_arena_base() const noexcept {
            return base_ptr_;
        }

        /**
         * @brief Sovereign Stream Reservation.
         * @details Acquires a slot directly from the persistent arena base.
         */
        [[nodiscard]] SLAB_FORCE_INLINE EventType* reserve_slot() noexcept {
            // Atomic Reservation: Relaxed ordering is sufficient as the 
            // publishing barrier is deferred to the commit phase.
            const uint64_t offset = write_cursor_.fetch_add(STRIDE, std::memory_order_relaxed);

            if (__builtin_expect(offset >= JournalSizeBytes, 0)) [[unlikely]] {
                return nullptr;
            }

            return reinterpret_cast<EventType*>(base_ptr_ + offset);
        }

        /**
         * @brief Atomic Horizon Advancement.
         */
        SLAB_FORCE_INLINE void commit_slot() noexcept {
            // Horizon Shift: Release semantics act as the persistent barrier, 
            // making sure the payload is retired before the reader horizon advances.
            persistent_horizon_.fetch_add(STRIDE, std::memory_order_release);
        }

        /** @brief Legacy alias for scalar commitment and industrial parity. */
        SLAB_FORCE_INLINE void commit() noexcept { commit_slot(); }

        /** @brief Pointer-based commit pulse for zero-copy interface parity. */
        SLAB_FORCE_INLINE void commit_slot(EventType*) noexcept { commit_slot(); }

        /**
         * @brief Reader Horizon Query.
         */
        [[nodiscard]] SLAB_FORCE_INLINE uint64_t get_sync_watermark() const noexcept {
            return persistent_horizon_.load(std::memory_order_acquire);
        }

        SLAB_FORCE_INLINE void reset() noexcept {
            write_cursor_.store(0, std::memory_order_relaxed);
            persistent_horizon_.store(0, std::memory_order_relaxed);
        }
    };
}