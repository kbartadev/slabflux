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
 *
 * @file blackbox_recorder.hpp
 * @brief Micro-architectural Blackbox.
 * @details Re-architected using file-backed memory mapping and C++20 spans.
 * High-speed cycle telemetry survives process crashes by residing in the
 * kernel's page cache via mmap.
 */

#pragma once

#include <atomic>
#include <span>
#include <syncstream>
#include <ostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::sys {

    /** @brief Struct-of-Arrays aligned cycle telemetry frame. */
    struct cycle_stats {
        uint64_t ingress_tsc;
        uint64_t logic_tsc;
        uint64_t journal_tsc;
        uint64_t recovery_delay_cycles;
        uint64_t egress_tsc;
        uint64_t cascade_limit;
        float    divergence_mse;
        float    mse_ema;
        uint64_t full_drop_count;
    };

    template<size_t Capacity = 1024>
    class alignas(64) blackbox_recorder {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

        struct mmap_layout {
            alignas(64) std::atomic<uint64_t> head;
            cycle_stats ring[Capacity];
        };

        int fd_{ -1 };
        mmap_layout* base_{ nullptr };
        size_t mapped_size_{ 0 };

    public:
        /** @brief Default constructor for stall-free stack allocation in tests/mocks. */
        blackbox_recorder() = default;

        /** @brief Initializes the blackbox backing store at the given filesystem path. */
        explicit blackbox_recorder(const char* path) {
            mapped_size_ = sizeof(mmap_layout);
            
            fd_ = ::open(path, O_RDWR | O_CREAT, 0644);
            if (fd_ < 0) return;

            if (::ftruncate(fd_, mapped_size_) != 0) return;

            void* mem = ::mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
            if (mem == MAP_FAILED) return;

            base_ = static_cast<mmap_layout*>(mem);
        }

        ~blackbox_recorder() {
            if (base_) ::munmap(base_, mapped_size_);
            if (fd_ >= 0) ::close(fd_);
        }

        /**
         * @brief Records a full cycle's performance data into the mapped span.
         * @details O(1) wait-free store. Survives process crashes via Shared Memory mapping.
         */
        SLAB_FORCE_INLINE void record(const cycle_stats& s) noexcept {
            if (SL_EXPECT_FALSE(!base_)) return;

            // Atomic head increment ensures cross-core recording safety
            const uint64_t idx = base_->head.fetch_add(1, std::memory_order_relaxed);
            
            // Memory-mapped std::span (C++20) view over the telemetry ring
            std::span<cycle_stats, Capacity> view(base_->ring);
            view[idx & (Capacity - 1)] = s;
        }

        /**
         * @brief Thread-safe telemetry dump using C++20 synchronized buffered streams.
         * @param os Target output stream for the telemetry report.
         */
        void snapshot(std::ostream& os) const {
            if (!base_) return;
            // Synchronized emission to ensure thread-safe crash analysis
            std::osyncstream bout(os);
            
            const uint64_t current_head = base_->head.load(std::memory_order_acquire);
            const size_t count = (current_head < Capacity) ? static_cast<size_t>(current_head) : Capacity;
            const uint64_t start_idx = (current_head > Capacity) ? (current_head - Capacity) : 0;

            bout << "[BLACKBOX] Telemetry Snapshot (Head Index: " << current_head << ")\n";
            
            std::span<const cycle_stats, Capacity> view(base_->ring);
            for (uint64_t i = 0; i < count; ++i) {
                const uint64_t lsn = start_idx + i;
                const auto& s = view[lsn & (Capacity - 1)];
                bout << "  [LSN " << lsn << "] Jitter: " << (s.logic_tsc - s.ingress_tsc) << " cycles\n";
            }
            bout.emit();
        }

        [[nodiscard]] const cycle_stats* data() const noexcept { return base_ ? base_->ring : nullptr; }
        [[nodiscard]] size_t capacity() const noexcept { return Capacity; }
        [[nodiscard]] uint64_t head() const noexcept { return base_ ? base_->head.load(std::memory_order_relaxed) : 0; }
        [[nodiscard]] bool is_valid() const noexcept { return base_ != nullptr; }
    };
}
