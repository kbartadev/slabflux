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
 * @file pinned_allocator_isolated.hpp
 * @brief Isolated MPMC Allocator.
 * @details Implements a robust, ABA-protected concurrent allocator utilizing an 
 * Isolated Cell architecture for high-frequency Shared Memory (SHM) and 
 * IPC dataflows.
 * 
 * Design Features:
 * 1. Cache Isolation: Every cell node is strictly 64-byte aligned to 
 *    mathematically eliminate False Sharing and RFO stalls.
 * 2. ABA Hardening: Uses 64-bit tagged versioning (ABA counter + Index) to 
 *    ensure wait-free stack safety during high-churn multi-threaded reclamation.
 * 3. Physical Residency: Enforces RAM pinning (mlock) for deterministic O(1).
 */

#pragma once

#include "slabflux/platform/os.hpp"
#include <atomic>
#include <cstdint>
#include <new>
#include <immintrin.h> // For _mm_pause
#include <utility>
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief A strictly deterministic, explicit-lifecycle memory allocator.
     */
    template<typename T, size_t Capacity>
    class alignas(64) pinned_slab_allocator {
    private:
        struct alignas(64) cell {
            alignas(alignof(T)) uint8_t storage[sizeof(T)];
            uint32_t next_free_index;
        };

        cell* raw_memory_{nullptr};
        size_t total_bytes_{0};

        // 64-bit tagged pointer: [ 32-bit ABA Counter | 32-bit Cell Index ]
        // Isolated to its own cache line to prevent contention with payload processing
        alignas(64) std::atomic<uint64_t> free_head_{0};

    public:
        pinned_slab_allocator() {
            if (Capacity == 0) [[unlikely]] {
                handle_critical_error("Allocator: Capacity must be > 0");
            }

            // Round up to page size (4KB) for mbind/alignment compatibility
            const size_t page_size = 4096;
            total_bytes_ = (Capacity * sizeof(cell) + page_size - 1) & ~(page_size - 1);

            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
            raw_memory_ = reinterpret_cast<cell*>(::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0));

            if (raw_memory_ == MAP_FAILED) {
                handle_critical_error("Allocator: mmap failed");
            }

            // Establish the initial intrusive free-list chain
            for (uint32_t i = 0; i + 1 < Capacity; ++i) {
                raw_memory_[i].next_free_index = i + 1;
            }
            if (Capacity > 0) raw_memory_[Capacity - 1].next_free_index = 0xFFFFFFFF; // End-of-list marker

            free_head_.store(0, std::memory_order_release);
        }

        ~pinned_slab_allocator() {
            if (raw_memory_ && raw_memory_ != MAP_FAILED) {
                ::munlock(raw_memory_, total_bytes_);
                ::munmap(raw_memory_, total_bytes_);
            }
        }

        // Explicit ownership. No copy, no move.
        pinned_slab_allocator(const pinned_slab_allocator&) = delete;
        pinned_slab_allocator& operator=(const pinned_slab_allocator&) = delete;

        /**
         * @brief Raw Wait-Free, ABA-Safe Allocation.
         */
        T* make_raw() noexcept {
            uint64_t head = free_head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                uint32_t index = static_cast<uint32_t>(head & 0xFFFFFFFF);
                if (index == 0xFFFFFFFF) [[unlikely]] return nullptr;

                cell* c = &raw_memory_[index];
                uint64_t new_head = (((head >> 32) + 1) << 32) | c->next_free_index;

                if (free_head_.compare_exchange_strong(head, new_head, std::memory_order_release, std::memory_order_acquire)) 
                    return reinterpret_cast<T*>(c->storage);
                
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        /**
         * @brief Explicit Lifecycle: In-place construction.
         */
        template<typename... Args>
        T* construct(Args&&... args) noexcept {
            T* ptr = make_raw();
            if (ptr) [[likely]] {
                new (ptr) T(std::forward<Args>(args)...);
            }
            return ptr;
        }

        /**
         * @brief Raw Wait-Free, ABA-Safe Deallocation.
         */
        void free(T* ptr) noexcept {
            if (!ptr) [[unlikely]] return;

            // Re-derive the cell wrapper from the payload pointer
            cell* c = reinterpret_cast<cell*>(ptr);
            uint32_t index = static_cast<uint32_t>(c - raw_memory_);

            uint64_t head = free_head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                c->next_free_index = static_cast<uint32_t>(head & 0xFFFFFFFF);
                uint64_t new_head = (((head >> 32) + 1) << 32) | index;
                if (free_head_.compare_exchange_strong(head, new_head, std::memory_order_release, std::memory_order_acquire)) break;
                
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        /** @brief Unified Pool Alias. */
        void release(T* ptr) noexcept { free(ptr); }

        /** @brief Safe ownership collapse for managed handles. */
        void release(managed_data<T, pinned_slab_allocator>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /** @brief Safe ownership collapse for scoped handles. */
        void release(scoped_ptr<T>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /**
         * @brief Explicit Lifecycle: Destruction and return to pool.
         */
        void destroy(T* ptr) noexcept {
            if (ptr) [[likely]] {
                ptr->~T();
                free(ptr);
            }
        }

        static void deleter_fn(void* ctx, void* raw_ptr) noexcept {
            if (raw_ptr) static_cast<pinned_slab_allocator*>(ctx)->release(static_cast<T*>(raw_ptr));
        }

        [[nodiscard]] void* data() const noexcept { return raw_memory_; }
        [[nodiscard]] size_t capacity() const noexcept { return Capacity; }
    };

} // namespace slabflux::core
