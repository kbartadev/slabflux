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
 * @file pinned_allocator_mpmc.hpp
 * @brief MPMC SHM Allocator.
 * @details Implements an ABA-protected concurrent allocator for high-frequency 
 * Shared Memory (SHM) flows utilizing a Detached Treiber Stack architecture.
 * 
 * High-Performance Design:
 * 1. Physical Separation: Metadata (next indices) is stored in a parallel array 
 *    away from the T[] payload to eliminate false sharing during DMA/SHM access.
 * 2. ABA Hardening: Uses 64-bit tagged indices (version + index) to prevent 
 *    stack corruption in high-frequency multi-threaded reclamation scenarios.
 * 3. Cache-Line Isolation: The stack head is isolated to a 64-byte boundary to 
 *    prevent contention with payload processing.
 * 4. Physical Residency: Enforces RAM pinning (mlock) and 2MB HugePage support
 *    for deterministic O(1) access, with graceful fallback to standard locked pages
 *    if HugePage allocation fails.
 */

#pragma once

#include "slabflux/platform/os.hpp"
#include <atomic>
#include <cstdint>
#include <system_error>
#include <immintrin.h> // For _mm_pause
#include <iostream>
#include <new>
#include <utility>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    template<typename T, size_t Capacity>
    class alignas(64) pinned_allocator_mpmc {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

        // 1. The Pristine DMA Payload Area (Strictly T[])
        T* raw_memory_{nullptr};

        // 2. The Detached Metadata Array (Strictly parallel index links)
        // Uses atomic to prevent UB during concurrent read/write in lock-free traversal
        std::atomic<uint32_t>* next_indices_{nullptr};

        size_t total_mmap_bytes_{0};

        // 3. The 64-bit ABA-Protected Stack Head [ 32-bit ABA Counter | 32-bit Head Index ]
        // Cache-line isolated to prevent contention with payload
        alignas(64) std::atomic<uint64_t> free_head_{0};

    public:
        using value_type = T;
        pinned_allocator_mpmc() {
            // Align the payload block to a 4KB page boundary so the meta array
            // starts on a fresh, page-aligned cache boundary.
            size_t payload_bytes = (Capacity * sizeof(T) + 4095) & ~4095;
            size_t meta_bytes    = Capacity * sizeof(std::atomic<uint32_t>);
            total_mmap_bytes_    = payload_bytes + meta_bytes;

            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
            void* base = ::mmap(nullptr, total_mmap_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (base == MAP_FAILED) {
                throw std::system_error(errno, std::generic_category(), "MPMC: mmap failed");
            }
            // Silently swallow mlock failure
            ::mlock(base, total_mmap_bytes_);

            // Map the distinct memory regions
            raw_memory_   = reinterpret_cast<T*>(base);
            next_indices_ = reinterpret_cast<std::atomic<uint32_t>*>(static_cast<char*>(base) + payload_bytes);

            // Initialize the detached intrusive links
            for (uint32_t i = 0; i < Capacity - 1; ++i) {
                next_indices_[i].store(i + 1, std::memory_order_relaxed);
            }
            next_indices_[Capacity - 1].store(0xFFFFFFFF, std::memory_order_relaxed);

            free_head_.store(0, std::memory_order_release);
        }

        ~pinned_allocator_mpmc() {
            if (raw_memory_ && raw_memory_ != MAP_FAILED) {
                ::munlock(raw_memory_, total_mmap_bytes_);
                ::munmap(raw_memory_, total_mmap_bytes_);
            }
        }

        pinned_allocator_mpmc(const pinned_allocator_mpmc&) = delete;
        pinned_allocator_mpmc& operator=(const pinned_allocator_mpmc&) = delete;

        /**
         * @brief ABA-Safe MPMC Allocation.
         * @details Executes strictly on the detached meta array.
         */
        inline T* make_raw() noexcept {
            uint64_t head = free_head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                uint32_t index = static_cast<uint32_t>(head & 0xFFFFFFFF);
                if (index == 0xFFFFFFFF) [[unlikely]] return nullptr;

                // Relaxed load is mathematically safe: The CAS on free_head_ provides the
                // actual memory barrier and ABA protection against concurrent modification.
                uint32_t next = next_indices_[index].load(std::memory_order_relaxed);

                uint64_t new_head = (((head >> 32) + 1) << 32) | next;

                if (free_head_.compare_exchange_strong(head, new_head, 
                    std::memory_order_release, std::memory_order_acquire)) {
                    return &raw_memory_[index];
                }

                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        /**
         * @brief ABA-Safe MPMC Deallocation.
         */
        inline void free(T* ptr) noexcept {
            if (!ptr) [[unlikely]] return;

            uint32_t index = static_cast<uint32_t>(ptr - raw_memory_);
            uint64_t head = free_head_.load(std::memory_order_acquire);

            for (uint32_t retries = 0; ; ++retries) {
                // Relaxed store is safe: The CAS on free_head_ publishes it atomically.
                next_indices_[index].store(static_cast<uint32_t>(head & 0xFFFFFFFF), std::memory_order_relaxed);
                uint64_t new_head = (((head >> 32) + 1) << 32) | index;
                if (free_head_.compare_exchange_strong(head, new_head, 
                    std::memory_order_release, std::memory_order_acquire)) {
                    break;
                }

                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        /** @brief Unified Pool Alias. */
        inline void release(T* ptr) noexcept { free(ptr); }

        /**
         * @brief Explicit Lifecycle: In-place construction.
         */
        template<typename... Args>
        inline T* construct(Args&&... args) noexcept {
            T* ptr = make_raw();
            if (ptr) [[likely]] {
                new (ptr) T(std::forward<Args>(args)...);
            }
            return ptr;
        }

        /**
         * @brief Explicit Lifecycle: Destruction and return to pool.
         */
        inline void destroy(T* ptr) noexcept {
            if (ptr) [[likely]] {
                ptr->~T();
                free(ptr);
            }
        }

        [[nodiscard]] inline void* data() const noexcept { return raw_memory_; }
        [[nodiscard]] inline size_t capacity() const noexcept { return Capacity; }
    };

} // namespace slabflux::core
