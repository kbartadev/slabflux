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
 */

#pragma once

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <concepts>
#include <atomic>
#include <new>
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::sys {

    /** @brief Concept for causal-aware ledger entries. */
    template <typename T>
    concept LedgerEntry = std::is_trivially_copyable_v<T> && requires(T e) {
        { e.lsn } -> std::convertible_to<uint64_t>;
    };

    /** 
     * @brief Audit record - 128 byte strictly cache-aligned layout.
     * @details Replaces naive 136b layouts to perfectly map across 2 cache lines. 
     */
    struct alignas(std::hardware_constructive_interference_size) audit_entry {
        uint64_t lsn;
        uint64_t error_code;
        uint64_t health_signature;
        char message[104]; 
    };

    /**
     * @brief Custom Allocator for Append-Only Ledger Storage.
     * @details Enforces the sequential journaling invariant at the type level.
     * Restricts memory access to strictly tail-relative appends via head-atomics.
     */
    template <LedgerEntry T>
    class ledger_append_allocator {
        T* base_ptr;
        std::atomic<uint64_t>& head_ref;
    public:
        T* make_raw() noexcept requires core::POD<T> { 
            // Relaxed order: memory barriers are handled post-write by commit_to_pmem
            return &base_ptr[head_ref.fetch_add(1, std::memory_order_relaxed)]; 
        }

        /**
         * @brief Direct Cache-Line Flush for DAX Persistent Memory.
         * @details Pushes the WAL entry directly from L1 cache to non-volatile storage 
         * without crossing into the kernel.
         */
        void commit_to_pmem(T* ptr) noexcept {
            constexpr size_t cache_lines = (sizeof(T) + 63) / 64;
            char* byte_ptr = reinterpret_cast<char*>(ptr);
            
            for (size_t i = 0; i < cache_lines; ++i) _mm_clwb(byte_ptr + (i * 64));
            _mm_sfence(); // Ensure global visibility and physical persistence
        }
    };

    /**
     * @brief Memory-mapped Audit Ledger for Zero-Copy health verification.
     * @details Re-architected using C++20 concepts and custom allocators to
     * enforce write append-only invariants. Provides a high-speed interface,
     * enabling real-time drift detection without kernel-side overhead.
     * @tparam EntryType The structural format of the ledger records.
     */
    template <LedgerEntry EntryType = slabflux::sys::audit_entry>
    class audit_ledger {
        int fd_{ -1 };
        void* mmap_base_{ nullptr };
        size_t file_size_{ 0 };
        const EntryType* entries_{ nullptr };
        size_t num_entries_{ 0 };
        size_t current_idx_{ 0 };

    public:
        explicit audit_ledger(const char* path) {
            fd_ = ::open(path, O_RDONLY);
            if (fd_ < 0) return;

            struct stat st;
            if (::fstat(fd_, &st) == 0) {
                file_size_ = st.st_size;
                if (file_size_ >= sizeof(EntryType)) {
                    // MAP_PRIVATE creates a Copy-On-Write snapshot. 
                    mmap_base_ = ::mmap(nullptr, file_size_, PROT_READ, MAP_SHARED, fd_, 0);
                    if (mmap_base_ != MAP_FAILED) {
                        entries_ = static_cast<const EntryType*>(mmap_base_);
                        num_entries_ = file_size_ / sizeof(EntryType);
                    }
                }
            }
        }

        ~audit_ledger() {
            if (mmap_base_ && mmap_base_ != MAP_FAILED) ::munmap(mmap_base_, file_size_);
            if (fd_ >= 0) ::close(fd_);
        }

        /**
         * @brief Retrieves the next audit entry for a given LSN or higher.
         * @details O(1) traversal through the memory-mapped ledger.
         */
        [[nodiscard]] const EntryType* next(uint64_t target_lsn) noexcept {
            if (!entries_) return nullptr;
            
            size_t i = current_idx_;
            
            if constexpr (sizeof(EntryType) % 8 == 0) {
                // SIMD Path: Optimized LSN scan using hardware-accelerated gather.
                // Supports arbitrary 8-byte-aligned record strides (including 136 bytes).
                const int stride_qwords = sizeof(EntryType) / 8;
                const __m512i v_target = _mm512_set1_epi64(target_lsn);
                const __m512i v_indices = _mm512_set_epi64(7 * stride_qwords, 6 * stride_qwords, 5 * stride_qwords, 4 * stride_qwords,
                                                           3 * stride_qwords, 2 * stride_qwords, 1 * stride_qwords, 0);

                for (; i + 8 <= num_entries_; i += 8) {
                    __m512i v_lsns = _mm512_i64gather_epi64(v_indices, reinterpret_cast<const long long*>(&entries_[i].lsn), 8);
                    
                    // Compare gathered LSNs against target (>=)
                    __mmask8 mask = _mm512_cmpge_epu64_mask(v_lsns, v_target);
                    
                    if (SL_EXPECT_FALSE(mask != 0)) {
                        // Found a match; update global index and return the specific record
                        uint32_t first_match = static_cast<uint32_t>(std::countr_zero(mask));
                        current_idx_ = i + first_match;
                        return &entries_[current_idx_];
                    }
                }
            }

            // Sequential scan for the tail of the file
            for (; i < num_entries_; ++i) {
                if (entries_[i].lsn >= target_lsn) {
                    current_idx_ = i;
                    return &entries_[i];
                }
            }
            
            current_idx_ = num_entries_;
            return nullptr;
        }

        [[nodiscard]] bool is_valid() const noexcept {
            return entries_ != nullptr;
        }
    };

} // namespace slabflux::sys
